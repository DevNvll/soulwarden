#!/usr/bin/env python3
"""Fast static queries on the New World client image.

This tool does not need a completed Ghidra analysis. It uses the PE exception
directory for function bounds and a vectorized displacement scan for
cross-references. All addresses in the output are image-relative (RVA).

Commands:
  info                          sections and directories
  imports [dll-substring]       imported functions
  exports [substring]           exported names
  func RVA...                   function bounds from .pdata
  xref RVA...                   code and data references to each target
  str REGEX                     strings that match, with RVAs
  strx REGEX [--max N]          strings that match, with referencing functions
  dis RVA [COUNT]               disassembly with string annotations
  disf RVA                      disassembly of the full function that contains RVA
  consts RVA                    string references and ASCII immediates of one function, in order
  ptrs RVA...                   aligned 64-bit absolute pointers to each target
  bytes RVA [LEN]               hex dump
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import struct
import sys
from pathlib import Path

import numpy as np

DEFAULT_GAME_DIR = Path.home() / "Games/soulwarden/game"
DEFAULT_WORK_DIR = Path.home() / "Games/soulwarden/work"


def game_executable() -> Path:
    root = Path(os.environ.get("SOULWARDEN_GAME_DIR", DEFAULT_GAME_DIR))
    return root / "Bin64" / "NewWorld.exe"


def work_directory() -> Path:
    return Path(os.environ.get("SOULWARDEN_WORK_DIR", DEFAULT_WORK_DIR))


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.data = np.memmap(path, dtype=np.uint8, mode="r")
        raw = self.data
        pe = int.from_bytes(raw[0x3C:0x40].tobytes(), "little")
        self.section_count = int.from_bytes(raw[pe + 6:pe + 8].tobytes(), "little")
        optional_size = int.from_bytes(raw[pe + 20:pe + 22].tobytes(), "little")
        optional = pe + 24
        self.image_base = int.from_bytes(raw[optional + 24:optional + 32].tobytes(), "little")
        self.entry = int.from_bytes(raw[optional + 16:optional + 20].tobytes(), "little")
        directory = optional + 112
        self.directories = []
        for index in range(16):
            rva, size = struct.unpack_from("<II", raw[directory + index * 8:directory + index * 8 + 8].tobytes())
            self.directories.append((rva, size))
        self.sections = []
        table = optional + optional_size
        for index in range(self.section_count):
            entry = raw[table + index * 40:table + index * 40 + 40].tobytes()
            name = entry[:8].rstrip(b"\0").decode("latin1")
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", entry, 8)
            characteristics = struct.unpack_from("<I", entry, 36)[0]
            self.sections.append((name, virtual_address, virtual_size, raw_pointer, raw_size, characteristics))
        self._pdata = None
        self._strings = None

    # -- address conversion -------------------------------------------------
    def section_of(self, rva: int):
        for section in self.sections:
            if section[1] <= rva < section[1] + max(section[2], section[4]):
                return section
        return None

    def offset(self, rva: int) -> int:
        section = self.section_of(rva)
        if section is None:
            raise ValueError(f"RVA 0x{rva:X} is not in a section")
        delta = rva - section[1]
        if delta >= section[4]:
            raise ValueError(f"RVA 0x{rva:X} has no file data")
        return section[3] + delta

    def rva_of_offset(self, offset: int) -> int | None:
        for section in self.sections:
            if section[3] <= offset < section[3] + section[4]:
                return section[1] + offset - section[3]
        return None

    def read(self, rva: int, size: int) -> bytes:
        start = self.offset(rva)
        return self.data[start:start + size].tobytes()

    def normalize(self, value: int) -> int:
        return value - self.image_base if value >= self.image_base else value

    # -- function table -----------------------------------------------------
    @property
    def pdata(self):
        if self._pdata is None:
            rva, size = self.directories[3]
            start = self.offset(rva)
            table = np.frombuffer(self.data[start:start + size - size % 12].tobytes(), dtype="<u4").reshape(-1, 3)
            self._pdata = table[np.argsort(table[:, 0], kind="stable")]
            self._pdata_begin = self._pdata[:, 0].tolist()
        return self._pdata

    def function_of(self, rva: int):
        table = self.pdata
        index = bisect.bisect_right(self._pdata_begin, rva) - 1
        if index < 0:
            return None
        begin, end, unwind = (int(value) for value in table[index])
        if not begin <= rva < end:
            return None
        # Follow chained unwind information to the primary function entry.
        for _ in range(8):
            try:
                header = self.read(unwind, 4)
            except ValueError:
                break
            flags = header[0] >> 3
            if not flags & 0x4:
                break
            count = header[2]
            chained = unwind + 4 + ((count + 1) & ~1) * 2
            parent_begin, parent_end, unwind = struct.unpack("<III", self.read(chained, 12))
            begin = parent_begin
        return begin, end

    # -- strings ------------------------------------------------------------
    @property
    def strings(self):
        if self._strings is None:
            entries = []
            path = work_directory() / "nw.strings.txt"
            with open(path, "rb") as handle:
                for line in handle:
                    line = line.rstrip(b"\r\n").lstrip()
                    position, _, text = line.partition(b" ")
                    try:
                        offset = int(position, 16)
                    except ValueError:
                        continue
                    entries.append((offset, text.decode("latin1")))
            self._strings = entries
            self._string_offsets = [entry[0] for entry in entries]
        return self._strings

    def string_at(self, rva: int, limit: int = 96) -> str | None:
        section = self.section_of(rva)
        if section is None or section[0] not in (".rdata", ".data", "_RDATA"):
            return None
        try:
            raw = self.read(rva, limit)
        except ValueError:
            return None
        end = raw.find(b"\0")
        if end < 4:
            return None
        text = raw[:end]
        if all(32 <= byte < 127 for byte in text):
            return text.decode("ascii")
        return None

    # -- reference scans ----------------------------------------------------
    def code_references(self, targets: list[int]):
        """Return {target: [(site_rva, trailing_immediate_size)]} for rel32 references."""
        name, text_rva, text_size, raw_pointer, raw_size, _ = self.sections[0]
        size = min(text_size, raw_size)
        buffer = self.data[raw_pointer:raw_pointer + size]
        wanted = np.array(sorted(set(targets)), dtype=np.int64)
        result = {target: [] for target in targets}
        for phase in range(4):
            count = (size - phase) // 4
            values = np.frombuffer(buffer, dtype="<i4", offset=phase, count=count).astype(np.int64)
            sites = text_rva + phase + 4 * np.arange(count, dtype=np.int64)
            base = sites + 4 + values
            for trailing in (0, 1, 4):
                candidate = base + trailing
                mask = np.isin(candidate, wanted)
                for site, target in zip(sites[mask].tolist(), candidate[mask].tolist()):
                    result[target].append((site, trailing))
        for target in result:
            result[target].sort()
        return result

    def pointer_references(self, targets: list[int]):
        wanted = np.array(sorted({self.image_base + target for target in targets}), dtype=np.uint64)
        result = {target: [] for target in targets}
        for name, rva, virtual_size, raw_pointer, raw_size, _ in self.sections:
            if name not in (".rdata", ".data", "_RDATA"):
                continue
            count = raw_size // 8
            values = np.frombuffer(self.data[raw_pointer:raw_pointer + count * 8], dtype="<u8")
            mask = np.isin(values, wanted)
            for index in np.nonzero(mask)[0].tolist():
                result[int(values[index]) - self.image_base].append(rva + index * 8)
        return result


def plausible_reference(image: Image, site: int, trailing: int) -> bool:
    """Reject displacement matches that are not at a likely ModRM or branch position."""
    try:
        before = image.read(site - 3, 3)
    except ValueError:
        return False
    opcode = before[2]
    if trailing == 0 and opcode in (0xE8, 0xE9):
        return True
    if trailing == 0 and before[1] == 0x0F and 0x80 <= opcode <= 0x8F:
        return True
    # RIP-relative ModRM: mod=00, rm=101.
    return (opcode & 0xC7) == 0x05


def describe_function(image: Image, rva: int) -> str:
    bounds = image.function_of(rva)
    if bounds is None:
        return "no-function"
    return f"fn 0x{bounds[0]:X}+0x{rva - bounds[0]:X}"


def parse_address(image: Image, text: str) -> int:
    return image.normalize(int(text, 16))


def command_info(image: Image, _arguments) -> None:
    print(f"image_base 0x{image.image_base:X} entry 0x{image.entry:X}")
    for name, rva, virtual_size, raw_pointer, raw_size, flags in image.sections:
        print(f"{name:<9} rva 0x{rva:08X} vsize 0x{virtual_size:08X} raw 0x{raw_pointer:08X} rsize 0x{raw_size:08X} flags 0x{flags:08X}")
    print(f"functions {len(image.pdata)}")


def command_imports(image: Image, arguments) -> None:
    import pefile

    pe = pefile.PE(str(image.path), fast_load=True)
    pe.parse_data_directories(directories=[1, 13])
    for group in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for entry in getattr(pe, group, []):
            dll = entry.dll.decode()
            if arguments.filter and arguments.filter.lower() not in dll.lower():
                continue
            print(f"[{dll}] {len(entry.imports)} imports ({group[16:].lower()})")
            if arguments.filter:
                for symbol in entry.imports:
                    name = symbol.name.decode() if symbol.name else f"ordinal {symbol.ordinal}"
                    print(f"  0x{symbol.address - image.image_base:X} {name}")


def command_exports(image: Image, arguments) -> None:
    import pefile

    pe = pefile.PE(str(image.path), fast_load=True)
    pe.parse_data_directories(directories=[0])
    for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        name = symbol.name.decode() if symbol.name else f"ordinal {symbol.ordinal}"
        if arguments.filter and arguments.filter.lower() not in name.lower():
            continue
        print(f"0x{symbol.address:X} {name}")


def command_func(image: Image, arguments) -> None:
    for text in arguments.addresses:
        rva = parse_address(image, text)
        bounds = image.function_of(rva)
        print(f"0x{rva:X}: " + ("no function" if bounds is None else f"0x{bounds[0]:X}..0x{bounds[1]:X}"))


def print_references(image: Image, targets: list[int], labels: dict[int, str] | None = None) -> None:
    code = image.code_references(targets)
    pointers = image.pointer_references(targets)
    for target in targets:
        label = (labels or {}).get(target) or image.string_at(target) or ""
        print(f"TARGET 0x{target:X} {label!r}")
        for site, trailing in code[target]:
            if not plausible_reference(image, site, trailing):
                continue
            print(f"  code 0x{site:X} {describe_function(image, site)}")
        for site in pointers[target]:
            print(f"  data 0x{site:X}")


def command_xref(image: Image, arguments) -> None:
    print_references(image, [parse_address(image, text) for text in arguments.addresses])


def find_strings(image: Image, pattern: str):
    regex = re.compile(pattern)
    for offset, text in image.strings:
        if regex.search(text):
            rva = image.rva_of_offset(offset)
            if rva is not None:
                yield rva, text


def command_str(image: Image, arguments) -> None:
    for rva, text in find_strings(image, arguments.pattern):
        print(f"0x{rva:X} {text[:200]}")


def command_strx(image: Image, arguments) -> None:
    matches = list(find_strings(image, arguments.pattern))[:arguments.max]
    labels = {rva: text[:120] for rva, text in matches}
    print_references(image, [rva for rva, _ in matches], labels)


def disassemble(image: Image, start: int, end: int | None, count: int | None) -> None:
    import capstone

    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    size = (end - start) if end is not None else (count or 40) * 15
    code = image.read(start, size)
    emitted = 0
    for instruction in engine.disasm(code, start):
        note = ""
        for operand in instruction.operands:
            target = None
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                target = instruction.address + instruction.size + operand.mem.disp
            elif operand.type == capstone.x86.X86_OP_IMM and instruction.group(capstone.CS_GRP_CALL):
                target = operand.imm
            if target is not None:
                text = image.string_at(target)
                if text:
                    note = f'  ; "{text[:70]}"'
                else:
                    note = f"  ; 0x{target:X}"
        print(f"0x{instruction.address:08X}  {instruction.mnemonic:<7} {instruction.op_str}{note}")
        emitted += 1
        if count is not None and end is None and emitted >= count:
            break


def command_dis(image: Image, arguments) -> None:
    disassemble(image, parse_address(image, arguments.address), None, arguments.count)


def command_disf(image: Image, arguments) -> None:
    rva = parse_address(image, arguments.address)
    bounds = image.function_of(rva)
    if bounds is None:
        sys.exit("no function contains that address")
    print(f"; function 0x{bounds[0]:X}..0x{bounds[1]:X}")
    disassemble(image, bounds[0], bounds[1], None)


def ascii_immediate(value: int, size: int) -> str | None:
    raw = (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little").rstrip(b"\0")
    if len(raw) >= 2 and all(32 <= byte < 127 for byte in raw):
        return raw.decode("ascii")
    return None


def command_consts(image: Image, arguments) -> None:
    """List literal text used by a function. MSVC stores short strings as immediates."""
    import capstone

    rva = parse_address(image, arguments.address)
    bounds = image.function_of(rva)
    if bounds is None:
        sys.exit("no function contains that address")
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    pending = ""
    pending_address = 0

    def flush() -> None:
        nonlocal pending
        if pending:
            print(f"0x{pending_address:08X}  imm  {pending!r}")
            pending = ""

    for instruction in engine.disasm(image.read(bounds[0], bounds[1] - bounds[0]), bounds[0]):
        text = None
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                literal = image.string_at(instruction.address + instruction.size + operand.mem.disp)
                if literal:
                    flush()
                    print(f"0x{instruction.address:08X}  str  {literal[:110]!r}")
            elif operand.type == capstone.x86.X86_OP_IMM and instruction.mnemonic in ("mov", "movabs"):
                text = ascii_immediate(operand.imm, operand.size)
        if text is not None:
            if not pending:
                pending_address = instruction.address
            pending += text
        elif instruction.group(capstone.CS_GRP_CALL):
            flush()
            if arguments.calls:
                for operand in instruction.operands:
                    if operand.type == capstone.x86.X86_OP_IMM:
                        print(f"0x{instruction.address:08X}  call 0x{operand.imm:X}")
    flush()


def command_ptrs(image: Image, arguments) -> None:
    targets = [parse_address(image, text) for text in arguments.addresses]
    for target, sites in image.pointer_references(targets).items():
        print(f"TARGET 0x{target:X}: " + " ".join(f"0x{site:X}" for site in sites))


def command_bytes(image: Image, arguments) -> None:
    rva = parse_address(image, arguments.address)
    raw = image.read(rva, arguments.length)
    for index in range(0, len(raw), 16):
        chunk = raw[index:index + 16]
        printable = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in chunk)
        print(f"0x{rva + index:08X}  {chunk.hex(' '):<47}  {printable}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("info").set_defaults(run=command_info)
    sub = commands.add_parser("imports"); sub.add_argument("filter", nargs="?"); sub.set_defaults(run=command_imports)
    sub = commands.add_parser("exports"); sub.add_argument("filter", nargs="?"); sub.set_defaults(run=command_exports)
    sub = commands.add_parser("func"); sub.add_argument("addresses", nargs="+"); sub.set_defaults(run=command_func)
    sub = commands.add_parser("xref"); sub.add_argument("addresses", nargs="+"); sub.set_defaults(run=command_xref)
    sub = commands.add_parser("str"); sub.add_argument("pattern"); sub.set_defaults(run=command_str)
    sub = commands.add_parser("strx"); sub.add_argument("pattern"); sub.add_argument("--max", type=int, default=40); sub.set_defaults(run=command_strx)
    sub = commands.add_parser("dis"); sub.add_argument("address"); sub.add_argument("count", nargs="?", type=int, default=40); sub.set_defaults(run=command_dis)
    sub = commands.add_parser("disf"); sub.add_argument("address"); sub.set_defaults(run=command_disf)
    sub = commands.add_parser("consts"); sub.add_argument("address"); sub.add_argument("--calls", action="store_true"); sub.set_defaults(run=command_consts)
    sub = commands.add_parser("ptrs"); sub.add_argument("addresses", nargs="+"); sub.set_defaults(run=command_ptrs)
    sub = commands.add_parser("bytes"); sub.add_argument("address"); sub.add_argument("length", nargs="?", type=int, default=128); sub.set_defaults(run=command_bytes)
    arguments = parser.parse_args()
    arguments.run(Image(game_executable()), arguments)


if __name__ == "__main__":
    main()

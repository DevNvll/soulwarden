#!/usr/bin/env python3
"""Find the client handler of a facet message type (`...ClientFacet_...` or `...ServerFacet_...`).

The unmarshal function of a message type sets the virtual table of the message class. In that
table, slot 11 (+0x58) is the invoke function: it finds the facet of the target component and
calls the handler with the message fields. Slot 12 (+0x60) writes the message fields and slot 13
(+0x68) reads them.

Output for each type: virtual table, invoke function, field writer, field reader, and the direct
calls of the invoke function in order. The last call is the handler in most types.

Usage: facet_handlers.py TYPE_INDEX_OR_NAME_REGEX...
"""

from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nwbin import Image, game_executable  # noqa: E402

WORK = Path.home() / "Games/soulwarden/work"
TEXT_END = 0x7EC8000
INVOKE_SLOT = 11
WRITER_SLOT = 12
READER_SLOT = 13
# Virtual table of the common base class of facet messages. A constructor stores it first.
BASE_TABLE = 0x8019848


def pointer(image: Image, rva: int) -> int:
    return struct.unpack("<Q", image.read(rva, 8))[0] - image.image_base


def is_virtual_table(image: Image, rva: int) -> bool:
    section = image.section_of(rva)
    if not section or section[0] != ".rdata":
        return False
    try:
        return 0x1000 <= pointer(image, rva) < TEXT_END
    except (ValueError, struct.error):
        return False


def is_message_table(image: Image, rva: int) -> bool:
    if not is_virtual_table(image, rva):
        return False
    try:
        return all(0x1000 <= pointer(image, rva + 8 * slot) < TEXT_END for slot in (INVOKE_SLOT, WRITER_SLOT, READER_SLOT))
    except (ValueError, struct.error):
        return False


def instructions(image: Image, engine, start: int, limit: int = 0x600):
    """Linear disassembly from the entry to the padding after the function (int3)."""
    for instruction in engine.disasm(image.read(start, limit), start):
        if instruction.mnemonic == "int3":
            return
        yield instruction


def message_table(image: Image, engine, unmarshal: int) -> int | None:
    """@return The first virtual table, after the base class, that the unmarshal function stores."""
    for instruction in instructions(image, engine, unmarshal):
        if instruction.mnemonic != "lea":
            continue
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                target = instruction.address + instruction.size + operand.mem.disp
                if target != BASE_TABLE and is_message_table(image, target):
                    return target
    return None


def direct_calls(image: Image, engine, start: int) -> list[int]:
    targets = []
    for instruction in instructions(image, engine, start):
        if instruction.mnemonic in ("call", "jmp") and instruction.operands:
            operand = instruction.operands[0]
            if operand.type == capstone.x86.X86_OP_IMM and not start <= operand.imm < instruction.address + 0x100:
                targets.append(operand.imm)
    return targets


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    entries = [entry for entry in json.loads((WORK / "hubtypes.json").read_text()) if entry.get("type_index") is not None]

    selected = []
    for argument in sys.argv[1:]:
        if argument.isdigit():
            selected += [entry for entry in entries if entry["type_index"] == int(argument)]
        else:
            selected += [entry for entry in entries if re.search(argument, entry["name"])]

    for entry in selected:
        short = entry["name"].replace("Javelin::ClientMessages::", "")
        if "unmarshal" not in entry:
            print(f"{entry['type_index']} {short}: no unmarshal")
            continue
        unmarshal = entry["unmarshal"]
        table = message_table(image, engine, unmarshal)
        if table is None:
            print(f"{entry['type_index']} {short}: unmarshal 0x{unmarshal:X}, no message table")
            continue
        invoke = pointer(image, table + 8 * INVOKE_SLOT)
        writer = pointer(image, table + 8 * WRITER_SLOT)
        reader = pointer(image, table + 8 * READER_SLOT)
        calls = " ".join(f"0x{target:X}" for target in direct_calls(image, engine, invoke))
        print(f"{entry['type_index']} {short}: unmarshal 0x{unmarshal:X} table 0x{table:X} "
              f"invoke 0x{invoke:X} writer 0x{writer:X} reader 0x{reader:X} calls {calls}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Decode ActionListComponentServerFacet_ReceiveInputFromClient (type 1589) of a captured run.

Usage: input_packet.py RUN_DIRECTORY [--max N] [--changes] [--all] [--session N]

RUN_DIRECTORY is the capture directory of a run (DATA_DIR/captures). Without --session the tool
reads the session folders rep/s0001, rep/s0002, ... in sequence.

Layout (client readers, image-relative): body 0x4867C70, packet 0x2A2E490, header 0x2A2E6B0,
entry 0x2A25D90. The entries are newest first. Each entry is delta-coded against the entry
before it in the packet (the first one against a default entry), thus a field that is absent
keeps the value of the entry before it.

  --changes   print a message only when its newest entry differs from the one before
  --all       print each entry of each message
  --summary   print only the counts: the flags of the newest entries, and how many of them
              have the position of the character

Fields of an entry in the output: flags (entry +0x0; bit 15, 0x8000, is the position flag
that the game hook sets), pos (the camera position), posf0 (the position of the character:
entry +0xF0, only with flag bit 15), target (the kind of the target data; a kind that is not
3 has 17 more bytes: targetdata).
"""

import argparse
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from client_msgs import messages  # noqa: E402

TYPE_INPUT = 1589


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def u8(self) -> int:
        value = self.data[self.position]
        self.position += 1
        return value

    def raw(self, size: int) -> bytes:
        value = self.data[self.position:self.position + size]
        if len(value) != size:
            raise IndexError("short read")
        self.position += size
        return value

    def u16(self) -> int:
        return struct.unpack(">H", self.raw(2))[0]

    def u32(self) -> int:
        return struct.unpack(">I", self.raw(4))[0]

    def u64(self) -> int:
        return struct.unpack(">Q", self.raw(8))[0]

    def f32(self) -> float:
        return struct.unpack(">f", self.raw(4))[0]

    def size(self) -> int:
        """Prefix code (0x87B5C0, 0x87B770): N leading one bits give N more bytes, low bits first."""
        first = self.u8()
        extra = 0
        while extra < 8 and first & (0x80 >> extra):
            extra += 1
        value = first & (0x7F >> extra) if extra < 7 else 0
        shift = 7 - extra if extra < 7 else 0
        for _ in range(extra):
            value |= self.u8() << shift
            shift += 8
        return value

    def half(self) -> float:
        """IEEE half float, big-endian (0x87AE90)."""
        return struct.unpack(">e", self.raw(2))[0]

    def quantized16(self, low: float, high: float) -> float:
        return low + self.u16() / 65535.0 * (high - low)

    def world_position(self):
        """x:f32 y:f32 height:u16 over [-100, 1000] (0x2A19B20)."""
        x = self.f32()
        y = self.f32()
        return (x, y, self.quantized16(-100.0, 1000.0))


def read_header(reader: Reader) -> dict:
    header = {}
    header["b9"] = reader.u8()
    header["b10"] = reader.u8()
    header["b8"] = reader.u8()
    header["b11"] = reader.u8()
    header["q10"] = reader.size()
    header["d1c8"] = reader.size()
    header["d18"] = reader.size()
    header["b1d"] = reader.u8()
    header["ref"] = reader.size() if reader.u8() else None
    header["ref2"] = reader.u64()
    header["b1c"] = reader.u8()
    header["w20"] = reader.size()
    header["w22"] = reader.size()
    header["w24"] = reader.size()
    header["b1e"] = reader.u8()
    count = reader.size()
    header["list"] = [(reader.size(), reader.size()) for _ in range(count)]
    header["q1b0"] = reader.size()
    header["q1b8"] = reader.size()
    header["d1c0"] = reader.size()
    header["d1c4"] = reader.size()
    return header


def read_entry(reader: Reader, previous: dict) -> dict:
    entry = dict(previous)
    mask = int.from_bytes(reader.raw(3), "little")
    entry["mask"] = mask
    entry["seq"] = reader.u16()
    if mask & 0x01:
        entry["flags"] = reader.size()
    if mask & 0x02:
        entry["b8"] = reader.u8()
    if mask & 0x04:
        entry["bc"] = reader.u8()
    if mask & 0x08:
        entry["b28"] = reader.u8()
    if mask & 0x10:
        entry["f2c"] = round(reader.quantized16(0.0, 10.0), 4)
    count = reader.u8()
    values = []
    remaining = count
    while remaining > 0:
        if remaining < 2:
            values.append(reader.size())
            remaining -= 1
        else:
            pair = reader.size()
            values += [pair & 0xFFFFFFFF, pair >> 32]
            remaining -= 2
    entry["actions"] = values
    entry["axes"] = tuple(round(reader.u8() / 256.0 * 2.0 - 1.0, 3) for _ in range(4))
    if mask & 0x20:
        entry["fc0"] = reader.f32()
    if mask & 0x40:
        # Camera direction: yaw about Z, then pitch about X (0x87B4A0).
        entry["look"] = (round(reader.f32(), 4), round(reader.f32(), 4))
    if mask & 0x80:
        entry["pos"] = reader.world_position()
    kind = reader.u8()
    entry["target"] = kind
    if kind != 3:
        # Target data (reader 0x416EB00): sub kind, a world position, three 16-bit values.
        # Static proof only: no capture had it when this code was written.
        entry["targetdata"] = (reader.u8(), reader.world_position(), (reader.u16(), reader.u16(), reader.u16()))
    else:
        entry.pop("targetdata", None)
    flags = entry.get("flags", 0)
    if mask & 0x100:
        raise NotImplementedError("mask bit 8 block")
    if flags & 0x1:
        entry["d350"] = tuple(reader.size() - 1 for _ in range(3))
    bit = 9
    if flags & 0x800:
        if mask & (1 << 9):
            entry["pos360"] = reader.world_position()
        bit = 10
    if flags & 0x100:
        # Static proof only (reader 0x2A25D90): a world position, then 16 bytes and a u64.
        if mask & (1 << bit):
            entry["pos3c0"] = reader.world_position()
        if mask & (1 << (bit + 1)):
            entry["q3e0"] = reader.raw(24).hex()
        bit += 2
    if mask & (1 << bit):
        entry["b380"] = reader.u8()
    bit += 1
    if entry.get("b380", 0):
        raise NotImplementedError("b380 block")
    if flags & 0x4000:
        if mask & (1 << bit):
            entry["q3f8"] = reader.raw(8).hex()
        bit += 1
    lists = [reader.size() for _ in range(1)]
    if lists[0]:
        raise NotImplementedError("list 0x400")
    lists.append(reader.size())
    if lists[1]:
        raise NotImplementedError("list 0x420")
    lists.append(reader.size())
    if lists[2]:
        raise NotImplementedError("list 0x440")
    if flags & 0x8000:
        if mask & (1 << bit):
            entry["posf0"] = reader.world_position()
        bit += 1
    if mask & (1 << bit):
        entry["v100"] = (reader.half(), reader.half(), reader.half())
    if flags & 0x10000:
        raise NotImplementedError("flags bit 16 block")
    if flags & 0x40000:
        entry["b35c"] = reader.u8()
    if mask & (1 << (bit + 1)):
        entry["b470"] = reader.u8()
    if mask & (1 << (bit + 2)):
        raise NotImplementedError("0x474 block")
    if flags & 0x1 and mask & (1 << (bit + 3)):
        entry["v478"] = reader.half()
    return entry


def decode(body: bytes):
    reader = Reader(body)
    component = reader.u64()
    entity = reader.u64()
    header = read_header(reader)
    count = reader.u8()
    entries = []
    previous = {}
    for _ in range(count):
        previous = read_entry(reader, previous)
        entries.append(previous)
    return component, entity, header, entries, len(body) - reader.position


def brief(entry: dict) -> str:
    keys = [key for key in entry if key not in ("mask", "seq")]
    return f"seq {entry['seq']:5d} mask {entry['mask']:06x} " + " ".join(
        f"{key}={entry[key]:#x}" if key == "flags" else f"{key}={entry[key]}" for key in keys)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run")
    parser.add_argument("--max", type=int, default=100000)
    parser.add_argument("--changes", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--session", type=int, help="read only the folder of this session (1, 2, ...)")
    arguments = parser.parse_args()
    last_key = None
    last_header = None
    shown = 0
    total = failed = 0
    flag_counts: dict[int, int] = {}
    with_flag = with_position = with_camera = with_target = 0
    for name, type_index, body in messages(Path(arguments.run), arguments.session):
        if type_index != TYPE_INPUT:
            continue
        total += 1
        try:
            component, entity, header, entries, rest = decode(body)
        except (NotImplementedError, IndexError) as error:
            failed += 1
            print(f"{name} NOT DECODED ({error}) {body[16:].hex()}")
            continue
        newest = entries[0] if entries else {}
        flags = newest.get("flags", 0)
        flag_counts[flags] = flag_counts.get(flags, 0) + 1
        with_flag += 1 if flags & 0x8000 else 0
        with_position += 1 if "posf0" in newest else 0
        with_camera += 1 if "pos" in newest else 0
        with_target += 1 if newest.get("target", 3) != 3 else 0
        if arguments.summary:
            continue
        key = tuple((k, str(v)) for k, v in newest.items() if k not in ("seq", "mask"))
        header_key = {k: v for k, v in header.items() if k not in ("d1c8",)}
        if arguments.changes and key == last_key and header_key == last_header and rest == 0:
            continue
        last_key, last_header = key, header_key
        print(f"{name} component {component:016x} entity {entity:016x} rest {rest}")
        print("   header " + " ".join(f"{k}={v}" for k, v in header.items()))
        for entry in (entries if arguments.all else entries[:1]):
            print("   " + brief(entry))
        shown += 1
        if shown >= arguments.max:
            break
    print(f"messages {total}, not decoded {failed}")
    if arguments.summary:
        print(f"newest entries with a camera position: {with_camera}")
        print(f"newest entries with the position flag (flag bit 15): {with_flag}")
        print(f"newest entries with the position of the character (posf0): {with_position}")
        print(f"newest entries with target data (a kind that is not 3): {with_target}")
        for flags, count in sorted(flag_counts.items(), key=lambda item: -item[1]):
            print(f"  flags {flags:#x}: {count}")


if __name__ == "__main__":
    main()

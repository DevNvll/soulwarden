#!/usr/bin/env python3
"""Decode ActionListComponentServerFacet_ReceiveInputFromClient (type 1589) of a captured run.

Usage: input_packet.py RUN_DIRECTORY [--max N] [--changes] [--all] [--summary] [--contacts]
                       [--session N]

RUN_DIRECTORY is the capture directory of a run (DATA_DIR/captures). Without --session the tool
reads the session folders rep/s0001, rep/s0002, ... in sequence.

Layout (client readers, image-relative): body 0x4867C70, packet 0x2A2E490, header 0x2A2E6B0,
entry 0x2A25D90. The entries are newest first. Each entry is delta-coded against the entry
before it in the packet (the first one against a default entry), thus a field that is absent
keeps the value of the entry before it.

  --changes   print a message only when its newest entry differs from the one before
  --all       print each entry of each message
  --summary   print only the counts: the flags of the newest entries, how many of them
              have the position of the character, and the items of the three lists
  --contacts  print each entry that has an item in one of its three lists, one time for
              each input number (an entry is in up to 5 reports)

Fields of an entry in the output: flags (entry +0x0; bit 15, 0x8000, is the position flag
that the game hook sets), pos (the camera position), posf0 (the position of the character:
entry +0xF0, only with flag bit 15), target (the kind of the target data; a kind that is not
3 has 17 more bytes: targetdata), lists (the item counts of the three lists).

The three lists of an entry (protocol notes 4.7): list 1 has the hits of a projectile with no
flight (104 bytes for each item), list 2 has the shots (192 bytes), and list 3 has the melee
contacts (96 bytes). The client fills list 3 only with its setting
javelin.enable-client-authoritative-melee on (protocol notes 2.3). An item of list 3:

  provider:u32  target group (actor id:16 bytes, G:u64)  receiver (actor id, G, facet target:u64)
  point:v3  normal:v3  direction:v3

The tool reads each entry to its last byte: a report with bytes left is "NOT DECODED".
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

    def boolean(self) -> int:
        value = self.u8()
        if value > 1:
            raise IndexError("a bool that is not 0 or 1")
        return value

    def v3(self):
        """Three f32 values (0x15ACFC0)."""
        return (self.f32(), self.f32(), self.f32())

    def group(self) -> dict:
        """Group reference: the actor id (16 raw bytes) and G."""
        return {"actor": self.raw(16).hex(), "g": self.u64()}

    def facet(self) -> dict:
        """Facet reference: the actor id, G, and the target of the facet."""
        return {"actor": self.raw(16).hex(), "g": self.u64(), "target": self.u64()}

    def text(self) -> str:
        return self.raw(self.size()).hex()


def read_projectile_hit(reader: Reader) -> dict:
    """Item of list 1, 104 bytes (0x2A1A310, 0x2A195D0)."""
    return {"origin": reader.v3(), "point": reader.v3(), "receiver": reader.facet(), "group": reader.group(),
            "token": reader.u64(), "ammunition": reader.u32(), "row": reader.u32(), "weapon": reader.u32(),
            "attack": reader.u32()}


def read_projectile_data(reader: Reader) -> dict:
    """Item of list 2, 192 bytes (0x2A1A0D0, 0x2A1A590)."""
    return {"origin": reader.v3(), "direction": reader.v3(), "offset": reader.v3(), "point": reader.v3(),
            "identifier": reader.raw(16).hex(), "sub": reader.u32(), "owner": reader.group(),
            "receiver": reader.facet(), "group": reader.group(), "token": reader.u64(), "q": reader.u64(),
            "f100": reader.f32(), "f104": reader.f32(), "charge": reader.f32(), "ammunition": reader.u32(),
            "attack": reader.u32(), "row": reader.u32(), "b118": reader.boolean(), "b119": reader.boolean(),
            "category": reader.u8(), "b11b": reader.boolean()}


def read_contact(reader: Reader) -> dict:
    """Item of list 3, 96 bytes: one melee contact (0x2A19E60, 0x2A18490)."""
    return {"provider": reader.u32(), "group": reader.group(), "receiver": reader.facet(), "point": reader.v3(),
            "normal": reader.v3(), "direction": reader.v3()}


LIST_ITEMS = ((read_projectile_hit, 104), (read_projectile_data, 192), (read_contact, 96))


def read_block8(reader: Reader) -> dict:
    """The object of mask bit 8 (entry +0x140, reader 0x35EBC30). Its meaning is not known."""
    block = {"present": reader.boolean()}
    if not block["present"]:
        return block
    block["ref"] = (reader.raw(16).hex(), reader.u64(), reader.u64())
    block["b30"] = (reader.u8(), reader.u8(), reader.u64(), reader.u32())
    block["t48"] = (reader.text(), reader.u32(), reader.u32())
    block["f78"] = (reader.f32(), reader.boolean())
    block["texts"] = tuple(reader.text() for _ in range(4))
    block["f7c"] = reader.f32()
    block["b81"] = reader.boolean()
    if block["b81"]:
        block["vectors"] = (reader.v3(), reader.v3())
    block["b150"] = (reader.boolean(), reader.u32(), reader.u8(), reader.u32())
    block["d128"] = tuple(reader.u32() for _ in range(4))
    block["q168"] = (reader.size(), reader.u32(), reader.size(), reader.text())
    block["d1a4"] = (reader.u32(), reader.u8(), reader.boolean())
    block["d1e8"] = (reader.u32(), reader.text(), reader.u32(), reader.u64())
    block["d200"] = (reader.u32(), reader.boolean())
    return block


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
        entry["block8"] = read_block8(reader)
    else:
        entry.pop("block8", None)
    if flags & 0x1:
        entry["d350"] = tuple(reader.size() - 1 for _ in range(3))
    # From here each field takes the next mask bit, and a field of a flag takes its bit only
    # when the entry has the flag (reader 0x2A25D90).
    bit = 9
    if flags & 0x800:
        if mask & (1 << bit):
            entry["pos360"] = reader.world_position()
        bit += 1
    if flags & 0x100:
        if mask & (1 << bit):
            entry["pos3c0"] = reader.world_position()
        if mask & (1 << (bit + 1)):
            entry["q3e0"] = reader.raw(24).hex()
        bit += 2
    if mask & (1 << bit):
        entry["b380"] = reader.u8()
    bit += 1
    if entry.get("b380", 0):
        if mask & (1 << bit):
            entry["pos370"] = reader.world_position()
        if mask & (1 << (bit + 1)):
            entry["r388"] = reader.raw(32).hex()
        bit += 2
    if flags & 0x4000:
        if mask & (1 << bit):
            entry["q3f8"] = reader.raw(8).hex()
        bit += 1
    # The three lists. The items of an entry are its own: an entry does not take them from
    # the entry before it.
    lists = []
    for read_item, width in LIST_ITEMS:
        count = reader.size()
        if count * width > len(reader.data) - reader.position:
            raise IndexError(f"a list with {count} items is longer than the report")
        lists.append([read_item(reader) for _ in range(count)])
    entry["lists"] = tuple(len(items) for items in lists)
    entry["items"] = lists
    if flags & 0x8000:
        if mask & (1 << bit):
            entry["posf0"] = reader.world_position()
        bit += 1
    if mask & (1 << bit):
        entry["v100"] = (reader.half(), reader.half(), reader.half())
    bit += 1
    if flags & 0x10000:
        entry["q468"] = reader.u64()
    if flags & 0x40000:
        entry["b35c"] = reader.u8()
    if mask & (1 << bit):
        entry["b470"] = reader.u8()
    if mask & (1 << (bit + 1)):
        entry["d474"] = reader.u32()
    if flags & 0x1 and mask & (1 << (bit + 2)):
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


def action_bits(entry: dict) -> list:
    """The numbers of the action bits of an entry: bit 0 is the lowest bit of the first word."""
    return [index * 32 + bit for index, word in enumerate(entry.get("actions", [])) for bit in range(32)
            if word >> bit & 1]


def vector(value) -> str:
    return "(" + " ".join(f"{part:.3f}" for part in value) + ")"


def contact_text(item: dict) -> str:
    group, receiver = item["group"], item["receiver"]
    same = "the same group" if (receiver["actor"], receiver["g"]) == (group["actor"], group["g"]) else \
        f"group actor {group['actor']} G {group['g']:016x}"
    return (f"contact provider {item['provider']:#010x} target actor {receiver['actor']} G {receiver['g']:016x} "
            f"facet {receiver['target']:016x} ({same}) point {vector(item['point'])} normal "
            f"{vector(item['normal'])} direction {vector(item['direction'])}")


def brief(entry: dict) -> str:
    keys = [key for key in entry if key not in ("mask", "seq", "items")]
    return f"seq {entry['seq']:5d} mask {entry['mask']:06x} " + " ".join(
        f"{key}={entry[key]:#x}" if key == "flags" else f"{key}={entry[key]}" for key in keys)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run")
    parser.add_argument("--max", type=int, default=100000)
    parser.add_argument("--changes", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--contacts", action="store_true")
    parser.add_argument("--session", type=int, help="read only the folder of this session (1, 2, ...)")
    arguments = parser.parse_args()
    last_key = None
    last_header = None
    shown = 0
    total = failed = 0
    flag_counts: dict[int, int] = {}
    with_flag = with_position = with_camera = with_target = 0
    # Items of the three lists, one time for each input number of a session folder.
    list_items = [0, 0, 0]
    counted: set = set()
    with_block8 = 0
    for name, type_index, body in messages(Path(arguments.run), arguments.session):
        if type_index != TYPE_INPUT:
            continue
        total += 1
        try:
            component, entity, header, entries, rest = decode(body)
        except (NotImplementedError, IndexError, struct.error) as error:
            failed += 1
            print(f"{name} NOT DECODED ({error}) {body[16:].hex()}")
            continue
        if rest:
            failed += 1
            print(f"{name} NOT DECODED ({rest} bytes left) {body[16:].hex()}")
            continue
        folder = Path(name).parent.name if "/" in str(name) else ""
        for entry in entries:
            if not any(entry["lists"]) and "block8" not in entry:
                continue
            key = (folder, entity, entry["seq"], entry["lists"])
            if key in counted:
                continue
            counted.add(key)
            with_block8 += 1 if "block8" in entry else 0
            for index, items in enumerate(entry["items"]):
                list_items[index] += len(items)
            if arguments.contacts:
                position = entry.get("posf0")
                print(f"{name} input {entry['seq']} lists {entry['lists']} flags {entry.get('flags', 0):#x}"
                      + (f" character {vector(position)}" if position else "")
                      + f" action bits {action_bits(entry)}")
                for item in entry["items"][2]:
                    print("   " + contact_text(item))
                for index in (0, 1):
                    for item in entry["items"][index]:
                        print(f"   list {index + 1}: {item}")
        newest = entries[0] if entries else {}
        flags = newest.get("flags", 0)
        flag_counts[flags] = flag_counts.get(flags, 0) + 1
        with_flag += 1 if flags & 0x8000 else 0
        with_position += 1 if "posf0" in newest else 0
        with_camera += 1 if "pos" in newest else 0
        with_target += 1 if newest.get("target", 3) != 3 else 0
        if arguments.summary or arguments.contacts:
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
    if arguments.summary or arguments.contacts:
        print(f"items of the three lists (each input number one time): projectile hits {list_items[0]}, "
              f"shots {list_items[1]}, melee contacts {list_items[2]}; entries with the object of mask bit 8: "
              f"{with_block8}")
    if arguments.summary:
        print(f"newest entries with a camera position: {with_camera}")
        print(f"newest entries with the position flag (flag bit 15): {with_flag}")
        print(f"newest entries with the position of the character (posf0): {with_position}")
        print(f"newest entries with target data (a kind that is not 3): {with_target}")
        for flags, count in sorted(flag_counts.items(), key=lambda item: -item[1]):
            print(f"  flags {flags:#x}: {count}")


if __name__ == "__main__":
    main()

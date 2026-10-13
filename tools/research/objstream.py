#!/usr/bin/env python3
"""Print an engine object stream (binary serialization of slices and data assets) as a tree.

Format (big-endian): `tag:u8 = 0`, `version:u32`, then elements. Each element starts with a
flags byte: 0x08 element, 0x10 has value, 0x20 value size in an extra field (the low 3 bits
give the size of that field), 0x40 has name (CRC-32 of the lower-case field name), 0x80 has a
class version byte. A flags byte of 0 ends the current element. After the flags: name, class
version, 16-byte type id, value size, value. A file can have the "AZCS" header (azcs.py).

Field names and class names come from the text table of the client image
(~/Games/soulwarden/work/nw.strings.txt): a field name is found by its CRC-32, a class name is
the text before the text of its type id.

Usage: objstream.py FILE [--depth N] [--grep REGEX] [--raw]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from azcs import decode  # noqa: E402

STRINGS = Path.home() / "Games/soulwarden/work/nw.strings.txt"
UUID_TEXT = re.compile(r"^\{?([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\}?$")
NAME_TEXT = re.compile(r"^[A-Za-z_][A-Za-z0-9_ :<>,.\-/()]*$")

# Type ids of the engine value types (public engine source).
KNOWN_TYPES = {
    "03aaab3f-5c47-5a66-9ebc-d5fa4db353c9": "AZStd::string",
    "ef8fb7e6-b745-4c16-a4b0-c1cc8f9e7e8c": "char",
    "a0ca880c-afe4-43cb-926c-59ac48496112": "bool",
    "72039442-eb38-4d42-a1ad-cb68f7e0eef6": "unsigned int",
    "72b9409a-7d1a-4831-9cfe-fcb3fadd3426": "int",
    "ea2c3e90-afbe-44d4-a90d-faaf79baf93d": "float",
    "110c4b14-11a8-4e9d-8638-5051013a56ac": "double",
    "d6597933-47cd-4fc8-b911-63f3e2b0993a": "AZ::u64",
    "70d8a282-a1ea-462d-9d04-51ede81fac2f": "AZ::s64",
    "72b9409a-7d1a-4831-9cfe-fcb3fadd3426 ": "int",
    "ecb0c8a1-5b36-4d6e-8b6b-1f1a5f8a1b0a": "?",
    "58422c0e-1e47-4854-98e6-34098f6fe12d": "AZ::s8",
    "b8a56d56-a10d-4dce-9f63-405ee243dd3c": "AZ::s16",
    "43da906b-7def-4ca8-9790-854106d3f983": "unsigned char",
    "ecdbb2a0-fc91-47ec-81cc-cb5cba56e4e7": "unsigned short",
    "e152c105-a133-4d03-bbf8-3d4b2fbbca8e": "AZ::Uuid",
    "6383f1d3-bb27-4e6b-a49a-6409b2059eaa": "AZ::EntityId",
    "8379eb7d-01fa-4538-b64b-a6543b4be73d": "AZ::Vector3",
    "5d9958e9-9f1e-4985-b532-ffffde75fedd": "AZ::Transform",
    "77a19d40-8731-4d3c-9041-1b43047366a4": "AZ::Data::Asset",
}


def load_names():
    """@return ({crc: field name}, {type id: class name}) from the image text table."""
    names: dict[int, str] = {}
    types: dict[str, str] = dict(KNOWN_TYPES)
    previous = ""
    with open(STRINGS, "rb") as handle:
        for line in handle:
            text = line.rstrip(b"\r\n").lstrip().partition(b" ")[2].decode("latin1")
            match = UUID_TEXT.match(text)
            if match:
                if previous and NAME_TEXT.match(previous):
                    types.setdefault(match.group(1).lower(), previous)
            elif len(text) <= 80 and NAME_TEXT.match(text):
                names.setdefault(zlib.crc32(text.lower().encode()) & 0xFFFFFFFF, text)
            previous = text
    return names, types


class Element:
    __slots__ = ("name", "version", "type_id", "value", "children")

    def __init__(self):
        self.name = None
        self.version = None
        self.type_id = ""
        self.value = None
        self.children = []


def parse(data: bytes):
    """@return (stream version, root elements)."""
    if data[0] != 0:
        raise ValueError("not a binary object stream")
    version = struct.unpack_from(">I", data, 1)[0]
    position = 5
    roots: list[Element] = []
    stack: list[Element] = []
    while position < len(data):
        flags = data[position]
        position += 1
        if flags == 0:
            if not stack:
                break
            stack.pop()
            continue
        element = Element()
        if flags & 0x40:
            element.name = struct.unpack_from(">I", data, position)[0]
            position += 4
        if flags & 0x80:
            element.version = data[position]
            position += 1
        element.type_id = str(uuid.UUID(bytes=data[position:position + 16]))
        position += 16
        if flags & 0x10:
            size = flags & 7
            if flags & 0x20:
                size = int.from_bytes(data[position:position + size], "big")
                position += flags & 7
            element.value = data[position:position + size]
            position += size
        (stack[-1].children if stack else roots).append(element)
        stack.append(element)
    return version, roots


def show_value(type_name: str, value: bytes) -> str:
    if type_name in ("AZStd::string", "char"):
        return repr(value.decode("utf-8", "replace"))
    if type_name == "bool" and len(value) == 1:
        return "true" if value[0] else "false"
    if type_name == "float" and len(value) == 4:
        return f"{struct.unpack('>f', value)[0]:g}"
    if type_name == "double" and len(value) == 8:
        return f"{struct.unpack('>d', value)[0]:g}"
    if type_name == "AZ::Uuid" and len(value) == 16:
        return str(uuid.UUID(bytes=value))
    if type_name == "AZ::EntityId" and len(value) == 8:
        return f"0x{int.from_bytes(value, 'big'):016X}"
    if len(value) in (1, 2, 4, 8) and (type_name.startswith(("AZ::s", "AZ::u", "int", "unsigned")) or len(value) <= 4):
        return f"{int.from_bytes(value, 'big')} (0x{value.hex()})"
    if type_name == "AZ::Data::Asset" and len(value) >= 56:
        # Asset reference: asset id (16 bytes), sub id (16 bytes), asset type (16 bytes), hint.
        return (f"asset {uuid.UUID(bytes=value[:16])}:{int.from_bytes(value[16:32], 'big')}"
                f" type {uuid.UUID(bytes=value[32:48])} hint {value[56:].decode('ascii', 'replace')!r}")
    text = value.hex()
    printable = "".join(chr(byte) if 32 <= byte < 127 else "." for byte in value)
    return f"{text[:96]}{'...' if len(text) > 96 else ''} |{printable[:120]}|"


def dump(element: Element, names, types, depth: int, limit: int, raw: bool, out: list[str]) -> None:
    name = "" if element.name is None else names.get(element.name, f"#{element.name:08X}")
    type_name = types.get(element.type_id, element.type_id)
    line = f"{'  ' * depth}{name or '-'} : {type_name}"
    if element.version is not None and raw:
        line += f" v{element.version}"
    if element.value is not None:
        line += f" = {show_value(type_name, element.value)}"
    out.append(line)
    if depth + 1 < limit:
        for child in element.children:
            dump(child, names, types, depth + 1, limit, raw, out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file")
    parser.add_argument("--depth", type=int, default=99)
    parser.add_argument("--grep")
    parser.add_argument("--raw", action="store_true", help="show class versions")
    arguments = parser.parse_args()
    version, roots = parse(decode(Path(arguments.file).read_bytes()))
    names, types = load_names()
    lines: list[str] = [f"object stream version {version}"]
    for root in roots:
        dump(root, names, types, 0, arguments.depth, arguments.raw, lines)
    pattern = re.compile(arguments.grep) if arguments.grep else None
    try:
        for line in lines:
            if pattern is None or pattern.search(line):
                print(line)
    except BrokenPipeError:
        pass


if __name__ == "__main__":
    main()

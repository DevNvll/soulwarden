#!/usr/bin/env python3
"""Extract the protobuf file descriptors that are embedded in the client image.

Generated C++ protobuf code keeps each serialized FileDescriptorProto in read-only data.
This tool finds them, bounds each one with a strict wire-format walk, and writes a
text .proto reconstruction for each file to the output directory.

Usage: extract_protos.py OUTPUT_DIRECTORY
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from google.protobuf import descriptor_pb2

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nwbin import Image, game_executable  # noqa: E402

# FileDescriptorProto field number -> wire type.
FILE_FIELDS = {1: 2, 2: 2, 3: 2, 4: 2, 5: 2, 6: 2, 7: 2, 8: 2, 9: 2, 10: 0, 11: 0, 12: 2, 14: 0}

TYPE_NAMES = {
    1: "double", 2: "float", 3: "int64", 4: "uint64", 5: "int32", 6: "fixed64", 7: "fixed32",
    8: "bool", 9: "string", 12: "bytes", 13: "uint32", 15: "sfixed32", 16: "sfixed64",
    17: "sint32", 18: "sint64",
}


def read_varint(data: bytes, position: int):
    value = 0
    shift = 0
    while position < len(data):
        byte = data[position]
        position += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, position
        shift += 7
        if shift > 63:
            break
    return None, position


def descriptor_length(data: bytes) -> int:
    """Return the length of the longest valid FileDescriptorProto prefix."""
    position = 0
    while position < len(data):
        tag, after = read_varint(data, position)
        if tag is None:
            break
        field, wire = tag >> 3, tag & 7
        if FILE_FIELDS.get(field) != wire:
            break
        if wire == 0:
            value, after = read_varint(data, after)
            if value is None:
                break
        else:
            size, after = read_varint(data, after)
            if size is None or after + size > len(data):
                break
            after += size
        position = after
    return position


def type_text(field) -> str:
    if field.type in (11, 14, 10):
        return field.type_name.lstrip(".")
    return TYPE_NAMES.get(field.type, f"type{field.type}")


def render_message(message, indent: str, lines: list[str]) -> None:
    lines.append(f"{indent}message {message.name} {{")
    inner = indent + "  "
    for enum in message.enum_type:
        render_enum(enum, inner, lines)
    map_entries = {nested.name: nested for nested in message.nested_type if nested.options.map_entry}
    for nested in message.nested_type:
        if not nested.options.map_entry:
            render_message(nested, inner, lines)
    oneofs: dict[int, list] = {}
    for field in message.field:
        if field.HasField("oneof_index") and not field.proto3_optional:
            oneofs.setdefault(field.oneof_index, []).append(field)
            continue
        entry = map_entries.get(field.type_name.split(".")[-1]) if field.type == 11 else None
        if entry is not None:
            kind = f"map<{type_text(entry.field[0])}, {type_text(entry.field[1])}>"
        else:
            label = "repeated " if field.label == 3 else ("optional " if field.proto3_optional else "")
            kind = label + type_text(field)
        lines.append(f"{inner}{kind} {field.name} = {field.number};")
    for index, fields in oneofs.items():
        lines.append(f"{inner}oneof {message.oneof_decl[index].name} {{")
        for field in fields:
            lines.append(f"{inner}  {type_text(field)} {field.name} = {field.number};")
        lines.append(f"{inner}}}")
    lines.append(f"{indent}}}")


def render_enum(enum, indent: str, lines: list[str]) -> None:
    lines.append(f"{indent}enum {enum.name} {{")
    for value in enum.value:
        lines.append(f"{indent}  {value.name} = {value.number};")
    lines.append(f"{indent}}}")


def render_file(descriptor) -> str:
    lines = [f'syntax = "{descriptor.syntax or "proto2"}";', f"// source: {descriptor.name}"]
    if descriptor.package:
        lines.append(f"package {descriptor.package};")
    for dependency in descriptor.dependency:
        lines.append(f'import "{dependency}";')
    lines.append("")
    for enum in descriptor.enum_type:
        render_enum(enum, "", lines)
    for message in descriptor.message_type:
        render_message(message, "", lines)
    for service in descriptor.service:
        lines.append(f"service {service.name} {{")
        for method in service.method:
            client = "stream " if method.client_streaming else ""
            server = "stream " if method.server_streaming else ""
            lines.append(
                f"  rpc {method.name}({client}{method.input_type.lstrip('.')}) "
                f"returns ({server}{method.output_type.lstrip('.')});"
            )
        lines.append("}")
    return "\n".join(lines) + "\n"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    image = Image(game_executable())
    found = 0
    for name, rva, virtual_size, raw_pointer, raw_size, _ in image.sections:
        if name not in (".rdata", ".data"):
            continue
        blob = image.data[raw_pointer:raw_pointer + raw_size].tobytes()
        for match in re.finditer(rb"\x0a([\x05-\x7f])([A-Za-z0-9_./\-]{4,126}\.proto)", blob):
            if match.group(1)[0] != len(match.group(2)):
                continue
            start = match.start()
            length = descriptor_length(blob[start:start + 4 * 1024 * 1024])
            if length < 16:
                continue
            descriptor = descriptor_pb2.FileDescriptorProto()
            try:
                descriptor.ParseFromString(blob[start:start + length])
            except Exception:
                continue
            if not (descriptor.message_type or descriptor.enum_type or descriptor.service):
                continue
            target = output / descriptor.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(render_file(descriptor))
            (output / (descriptor.name + ".bin")).write_bytes(blob[start:start + length])
            found += 1
            print(f"0x{rva + start:X} {length:7d} {descriptor.name} messages={len(descriptor.message_type)} services={len(descriptor.service)}")
    print(f"{found} descriptors")


if __name__ == "__main__":
    main()

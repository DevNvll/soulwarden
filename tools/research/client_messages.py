#!/usr/bin/env python3
"""Group the captured client actor messages of one run by type index and print samples.

Input: a capture directory of the backend (`NNNNN-msg-LEN.bin`, one client frame each;
format in apps/backend/src/service/rep_server.cpp and section 3.3 of the protocol notes):

    frame    := crc32:u32 size:u32 payload
    payload  := route:16 bytes envelope
    envelope := flags:u8 [8 bytes if bit 0] [8 bytes if bit 1] present:u8 type:size body

A body of a `Javelin::ClientMessages::<Component>ServerFacet_<Method>` message starts with the
facet address (component type id: 8 bytes, entity id: 8 bytes). The tool splits that prefix.

Usage: client_messages.py [CAPTURE_DIR] [--type N] [--samples N] [--all] [--session N]

CAPTURE_DIR is the folder DATA_DIR/captures/rep of a run, or the folder of one session
(DATA_DIR/captures/rep/s0001). The game server keeps the files of each session in its own
folder with its own packet counter. Without --session the tool reads all sessions in sequence,
and each packet number has its session folder ("s0001/00012").
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zlib
from collections import defaultdict
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from client_msgs import capture_files  # noqa: E402

WORK = Path.home() / "Games/soulwarden/work"
DEFAULT_CAPTURE = WORK / "research/run-r056/rep"
ROUTE_SIZE = 16
FACET_PREFIX = "Javelin::ClientMessages::"


def read_size(data: bytes, position: int) -> tuple[int, int]:
    """@return (value, next position) of one `size` prefix code."""
    first = data[position]
    if first < 0x80:
        return first, position + 1
    if first < 0xC0:
        return (first & 0x3F) | (data[position + 1] << 6), position + 2
    if first < 0xE0:
        return (first & 0x1F) | (data[position + 1] << 5) | (data[position + 2] << 13), position + 3
    if first < 0xF0:
        value = (first & 0x0F) | (data[position + 1] << 4) | (data[position + 2] << 12) | (data[position + 3] << 20)
        return value, position + 4
    value = (first & 0x07) | (data[position + 1] << 3) | (data[position + 2] << 11) | (data[position + 3] << 19)
    return value | (data[position + 4] << 27), position + 5


def decode(frame: bytes):
    """@return (route, type index, body) of one client frame, or None for a bad frame."""
    if len(frame) < 8 + ROUTE_SIZE + 3:
        return None
    checksum = int.from_bytes(frame[0:4], "big")
    size = int.from_bytes(frame[4:8], "big")
    payload = frame[8:8 + size]
    if len(payload) != size or zlib.crc32(payload) != checksum:
        return None
    route = payload[:ROUTE_SIZE]
    position = ROUTE_SIZE
    flags = payload[position]
    position += 1
    position += 8 if flags & 1 else 0
    position += 8 if flags & 2 else 0
    if flags & 4 or payload[position] != 1:
        return None
    type_index, position = read_size(payload, position + 1)
    return route, type_index, payload[position:]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("capture", nargs="?", default=str(DEFAULT_CAPTURE))
    parser.add_argument("--type", type=int, action="append", help="print only this type index")
    parser.add_argument("--samples", type=int, default=3, help="different bodies to print for each type")
    parser.add_argument("--all", action="store_true", help="print each message in arrival order")
    parser.add_argument("--session", type=int, help="read only the folder of this session (1, 2, ...)")
    arguments = parser.parse_args()

    names = {}
    for entry in json.loads((WORK / "hubtypes.json").read_text()):
        if entry.get("type_index") is not None:
            names[entry["type_index"]] = entry["name"]

    groups: dict[int, list[tuple[str, bytes, bytes]]] = defaultdict(list)
    order: list[int] = []
    bad = 0
    files = capture_files(Path(arguments.capture), "msg", arguments.session)
    for label, path in files:
        decoded = decode(path.read_bytes())
        if decoded is None:
            bad += 1
            continue
        route, type_index, body = decoded
        # The packet number, with the session folder before it when the run has session folders.
        sequence = re.match(r"((?:s\d+/)?\d+)-", label).group(1)
        if type_index not in groups:
            order.append(type_index)
        groups[type_index].append((sequence, route, body))
        if arguments.all and (not arguments.type or type_index in arguments.type):
            print(f"{sequence} type {type_index:5d} {len(body):4d} {body.hex()}")
    if arguments.all:
        return

    print(f"{len(files)} messages, {bad} not decoded, {len(groups)} types")
    for type_index in order:
        if arguments.type and type_index not in arguments.type:
            continue
        rows = groups[type_index]
        name = names.get(type_index, "?")
        sizes = sorted({len(body) for _, _, body in rows})
        routes = sorted({route.hex() for _, route, _ in rows})
        print(f"\n{type_index} {name}")
        print(f"  count {len(rows)}, first packet {rows[0][0]}, body sizes {sizes}")
        for route in routes:
            print(f"  route {route}")
        seen: set[bytes] = set()
        for sequence, _, body in rows:
            if body in seen:
                continue
            seen.add(body)
            if len(seen) > arguments.samples:
                break
            if name.startswith(FACET_PREFIX) and len(body) >= 16:
                print(f"  {sequence} component {body[:8].hex()} entity {body[8:16].hex()} args {body[16:].hex()}")
            else:
                print(f"  {sequence} body {body.hex()}")
        print(f"  different bodies: {len({body for _, _, body in rows})}")


if __name__ == "__main__":
    main()

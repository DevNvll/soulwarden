#!/usr/bin/env python3
"""Compare the ability table list of the live client with the list of the backend.

The client finds the ability bar of a weapon by the index of its ability table in the list of
its ability data manager (protocol notes 4.13). The same index is the table index of the
requests of the client for a bar and for a tree reset. The client makes the list at its start
from its tables of the type AbilityData, in a sequence that the game files do not give. Thus
the backend has the sequence as a constant of the supported client build
(`kAbilityTableOrder` in apps/backend/src/gamedata/progression_tables.h). A list that is not
equal puts the abilities of a weapon on the bar of a different weapon, with no log line.

The tool reads the list from the memory of the client through the automation harness
(tools/hx): the pointer at image +0xA3370A8, then the pointer at that address (the manager),
then the begin and the end of the list at manager +0x3E8 and +0x3F0. Each entry is the id of
one table (the CRC-32 of its lower-case name). The client has the list in the world.

Usage: ability_tables.py
Status 0: each entry is equal. Status 1: a difference, or no read.
Run it after a change of the game copy. Do not run two harness tools at the same time: the
harness has one command file.
"""

from __future__ import annotations

import re
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

REPOSITORY = Path(__file__).resolve().parents[2]
HX = REPOSITORY / "tools/hx"
HEADER = REPOSITORY / "apps/backend/src/gamedata/progression_tables.h"
IMAGE = 0x140000000
MANAGER_POINTER = 0xA3370A8
LIST_BEGIN = 0x3E8
# The harness gives 512 bytes at most for one read: 128 entries.
LARGEST_LIST = 512


def read(address: int, size: int) -> bytes:
    output = subprocess.run([str(HX), f"read 0x{address:x} {size}"], capture_output=True, text=True, check=False).stdout
    lines = output.strip().splitlines()
    try:
        return bytes.fromhex(lines[-1].strip()) if lines else b""
    except ValueError:
        return b""


def client_list() -> list[int]:
    holder = read(IMAGE + MANAGER_POINTER, 8)
    if len(holder) != 8:
        sys.exit("ability-tables: no read of the client memory (is a development run alive?)")
    manager = read(struct.unpack("<Q", holder)[0], 8)
    if len(manager) != 8 or manager == bytes(8):
        sys.exit("ability-tables: the client has no ability data manager now")
    bounds = read(struct.unpack("<Q", manager)[0] + LIST_BEGIN, 16)
    if len(bounds) != 16:
        sys.exit("ability-tables: no read of the list of the manager")
    begin, end = struct.unpack("<QQ", bounds)
    if end < begin or end - begin > LARGEST_LIST or (end - begin) % 4 != 0:
        sys.exit("ability-tables: the list of the manager does not have the expected form")
    data = read(begin, end - begin)
    if len(data) != end - begin:
        sys.exit("ability-tables: no read of the entries of the list")
    return list(struct.unpack(f"<{len(data) // 4}I", data))


def backend_list() -> list[int]:
    """@return The numbers of the constant of the backend, in its sequence."""
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(r"kAbilityTableOrder\{(.*?)\};", text, re.DOTALL)
    if not match:
        sys.exit(f"ability-tables: no constant kAbilityTableOrder in {HEADER.relative_to(REPOSITORY)}")
    return [int(number, 16) for number in re.findall(r"0x([0-9A-Fa-f]{8})", match.group(1))]


def main() -> int:
    if len(sys.argv) > 1:
        print(__doc__.strip())
        return 0 if sys.argv[1] in ("-h", "--help") else 1
    client = client_list()
    backend = backend_list()
    different = 0
    for index in range(max(len(client), len(backend))):
        ours = f"{backend[index]:08X}" if index < len(backend) else "none"
        theirs = f"{client[index]:08X}" if index < len(client) else "none"
        mark = "" if ours == theirs else "   <- not equal"
        different += 1 if mark else 0
        print(f"{index:2d}  client {theirs}  backend {ours}{mark}")
    if different:
        print(f"ability-tables: {different} of {max(len(client), len(backend))} entries are not equal. Put the list of the "
              f"client into kAbilityTableOrder.")
        return 1
    print(f"ability-tables: the {len(client)} entries of the client are equal to the list of the backend")
    return 0


if __name__ == "__main__":
    sys.exit(main())

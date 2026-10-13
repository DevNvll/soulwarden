#!/usr/bin/env python3
"""Print the action table of a creature from the memory of the live client.

A movement state gives the action of a creature as two indexes: the index of the action in
the action table that the client makes for the slice of the creature, and the index of its
animation fragment (protocol notes 4.14). The backend reads these tables from the creature
data file (tools/gamedata/creatures.sh), which computes them from the game files. This tool
reads the table that the client has, and it can compare it with the file: run it after a
change of the game copy or of the tool.

Run the game with SOULWARDEN_TRACE=683E670 (the apply function of the vitals facet), and have a
creature near the character (tools/world creature spawn KIND CHARACTER). The tool takes the
vitals facet of the newest call of the trace, goes to its component (facet +8), to the entity
group of the component (+0x60), and through the component list of the group (+0x160) to the
movement component: the component with the class table of the action list component of a
creature (image address 0x83865A0). The table of the runtime actions is at movement
component +0xB58 (begin and end; one action has 0x1A0 bytes: the name at +0x90, the index at
+0x130, the fragment name at +0x138, the fragment index at +0x164, 0xFFFFFFFF for none).
Addresses apply to the client build 1.400.6031.6004151.

Usage: creature_actions.py [--call N] [--compare CREATURES.json SLICE]
  --call N        use call number N of the trace point, not the newest one
  --compare F S   compare the table with the action table of the slice S of the creature data
                  file F (S: a part of the slice path, for example wolf_grey_5). Status 0: equal.
Do not run two harness tools at the same time: the harness has one command file.
"""

from __future__ import annotations

import json
import re
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_env  # noqa: E402

DATA = dev_env.data_dir()
HX = Path(__file__).resolve().parents[1] / "hx"
IMAGE_BASE = 0x140000000
MOVEMENT_TABLE = 0x83865A0
ACTIONS = 0xB58
ACTION_SIZE = 0x1A0


def read(address: int, size: int) -> bytes:
    """@return The bytes of the client memory (the harness reads 512 bytes at most in one call)."""
    data = b""
    while size > 0:
        part = min(size, 512)
        lines = subprocess.run([str(HX), f"read 0x{address:x} {part}"], capture_output=True, text=True,
                               check=False).stdout.strip().splitlines()
        try:
            chunk = bytes.fromhex(lines[-1]) if lines else b""
        except ValueError:
            chunk = b""
        if len(chunk) != part:
            sys.exit(f"creature-actions: no read of 0x{address:x}")
        data += chunk
        address += part
        size -= part
    return data


def qword(address: int) -> int:
    return struct.unpack("<Q", read(address, 8))[0]


def text_at(action: bytes, offset: int) -> str:
    """@return A text of the client: 16 bytes in place, or a pointer when its capacity is larger."""
    size, capacity = struct.unpack_from("<QQ", action, offset + 0x10)
    if capacity <= 15:
        return action[offset:offset + min(size, 15)].decode("latin1")
    return read(struct.unpack_from("<Q", action, offset)[0], min(size, 200)).decode("latin1")


def main() -> int:
    arguments = sys.argv[1:]
    call = None
    compare = None
    if "--call" in arguments:
        call = int(arguments[arguments.index("--call") + 1])
    if "--compare" in arguments:
        place = arguments.index("--compare")
        compare = (arguments[place + 1], arguments[place + 2])
    facet = 0
    for line in (DATA / "client/logs/hook.log").read_text(errors="replace").splitlines():
        match = re.search(r"trace: 0x683E670 #(\d+) rcx=(\w+)", line)
        if match and (call is None or int(match.group(1)) == call):
            facet = int(match.group(2), 16)
    if not facet:
        sys.exit("creature-actions: no call of the trace point 0x683E670 in the hook log (SOULWARDEN_TRACE=683E670)")
    group = qword(qword(facet + 8) + 0x60)
    begin, end = struct.unpack("<QQ", read(group + 0x160, 16))
    movement = 0
    for slot in range((end - begin) // 8):
        component = qword(begin + 8 * slot)
        if component and qword(component) - IMAGE_BASE == MOVEMENT_TABLE:
            movement = component
            print(f"vitals facet 0x{facet:x}, entity group 0x{group:x}, movement component in slot {slot}")
    if not movement:
        sys.exit("creature-actions: the entity group of the facet has no action list component of a creature")
    first, last = struct.unpack("<QQ", read(movement + ACTIONS, 16))
    count = (last - first) // ACTION_SIZE
    table = read(first, count * ACTION_SIZE)
    rows = []
    for index in range(count):
        action = table[index * ACTION_SIZE:(index + 1) * ACTION_SIZE]
        fragment = struct.unpack_from("<I", action, 0x164)[0]
        rows.append([text_at(action, 0x90), text_at(action, 0x138), -1 if fragment == 0xFFFFFFFF else fragment])
        if struct.unpack_from("<I", action, 0x130)[0] != index:
            sys.exit(f"creature-actions: action {index} does not have its index at +0x130")
        print(f"{index:3d}  {rows[-1][0]:<34} fragment {rows[-1][1]:<30} {rows[-1][2]}")
    print(f"{count} actions")
    if compare is None:
        return 0
    document = json.loads(Path(compare[0]).read_text())
    slices = [entry for entry in document["slices"] if compare[1] in entry["path"]]
    if len(slices) != 1:
        sys.exit(f"creature-actions: {len(slices)} slices of the file have {compare[1]} in their path")
    expected = [row[:3] for row in slices[0]["actions"]]
    if expected == rows:
        print(f"equal to the action table of {slices[0]['path']} in the file")
        return 0
    for index in range(max(len(expected), len(rows))):
        ours = expected[index] if index < len(expected) else None
        theirs = rows[index] if index < len(rows) else None
        if ours != theirs:
            print(f"DIFFERENT at index {index}: the file has {ours}, the client has {theirs}")
            break
    return 1


if __name__ == "__main__":
    sys.exit(main())

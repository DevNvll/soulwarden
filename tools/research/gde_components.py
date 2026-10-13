#!/usr/bin/env python3
"""List the faceted components of the entity group that holds the local player component.

Run the game with SOULWARDEN_TRACE=6769980 (player registration). This tool takes the player
component address from the trace, then reads the component list of its entity group through
the automation harness. It prints each fragment slot with the virtual tables of the
component and of its client facet (image-relative addresses).

Usage: gde_components.py
"""

from __future__ import annotations

import re
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_env  # noqa: E402

DATA = dev_env.data_dir()
HX = Path(__file__).resolve().parents[1] / "hx"
IMAGE_BASE = 0x140000000


def read(address: int, size: int) -> bytes:
    lines = subprocess.run([str(HX), f"read 0x{address:x} {size}"], capture_output=True, text=True, check=True)
    text = lines.stdout.strip().splitlines()[-1] if lines.stdout.strip() else ""
    try:
        return bytes.fromhex(text)
    except ValueError:
        return b""


def qword(address: int) -> int:
    data = read(address, 8)
    return struct.unpack("<Q", data)[0] if len(data) == 8 else 0


def main():
    log = DATA / "client/logs/hook.log"
    component = 0
    for _ in range(240):
        for line in log.read_text(errors="replace").splitlines():
            match = re.search(r"trace: 0x6769980 #\d+ rcx=\w+ rdx=(\w+)", line)
            if match:
                component = int(match.group(1), 16)
        if component:
            break
        time.sleep(0.5)
    if not component:
        sys.exit("no player registration in the hook log")
    group = qword(component + 0x60)
    begin, end = struct.unpack("<QQ", read(group + 0x160, 16))
    count = (end - begin) // 8
    print(f"player component 0x{component:x} group 0x{group:x} components {count}")
    table = b"".join(read(begin + offset, min(256, count * 8 - offset)) for offset in range(0, count * 8, 256))
    rows = []
    for slot in range(count):
        pointer = struct.unpack_from("<Q", table, slot * 8)[0]
        if pointer == 0:
            print(f"{slot:3d} empty")
            continue
        head = read(pointer, 0x90)
        vtable = struct.unpack_from("<Q", head, 0)[0]
        facet = struct.unpack_from("<Q", head, 0x80)[0]
        facet_table = qword(facet) if facet else 0
        mark = " <- player" if pointer == component else ""
        rows.append({"slot": slot, "component_table": vtable - IMAGE_BASE,
                     "facet_table": (facet_table - IMAGE_BASE) if facet_table else 0})
        print(
            f"{slot:3d} component 0x{pointer:x} table 0x{vtable - IMAGE_BASE:X}"
            f" facet table 0x{(facet_table - IMAGE_BASE) if facet_table else 0:X}{mark}"
        )


    return rows


if __name__ == "__main__":
    import json

    result = main()
    target = Path.home() / "Games/soulwarden/work/player_components.json"
    target.write_text(json.dumps(result, indent=1))
    print(f"wrote {target}")

#!/usr/bin/env python3
"""Print the movement state of the local player from the memory of the live client.

Run the game with SOULWARDEN_TRACE=6769980 (player registration). The tool takes the player
component of the local player from the trace (the call from the player registry), finds the movement component in the component list of its entity
group (slot 16) and the client facet of that component (component +0x80), and reads through
the automation harness:

  position      predicted state of the facet (+0xAE0), position at +0x460
  ground        facet +0x1690: 1 when the client found collision below the spawn position
  permissions   byte +8 of the current input entry (facet +0x1190): bit 0 sprint, bit 1 run,
                bit 2 navigate, bit 3 dodge
  action        first layer of the action list of the predicted state (+0x50): state number,
                start count, action number
  camera        position (+0xE0) and direction (+0xD0, quaternion) of the current input entry

Do not run two harness tools at the same time: the harness has one command file.

Usage: player_position.py [COUNT] [INTERVAL_SECONDS]
"""

from __future__ import annotations

import math
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
MOVEMENT_SLOT = 16
GROUP = 0x60
GROUP_COMPONENTS = 0x160
FACET = 0x80
PREDICTED_STATE = 0xAE0
STATE_POSITION = 0x460
STATE_LAYERS = 0x50
INPUT_ENTRY = 0x1190
GROUND_FOUND = 0x1690


def harness(*commands: str) -> list[bytes]:
    """@return The bytes of each read command of one harness call."""
    output = subprocess.run([str(HX), *commands], capture_output=True, text=True, check=True).stdout
    result = []
    for line in output.splitlines():
        try:
            result.append(bytes.fromhex(line.strip()))
        except ValueError:
            result.append(b"")
    return result


def qword(address: int) -> int:
    data = harness(f"read 0x{address:x} 8")[0]
    return struct.unpack("<Q", data)[0] if len(data) == 8 else 0


def movement_facet() -> int:
    component = 0
    for line in (DATA / "client/logs/hook.log").read_text(errors="replace").splitlines():
        # The client calls this function for each player component, also for the player of a
        # different client. Only the call from the player registry (return address 0x6928E1A)
        # is for the local player. The last one is the player of the newest world entry.
        match = re.search(r"trace: 0x6769980 #\d+ rcx=\w+ rdx=(\w+) .* ret=0x6928E1A", line)
        if match:
            component = int(match.group(1), 16)
    if not component:
        sys.exit("no player registration in the hook log (run with SOULWARDEN_TRACE=6769980)")
    components = qword(qword(component + GROUP) + GROUP_COMPONENTS)
    facet = qword(qword(components + 8 * MOVEMENT_SLOT) + FACET)
    if not facet:
        sys.exit("the movement component has no client facet")
    return facet


def main() -> None:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
    facet = movement_facet()
    print(f"movement facet 0x{facet:x}")
    for index in range(count):
        position, ground, entry, layers = harness(
            f"read 0x{facet + PREDICTED_STATE + STATE_POSITION:x} 12",
            f"read 0x{facet + GROUND_FOUND:x} 1",
            f"read 0x{facet + INPUT_ENTRY:x} 240",
            f"read 0x{facet + PREDICTED_STATE + STATE_LAYERS:x} 8",
        )
        x, y, z = struct.unpack("<3f", position)
        qx, qy, qz, qw = struct.unpack_from("<4f", entry, 0xD0)
        camera = struct.unpack_from("<3f", entry, 0xE0)
        # Forward is the quaternion applied to the +y axis.
        forward = (2 * (qx * qy - qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qw * qx))
        yaw = math.atan2(-forward[0], forward[1])
        pitch = math.asin(max(-1.0, min(1.0, forward[2])))
        action = "?"
        begin = struct.unpack("<Q", layers)[0]
        if begin:
            first = harness(f"read 0x{begin:x} 12")[0]
            if len(first) == 12:
                state, starts, number = struct.unpack("<iHxxi", first)
                action = f"{state}/{starts}/{number}"
        print(
            f"{time.strftime('%H:%M:%S')} position {x:.2f} {y:.2f} {z:.2f} ground {ground[0]}"
            f" permissions 0x{entry[8]:02x} action {action}"
            f" camera {camera[0]:.2f} {camera[1]:.2f} {camera[2]:.2f} yaw {yaw:.3f} pitch {pitch:.3f}",
            flush=True,
        )
        if index + 1 < count:
            time.sleep(interval)


if __name__ == "__main__":
    main()

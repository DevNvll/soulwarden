#!/usr/bin/env python3
"""Print the time of day of the live client of a development run (stage 2.2, docs/systems/world-time-and-weather.md).

The tool reads the memory of the client through the automation harness (tools/hx; it writes nothing) and prints one JSON line:

  hour          the hour that the sky uses (time of day object +0x1C34)
  pause         the pause flag of the time of day object (+0x1C9A); overrideFlag, overrideHour (+0x1C98, +0x1C94)
  dawnDusk      the four hours of the engine for dawn and dusk (engine +0x270): 6.0, 6.37, 19.0, 19.37 after a record
  record        the pointer of the record of the game (game +0x730; 0: the client has no record) and its values
  clockLast     the last value of the server clock of the client (game +0x1F8)
  modelHour     the hour of the formula of the client (0x6A0ADC0, 0x6A09E60) for the record at clockLast
  modelPhase    the phase of that formula (0 night, 1 dawn, 2 day, 3 dusk)
  editor        the byte env +0x244 (must be 0); localSync the setting byte at image RVA 0x9EFF37C

The memory chain is the chain of the research (world-time.md section 5): env = [image + 0xA7BA0E0], engine = [env + 8],
game = [env + 0x60], time of day = [engine + 0x116C0]. Two reads of the hour some seconds apart give the rate (26 game seconds in
one real second by day, 44 by night). Do not run two harness tools at the same time.

Usage: time_of_day.py
"""

from __future__ import annotations

import json
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

HX = Path(__file__).resolve().parents[1] / "hx"
BASE = 0x140000000
ENV = BASE + 0xA7BA0E0
LOCAL_SYNC = BASE + 0x9EFF37C
DAY_LENGTH, NIGHT_LENGTH, DAWN, DUSK = 1800, 900, 21600, 68400


def harness(*commands: str) -> list[bytes]:
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


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def model(record: dict, now: int) -> tuple[float, int]:
    """@return The hour and the phase of the formula of the client (0x6A0ADC0, 0x6A09E60, 0x6A08270) for a record at a server time."""
    day, night, dawn, dusk = record["ints"][4], record["ints"][5], record["ints"][0], record["ints"][2]
    cycle = (day + night) * 1_000_000_000
    year = cycle * 365
    total = f32(f32(abs(now - record["anchor"])) + f32(f32(year) * record["offset"]))
    part = (int(total) % year) % cycle / cycle
    night_game = 86400 - dusk + dawn
    before = dawn / night_game * night
    starts = record["starts"]
    x = (day + night) * part
    if part <= starts[0]:
        second = x / night * night_game
    elif part <= starts[3]:
        second = (x - before) / day * (86400 - night_game) + dawn
    else:
        second = (x - before - day) / night * night_game + dusk
    if part < starts[0] or part > starts[3]:
        phase = 0
    elif part < starts[1]:
        phase = 1
    elif part < starts[2]:
        phase = 2
    else:
        phase = 3
    return second / 3600.0, phase


def main() -> int:
    env = qword(ENV)
    out: dict = {"env": env != 0}
    if env == 0:
        print(json.dumps(out))
        return 1
    engine = qword(env + 0x08)
    game = qword(env + 0x60)
    data = harness(f"read 0x{env + 0x244:x} 1", f"read 0x{LOCAL_SYNC:x} 1", f"read 0x{engine + 0x116C0:x} 8",
                   f"read 0x{engine + 0x270:x} 16", f"read 0x{game + 0x730:x} 8", f"read 0x{game + 0x1F8:x} 9")
    out["editor"] = data[0].hex()
    out["localSync"] = data[1].hex()
    tod = struct.unpack("<Q", data[2])[0] if len(data[2]) == 8 else 0
    if len(data[3]) == 16:
        out["dawnDusk"] = [round(value, 4) for value in struct.unpack("<4f", data[3])]
    record_address = struct.unpack("<Q", data[4])[0] if len(data[4]) == 8 else 0
    if len(data[5]) == 9:
        out["clockLast"] = struct.unpack("<Q", data[5][:8])[0]
    out["record"] = hex(record_address)
    if tod:
        values = harness(f"read 0x{tod + 0x1C34:x} 4", f"read 0x{tod + 0x1C94:x} 8")
        if len(values[0]) == 4:
            out["hour"] = round(struct.unpack("<f", values[0])[0], 5)
        if len(values[1]) == 8:
            out["overrideHour"] = struct.unpack("<f", values[1][:4])[0]
            out["overrideFlag"] = values[1][4]
            out["pause"] = values[1][6]
    if record_address:
        raw = harness(f"read 0x{record_address:x} 72")[0]
        if len(raw) == 72:
            record = {"offset": struct.unpack("<f", raw[8:12])[0], "ints": list(struct.unpack("<6I", raw[12:36])),
                      "fixed": struct.unpack("<Q", raw[40:48])[0], "anchor": struct.unpack("<Q", raw[48:56])[0],
                      "starts": [round(value, 7) for value in struct.unpack("<4f", raw[56:72])]}
            out["rec"] = record
            if "clockLast" in out and record["anchor"] and record["ints"][4] + record["ints"][5] > 0:
                hour, phase = model(record, out["clockLast"])
                out["modelHour"] = round(hour, 5)
                out["modelPhase"] = phase
    print(json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Read client memory through the harness. Usage: peek.py ADDRESS [LENGTH] (hexadecimal address)."""
import struct
import subprocess
import sys
from pathlib import Path

HX = Path(__file__).resolve().parents[1] / "hx"


def read(address: int, length: int = 64) -> bytes:
    out = subprocess.run([str(HX), f"read {address:x} {length}"], capture_output=True, text=True).stdout.strip()
    try:
        return bytes.fromhex(out)
    except ValueError:
        return b""


def base() -> int:
    out = subprocess.run([str(HX), "base"], capture_output=True, text=True).stdout.strip()
    return int(out, 16)


if __name__ == "__main__":
    address = int(sys.argv[1], 16)
    length = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    data = read(address, length)
    image = base()
    for offset in range(0, len(data) - 7, 8):
        value = struct.unpack_from("<Q", data, offset)[0]
        note = f"  image+0x{value - image:X}" if image <= value < image + 0xAEBA000 else ""
        print(f"+0x{offset:03X}: {value:016x}{note}")

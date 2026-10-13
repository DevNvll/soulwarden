#!/usr/bin/env python3
"""Research: locate C++ objects referenced from a live object and print their vtable slots.

Usage: find_carrier.py OBJECT_ADDRESS [SCAN_BYTES]
For each pointer field whose target starts with a vtable inside the client image, print the
field offset, the vtable RVA, and the first 16 virtual function RVAs.
"""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from peek import base, read  # noqa: E402

IMAGE_SIZE = 0xAEBA000


def main() -> None:
    address = int(sys.argv[1], 16)
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 512
    image = base()
    data = b""
    for offset in range(0, size, 512):
        data += read(address + offset, min(512, size - offset))
    for offset in range(0, len(data) - 7, 8):
        pointer = struct.unpack_from("<Q", data, offset)[0]
        if not (0x10000 < pointer < 0x7FFFFFFFFFFF) or image <= pointer < image + IMAGE_SIZE:
            if image <= pointer < image + IMAGE_SIZE:
                print(f"+0x{offset:03X}: image+0x{pointer - image:X}")
            continue
        head = read(pointer, 8)
        if len(head) < 8:
            continue
        vtable = struct.unpack("<Q", head)[0]
        if not image + 0x7EC9000 <= vtable < image + 0x9E7B000:
            continue
        slots = read(vtable, 8 * 16)
        rvas = [struct.unpack_from("<Q", slots, index * 8)[0] - image for index in range(len(slots) // 8)]
        if not all(0x1000 <= rva < 0x7EC8000 for rva in rvas[:3]):
            continue
        print(f"+0x{offset:03X}: object {pointer:x} vtable image+0x{vtable - image:X}: "
              + " ".join(f"{rva:X}" for rva in rvas))


if __name__ == "__main__":
    main()

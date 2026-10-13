#!/usr/bin/env python3
"""Print entries of a virtual table of the client image.

Usage: vt.py VTABLE_RVA OFFSET...        function RVA at each byte offset (hexadecimal)
       vt.py VTABLE_RVA --count N        the first N entries
"""

import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from nwbin import Image, game_executable  # noqa: E402


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    image = Image(game_executable())
    table = int(sys.argv[1], 16)
    if sys.argv[2] == "--count":
        offsets = [index * 8 for index in range(int(sys.argv[3]))]
    else:
        offsets = [int(text, 16) for text in sys.argv[2:]]
    for offset in offsets:
        value = struct.unpack("<Q", image.read(table + offset, 8))[0]
        print(f"+0x{offset:03X} 0x{value - image.image_base:X}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Find the virtual tables of a class from its RTTI name.

Usage: rtti_vtable.py NAME_REGEX

The tool finds the RTTI type descriptors whose decorated name matches, then the complete
object locators that point to them, then the virtual tables that point to the locators. It
prints each virtual table with the offset of its sub-object in the class.
"""

import re
import struct
import sys
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from nwbin import Image, game_executable  # noqa: E402


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pattern = re.compile(sys.argv[1].encode())
    image = Image(game_executable())
    sections = {name: (rva, max(vsize, rsize)) for name, rva, vsize, _, rsize, _ in image.sections}
    data_rva, data_size = sections[".data"]
    rdata_rva, rdata_size = sections[".rdata"]
    data = image.read(data_rva, sections[".data"][1] if False else image.sections[2][4])
    rdata = image.read(rdata_rva, image.sections[1][4])
    words = np.frombuffer(rdata[: len(rdata) - len(rdata) % 4], dtype="<u4")
    quads = np.frombuffer(rdata[: len(rdata) - len(rdata) % 8], dtype="<u8")
    for match in re.finditer(rb"\.\?A[VU][^\0]{1,300}\0", data):
        if not pattern.search(match.group(0)):
            continue
        descriptor = data_rva + match.start() - 16
        name = match.group(0)[:-1].decode("latin1")
        print(f"type 0x{descriptor:X} {name}")
        for index in np.nonzero(words == descriptor)[0]:
            locator = rdata_rva + int(index) * 4 - 12
            signature, offset = struct.unpack_from("<II", rdata, int(index) * 4 - 12)
            if signature != 1:
                continue
            target = image.image_base + locator
            for slot in np.nonzero(quads == target)[0]:
                vtable = rdata_rva + int(slot) * 8 + 8
                print(f"  vtable 0x{vtable:X} sub-object +0x{offset:X}")


if __name__ == "__main__":
    main()

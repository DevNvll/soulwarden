#!/usr/bin/env python3
"""List key-like string literals for each function in an RVA range.

Usage: scan_keys.py START_RVA END_RVA [MIN_KEYS]
Use it to find JSON readers and writers: a model reader references its field names.
"""

import bisect
import re
import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nwbin import Image, game_executable  # noqa: E402


def main() -> None:
    low, high = int(sys.argv[1], 16), int(sys.argv[2], 16)
    minimum = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    image = Image(game_executable())
    table = image.pdata
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    key = re.compile(r"^[A-Za-z][A-Za-z0-9_.\-/]{1,48}$")
    first = bisect.bisect_left(image._pdata_begin, low)
    last = bisect.bisect_left(image._pdata_begin, high)
    for index in range(first, last):
        begin, end, _ = (int(value) for value in table[index])
        found: list[str] = []
        try:
            code = image.read(begin, end - begin)
        except ValueError:
            continue
        for instruction in engine.disasm(code, begin):
            for operand in instruction.operands:
                if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                    text = image.string_at(instruction.address + instruction.size + operand.mem.disp)
                    if text and key.match(text) and text not in found:
                        found.append(text)
        if len(found) >= minimum:
            print(f"0x{begin:X} ({end - begin}): " + " ".join(found))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""List the texts that the code in a range of the client image refers to.

Use it to find log and assertion texts of one subsystem (for example the objectives code).
Output: RVA of the reference, start of the function, text.

Usage: range_strings.py START_RVA END_RVA [MIN_LENGTH]
"""

from __future__ import annotations

import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from nwbin import Image, game_executable  # noqa: E402


def main() -> None:
    start, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
    minimum = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    seen = set()
    for begin, stop, _ in ((int(a), int(b), int(c)) for a, b, c in image.pdata):
        if stop <= start or begin >= end:
            continue
        for instruction in engine.disasm(image.read(begin, stop - begin), begin):
            if instruction.mnemonic != "lea":
                continue
            operand = instruction.operands[1]
            if operand.type != capstone.x86.X86_OP_MEM or operand.mem.base != capstone.x86.X86_REG_RIP:
                continue
            target = instruction.address + instruction.size + operand.mem.disp
            try:
                text = image.string_at(target)
            except ValueError:
                continue
            if not text or len(text) < minimum or (begin, text) in seen:
                continue
            seen.add((begin, text))
            print(f"0x{instruction.address:X} fn 0x{begin:X}  {text}")


if __name__ == "__main__":
    main()

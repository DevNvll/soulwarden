#!/usr/bin/env python3
"""Find instructions that use a memory displacement, in a range of the client image.

Usage: disp_refs.py START_RVA END_RVA DISPLACEMENT... [--writes]

The tool disassembles each function of the range from its start (function bounds from the
exception directory) and prints the instructions with a memory operand that has one of the
displacements. Use it to find the readers and writers of a member (`[reg + 0x1624]`).
"""

import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from nwbin import Image, game_executable  # noqa: E402


def main() -> None:
    arguments = [text for text in sys.argv[1:] if not text.startswith("--")]
    writes_only = "--writes" in sys.argv
    if len(arguments) < 3:
        sys.exit(__doc__)
    start, end = int(arguments[0], 16), int(arguments[1], 16)
    wanted = {int(text, 16) for text in arguments[2:]}
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    patterns = [struct.pack("<I", value) for value in wanted]
    table = image.pdata
    for row in table[(table[:, 1] > start) & (table[:, 0] < end)]:
        begin, finish = int(row[0]), int(row[1])
        code = image.read(begin, finish - begin)
        if not any(pattern in code for pattern in patterns):
            continue
        for instruction in engine.disasm(code, begin):
            for index, operand in enumerate(instruction.operands):
                if operand.type != x86.X86_OP_MEM or operand.mem.disp not in wanted:
                    continue
                if operand.mem.base in (x86.X86_REG_RSP, x86.X86_REG_RBP, x86.X86_REG_RIP):
                    continue
                is_write = index == 0 and instruction.mnemonic not in ("cmp", "test", "lea", "push", "call", "jmp")
                if writes_only and not is_write:
                    continue
                kind = "W" if is_write else ("A" if instruction.mnemonic == "lea" else "R")
                print(f"{kind} 0x{instruction.address:X} fn 0x{begin:X} {instruction.mnemonic} {instruction.op_str}")


if __name__ == "__main__":
    main()

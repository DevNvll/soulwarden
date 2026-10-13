#!/usr/bin/env python3
"""Print the JSON fields that an AWS SDK model reader accepts, with their value types.

A generated reader calls ValueExists(key) and then one typed getter for each field.
Usage: aws_model.py READER_RVA...
"""

import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nwbin import Image, ascii_immediate, game_executable  # noqa: E402

VALUE_EXISTS = 0x7465730
GETTERS = {
    0x7465410: "bool",
    0x7465490: "int32",
    0x7465460: "int64",
    0x74654E0: "string",
    0x74654B0: "object",
    0x7465430: "double",
    0x7465390: "array",
    0x7465440: "int64",
}


def describe(image: Image, rva: int) -> None:
    bounds = image.function_of(rva)
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    last_text = None
    pending_key = None
    immediate = ""
    seen = []
    print(f"reader 0x{bounds[0]:X}")
    for instruction in engine.disasm(image.read(bounds[0], bounds[1] - bounds[0]), bounds[0]):
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                text = image.string_at(instruction.address + instruction.size + operand.mem.disp)
                if text and last_text is None:
                    last_text = text
            elif operand.type == capstone.x86.X86_OP_IMM and instruction.mnemonic in ("mov", "movabs"):
                text = ascii_immediate(operand.imm, operand.size)
                if text:
                    immediate += text
        if not instruction.group(capstone.CS_GRP_CALL):
            continue
        target = next((op.imm for op in instruction.operands if op.type == capstone.x86.X86_OP_IMM), None)
        key = last_text or immediate or None
        if target == VALUE_EXISTS:
            pending_key = key
        elif pending_key is not None and target in GETTERS:
            kind = GETTERS[target]
            if (pending_key, kind) not in seen:
                seen.append((pending_key, kind))
                print(f"  {pending_key:<40} {kind}")
            pending_key = None
        last_text = None
        immediate = ""


def main() -> None:
    image = Image(game_executable())
    for argument in sys.argv[1:]:
        describe(image, int(argument, 16))


if __name__ == "__main__":
    main()

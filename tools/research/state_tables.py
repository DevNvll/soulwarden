#!/usr/bin/env python3
"""Build a table of the replicated state types: type index, constructor, and virtual table.

Reads ~/Games/soulwarden/work/hubtypes.json and the game image. Writes
~/Games/soulwarden/work/statetypes.json. The virtual table address identifies the state type of a
state object in a memory dump of the client.

Usage: state_tables.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nwbin import Image, game_executable  # noqa: E402

BASE_CONSTRUCTOR = 0x162F3E0
WORK = Path.home() / "Games/soulwarden/work"


def first_call(image: Image, engine, start: int, limit: int = 0x80):
    for instruction in engine.disasm(image.read(start, limit), start):
        if instruction.mnemonic == "call" and instruction.operands[0].type == capstone.x86.X86_OP_IMM:
            return instruction.operands[0].imm
        if instruction.mnemonic in ("ret", "jmp"):
            break
    return None


def vtable_of(image: Image, engine, constructor: int, depth: int = 0):
    """@return The virtual table that the constructor stores at offset 0 of the object."""
    seen_base = False
    for instruction in engine.disasm(image.read(constructor, 0x200), constructor):
        if instruction.mnemonic == "call" and instruction.operands[0].type == capstone.x86.X86_OP_IMM:
            target = instruction.operands[0].imm
            if not seen_base:
                seen_base = True
                continue
        if seen_base and instruction.mnemonic == "lea" and "rip" in instruction.op_str:
            return instruction.address + instruction.size + instruction.operands[1].mem.disp
        if instruction.mnemonic == "ret":
            break
    return None


def main() -> None:
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    out = []
    for entry in json.loads((WORK / "hubtypes.json").read_text()):
        name = str(entry.get("name", ""))
        if not name.endswith("ReplicatedState"):
            continue
        constructor = first_call(image, engine, int(entry["unmarshal"]))
        table = vtable_of(image, engine, constructor) if constructor else None
        out.append(
            {
                "name": name,
                "type_index": entry.get("type_index"),
                "constructor": constructor,
                "vtable": table,
            }
        )
    (WORK / "statetypes.json").write_text(json.dumps(out, indent=1))
    resolved = sum(1 for row in out if row["vtable"])
    print(f"{len(out)} state types, {resolved} with a virtual table")


if __name__ == "__main__":
    main()

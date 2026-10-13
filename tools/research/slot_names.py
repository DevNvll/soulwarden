#!/usr/bin/env python3
"""Print the class name of each component and client facet of the player entity group.

Input: ~/Games/soulwarden/work/player_components.json (tools/research/gde_components.py).
The image has no compiler RTTI for these classes. The name comes from the engine RTTI: one
of the first virtual functions of each class only returns the address of its name text.

Usage: slot_names.py [TABLE_RVA...]
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nwbin import Image, game_executable  # noqa: E402

WORK = Path.home() / "Games/soulwarden/work"
TEXT_END = 0x7EC8000
MAX_SLOTS = 14


def class_names(image: Image, engine, table: int) -> list[str]:
    """@return The texts that the small getter functions of a virtual table return."""
    names: list[str] = []
    if not table:
        return names
    for slot in range(MAX_SLOTS):
        try:
            function = struct.unpack("<Q", image.read(table + 8 * slot, 8))[0] - image.image_base
        except (ValueError, struct.error):
            break
        if not 0x1000 <= function < TEXT_END:
            break
        instructions = list(engine.disasm(image.read(function, 16), function))[:2]
        if len(instructions) < 2 or instructions[0].mnemonic != "lea" or instructions[1].mnemonic != "ret":
            continue
        operand = instructions[0].operands[1]
        if operand.type != capstone.x86.X86_OP_MEM or operand.mem.base != capstone.x86.X86_REG_RIP:
            continue
        text = image.string_at(instructions[0].address + instructions[0].size + operand.mem.disp)
        if text and text not in names:
            names.append(text)
    return names


def main() -> None:
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    if len(sys.argv) > 1:
        for text in sys.argv[1:]:
            table = image.normalize(int(text, 16))
            print(f"0x{table:X} {class_names(image, engine, table)}")
        return
    for row in json.loads((WORK / "player_components.json").read_text()):
        component = ", ".join(class_names(image, engine, row["component_table"])) or "?"
        facet = ", ".join(class_names(image, engine, row["facet_table"])) or "?"
        print(f"{row['slot']:3d} 0x{row['component_table']:X} {component} | 0x{row['facet_table']:X} {facet}")


if __name__ == "__main__":
    main()

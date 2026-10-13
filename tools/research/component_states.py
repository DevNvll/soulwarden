#!/usr/bin/env python3
"""Find the replicated state type of each component slot of the player entity group.

Input: ~/Games/soulwarden/work/player_components.json (tools/research/gde_components.py) and
~/Games/soulwarden/work/statetypes.json (tools/research/state_tables.py).
For each slot the tool follows the virtual functions of the component and of its client facet
and looks for a call to a state constructor or a reference to a state virtual table.

Usage: component_states.py
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
MAX_SLOTS = 80
MAX_DEPTH = 3
MAX_FUNCTION = 0x1800


def main() -> None:
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    states = json.loads((WORK / "statetypes.json").read_text())
    by_constructor = {row["constructor"]: row for row in states if row["constructor"]}
    by_table = {row["vtable"]: row for row in states if row["vtable"]}
    # The type descriptor getter of each state type is one more link from code to a type.
    indexes = {row["type_index"] for row in states}
    for entry in json.loads((WORK / "hubtypes.json").read_text()):
        if entry.get("type_index") in indexes and str(entry.get("name", "")).endswith("ReplicatedState"):
            by_constructor.setdefault(int(entry["getter"]), {"name": entry["name"], "type_index": entry["type_index"]})
    cache: dict[int, tuple[set[int], set[int]]] = {}

    def scan(function: int):
        """@return (direct call targets, rip-relative data targets) of one function."""
        if function in cache:
            return cache[function]
        bounds = image.function_of(function)
        size = min(bounds[1] - function, MAX_FUNCTION) if bounds and bounds[0] == function else 0x100
        calls: set[int] = set()
        data: set[int] = set()
        for instruction in engine.disasm(image.read(function, size), function):
            if instruction.mnemonic in ("call", "jmp") and instruction.operands:
                operand = instruction.operands[0]
                if operand.type == capstone.x86.X86_OP_IMM and not function <= operand.imm < function + size:
                    calls.add(operand.imm)
            for operand in instruction.operands:
                if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                    data.add(instruction.address + instruction.size + operand.mem.disp)
        cache[function] = (calls, data)
        return cache[function]

    def states_of(table: int) -> dict[str, int]:
        found: dict[str, int] = {}
        if not table:
            return found
        for slot in range(MAX_SLOTS):
            function = struct.unpack("<Q", image.read(table + 8 * slot, 8))[0] - image.image_base
            if not 0x1000 <= function < TEXT_END:
                break
            frontier = {function}
            visited: set[int] = set()
            for _ in range(MAX_DEPTH):
                following: set[int] = set()
                for item in frontier:
                    if item in visited or not 0x1000 <= item < TEXT_END:
                        continue
                    visited.add(item)
                    calls, data = scan(item)
                    for call in calls:
                        if call in by_constructor:
                            found[by_constructor[call]["name"]] = by_constructor[call]["type_index"]
                    for target in data:
                        if target in by_table:
                            found[by_table[target]["name"]] = by_table[target]["type_index"]
                    following |= calls
                frontier = following
        return found

    rows = json.loads((WORK / "player_components.json").read_text())
    counts: dict[str, int] = {}
    for row in rows:
        found = states_of(row["facet_table"])
        found.update(states_of(row["component_table"]))
        row["states"] = found
        for name in found:
            counts[name] = counts.get(name, 0) + 1
    for row in rows:
        # A state type that many slots reach comes from shared helper code. Drop it.
        row["states"] = {name: index for name, index in row["states"].items() if counts[name] <= 6}
        names = ", ".join(
            f"{name.split('::')[-1].replace('ReplicatedState', '')} ({index})" for name, index in row["states"].items()
        ) or "?"
        print(f"{row['slot']:3d} component 0x{row['component_table']:X} facet 0x{row['facet_table']:X}: {names}")
    (WORK / "player_components.json").write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()

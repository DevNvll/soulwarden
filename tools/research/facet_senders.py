#!/usr/bin/env python3
"""Find the client code that sends a `...ServerFacet_...` message type.

For each type index the tool reads the unmarshal function from hubtypes.json, takes the
virtual table of the message class from it, and lists the other functions that construct that
class (the send stubs). Then it lists the callers of each send stub (direct calls and pointers
in data, for example in a virtual table).

The reference scans are done in three batches (tables, stubs, callers): one scan of the image
is slow.

Usage: facet_senders.py TYPE_INDEX...
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import capstone

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nwbin import Image, game_executable, plausible_reference  # noqa: E402

WORK = Path.home() / "Games/soulwarden/work"
TEXT_END = 0x7EC8000
# A table with more constructors than this belongs to a base class of many message types.
MAX_CONSTRUCTORS = 12


def lea_targets(image: Image, engine, start: int, size: int) -> list[int]:
    targets = []
    for instruction in engine.disasm(image.read(start, size), start):
        if instruction.mnemonic != "lea":
            continue
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                targets.append(instruction.address + instruction.size + operand.mem.disp)
    return targets


def is_virtual_table(image: Image, rva: int) -> bool:
    section = image.section_of(rva)
    if not section or section[0] != ".rdata":
        return False
    try:
        first = struct.unpack("<Q", image.read(rva, 8))[0] - image.image_base
    except (ValueError, struct.error):
        return False
    return 0x1000 <= first < TEXT_END


def functions_of(image: Image, references) -> list[int]:
    found = set()
    for site, trailing in references:
        bounds = image.function_of(site)
        if bounds and plausible_reference(image, site, trailing):
            found.add(bounds[0])
    return sorted(found)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("types", nargs="+", type=int)
    arguments = parser.parse_args()

    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    by_index = {entry["type_index"]: entry for entry in json.loads((WORK / "hubtypes.json").read_text())}

    tables_of: dict[int, list[int]] = {}
    for type_index in arguments.types:
        entry = by_index.get(type_index)
        if entry is None or "unmarshal" not in entry:
            print(f"{type_index}: not in hubtypes.json")
            continue
        unmarshal = entry["unmarshal"]
        bounds = image.function_of(unmarshal)
        size = bounds[1] - unmarshal if bounds and bounds[0] == unmarshal else 0x200
        tables = [target for target in lea_targets(image, engine, unmarshal, size) if is_virtual_table(image, target)]
        tables_of[type_index] = list(dict.fromkeys(tables))

    all_tables = sorted({table for tables in tables_of.values() for table in tables})
    table_references = image.code_references(all_tables)
    stubs_of: dict[int, list[int]] = {}
    for table in all_tables:
        functions = functions_of(image, table_references[table])
        if len(functions) <= MAX_CONSTRUCTORS:
            stubs_of[table] = functions

    all_stubs = sorted({stub for stubs in stubs_of.values() for stub in stubs})
    stub_code = image.code_references(all_stubs)
    stub_data = image.pointer_references(all_stubs)

    for type_index, tables in tables_of.items():
        entry = by_index[type_index]
        unmarshal = entry["unmarshal"]
        print(f"\n{type_index} {entry['name']}  unmarshal 0x{unmarshal:X}")
        for table in tables:
            if table not in stubs_of:
                continue
            print(f"  message table 0x{table:X}")
            for stub in stubs_of[table]:
                if stub == unmarshal:
                    continue
                print(f"    send stub 0x{stub:X}")
                for site, trailing in stub_code[stub]:
                    if not plausible_reference(image, site, trailing):
                        continue
                    bounds = image.function_of(site)
                    print(f"      code 0x{site:X} in " + (f"fn 0x{bounds[0]:X}" if bounds else "no-function"))
                for slot in stub_data[stub]:
                    print(f"      data 0x{slot:X}")


if __name__ == "__main__":
    main()

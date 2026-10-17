#!/usr/bin/env python3
"""Check of the item catalog tool (item_catalog.py) with made-up tables (no file of the game). Stage 2.2.

The tool writes the removal protections of each master item row (format 2: the columns nonRemovable and notDroppable from the cells
Nonremovable and NotDroppable; code review of stage 3.5b.2, finding LB-R1). The item tables of the game have such a cell as a flag, a
number (0 or 1), or a text ("" or "1"). This check gives the function build_catalog made-up tables of the types MasterItemDefinitions
and VariationData (the two house tables) and compares the result. It writes the catalog of the case "protections" to
FOLDER/item-catalog-protections.json: the test program of the backend loads that file (case RLB1).

  protections   a helm with Nonremovable (a flag), an ore with NotDroppable (the text "1"), and a resource with NotDroppable (the
                number 1): the columns of format 2 and the protection of each row
  control       a resource with no protection (the number 0, the text "", no cell) and a house item: no protection

The cases of the code review of stage 2.2 (finding WT-R3: the tool must not replace the catalog with items that lost their protection).
Each one runs the main function of the tool on an old file: a source that is not complete stops the tool (status 1), and the old
file stays.

  missing-nonremovable   a master table with no column Nonremovable: status 1, the old file stays
  missing-notdroppable   a master table (not a store table) with no column NotDroppable: status 1, the old file stays
  invalid-flag           a protection cell with the text "tru": status 1, the old file stays
  store-table            a store table (MasterItemDefinitions_MTX_*) with no column NotDroppable (the one exception of the game
                         data): status 0, the file has its items with Nonremovable and no NotDroppable

Usage: check_item_catalog_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import item_catalog as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

# The text of an old catalog file (the cases of finding WT-R3: the tool must not replace it).
OLD_FILE = "old file\n"
SOURCE_CASES = ("missing-nonremovable", "missing-notdroppable", "invalid-flag", "store-table")

# The made-up items of the check (the ids of the backend case RLB1).
PROTECTED_HELM = "ReviewProtectedHelm"
NO_DROP_ORE = "ReviewNoDropOre"
NO_DROP_HIDE = "ReviewNoDropHide"
FREE_ORE = "ReviewFreeOre"
FREE_HIDE = "ReviewFreeHide"
FREE_STONE = "ReviewFreeStone"
HOUSE_CHAIR = "House_ReviewChair"


def master_row(item_id: str, item_type: str, item_class: str, stack: int, **cells) -> dict:
    row = {"ItemID": item_id, "Name": f"@{item_id}", "ItemType": item_type, "ItemClass": item_class, "Tier": 1, "Weight": 1,
           "MaxStackSize": stack, "Durability": 1000 if stack == 1 else 0, "MinGearScore": 75 if stack == 1 else 0,
           "MaxGearScore": 75 if stack == 1 else 0, "GearScoreOverride": 0, "BindOnPickup": False}
    row.update(cells)
    return row


def catalog() -> dict:
    """@return The catalog of the made-up tables (the production function build_catalog)."""
    masters = FakeSheet("ReviewItems", [
        master_row(PROTECTED_HELM, "Armor", "EquippableHead+Armor+Light", 1, Nonremovable=True, NotDroppable=False),
        master_row(NO_DROP_ORE, "Resource", "Resource", 100, Nonremovable="", NotDroppable="1"),
        master_row(NO_DROP_HIDE, "Resource", "Resource", 100, Nonremovable=0, NotDroppable=1),
        master_row(FREE_ORE, "Resource", "Resource", 100, Nonremovable=0, NotDroppable=""),
        master_row(FREE_HIDE, "Resource", "Resource", 100, Nonremovable="", NotDroppable=False),
        master_row(FREE_STONE, "Resource", "Resource", 1)])
    sheets = {masters.name: masters}
    kinds = {masters.name: tool.MASTER_TYPE}
    for name in tool.HOUSE_TABLES:
        rows = [{"HouseItemID": "DEFAULTS"}]
        if name == tool.HOUSE_TABLES[0]:
            rows.append({"HouseItemID": HOUSE_CHAIR, "Name": "@chair", "ItemType": "HousingItem", "Tier": 1, "Weight": 10,
                         "MaxStackSize": 1, "BindOnPickup": False})
        sheets[name] = FakeSheet(name, rows)
        kinds[name] = tool.HOUSE_TYPE
    with patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()]):
        return tool.build_catalog([name.encode() for name in sheets])


def run_tool(target: Path, rows: list[dict], table: str = "ReviewItems") -> int:
    """Runs the main function of the tool with one made-up master table (and the two house tables) into `target`. @return Its status."""
    sheets = {table: FakeSheet(table, rows)}
    kinds = {table: tool.MASTER_TYPE}
    for name in tool.HOUSE_TABLES:
        sheets[name] = FakeSheet(name, [{"HouseItemID": "DEFAULTS"}])
        kinds[name] = tool.HOUSE_TYPE
    with patch.object(tool, "table_entries", return_value=[None]), \
            patch.object(tool, "tables_of_paks", return_value=([name.encode() for name in sheets], "made-up tables")), \
            patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()]), \
            patch.object(sys, "argv", ["item_catalog.py", str(target)]), \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return tool.main()


def source_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of finding WT-R3: the tool on an old file. @return Passed, and the detail."""
    row = master_row(PROTECTED_HELM, "Armor", "EquippableHead+Armor+Light", 1, Nonremovable=True, NotDroppable=False)
    table = "ReviewItems"
    if case == "missing-nonremovable":
        del row["Nonremovable"]
    elif case == "missing-notdroppable":
        del row["NotDroppable"]
    elif case == "invalid-flag":
        row["Nonremovable"] = "tru"
    elif case == "store-table":
        del row["NotDroppable"]
        table = "MasterItemDefinitions_MTX_Review"
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_tool(target, [row], table)
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    flags = None if kept else protections(json.loads(target.read_text(encoding="utf-8"))).get(PROTECTED_HELM)
    if case == "store-table":
        return status == 0 and flags == (True, False), f"status {status}, the protections of the helm {flags}"
    return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"


def protections(document: dict) -> dict:
    """@return Item id to (nonRemovable, notDroppable) of the document, or an empty map for a document with no such columns."""
    columns = document.get("columns", [])
    if "nonRemovable" not in columns or "notDroppable" not in columns:
        return {}
    first, second = columns.index("nonRemovable"), columns.index("notDroppable")
    return {row[0]: (row[first], row[second]) for row in document["items"]}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    folder = Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    cases = sys.argv[2:] or ["protections", "control", *SOURCE_CASES]
    document = catalog()
    found = protections(document)
    failures = 0
    for case in cases:
        if case == "protections":
            target = folder / "item-catalog-protections.json"
            target.write_text(json.dumps(document) + "\n", encoding="utf-8")
            passed = (document.get("format") == 2 and document.get("columns", [])[-2:] == ["nonRemovable", "notDroppable"]
                      and all(len(row) == len(document["columns"]) for row in document["items"])
                      and found.get(PROTECTED_HELM) == (True, False) and found.get(NO_DROP_ORE) == (False, True)
                      and found.get(NO_DROP_HIDE) == (False, True))
            detail = (f"format {document.get('format')}, the helm {found.get(PROTECTED_HELM)}, the ore {found.get(NO_DROP_ORE)}, "
                      f"the hide {found.get(NO_DROP_HIDE)}; the file {target.name}")
        elif case == "control":
            passed = (found.get(FREE_ORE) == (False, False) and found.get(FREE_HIDE) == (False, False)
                      and found.get(FREE_STONE) == (False, False) and found.get(HOUSE_CHAIR) == (False, False))
            detail = f"the free ore {found.get(FREE_ORE)}, the stone {found.get(FREE_STONE)}, the house item {found.get(HOUSE_CHAIR)}"
        elif case in SOURCE_CASES:
            passed, detail = source_case(folder, case)
        else:
            passed, detail = False, "not a case of this check"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

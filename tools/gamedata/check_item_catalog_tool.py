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

Usage: check_item_catalog_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import item_catalog as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

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
    cases = sys.argv[2:] or ["protections", "control"]
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
        else:
            passed, detail = False, "not a case of this check"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

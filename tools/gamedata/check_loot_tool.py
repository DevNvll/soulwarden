#!/usr/bin/env python3
"""Check of the loot tables tool (loot_tables.py) with made-up data tables.

The check gives the readers of the tool made-up tables of each type that the tool uses (the reader of
the pak files and the base attributes file are replaced: no file of the game is necessary), and it
runs the document builder and the writer of the tool (main).

  case                     the tables                                         expected result
  valid                    each table complete                                status 0; the values of the
                                                                              document (below)
  unresolved-table         an entry names a table that no table file has      status 1, the file stays
  unresolved-bucket        an entry names a bucket that no bucket file has    status 1, the file stays
  base-table               a vitals row names a table that is not there       status 1, the file stays
  quantity-text            a quantity "1-x"                                   status 1, the file stays
  reward-levels            the reward levels 1, 2, 4                          status 1, the file stays
  not-finite               a reward factor "nan"                              status 1, the file stays
  variant-base             a variant names a base row that is not there       status 1, the file stays
  event-override           a base row names a kill event that is not there    status 1, the file stays
  creature-type            a base row names a creature type with no modifier  status 1, the file stays
  negative-experience      a kill experience -10                              status 1, the file stays
  negative-factor          an experience factor -1                            status 1, the file stays
  bad-flag                 the flag text "NONE"                               status 1, the file stays
  negative-disparity       a KillExpModifier -2                               status 1, the file stays
  large-disparity          a MaxRewardLevelDelta 1001                         status 1, the file stays
  negative-bucket-odds     the odds -0.2 of a bucket candidate                status 1, the file stays
  chest-root               a row of the chest variation table names a root    status 1, the file stays
                           that no table has (stage 3.5b.1)
The six cases after not-finite are the corrections of the findings F3 and F4 of the code review of stage 3.5a, the
last three the correction of finding F13 of the code review of stage 3.3a (each value is outside the range of the
backend, LootTables::check: the tool writes no file that the backend refuses).

The values of the valid document: the entries of a table in the sequence of their thresholds with the
quantity ranges of the row NAME_Qty and MaxRoll of the row NAME_Probs; the references resolved by their
id with no regard to the case; a bucket tag "Level:5" with the range 5 to 10000 and a tag
"Level:49-59-59" (a typing fault of the game data) with the range 49 to 59; an empty odds cell is 1;
an empty ProgressionCurrencyAmount is null; the flag text "TRULE" (a fact of the game data) is true; a
variant vitals row has the loot data of its base row; only the tables that a vitals row reaches are in
the document. Format 2 (stage 3.5b.1): the root of each row of the chest variation table Gatherable_LootContainers is a root
(chestRoots), but Empty and the roots of CHEST_ROOTS_NOT_IN_GAME; a variation table of a different name is not read; the container
floor of each level; the loot limits. Stage 1.8d: the loot table of each loot container item (containerRoots), but a row with a
salvage event and a row whose table the data does not have (counted).

Usage: check_loot_tool.py WORK_FOLDER [CASE...]     status 0: each case (or each named case) gives its result
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
import loot_tables as lt  # noqa: E402

MARK = "the file of an earlier run\n"


class FakeSheet:
    """A data table with the interface of item_catalog.Sheet: each cell is a source text."""

    def __init__(self, name: str, columns: list[str], rows: list[list]) -> None:
        self.name = name
        self.columns = {column: index for index, column in enumerate(columns)}
        self.row_count = len(rows)
        self._rows = [[str(value) for value in row] + [""] * (len(columns) - len(row)) for row in rows]

    def source(self, row: int, name: str) -> str:
        column = self.columns.get(name)
        return "" if column is None else self._rows[row][column]

    def cell(self, row: int, name: str):
        return self.source(row, name)


def loot_table_sheet(case: str) -> FakeSheet:
    columns = ["LootTableID", "AND/OR", "RollBonusSetting", "Conditions", "ConditionOverridesRoll", "UseLevelGS", "GSBonus",
               "MaxRoll", "Item1", "GearScoreRange1", "Item2", "GearScoreRange2", "Item3", "GearScoreRange3"]
    common_child = "[LTID]nothing_here" if case == "unresolved-table" else "[LTID]test_gear"
    bucket = "[LBID]NoBucket" if case == "unresolved-bucket" else "[LBID]Test_Weapons"
    quantity = "1-x" if case == "quantity-text" else "1-3"
    return FakeSheet("LootTables", columns, [
        ["Test_Master", "AND", "ClampMax", "GlobalMod", "", "TRUE", "0", "", "[LTID]Test_Common", "", bucket, ""],
        ["Test_Master_Qty", "", "", "", "", "", "", "", "1", "", "1", ""],
        ["Test_Master_Probs", "", "", "", "", "", "", "1000000", "0", "", "999900", ""],
        ["Test_Common", "OR", "", "Common", "", "", "5", "", "TestOre", "", common_child, "200-240", "TestTrophy", ""],
        ["Test_Common_Qty", "", "", "", "", "", "", "", quantity, "", "1", "", "1", ""],
        ["Test_Common_Probs", "", "", "", "", "", "", "100000", "90000", "", "35000", "", "99500", ""],
        ["Test_Gear", "OR", "AddToRoll", "", "", "TRUE", "", "", "TestSword", ""],
        ["Test_Gear_Probs", "", "", "", "", "", "", "100000", "40000", ""],
        ["Test_Unused", "OR", "", "", "", "", "", "", "TestUnused", ""],
        ["Test_ChestRoot", "AND", "AddToRoll", "MinPOIContLevel", "", "TRUE", "", "", "TestOre", ""],
        ["Test_ChestRoot_Probs", "", "", "", "", "", "", "100", "0", ""],
    ])


def bucket_sheet(case: str) -> FakeSheet:
    columns = ["RowPlaceholders", "LootBucket1", "Item1", "Quantity1", "Tags1", "MatchOne1", "Odds1",
               "LootBucket2", "Item2", "Quantity2", "Tags2", "MatchOne2"]
    return FakeSheet("LootBuckets", columns, [
        ["", "Test_Weapons", "TestSword", "1", "MinContLevel:0-18,Beast", "FALSE", "", "Test_Rare", "TestNamed", "1", "Named", "TRUE"],
        ["", "", "TestSwordT3", "1", "Level:5", "False", "-0.2" if case == "negative-bucket-odds" else "0.2", "", "", "", "", ""],
        ["", "", "TestSwordT5", "1", "Level:49-59-59", "0", "", "", "", "", "", ""],
    ])


def sheets(case: str) -> dict:
    rewards = [[1, -10 if case == "negative-experience" else 10, 310, 1], [2, 11, 320, 1], [4 if case == "reward-levels" else 3, 12, 330, 1]]
    experience = "nan" if case == "not-finite" else "-1" if case == "negative-factor" else "1"
    variants = [["Wolf_Grey_7", "Wolf"]] + ([["Ghost_1", "NoBase"]] if case == "variant-base" else [])
    rock_event = "KillMissing" if case == "event-override" else "KillStructure"
    rock_type = "Nope" if case == "creature-type" else "Critter"
    rock_flag = "NONE" if case == "bad-flag" else "TRULE"
    base_table = "Test_Missing" if case == "base-table" else "Test_Master"
    return {
        lt.REWARD_TYPE: [FakeSheet("Rewards", ["Level", "KillExperience", "KillCurrency", "KillProgressionCurrency"], rewards)],
        lt.MODIFIER_TYPE: [FakeSheet("RewardModifiers", ["Modifiers", "Experience", "Currency", "CurrencyRewardChance",
                                                         "LootDropModifier", "ProgressionCurrencyAmount"],
                                     [["Solo", experience, "1", "", "1", ""], ["Solo+", "2", "1.15", "0", "1", "3"],
                                      ["Critter", "0", "0", "", "1", ""]])],
        lt.DISPARITY_TYPE: [FakeSheet("AILevelDisparity", ["LevelDisparity", "KillExpModifier", "MaxRewardLevelDelta"],
                                      [["10", "0", "1001" if case == "large-disparity" else "10"],
                                       ["-10", "-2" if case == "negative-disparity" else "-0.4", "0"]])],
        lt.EVENT_TYPE: [FakeSheet("GameEvents", ["EventID", "CoinRewardChance"], [["KillEvent", "0.05"], ["KillStructure", "0"]]),
                        FakeSheet("GameEvents_01", ["EventID"], [["KillOther"]])],
        lt.EXPERIENCE_TYPE: [FakeSheet("XPLevels", ["Level Number"] + [f"GSLimitT{tier}" for tier in range(1, 6)],
                                       [["0", "100-100", "100-100", "100-100", "100-100", "100-100"],
                                        ["1", "100-120", "100-130", "100-130", "100-130", "100-130"]])],
        lt.VITALS_LEVEL_TYPE: [FakeSheet("VitalsLevels", ["Level", "AILootGSCeiling", "ContainerLootGSFloor"],
                                         [["1", "100", "100"], ["2", "120", "110"]])],
        # Stage 3.5b.1: the chest variation table (its roots; Empty and a root of CHEST_ROOTS_NOT_IN_GAME are not roots), a variation
        # table of a different name (not read), and the loot limits (exported).
        lt.VARIATION_TYPE: [FakeSheet("Gatherable_LootContainers", ["VariantID", "LootTableComponent"],
                                      [["Abandoned_Test_S", "Test_NoChestTable" if case == "chest-root" else "Test_ChestRoot"],
                                       ["Quest_Test", "Empty"], ["Season_Test", "SummerMedley_DancefloorBoogie"]]),
                            FakeSheet("Gatherable_Trees", ["VariantID", "LootTableComponent"], [["Tree_Test", "Test_NoTreeTable"]])],
        lt.LIMIT_TYPE: [FakeSheet("LootLimits", ["LootLimitID", "CountLimit", "MinLimitSeconds", "MaxLimitSeconds", "LimitExpireSeconds",
                                                 "MinLimitMult", "MaxLimitMult", "PityTickerOddsMod"],
                                  [["TestLimit", "10", "", "", "86400", "86400", "86400", "0.2"]])],
        lt.BASE_TYPE: [FakeSheet("BaseVitals_Common", ["VitalsID", "CreatureType", "CanDropLoot", "LootDropChance",
                                                       "IgnoreLootDropModifier", "LootTableId", "LootTags", "NoExpNoCoin",
                                                       "KillEventIdOverride", "VitalsCategories"],
                                 [["Wolf", "Solo", "True", "0.1", "FALSE", base_table, "Beast,Common", "", "", "Wolf, Hostile,Beast,Wolf"],
                                  ["Rock", rock_type, "False", "", "", "", "", rock_flag, rock_event, ""]])],
        lt.VARIANT_TYPE: [FakeSheet("LevelVariantVitals_Common", ["VitalsID", "BaseVitalsId"], variants)],
        lt.LOOT_TABLE_TYPE: [loot_table_sheet(case)],
        lt.LOOT_BUCKET_TYPE: [bucket_sheet(case)],
        # Stage 3.6a: the roots of the gatherable rows (a root with no table is named and not a stop; a loot container row and Empty are
        # not gathering roots).
        lt.GATHERABLE_TYPE: [FakeSheet("Gatherables", ["GatherableID", "FinalLootTable", "IsLootContainer"],
                                       [["Test_OreNode", "Test_Common", ""], ["Test_QuestNode", "Test_NoGatherTable", ""],
                                        ["Test_ChestNode", "Test_NoChestTable", "TRUE"], ["Test_EmptyNode", "Empty", ""]])],
        # Stage 1.8d: the loot container items (the class LootContainer with a RepairRecipe [LTID]NAME): a root of the data, a row with a
        # salvage event (left out), a row whose table the data does not have (counted, not a stop), and an item that is not a container.
        lt.ITEM_TYPE: [FakeSheet("MasterItemDefinitions_Test", ["ItemID", "ItemClass", "RepairRecipe", "SalvageGameEventID"],
                                 [["QuestReward_Test_Pouch", "Resource+LootContainer", "[LTID]Test_Common", ""],
                                  ["Test_EventBox", "LootContainer", "[LTID]Test_Gear", "Test_Box_Event"],
                                  ["Test_NoTableBox", "LootContainer", "[LTID]Test_NoBoxTable", ""],
                                  ["TestSword", "Weapon+Melee", "[LTID]Test_Gear", ""]])],
    }


CREDIT = {"timeLimit": 120, "healthShare": 0.01, "range": 200, "receivers": 2, "modifiers": [[1.0, 1.0, 1.0], [0.7, 1.0, 0.8]]}


def valid_values(document: dict) -> list[str]:
    """@return The values of the valid document that are not correct."""
    wrong = []
    tables = document["tables"]
    master = tables.get("Test_Master")
    common = tables.get("Test_Common")
    if sorted(tables) != ["Test_ChestRoot", "Test_Common", "Test_Gear", "Test_Master"]:
        wrong.append(f"the tables {sorted(tables)}")
    if document.get("gatheringRoots") != ["Test_Common"] or document.get("gatheringRootsWithNoTable") != ["Test_NoGatherTable"]:
        wrong.append(f"the gathering roots {document.get('gatheringRoots')} {document.get('gatheringRootsWithNoTable')}")
    if document.get("format") != 2 or document.get("chestRoots") != ["Test_ChestRoot"] \
            or document.get("containerFloor") != [[1, 100], [2, 110]] \
            or document.get("limits") != {"TestLimit": [10, 0.0, 0.0, 86400.0, 86400.0, 86400.0, 0.2]}:
        wrong.append(f"the format 2 members {document.get('format')} {document.get('chestRoots')} {document.get('containerFloor')} "
                     f"{document.get('limits')}")
    if master != [1, 1, 1000000, ["GlobalMod"], False, True, 0,
                  [["t", "Test_Common", 0, 1, 1, 0, 0], ["b", "Test_Weapons", 999900, 1, 1, 0, 0]]]:
        wrong.append(f"the master table {master}")
    if common != [0, 0, 100000, ["Common"], False, False, 5,
                  [["t", "Test_Gear", 35000, 1, 1, 200, 240], ["i", "TestOre", 90000, 1, 3, 0, 0], ["i", "TestTrophy", 99500, 1, 1, 0, 0]]]:
        wrong.append(f"the common table {common}")
    weapons = document["buckets"].get("Test_Weapons")
    if weapons != [["TestSword", 1, 1, 1.0, False, [["MinContLevel", 0, 18], ["Beast"]]],
                   ["TestSwordT3", 1, 1, 0.2, False, [["Level", 5, 10000]]],
                   ["TestSwordT5", 1, 1, 1.0, False, [["Level", 49, 59]]]]:
        wrong.append(f"the bucket {weapons}")
    if "Test_Rare" in document["buckets"]:
        wrong.append("a bucket that no table names")
    if document["modifiers"].get("Solo") != [1.0, 1.0, 0.0, 1.0, None] or document["modifiers"].get("Solo+") != [2.0, 1.15, 0.0, 1.0, 3.0] \
            or document["modifiers"].get("Critter") != [0.0, 0.0, 0.0, 1.0, None]:
        wrong.append(f"the modifiers {document['modifiers']}")
    bases = document["bases"]
    vitals = document["vitals"]
    if vitals.get("wolf_grey_7") != vitals.get("wolf") or "ghost_1" in vitals:
        wrong.append(f"the vitals ids {vitals}")
    rock = bases[vitals["rock"]] if "rock" in vitals else None
    if rock != ["Rock", "Critter", False, 0.0, False, "", [], True, "KillStructure", []]:
        wrong.append(f"the base row Rock {rock}")
    # Stage 1.8c: the vitals categories of a base row (the categories of a kill task of the quests), trimmed, no repeat, in their order.
    wolf = bases[vitals["wolf"]] if "wolf" in vitals else None
    if wolf is None or wolf[9] != ["Wolf", "Hostile", "Beast"]:
        wrong.append(f"the vitals categories of the base row Wolf {wolf}")
    if document["events"] != {"killevent": 0.05, "killstructure": 0.0}:
        wrong.append(f"the events {document['events']}")
    if document["rewards"] != [[1, 10, 310, 1], [2, 11, 320, 1], [3, 12, 330, 1]] or document["disparity"] != [[-10, -0.4, 0], [10, 0.0, 10]]:
        wrong.append("the rewards or the level differences")
    if document["gearScore"][1] != [100, 120, 100, 130, 100, 130, 100, 130, 100, 130] or document["lootCeiling"] != [[1, 100], [2, 120]]:
        wrong.append("the gear score ranges or the loot ceilings")
    counts = document["counts"]
    if document.get("containerRoots") != {"questreward_test_pouch": "Test_Common"} or counts.get("containersWithNoTable") != 1 \
            or counts.get("containersWithSalvageEvent") != 1:
        wrong.append(f"the container roots {document.get('containerRoots')} {counts.get('containersWithNoTable')} "
                     f"{counts.get('containersWithSalvageEvent')}")
    return wrong


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    cases = ("valid", "unresolved-table", "unresolved-bucket", "base-table", "quantity-text", "reward-levels", "not-finite",
             "variant-base", "event-override", "creature-type", "negative-experience", "negative-factor", "bad-flag",
             "negative-disparity", "large-disparity", "negative-bucket-odds", "chest-root")
    for case in sys.argv[2:] or cases:
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(lt, "Sources", lambda path: type("S", (), {"used": {}})()), \
                patch.object(lt, "read_sheets", lambda sources, case=case: sheets(case)), \
                patch.object(lt, "read_credit", lambda sources: json.loads(json.dumps(CREDIT))), \
                patch.object(sys, "argv", ["loot_tables.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            status = lt.main()
        text = target.read_text(encoding="utf-8")
        if case == "valid":
            wrong = valid_values(json.loads(text)) if status == 0 and text != MARK else ["no document"]
            passed = status == 0 and not wrong
            detail = f"status {status}" + (f", not correct: {'; '.join(wrong)}" if wrong else ", each value is correct")
        else:
            passed = status == 1 and text == MARK and not (work / f"{case}.json.tmp").exists()
            detail = f"status {status}, file {'kept' if text == MARK else 'replaced'}: {errors.getvalue().strip()}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

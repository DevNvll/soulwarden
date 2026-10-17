#!/usr/bin/env python3
"""Check of the gathering data tool (gathering.py) with made-up sources (no file of the game). Stage 3.6a.

The tool stops with status 1, and writes no file, at a source that it cannot use (its docstring has the list). This check gives the
functions of the tool made-up tables, rows, and distribution files, and compares each result. The test program of the backend runs it
(case GAT1 has the backend side of the files).

  offline-id         the G of a record: the values of live check L2 of stage 3.6a (records 18627, 18628, 18614 of r_+04_+02)
  region-name        the place of a region name; a name that is not r_+XX_+YY stops the tool
  row                a gatherable row: its values, the type None for an empty cell, the trade skill "None" as no trade skill,
                     ItemRequired with no item (nothing to check: supported)
  row-unsupported    the causes of a row that the backend refuses: ItemRequired with an item, a song, a loot container, two
                     gatherers, AzothStaff
  row-stops          BaseGatherTime 0 or not a number, the respawn bounds 120 to 60, a rank 1.5: each one stops the tool
  tables             the tables of each type: the rows, the variations, the ranks, the events, the tools (only weapon rows with a type)
  table-stops        a rank table with a gap, a table with no needed column, a tool range 3 to 2, two variation rows of one id with
                     different content, no table of a type, no rank table of a trade skill: each one stops the tool
  distribution       a distribution file: the slices, the variants, the records, the height modes, the tails
  distribution-stops a cut file, a file with more bytes, a record that names a slice outside the list: each one stops the tool

Usage: check_gathering_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gathering as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

CASES = ("offline-id", "region-name", "row", "row-unsupported", "row-stops", "tables", "table-stops", "distribution",
         "distribution-stops")


def stops(action) -> str | None:
    """@return The message of the Failure of an action, or no value when it did not stop."""
    try:
        action()
    except Failure as error:
        return str(error)
    return None


def good_row(**cells) -> dict:
    row = {"GatherableID": "Test_OreSmall", "GatheringType": "Mining", "Restriction": "ToolRequired", "Tradeskill": "Mining",
           "RequiredTradeskillLevel": "0", "BaseGatherTime": "4", "MinRespawnRate": "60", "MaxRespawnRate": "120",
           "FinalLootTable": "Test_GatherOre", "GameEventID": "Test_GatherMining", "ResetGatherTimeOnEndInteraction": "0"}
    row.update(cells)
    return row


class FakeSources:
    """The interface of creature_data.Sources that Tables uses: the paths of a folder, the bytes of a path (the table name)."""

    def __init__(self, sheets: dict[str, tuple[str, FakeSheet]]) -> None:
        self.sheets = sheets

    def paths(self, folder: str, suffix: str) -> list[str]:
        return [f"{folder}{name}{suffix}" for name in self.sheets]

    def read(self, path: str) -> bytes:
        return Path(path).stem.encode()

    def check_complete(self) -> None:
        return None


def made_up_sheets(**changes) -> dict[str, tuple[str, FakeSheet]]:
    """@return The tables of a made-up game copy: (type, sheet) by name. `changes` replaces a table (None: no table)."""
    ranks = {skill: (tool.RANK_TYPE, FakeSheet(skill, [{"Level": str(level), "RollBonus": str(level * 10), "GatheringEfficiency": "0.01",
                                                        "GatheringMultiplier": "0"} for level in range(3)]))
             for skill in tool.GATHERING_SKILLS}
    sheets = {
        "Gatherables": (tool.GATHERABLE_TYPE, FakeSheet("Gatherables", [good_row(), good_row(GatherableID="Test_Tree", GatheringType="Chopping",
                                                                                          Tradeskill="Logging")])),
        "Gatherable_Minerals": (tool.VARIATION_TYPE, FakeSheet("Gatherable_Minerals", [{"VariantID": "Ore_A", "GatherableEntryID": "Test_OreSmall"},
                                                                                       {"VariantID": "Ore_A", "GatherableEntryID": "Test_OreSmall"}])),
        "Gatherable_QuestThings": (tool.VARIATION_TYPE, FakeSheet("Gatherable_QuestThings", [{"VariantID": "Q", "GatherableEntryID": "x"}])),
        **ranks,
        "Riding": (tool.RANK_TYPE, FakeSheet("Riding", [{"Level": "0", "RollBonus": "0"}])),
        "GameEvents": (tool.EVENT_TYPE, FakeSheet("GameEvents", [{"EventID": "Test_GatherMining", "CategoricalProgressionId": "Mining",
                                                                  "CategoricalProgressionReward": "40"}])),
        "Items": (tool.ITEM_TYPE, FakeSheet("Items", [{"ItemID": "TestPick", "ItemStatsRef": "TestPick_Weapon", "ItemClass": "Pickaxe"},
                                                      {"ItemID": "TestSword", "ItemStatsRef": "TestSword_Weapon", "ItemClass": "Sword"}])),
        "Weapons": (tool.WEAPON_TYPE, FakeSheet("Weapons", [
            {"WeaponID": "TestPick_Weapon", "GatheringTypes": "Mining", "MinGatherEFF": "1", "MaxGatherEFF": "2", "GatheringEfficiency": "0"},
            {"WeaponID": "TestSword_Weapon", "GatheringTypes": "", "MinGatherEFF": "", "MaxGatherEFF": ""}])),
    }
    for name, value in changes.items():
        if value is None:
            sheets.pop(name, None)
        else:
            sheets[name] = value
    return sheets


def make_tables(sheets: dict[str, tuple[str, FakeSheet]]) -> tool.Tables:
    with patch.object(tool, "sheet_type", side_effect=lambda data: sheets[data.decode()][0]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()][1]):
        return tool.Tables(FakeSources(sheets))


def distribution(slices: list[str], variants: list[str], records: list[tuple[int, int]], tails: tuple[int, int] = (1, 2),
                 version: int = 1) -> bytes:
    """@return A made-up region.distribution file (the reader of the client 0x69DD030): records are (slice index, packed place)."""
    data = struct.pack("<Hh", 0xB9D6, version) + struct.pack("<H", len(slices))
    for name in slices + variants:
        data += bytes([len(name)]) + name.encode()
    data += struct.pack("<I", len(records))
    data += struct.pack(f"<{len(records)}H", *[index for index, _ in records])
    data += struct.pack(f"<{len(records)}I", *[place for _, place in records])
    data += bytes(4 * len(records)) + bytes(len(records))  # the rotations and the scales
    data += bytes([1] * len(records))  # the height modes
    for extra in tails:
        data += struct.pack("<I", extra) + bytes(5 * extra)
    return data


def check(case: str) -> tuple[bool, str]:
    if case == "offline-id":
        values = [tool.offline_id(4, 2, 18628), tool.offline_id(4, 2, 18629), tool.offline_id(4, 2, 18615)]
        return values == [0x09188221, 0x0918A221, 0x0916E221], ", ".join(f"0x{value:08X}" for value in values)
    if case == "region-name":
        place = tool.region_origin("r_+04_+02")
        stop = stops(lambda: tool.region_origin("region_4_2"))
        return place == (4, 2) and stop is not None, f"{place}; a wrong name: {stop}"
    if case == "row":
        row = tool.row_entry("Test_OreSmall", "Gatherables", good_row())
        plain = tool.row_entry("Test_Bush", "Gatherables", good_row(GatheringType="", Restriction="NoToolNecessary", Tradeskill="None"))
        no_item = tool.row_entry("Test_BushSmall", "Gatherables", good_row(GatheringType="", Restriction="ItemRequired"))
        passed = (row["type"] == "Mining" and row["time"] == 4.0 and row["respawn"] == [60.0, 120.0] and row["level"] == 0
                  and row["loot"] == "Test_GatherOre" and not row["reset"] and row["required"] == 1 and row["unsupported"] == []
                  and plain["type"] == "None" and plain["tradeskill"] == "" and plain["unsupported"] == [] and no_item["unsupported"] == [])
        return passed, (f"the ore {row['type']} {row['time']} s {row['respawn']}; the bush {plain['type']} {plain['tradeskill']!r}; "
                        f"ItemRequired with no item: {no_item['unsupported']}")
    if case == "row-unsupported":
        causes = {
            "restriction": tool.row_entry("A", "T", good_row(Restriction="ItemRequired", ItemRestrictionId="Test_Key"))["unsupported"],
            "song": tool.row_entry("B", "T", good_row(RequiredSongId="Song1"))["unsupported"],
            "container": tool.row_entry("C", "T", good_row(IsLootContainer="1"))["unsupported"],
            "gatherers": tool.row_entry("D", "T", good_row(NumRequiredToGather="2"))["unsupported"],
            "azoth": tool.row_entry("E", "T", good_row(GatheringType="AzothStaff"))["unsupported"],
        }
        passed = (causes["restriction"] == ["the restriction ItemRequired"] and causes["song"] == ["a song"]
                  and causes["container"] == ["a loot container (a chest: stage 3.5b)"]
                  and causes["gatherers"] == ["more than one gatherer (NumRequiredToGather)"]
                  and causes["azoth"] == ["the gathering type AzothStaff"])
        return passed, "; ".join(f"{name}: {value}" for name, value in causes.items())
    if case == "row-stops":
        messages = [stops(lambda cells=cells: tool.row_entry("X", "T", good_row(**cells))) for cells in
                    ({"BaseGatherTime": "0"}, {"BaseGatherTime": "nan"}, {"BaseGatherTime": "fast"},
                     {"MinRespawnRate": "120", "MaxRespawnRate": "60"}, {"RequiredTradeskillLevel": "1.5"})]
        return all(messages), f"{sum(1 for message in messages if message)} of 5 stop"
    if case == "tables":
        tables = make_tables(made_up_sheets())
        passed = (set(tables.rows) == {"test_oresmall", "test_tree"} and list(tables.variations) == ["Gatherable_Minerals"]
                  and tables.ranks["Mining"] == [[0.0, 0.01, 0.0], [10.0, 0.01, 0.0], [20.0, 0.01, 0.0]] and "Riding" not in tables.ranks
                  and tables.events["test_gathermining"] == ["Test_GatherMining", "Mining", 40.0]
                  and set(tables.weapons) == {"testpick_weapon"} and tables.weapons["testpick_weapon"]["max"] == 2.0)
        return passed, (f"{len(tables.rows)} rows, variations {list(tables.variations)}, ranks {sorted(tables.ranks)}, "
                        f"tools {sorted(tables.weapons)}")
    if case == "table-stops":
        gap = (tool.RANK_TYPE, FakeSheet("Mining", [{"Level": "0", "RollBonus": "0"}, {"Level": "2", "RollBonus": "0"}]))
        no_column = (tool.GATHERABLE_TYPE, FakeSheet("Gatherables", [{"GatherableID": "Test", "GatheringType": "Mining"}]))
        bad_tool = (tool.WEAPON_TYPE, FakeSheet("Weapons", [{"WeaponID": "W", "GatheringTypes": "Mining", "MinGatherEFF": "3",
                                                             "MaxGatherEFF": "2"}]))
        twice = (tool.VARIATION_TYPE, FakeSheet("Gatherable_Minerals", [{"VariantID": "Ore_A", "GatherableEntryID": "Test_OreSmall"},
                                                                        {"VariantID": "ore_a", "GatherableEntryID": "Test_Tree"}]))
        messages = [stops(lambda sheets=sheets: make_tables(sheets)) for sheets in (
            made_up_sheets(Mining=gap), made_up_sheets(Gatherables=no_column), made_up_sheets(Weapons=bad_tool),
            made_up_sheets(Gatherable_Minerals=twice), made_up_sheets(GameEvents=None), made_up_sheets(Skinning=None))]
        return all(messages), f"{sum(1 for message in messages if message)} of 6 stop"
    if case == "distribution":
        data = distribution(["slices/test/ore", "slices/test/tree"], ["Ore_A", "Tree_A"], [(0, 0x00010002), (1, 0x00030004), (0, 5)])
        names, variants, indexes, places, modes, tails = tool.read_distribution(data, "test.distribution")
        passed = (names == ["slices/test/ore", "slices/test/tree"] and variants == ["Ore_A", "Tree_A"] and list(indexes) == [0, 1, 0]
                  and list(places) == [0x00010002, 0x00030004, 5] and modes == bytes([1, 1, 1]) and tails == 3)
        return passed, f"{len(names)} slices, {len(indexes)} records, {tails} tail records"
    if case == "distribution-stops":
        good = distribution(["slices/test/ore"], ["Ore_A"], [(0, 1), (0, 2)])
        messages = [stops(lambda data=data: tool.read_distribution(data, "test.distribution")) for data in
                    (good[:-3], good + b"\0", distribution(["slices/test/ore"], ["Ore_A"], [(0, 1), (1, 2)]))]
        return all(messages), f"{sum(1 for message in messages if message)} of 3 stop"
    return False, "not a case of this check"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    Path(sys.argv[1]).mkdir(parents=True, exist_ok=True)
    failures = 0
    for case in sys.argv[2:] or CASES:
        try:
            passed, detail = check(case)
        except Failure as error:
            passed, detail = False, f"the tool stopped: {error}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

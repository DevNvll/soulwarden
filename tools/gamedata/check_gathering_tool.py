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

The cases of the code review of stage 3.6a run the main writer of the tool with a made-up game copy (tables, one region, one slice).
They run only when a name is given (the case GR4 and GR6 of the test program give them; GAT1 runs the cases above):

  gate-columns       GA-R4: a table with no column IsFFARestricted, ItemRestrictionId, or ResetGatherTimeOnEndInteraction stops the
                     tool and the earlier file stays; GatherablesDunwood (a named exception of the game data) can have no column
                     IsFFARestricted; ItemRequired with a blank item cell is a supported row
  flag-texts         GA-R4: the flag texts "tru" and "yes" stop the tool; "FALSE" is false and "TRUE" is true
  event-reward       GA-R4: an event table with no column CategoricalProgressionReward stops the tool; a blank reward cell is 0
  export-publish     GA-R6: an export that stops after it made a region file leaves the manifest and the region files of the
                     published set as they were (no staging folder stays); each region file has the digest of the manifest; the
                     earlier set stays after a publication, and an older set (also the layout of before) goes

Usage: check_gathering_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import shutil
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
REVIEW_CASES = ("gate-columns", "flag-texts", "event-reward", "export-publish")
# The made-up game copy of the review cases: one region with one record of one gathering slice (variant Ore_A: Test_OreSmall).
REGION = "r_+00_+00"
SLICE = "slices/gatherables/test_ore.dynamicslice"
TEMPLATE = {"slice": SLICE, "asset": "6a7e0000000000000000000000000001", "sub": 2, "root": "6a7e000000000a01", "controllerSlot": 3,
            "table": "Gatherable_Minerals", "entry": "",
            "bound": {"entity": "6a7e000000000a11", "component": "00c0ffee00000000", "slot": 20, "rangeSq": 9.0, "capacity": 1,
                      "translation": [0, 0, 1], "option": "6a7e000000000a21", "lineOfSight": False, "arcRequired": False, "requires": []}}
# The text of a file of before a run: a run that stops must leave it.
MARK = "earlier file\n"
# The gate columns of a complete GatherableData table (the columns that the retail table Gatherables has; finding GA-R4).
GATE_CELLS = ("Restriction", "ItemRestrictionId", "NumRequiredToGather", "ResetGatherTimeOnEndInteraction", "RequiredSongId",
              "RequiredWhisperId", "UsableEmoteTags", "AddedStatusEffect", "RequiredStatusEffect", "ConsumedStatusEffect",
              "RequireControllingFaction", "ProgressionCurrencyCosts", "IsLootContainer", "OpenAfterGathering", "RequireLootItems",
              "IsFFARestricted")


def stops(action) -> str | None:
    """@return The message of the Failure of an action, or no value when it did not stop."""
    try:
        action()
    except Failure as error:
        return str(error)
    return None


def good_row(**cells) -> dict:
    """@return A gatherable row with each gate column of the tool (a blank cell: no rule; code review of stage 3.6a, finding GA-R4)."""
    row = {"GatherableID": "Test_OreSmall", "GatheringType": "Mining", "Restriction": "ToolRequired", "Tradeskill": "Mining",
           "RequiredTradeskillLevel": "0", "BaseGatherTime": "4", "MinRespawnRate": "60", "MaxRespawnRate": "120",
           "FinalLootTable": "Test_GatherOre", "GameEventID": "Test_GatherMining", "ResetGatherTimeOnEndInteraction": "0"}
    row.update({column: "" for column in GATE_CELLS if column not in row})
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


class FakeWorld:
    """The interface of creature_data.Sources that the main writer uses, for a made-up game copy: the tables (the bytes of a table are
    its name), one region.distribution, one slice."""

    used = {"made-up game copy": 1}

    def __init__(self, sheets: dict[str, tuple[str, FakeSheet]], packed: int) -> None:
        self.sheets = sheets
        self.raw = distribution(["slices/gatherables/test_ore"], ["Ore_A"], [(0, packed)], tails=(0, 0))

    def paths(self, folder: str, suffix: str) -> list[str]:
        if suffix == ".datasheet":
            return [f"{folder}{name}{suffix}" for name in self.sheets]
        if suffix == "/region.distribution":
            return [f"{tool.REGIONS_FOLDER}{REGION}/region.distribution"]
        return []

    def read(self, path: str) -> bytes:
        if path.endswith("region.distribution"):
            return self.raw
        return Path(path).stem.encode() if path.endswith(".datasheet") else b"made-up"

    def has(self, path: str) -> bool:
        return path == SLICE

    def check_complete(self) -> None:
        return None


class FakeSlices:
    """The interface of gathering.Slices: one gathering slice."""

    def __init__(self, sources, catalog, option_id) -> None:
        self.templates = [json.loads(json.dumps(TEMPLATE))]
        self.counts: dict[str, int] = {}

    def template_of(self, path: str) -> int | None:
        return 0 if path == SLICE else None


class FakeHeights:
    """The interface of gathering.Heights: a flat terrain."""

    def __init__(self, folder, sources) -> None:
        return None

    def region(self, name: str) -> None:
        return None

    def height(self, region, local_x: float, local_y: float, mode: int) -> float:
        return 29.0


def run_main(folder: Path, sheets: dict[str, tuple[str, FakeSheet]], packed: int = 0) -> tuple[int, str]:
    """Runs the main writer of the tool into `folder` with a made-up game copy. @return The status and the messages."""
    folder.mkdir(parents=True, exist_ok=True)
    messages = io.StringIO()
    with patch.object(tool, "Sources", return_value=FakeWorld(sheets, packed)), \
            patch.object(tool, "sheet_type", side_effect=lambda data: sheets[data.decode()][0]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()][1]), \
            patch.object(tool, "Catalog", return_value=None), patch.object(tool, "Slices", FakeSlices), \
            patch.object(tool, "Heights", FakeHeights), \
            patch.object(tool, "read_options", return_value={tool.GATHER_OPTION: {"id": 0xA6E4C280, "action": 14}}), \
            patch.dict(os.environ, {"SOULWARDEN_GAMEDATA_DIR": str(folder), "SOULWARDEN_GAME_DIR": str(folder / "game")}), \
            patch.object(sys, "argv", ["gathering.py", str(folder / tool.OUTPUT_NAME)]), \
            contextlib.redirect_stdout(messages), contextlib.redirect_stderr(messages):
        status = tool.main()
    return status, messages.getvalue()


def sheets_with(row: dict, table: str = "Gatherables", **changes) -> dict[str, tuple[str, FakeSheet]]:
    """@return The made-up tables with one gatherable row in a table of that name."""
    tables: dict = {"Gatherables": None, table: (tool.GATHERABLE_TYPE, FakeSheet(table, [row]))}
    tables.update(changes)
    return made_up_sheets(**tables)


def without(row: dict, *columns: str) -> dict:
    return {name: value for name, value in row.items() if name not in columns}


def run_on_mark(folder: Path, sheets: dict[str, tuple[str, FakeSheet]], packed: int = 0) -> tuple[int, str, dict | None]:
    """Runs the main writer over an earlier file (MARK). @return The status, the messages, and the document (none when the earlier file
    stayed)."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / tool.OUTPUT_NAME).write_text(MARK, encoding="utf-8")
    status, messages = run_main(folder, sheets, packed)
    text = (folder / tool.OUTPUT_NAME).read_text(encoding="utf-8")
    return status, messages, None if text == MARK else json.loads(text)


def summary(results: dict[str, bool]) -> str:
    return "; ".join(f"{name}: {'as expected' if value else 'NOT as expected'}" for name, value in results.items())


def review(case: str, work: Path) -> tuple[bool, str]:
    """The cases of the code review of stage 3.6a (findings GA-R4 and GA-R6)."""
    if case == "gate-columns":
        results = {}
        status, _, document = run_on_mark(work / case / "ffa", sheets_with(good_row(IsFFARestricted="1")))
        results["ffa control"] = status == 0 and document is not None and document["rows"][0]["unsupported"] == ["IsFFARestricted"]
        for name, row in (("no IsFFARestricted", without(good_row(), "IsFFARestricted")),
                          ("no ItemRestrictionId", without(good_row(Restriction="ItemRequired"), "ItemRestrictionId")),
                          ("no ResetGatherTimeOnEndInteraction", without(good_row(), "ResetGatherTimeOnEndInteraction"))):
            status, messages, document = run_on_mark(work / case / name.replace(" ", "-"), sheets_with(row))
            results[name] = status == 1 and document is None and name.split()[1] in messages and "No file was written" in messages
        dunwood = without(good_row(), "IsFFARestricted", "OpenAfterGathering", "ProgressionCurrencyCosts")
        status, _, document = run_on_mark(work / case / "dunwood", sheets_with(dunwood, "GatherablesDunwood"))
        results["GatherablesDunwood exception"] = status == 0 and document is not None and document["rows"][0]["unsupported"] == []
        status, _, document = run_on_mark(work / case / "bush", sheets_with(good_row(Restriction="ItemRequired", ItemRestrictionId="")))
        results["ItemRequired with a blank item"] = status == 0 and document is not None and document["rows"][0]["unsupported"] == []
        return all(results.values()), summary(results)
    if case == "flag-texts":
        results = {}
        for name, cells in (("IsFFARestricted 'tru'", {"IsFFARestricted": "tru"}),
                            ("ResetGatherTimeOnEndInteraction 'yes'", {"ResetGatherTimeOnEndInteraction": "yes"})):
            status, messages, document = run_on_mark(work / case / name.split()[0], sheets_with(good_row(**cells)))
            results[name + " stops"] = status == 1 and document is None and "not a flag" in messages
        status, _, document = run_on_mark(work / case / "cases", sheets_with(good_row(WaitForImpact="FALSE", IsFFARestricted="TRUE")))
        results["'FALSE' false, 'TRUE' true"] = (status == 0 and document is not None and document["rows"][0]["waitForImpact"] is False
                                                 and document["rows"][0]["unsupported"] == ["IsFFARestricted"])
        return all(results.values()), summary(results)
    if case == "event-reward":
        no_column = (tool.EVENT_TYPE, FakeSheet("GameEvents", [{"EventID": "Test_GatherMining", "CategoricalProgressionId": "Mining"}]))
        blank = (tool.EVENT_TYPE, FakeSheet("GameEvents", [{"EventID": "Test_GatherMining", "CategoricalProgressionId": "Mining",
                                                            "CategoricalProgressionReward": ""}]))
        status, messages, document = run_on_mark(work / case / "no-column", sheets_with(good_row(), GameEvents=no_column))
        stopped = status == 1 and document is None and "CategoricalProgressionReward" in messages
        status, _, document = run_on_mark(work / case / "blank", sheets_with(good_row(), GameEvents=blank))
        zero = status == 0 and document is not None and document["events"].get("Test_GatherMining") == ["Mining", 0.0]
        return stopped and zero, f"no reward column: {'stops' if stopped else 'NOT a stop'}; a blank reward: {'0' if zero else 'NOT 0'}"
    if case == "export-publish":
        folder = work / case
        shutil.rmtree(folder, ignore_errors=True)
        sets = folder / tool.FOLDER
        sets.mkdir(parents=True)
        wrong = []
        # The layout of before the review: a manifest that names gathering/REGION.swgn.
        (sets / f"{REGION}.swgn").write_bytes(b"SWGN-before")
        before = {"format": 1, "regions": [{"name": REGION, "file": f"{tool.FOLDER}/{REGION}.swgn"}]}
        (folder / tool.OUTPUT_NAME).write_text(json.dumps(before), encoding="utf-8")

        def published() -> tuple[bytes, dict, Path]:
            manifest = (folder / tool.OUTPUT_NAME).read_bytes()
            document = json.loads(manifest)
            return manifest, document, folder / document["regions"][0]["file"]

        status, messages = run_main(folder, sheets_with(good_row()), packed=(100 << 16) | 200)
        manifest, document, region = published()
        first = region.parent.name
        bound = region.is_file() and hashlib.sha256(region.read_bytes()).hexdigest()[:16] == document["regions"][0].get("digest")
        if status != 0 or not bound:
            wrong.append(f"the first export: status {status}, {region.name} with the digest of the manifest: {bound}")
        if not (sets / f"{REGION}.swgn").is_file():
            wrong.append("the first export removed the region file of the earlier manifest (the layout of before)")
        old_region = region.read_bytes() if region.is_file() else b""
        # A late stop (an event that no event table has) after the region file of a changed record.
        no_event = (tool.EVENT_TYPE, FakeSheet("GameEvents", [{"EventID": "Test_Other", "CategoricalProgressionId": "Mining",
                                                               "CategoricalProgressionReward": "1"}]))
        status, messages = run_main(folder, sheets_with(good_row(), GameEvents=no_event), packed=(300 << 16) | 400)
        kept = (folder / tool.OUTPUT_NAME).read_bytes() == manifest and region.is_file() and region.read_bytes() == old_region
        staging = [entry.name for entry in sets.iterdir() if entry.name.startswith(".staging-")]
        if status != 1 or not kept or staging or "No file was written" not in messages:
            wrong.append(f"the stopped export: status {status}, the published set as it was: {kept}, staging folders: {staging}")
        # Two more publications: the earlier set stays, an older set goes.
        status_two, _ = run_main(folder, sheets_with(good_row()), packed=(300 << 16) | 400)
        _, _, region_two = published()
        second = region_two.parent.name
        if status_two != 0 or second == first or not (sets / first).is_dir() or (sets / f"{REGION}.swgn").exists():
            wrong.append(f"the second publication: status {status_two}, a new set {second != first}, the earlier set stays "
                         f"{(sets / first).is_dir()}, the layout of before goes {not (sets / f'{REGION}.swgn').exists()}")
        status_three, _ = run_main(folder, sheets_with(good_row()), packed=(500 << 16) | 600)
        _, document_three, region_three = published()
        third = region_three.parent.name
        if status_three != 0 or (sets / first).exists() or not (sets / second).is_dir() or not region_three.is_file():
            wrong.append(f"the third publication: status {status_three}, the oldest set goes {not (sets / first).exists()}, the earlier "
                         f"set stays {(sets / second).is_dir()}")
        x = struct.unpack_from("<f", region_three.read_bytes(), 26)[0] if region_three.is_file() else -1.0
        last = document_three["regions"][0].get("digest")
        return not wrong, "; ".join(wrong) or (f"the stopped export left the set {first}; sets {first} -> {second} -> {third}; the "
                                                f"region of the third set has x {x:.3f}, the digest {last}")
    return False, "not a case of this check"


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
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    for case in sys.argv[2:] or CASES:
        try:
            passed, detail = review(case, work) if case in REVIEW_CASES else check(case)
        except Failure as error:
            passed, detail = False, f"the tool stopped: {error}"
        except Exception as error:  # noqa: BLE001 (a case that cannot run is a FAIL line, and the next case runs)
            passed, detail = False, f"the case could not run: {type(error).__name__}: {error}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

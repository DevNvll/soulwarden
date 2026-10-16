#!/usr/bin/env python3
"""Check of the chest tool (chests.py, stage 3.5b.1) with made-up sources.

The check gives the tool made-up sources (no file of the game is necessary): the placements of one layer (a capitals file), the
gatherable records of its metadata file (an object stream), the base attributes of a player (the instanced chest values), and made-up
data tables (the variation rows, the gatherable rows, the territories, a quest task). It runs the document builder and the writer of
the tool (main).

  case                  the sources                                                expected result
  valid                 each source complete                                       status 0; the values of the document (below)
  no-capitals           the layer has no list Capitals                             status 1, the file stays
  chest-no-position     a chest placement has no position                          status 1, the file stays
  placement-twice       one placement id with two different placements             status 1, the file stays
  unknown-gatherable    a variation row of a chest names no gatherable row         status 1, the file stays
  gatherable-conflict   the gatherable row of a chest has two rows that differ     status 1, the file stays
  open-zero             the open time of a gatherable row of a chest is 0          status 1, the file stays
  reset-order           MaxRespawnRate is below MinRespawnRate                     status 1, the file stays
  record-position       a metadata record has a position that is not 3 numbers     status 1, the file stays
  no-reset-rule         the base attributes have no instanced chest reset          status 1, the file stays

The values of the valid document: the census (10 kind-0 records at 9 places: 8 joined to a chest (two records at the place of one
chest), 1 to a different placement, 1 to none, with its flag IsVariantOverride; a record of kind 4 is not kind 0); the state of each chest: the supply chest ordinary (the
loot tags of its territories, the content level of its area, the actor id of the UUID rule), the elite chest ordinary (the map
marker is not a lock), the lock placement unavailable (lockSlice), the glyph chest unavailable (lock), the key chest unavailable
(item), the quest chest unavailable (quest: a task of the quest data names its gatherable row), the encounter chest unavailable
(encounter); a placement that is not a chest is not in the file; the rules of the base attributes.

Usage: check_chests_tool.py WORK_FOLDER [CASE...]     status 0: each case (or each named case) gives its result
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import uuid
import zlib
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import chests  # noqa: E402
from appearance_tables import field_id  # noqa: E402
from slice_ids import Element  # noqa: E402

MARK = "the file of an earlier run\n"
# The table class of the tool (the check replaces the name in the module with a factory of made-up tables).
TABLES_CLASS = chests.Tables
REGION = "r_+04_+02"
LAYER = f"{chests.REGIONS_FOLDER}{REGION}/capitals/test_layer/test_layer"
IDS = {name: str(uuid.UUID(int=0x1CE79C8F00004000800000000000 + index))
       for index, name in enumerate(["supply", "elite", "lock", "glyph", "key", "quest", "encounter", "other"])}


def element(name: int | None, value: bytes | None = None, children: list | None = None) -> Element:
    node = Element()
    node.name = name
    node.value = value
    node.children = children or []
    return node


def record(gatherable: str, position: tuple, territories: list[int], kind: int = 0, encounter: int = 0, override: int = 0,
           root: str = "ContainerLootSmallLeveled", bad_position: bool = False) -> Element:
    fields = [element(field_id("GatherableId"), struct.pack(">I", zlib.crc32(gatherable.lower().encode()))),
              element(field_id("LootTableId"), root.encode()),
              element(field_id("WorldPosition"), struct.pack(">2f", *position[:2]) if bad_position else struct.pack(">3f", *position)),
              element(field_id("TerritoryIds"), None, [element(None, struct.pack(">I", territory)) for territory in territories]),
              element(field_id("InstancedLootType"), struct.pack(">I", kind)),
              element(field_id("SpawnedByCoatlicue"), struct.pack(">B", 1)),
              element(field_id("HasVariant"), struct.pack(">B", 1)),
              element(field_id("IsVariantOverride"), struct.pack(">B", override)),
              element(field_id("IsEncounter"), struct.pack(">B", encounter))]
    return element(None, None, fields)


def placement(name: str, variant: str, position: tuple | None, slice_name: str = "Master_LootContainer") -> dict:
    entry = {"id": IDS[name], "variantName": variant, "sliceName": slice_name, "rotation": {"x": 0, "y": 0, "z": 0, "w": 1}}
    if position is not None:
        entry["worldPosition"] = {"x": position[0], "y": position[1], "z": position[2]}
    return entry


class FakeSources:
    """The sources of one case: a layer of placements, its metadata, the base attributes."""

    def __init__(self, case: str) -> None:
        self.case = case
        self.used: dict[str, int] = {}

    def paths(self, prefix: str, suffix: str) -> list[str]:
        if suffix == ".capitals.json" and LAYER.startswith(prefix):
            return [LAYER + ".capitals.json"]
        if suffix == ".metadata" and LAYER.startswith(prefix):
            return [LAYER + ".metadata"]
        return []

    def read(self, path: str) -> bytes | None:
        if path != LAYER + ".capitals.json":
            return None
        if self.case == "no-capitals":
            return json.dumps({"Layer": "test"}).encode()
        places = [placement("supply", "Abandoned_T1_S", None if self.case == "chest-no-position" else (100.0, 200.0, 30.0)),
                  placement("elite", "ChestEliteT4", (110.0, 200.0, 30.0)),
                  placement("lock", "SoulWardenTomb_Test", (120.0, 200.0, 30.0), "Master_LootLock"),
                  placement("glyph", "16BS_EliteGlyphChest", (130.0, 200.0, 30.0)),
                  placement("key", "Doubloon_WeeklyKey", (140.0, 200.0, 30.0)),
                  placement("quest", "Werebear_Test", (150.0, 200.0, 30.0)),
                  placement("encounter", "Abandoned_T1_S", (160.0, 200.0, 30.0)),
                  placement("other", "Broccoli_a", (170.0, 200.0, 30.0), "Master_Plants")]
        if self.case == "placement-twice":
            places.append(placement("supply", "Abandoned_T1_S", (101.0, 200.0, 30.0)))
        return json.dumps({"Capitals": places}).encode()

    def stream(self, path: str):
        if path == chests.BASE_ATTRIBUTES_PATH:
            values = [element(field_id(chests.COUNT_FIELD), struct.pack(">I", 60))]
            if self.case != "no-reset-rule":
                values.append(element(field_id(chests.RESET_FIELD), struct.pack(">I", 60)))
            return [element(None, None, values)]
        if path != LAYER + ".metadata":
            return None
        records = [record("LootMediumT1", (100.0, 200.0, 30.0), [4, 9027]),
                   record("LootLargeT3ElitePOI", (110.02, 200.0, 30.0), [4, 9027], root="ChestLootElite"),
                   record("LootLargeT3", (120.0, 200.0, 30.0), [5], root="ReekwaterUplevelChestElite"),
                   record("LootLargeT3ElitePOI", (130.0, 200.0, 30.0), [16], root="BrimstoneGlyphLockChests"),
                   record("10ws_DoubloonChestWeeklyKey", (140.0, 200.0, 30.0), [10], root="DoubloonChestWeeklyKey",
                          bad_position=self.case == "record-position"),
                   record("LootMediumT1", (160.0, 200.0, 30.0), [4], encounter=1),
                   record("Broccoli", (170.0, 200.0, 30.0), [4], root="Empty"),
                   record("LootMediumT1", (900.0, 900.0, 30.0), [4], override=1),
                   record("LootMediumT1", (100.0, 200.0, 30.0), [4], kind=4)]
        # The quest chest: the same place as a second kind-0 record of the supply place (7 places for 8 records).
        records.append(record("MSQ_Test_Chest", (150.0, 200.0, 30.0), [4], root="ContainerLootLarge"))
        records.insert(7, record("LootMediumT1", (100.0, 200.0, 30.0), [4], override=1))
        return [element(None, None, [element(field_id("GatherableLocations"), None, records)])]

    def check_complete(self) -> None:
        pass


def gatherable(open_time: str = "2.5", low: str = "0", high: str = "0", **extra: str) -> dict:
    cells = {"BaseGatherTime": open_time, "MinRespawnRate": low, "MaxRespawnRate": high, "Restriction": "NoToolNecessary",
             "IsLootContainer": "1", "(table)": "Gatherables"}
    cells.update(extra)
    return cells


def fake_tables(case: str):
    tables = TABLES_CLASS.__new__(TABLES_CLASS)
    supply_row = "NoSuchRow" if case == "unknown-gatherable" else "LootMediumT1"
    tables.variations = {
        "Gatherable_LootContainers": {
            "Abandoned_T1_S": (2, {"VariantID": "Abandoned_T1_S", "GatherableEntryID": supply_row,
                                   "LootTableComponent": "ContainerLootSmallLeveled", "Name": "@Test_Supply"}),
            "ChestEliteT4": (40, {"VariantID": "ChestEliteT4", "GatherableEntryID": "LootLargeT3ElitePOI", "LootTableComponent": "ChestLootElite",
                                  "LootLockSlice": "slices\\POIs\\LootContainers\\MapIcon_Chest.dynamicslice"}),
            "SoulWardenTomb_Test": (41, {"VariantID": "SoulWardenTomb_Test", "GatherableEntryID": "LootLargeT3",
                                         "LootTableComponent": "ReekwaterUplevelChestElite"}),
            "16BS_EliteGlyphChest": (42, {"VariantID": "16BS_EliteGlyphChest", "GatherableEntryID": "LootLargeT3ElitePOI",
                                          "LootTableComponent": "BrimstoneGlyphLockChests",
                                          "LootLockSlice": "slices/worldevents/test/glyphlock.dynamicslice"}),
            "Doubloon_WeeklyKey": (43, {"VariantID": "Doubloon_WeeklyKey", "GatherableEntryID": "10ws_DoubloonChestWeeklyKey",
                                        "LootTableComponent": "DoubloonChestWeeklyKey"}),
            "Werebear_Test": (44, {"VariantID": "Werebear_Test", "GatherableEntryID": "MSQ_Test_Chest", "LootTableComponent": "ContainerLootLarge"}),
        },
        "LootContainers": {},
    }
    tables.gatherables = {
        "LootMediumT1": gatherable("0" if case == "open-zero" else "2.5"),
        "LootLargeT3": gatherable("5"),
        "LootLargeT3ElitePOI": gatherable("5", "82800", "80000" if case == "reset-order" else "82800", IsElite="1"),
        "10ws_DoubloonChestWeeklyKey": gatherable("5", Restriction="ItemRequired", ItemRestrictionId="DoubloonWeeklyKey",
                                                  ItemRestrictionQuantity="1"),
        "MSQ_Test_Chest": gatherable("5"),
        "Broccoli": gatherable("1", IsLootContainer="0"),
    }
    tables.gatherable_crc = {zlib.crc32(name.lower().encode()): name for name in tables.gatherables}
    tables.territories = {4: {"tags": ["Everfall"], "level": None}, 9027: {"tags": [], "level": 12}, 5: {"tags": ["Reekwater"], "level": 60},
                          16: {"tags": ["Brimstone"], "level": 65}, 10: {"tags": ["CutlassKeys"], "level": 60}}
    tables.conflicts = {"LootMediumT1"} if case == "gatherable-conflict" else set()
    tables.quest_named = {"msq_test_chest"}
    return tables


def valid_values(document: dict) -> list[str]:
    wrong = []
    counts = document["counts"]
    if counts["kind0"] != 10 or counts["kind0Positions"] != 9 or counts["kind0Joined"] != {"chest": 8, "other": 1, "none": 1} \
            or counts["unmatchedFlags"] != {"override": 1, "coatlicue": 1, "encounter": 0} or counts["recordsByType"] != {"0": 10, "4": 1}:
        wrong.append(f"the census {counts['kind0']} {counts['kind0Positions']} {counts['kind0Joined']} {counts['unmatchedFlags']} "
                     f"{counts['recordsByType']}")
    by_key = {chest["key"]: chest for chest in document["chests"]}
    if IDS["other"] in by_key or len(by_key) != 7:
        wrong.append(f"the chests {sorted(by_key)}")
    expected = {"supply": ("ordinary", ""), "elite": ("ordinary", ""), "lock": ("unavailable", "lockSlice"),
                "glyph": ("unavailable", "lock"), "key": ("unavailable", "item"), "quest": ("unavailable", "quest"),
                "encounter": ("unavailable", "encounter")}
    for name, (state, reason) in expected.items():
        chest = by_key.get(IDS[name])
        if chest is None or chest["state"] != state or chest["reason"] != (document["reasons"][reason] if reason else ""):
            wrong.append(f"the chest {name}: {chest and (chest['state'], chest['reason'])}")
    supply = by_key.get(IDS["supply"], {})
    if supply.get("tags") != ["Everfall"] or supply.get("contentLevel") != 12 or supply.get("actor") != chests.actor_of(IDS["supply"]).hex() \
            or supply.get("gatherable") != "LootMediumT1" or supply.get("root") != "ContainerLootSmallLeveled" or supply.get("kind") != "supply" \
            or supply.get("table") != "Gatherable_LootContainers" or supply.get("row") != 2 or supply.get("flags", {}).get("instancedLootType") != 0:
        wrong.append(f"the supply chest {supply}")
    elite = by_key.get(IDS["elite"], {})
    if elite.get("kind") != "elite" or document["gatherables"].get("LootLargeT3ElitePOI", {}).get("resetMax") != 82800.0:
        wrong.append(f"the elite chest {elite.get('kind')} {document['gatherables'].get('LootLargeT3ElitePOI')}")
    if document["rules"] != {"resetMinutes": 60, "countLimit": 60}:
        wrong.append(f"the rules {document['rules']}")
    return wrong


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    cases = ("valid", "no-capitals", "chest-no-position", "placement-twice", "unknown-gatherable", "gatherable-conflict", "open-zero",
             "reset-order", "record-position", "no-reset-rule")
    for case in sys.argv[2:] or cases:
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(chests, "Sources", lambda path, wanted, case=case: FakeSources(case)), \
                patch.object(chests, "Tables", lambda sources, case=case: fake_tables(case)), \
                patch.object(sys, "argv", ["chests.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            status = chests.main()
        text = target.read_text(encoding="utf-8")
        if case == "valid":
            wrong = valid_values(json.loads(text)) if status == 0 and text != MARK else [f"no document: {errors.getvalue().strip()}"]
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

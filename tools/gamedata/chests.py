#!/usr/bin/env python3
"""Write the chests of the world as one JSON file (stage 3.5b.1, docs/systems/chests.md).

The backend reads this file at its start. It has each chest placement of the main world: the placement (its UUID, its managed actor
id, its place), the variation row of the placement, the gatherable row of the variation (the time of an open, the reset values, the
restrictions), the loot root, the territories of its metadata record (the loot tags and the creature level of each), the flags of
the metadata record, the lock slice, and the state of the chest for this backend: "ordinary" (a player can open it) or
"unavailable" with its reason (a gate that the backend has no predicate for: stage 3.5b.3, 1.8, 3.6). The backend joins a chest with
the object of the interaction data by the placement UUID (interaction-data.json): the client request names that object.

The chain of one chest (all files are in the pak files of the game copy; research chests-ground-loot.md, Q1):

  1. A layer of a region has its placements: regions/REGION/capitals/LAYER/LAYER.capitals.json. A chest placement has a
     variant name (`variantName`) that is a row of a loot container variation table: Gatherable_LootContainers
     (javelindata_variations_gatherables_lootcontainers.datasheet, the table of the variation component of
     slices/master_lootcontainer.dynamicslice) or the older LootContainers (javelindata_variations_lootcontainers.datasheet).
     The two tables have their own row numbers: the file keeps the table and the row of each chest.
  2. The variation row names the gatherable row (GatherableEntryID), the loot root (LootTableComponent), the name of the
     prompt (Name), and a lock slice (LootLockSlice; slices/POIs/LootContainers/MapIcon_Chest.dynamicslice is only the map marker
     of an elite chest, not a lock).
  3. The gatherable row (GatherableData tables) gives the open time (BaseGatherTime: 2.5 s and 5 s), the reset values
     (MinRespawnRate, MaxRespawnRate: 82,800 s for an elite chest, 0 for a supply chest), and the restrictions (Restriction,
     ItemRestrictionId, Tradeskill and RequiredTradeskillLevel, RequiredStatusEffect, OpenAfterGathering, IsFFARestricted, ...).
  4. The layer metadata file (LAYER.metadata, the list GatherableLocations) has one record for each gatherable of the layer:
     GatherableId (the CRC-32 of the lower-case gatherable id), LootTableId, WorldPosition, TerritoryIds, InstancedLootType (0
     POILoot, 4 Quest, ...), SpawnedByCoatlicue, HasVariant, IsVariantOverride, IsEncounter. A record and a placement are one chest
     when they are in the same layer, 0.06 m or less apart (research K1), and the GatherableId of the record is the CRC-32 of the
     gatherable row of the variation (code review of stage 3.5b.1, finding CH-R7: a different record at the same place is not the
     record of the chest). The region metadata files repeat the layer records: the tool reads only the layer files.
  5. A territory id of a record is a row of a TerritoryDefinition table (each table of this type: the territories, the areas, the
     points of interest of pointofinterestdefinitions/, the arenas, ...; code review of stage 3.5b.1, finding CH-R6: the records
     name the ids of the points of interest, whose rows have LootTags such as "EPOI"): LootTags (the loot tags of the territory, for
     example "Everfall") and AIVariantLevelOverride (the creature level of an area). The tags of a chest are the LootTags of its
     territories; its content level is the largest AIVariantLevelOverride of its territories (a rule of this project from the data:
     docs/systems/chests.md, "Rules").
  6. The managed actor id of the placement is the UUID rule of interactions.py (actor_of).

The census (member "counts", research I1 and K1): each kind-0 record of the layer metadata of the main world is counted once (the
region files are not read), with its root, its gatherable row, and its join (a chest placement, a different placement, or none,
with the flags of the unmatched records).

The state of a chest (the reasons of "unavailable" are in UNAVAILABLE): the backend opens only an "ordinary" chest. The rules of
the data that make a chest unavailable: a gatherable row with a tool, an item (a key), a trade skill rank, a status effect, a song,
a faction, a cost, OpenAfterGathering, or IsFFARestricted; an encounter record (IsEncounter); a record that is not POILoot; a lock
slice that is not the map marker; the lock slice master_lootlock; a loot root Empty or with no table; and no gatherable row.

The tool stops with status 1 and one message, and writes no file, when:
  - a file that it needs has no source (the pak entries have Oodle data: see creature_data.py);
  - a layer has no list "Capitals", a layer metadata file is not an object stream, or a record has a position that is not three
    finite numbers;
  - a chest placement has no UUID, no position with three finite numbers, or one placement id has two different placements;
  - a variation row names a gatherable row that no GatherableData table has, other than the rows of GATHERABLES_NOT_IN_GAME;
  - a gatherable row of a chest has an open time that is not a finite number above 0, a reset value that is not a finite number
    of 0 or more, or a largest reset value below its smallest one;
  - a needed table (a variation table, a gatherable table, a territory table) or a needed column is not there, or the base
    attributes of a player have no "Instanced Loot Chest Reset Time Mins" (or 0) or no "Max Instanced Loot Chest Count";
  - two rows of one variation table or of the territory tables have one id and different content, or the gatherable row of a
    chest has two rows with different content (two rows of a gatherable that no chest names do not stop the tool: the quest
    gatherables of the game have such rows);
  - a chest that can be ordinary has no record of its gatherable row in the metadata of its layer (code review of stage 3.5b.1,
    finding CH-R5: a layer metadata file that the source does not have, or a record that is not there; the backend cannot check
    the gate of the record, such as IsEncounter), or two records of its gatherable row at the chest have different content
    (finding CH-R7);
  - a record of a chest names a territory id that no TerritoryDefinition table has (finding CH-R6: its LootTags and its level are
    not known).
tools/gamedata/check_chests_tool.py is the check of these rules, with made-up sources.

Output (one line of JSON):
  {"format": 1, "world": TEXT, "counts": {...}, "rules": {"resetMinutes", "countLimit"} (the base attributes of a player:
   "Instanced Loot Chest Reset Time Mins", "Max Instanced Loot Chest Count"), "reasons": {KEY: TEXT, ...}, "gatherables": {ID: {"open", "resetMin", "resetMax", "elite", "restriction",
   "item", "itemCount", "tradeskill", "tradeskillLevel", "event", "loot": FinalLootTable}, ...},
   "chests": [{"key", "actor", "position", "rotation", "slice", "variant", "table", "row", "name", "gatherable", "root", "kind",
               "tags": [...], "contentLevel": N or null, "territories": [...], "metadata": true|false,
               "flags": {"coatlicue", "variant", "override", "encounter", "instancedLootType"}, "lock": TEXT, "state", "reason"},
              ...]}
  `kind` is supply, provision, alchemy, elite, or other (KIND_OF_ROOT, then IsElite of the gatherable row). `reason` is empty for
  an ordinary chest.

Usage: chests.py [OUTPUT.json] [--region NAME]... [--radius METERS [--center X Y]] [--list STATE|KIND] [--find TEXT]
Default output: <SOULWARDEN_GAMEDATA_DIR>/chest-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import DEFAULT_CENTER, REGIONS_FOLDER, TABLES_FOLDER, WORLD, Sources, region_names  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import Element  # noqa: E402

OUTPUT_NAME = "chest-data.json"
FORMAT = 1
BASE_ATTRIBUTES_PATH = "sharedassets/genericassets/playerbaseattributes.pbadb"
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, BASE_ATTRIBUTES_PATH)
# The instanced loot values of the base attributes of a player (research Q1, "Resets and counts"; reflection 0x5CAD110): the reset of
# an instanced chest (minutes) and the largest count of the instanced chest states.
RESET_FIELD = "Instanced Loot Chest Reset Time Mins"
COUNT_FIELD = "Max Instanced Loot Chest Count"

VARIATION_TYPE = "VariationData"
GATHERABLE_TYPE = "GatherableData"
TERRITORY_TYPE = "TerritoryDefinition"
# The territory tables of a chest record (research Q1): the territories and the areas need the column LootTags. The tool reads each
# other table of this type too (code review of stage 3.5b.1, finding CH-R6: the records name the points of interest of
# pointofinterestdefinitions/ and the arenas); a table with no LootTags gives no tag (the invasions).
TERRITORY_FILES = ("javelindata_territorydefinitions.datasheet", "javelindata_areadefinitions.datasheet")
# The gatherable tables of the quests: a chest of their rows is a chest of a quest (stage 1.8). A gatherable row that a table of the
# quest data names (a task of a quest: MSQ_18018_WerebearChest in objectivetasks_quest_18018) is a chest of a quest too.
QUEST_GATHERABLE_TABLES = ("QuestGatherables",)
QUEST_TABLES_FOLDER = TABLES_FOLDER + "questdata/"
# The loot container variation tables (research Q1): the table name of each sheet.
VARIATION_TABLES = ("Gatherable_LootContainers", "LootContainers")
# The map marker of an elite chest: a lock slice of a variation row that is not a lock (research Q1, "LootLockSlice").
MAP_MARKER = "slices/pois/lootcontainers/mapicon_chest.dynamicslice"
# The slice of a chest with a lock (master_lootlock): a script lock of an event (research Q1, 3.5b.3).
LOCK_SLICES = ("master_lootlock",)
# A record and a placement are one chest at this distance or less (meters; research K1).
JOIN_DISTANCE = 0.06
# Gatherable rows that variation rows name and that no gatherable table of the game has (none in build 1.400.6031.6004151).
GATHERABLES_NOT_IN_GAME: frozenset[str] = frozenset()

# The kind of a chest from its loot root (a table of this project from the root names of the data, research Q1); the rows of an
# elite gatherable (IsElite) with a different root are "elite" too.
KIND_OF_ROOT = {
    "containerlootsmallleveled": "supply", "containerlootlarge": "supply",
    "containerlootcookingsmall": "provision", "containerlootcookinglarge": "provision",
    "containerlootalchemysmall": "alchemy", "containerlootalchemylarge": "alchemy",
    "chestlootelite": "elite",
}

# The reasons of an unavailable chest (docs/systems/chests.md, "Rules").
UNAVAILABLE = {
    "noGatherable": "the variation row names no gatherable row",
    "notContainer": "the gatherable row is not a loot container (IsLootContainer)",
    "quest": "the gatherable row is a row of the quest gatherables or a task of a quest names it (a chest of a quest, stage 1.8)",
    "emptyRoot": "the loot root is Empty or names no table: a quest or a script chest (stage 1.8)",
    "tool": "the gatherable row needs a tool or a trade skill rank (stage 3.5b.3, 3.6)",
    "item": "the gatherable row needs an item, a key (stage 3.5b.3)",
    "status": "the gatherable row needs or changes a status effect, a song, a faction, or a cost (stage 3.5b.3)",
    "openAfterGathering": "the gatherable row opens a storage after the gather (a battle royale chest, stage 3.5b.3)",
    "ffa": "the gatherable row is for a free-for-all area (stage 6.6)",
    "encounter": "the record is a chest of an encounter (IsEncounter: an event, stage 7.2)",
    "notPoiLoot": "the record is not POILoot (InstancedLootType: a quest chest, stage 1.8)",
    "lock": "the variation row has a lock slice (a glyph or a script lock, stage 3.5b.3)",
    "lockSlice": "the placement is a lock (master_lootlock: a script lock of an event, stage 3.5b.3)",
}

F_GATHERABLES = field_id("GatherableLocations")
F_GATHERABLE_ID = field_id("GatherableId")
F_LOOT_TABLE = field_id("LootTableId")
F_POSITION = field_id("WorldPosition")
F_TERRITORIES = field_id("TerritoryIds")
F_LOOT_TYPE = field_id("InstancedLootType")
F_COATLICUE = field_id("SpawnedByCoatlicue")
F_HAS_VARIANT = field_id("HasVariant")
F_OVERRIDE = field_id("IsVariantOverride")
F_ENCOUNTER = field_id("IsEncounter")


def walk(element: Element):
    yield element
    for child in element.children:
        yield from walk(child)


def whole(element: Element | None) -> int:
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    return int.from_bytes(element.value, "big") if element is not None and element.value is not None else 0


def finite(text: str, what: str, default: float | None = None) -> float:
    text = text.strip()
    if not text:
        if default is None:
            raise Failure(f"{what} is an empty cell")
        return default
    try:
        value = float(text)
    except ValueError as error:
        raise Failure(f"{what} is not a number ({text!r})") from error
    if not math.isfinite(value):
        raise Failure(f"{what} is not finite")
    return value


def actor_of(placement_id: str) -> bytes:
    """@return The managed actor id of a placement (the rule of interactions.py; protocol notes 4.35)."""
    raw = uuid.UUID(placement_id).bytes
    folded = bytes(a ^ b for a, b in zip(raw[:8], raw[8:]))[::-1]
    return bytes([(folded[0] & ~7) | 3]) + folded[1:] + bytes(8)


def is_true(text: str) -> bool:
    return text.strip().lower() in ("1", "true", "yes")


class Tables:
    """The data tables that the chests need: the variation rows, the gatherable rows, the territories."""

    def __init__(self, sources: Sources) -> None:
        self.variations: dict[str, dict[str, tuple[int, dict]]] = {name: {} for name in VARIATION_TABLES}
        self.gatherables: dict[str, dict] = {}
        self.gatherable_crc: dict[int, str] = {}
        self.territories: dict[int, dict] = {}
        self.conflicts: set[str] = set()
        found_tables: set[str] = set()
        for path in sources.paths(TABLES_FOLDER, ".datasheet"):
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            if kind not in (VARIATION_TYPE, GATHERABLE_TYPE, TERRITORY_TYPE):
                continue
            sheet = Sheet(data)
            if kind == VARIATION_TYPE and sheet.name in VARIATION_TABLES:
                found_tables.add(sheet.name)
                self._variations(sheet, path)
            elif kind == GATHERABLE_TYPE:
                found_tables.add(GATHERABLE_TYPE)
                self._gatherables(sheet, path)
            elif kind == TERRITORY_TYPE:
                found_tables.add(TERRITORY_TYPE)
                self._territories(sheet, path)
        # The gatherable rows that a quest task names (a chest of a quest: stage 1.8), from each table of the quest data.
        wanted = {cells.get("GatherableEntryID", "").strip().lower() for rows in self.variations.values() for _, cells in rows.values()}
        wanted.discard("")
        self.quest_named: set[str] = set()
        for path in sources.paths(QUEST_TABLES_FOLDER, ".datasheet"):
            data = sources.read(path)
            if data is None:
                continue
            sheet = Sheet(data)
            for name in sheet.columns:
                for row in range(sheet.row_count):
                    text = sheet.source(row, name).strip().lower()
                    if text in wanted:
                        self.quest_named.add(text)
        sources.check_complete()
        for needed in (*VARIATION_TABLES, GATHERABLE_TYPE, TERRITORY_TYPE):
            if needed not in found_tables:
                raise Failure(f"the game copy has no table {needed}")

    @staticmethod
    def _need(sheet: Sheet, path: str, columns: tuple[str, ...]) -> None:
        for column in columns:
            if column not in sheet.columns:
                raise Failure(f"{path}: the table {sheet.name} has no column {column}")

    def _variations(self, sheet: Sheet, path: str) -> None:
        # The older table LootContainers has no gatherable row: LootTableID is its root (a chest of it has no open rule).
        self._need(sheet, path, ("VariantID", "GatherableEntryID", "LootTableComponent") if sheet.name == VARIATION_TABLES[0]
                   else ("VariantID", "LootTableID"))
        rows = self.variations[sheet.name]
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "VariantID").strip()
            if not identifier:
                continue
            cells = {name: sheet.source(row, name) for name in sheet.columns if sheet.source(row, name) != ""}
            if identifier in rows and rows[identifier][1] != cells:
                raise Failure(f"{path}: the table {sheet.name} has two rows {identifier} with different content")
            rows.setdefault(identifier, (row, cells))

    def _gatherables(self, sheet: Sheet, path: str) -> None:
        self._need(sheet, path, ("GatherableID", "BaseGatherTime", "MinRespawnRate", "MaxRespawnRate", "Restriction",
                                 "IsLootContainer"))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "GatherableID").strip()
            if not identifier:
                continue
            cells = {name: sheet.source(row, name) for name in sheet.columns if sheet.source(row, name) != ""}
            # Two rows of one id with different content (the quest gatherables of the game have some): the first row of the byte
            # order of the paths is the row; a chest whose variation names such a row stops the tool (build_document).
            cells["(table)"] = sheet.name
            if identifier in self.gatherables and self.gatherables[identifier] != cells:
                self.conflicts.add(identifier)
            self.gatherables.setdefault(identifier, cells)
            self.gatherable_crc.setdefault(zlib.crc32(identifier.lower().encode()), identifier)

    def _territories(self, sheet: Sheet, path: str) -> None:
        main = path.rsplit("/", 1)[-1] in TERRITORY_FILES
        self._need(sheet, path, ("TerritoryID", "LootTags") if main else ("TerritoryID",))
        for row in range(sheet.row_count):
            text = sheet.source(row, "TerritoryID").strip()
            if not text:
                continue
            try:
                identifier = int(text)
            except ValueError as error:
                raise Failure(f"{path}: the territory id {text!r} is not a number") from error
            tags = [tag.strip() for tag in sheet.source(row, "LootTags").split(",") if tag.strip()] if "LootTags" in sheet.columns else []
            level_text = sheet.source(row, "AIVariantLevelOverride").strip() if "AIVariantLevelOverride" in sheet.columns else ""
            level = int(finite(level_text, f"{path}: AIVariantLevelOverride of the territory {identifier}")) if level_text else None
            entry = {"tags": tags, "level": level}
            if identifier in self.territories and self.territories[identifier] != entry:
                raise Failure(f"{path}: two territory rows {identifier} with different content")
            self.territories.setdefault(identifier, entry)


def read_rules(sources: Sources) -> dict:
    """@return The instanced chest values of the base attributes of a player: {"resetMinutes", "countLimit"}."""
    roots = sources.stream(BASE_ATTRIBUTES_PATH)
    if roots is None:
        sources.check_complete()
        raise Failure(f"the game copy has no source for {BASE_ATTRIBUTES_PATH}")
    found: dict[int, int] = {}
    for root in roots:
        for element in walk(root):
            if element.name in (field_id(RESET_FIELD), field_id(COUNT_FIELD)) and element.value is not None \
                    and element.name not in found:
                if len(element.value) != 4:
                    raise Failure(f"{BASE_ATTRIBUTES_PATH}: a field of the instanced chests is not a number of 4 bytes")
                found[element.name] = int.from_bytes(element.value, "big")
    for label in (RESET_FIELD, COUNT_FIELD):
        if field_id(label) not in found:
            raise Failure(f"{BASE_ATTRIBUTES_PATH} has no field \"{label}\"")
    if found[field_id(RESET_FIELD)] == 0:
        raise Failure(f"{BASE_ATTRIBUTES_PATH}: \"{RESET_FIELD}\" is 0")
    return {"resetMinutes": found[field_id(RESET_FIELD)], "countLimit": found[field_id(COUNT_FIELD)]}


def gatherable_entry(identifier: str, cells: dict) -> dict:
    """@return The values of a gatherable row that the backend needs. A value with a wrong form stops the tool."""
    what = f"the gatherable row {identifier}"
    open_time = finite(cells.get("BaseGatherTime", ""), f"BaseGatherTime of {what}")
    if not open_time > 0:
        raise Failure(f"BaseGatherTime of {what} is not above 0")
    low = finite(cells.get("MinRespawnRate", ""), f"MinRespawnRate of {what}", 0.0)
    high = finite(cells.get("MaxRespawnRate", ""), f"MaxRespawnRate of {what}", 0.0)
    if low < 0 or high < 0:
        raise Failure(f"a reset value of {what} is below 0")
    if high and high < low:
        raise Failure(f"MaxRespawnRate of {what} is below MinRespawnRate")
    count_text = cells.get("ItemRestrictionQuantity", "")
    level_text = cells.get("RequiredTradeskillLevel", "")
    return {"open": round(open_time, 4), "resetMin": round(low, 3), "resetMax": round(high, 3),
            "elite": is_true(cells.get("IsElite", "")), "restriction": cells.get("Restriction", "").strip(),
            "item": cells.get("ItemRestrictionId", "").strip(),
            "itemCount": int(finite(count_text, f"ItemRestrictionQuantity of {what}", 0.0)),
            "tradeskill": cells.get("Tradeskill", "").strip(),
            "tradeskillLevel": int(finite(level_text, f"RequiredTradeskillLevel of {what}", 0.0)),
            "event": cells.get("GameEventID", "").strip(), "loot": cells.get("FinalLootTable", "").strip(),
            "container": is_true(cells.get("IsLootContainer", "")), "table": cells.get("(table)", ""),
            "openAfterGathering": is_true(cells.get("OpenAfterGathering", "")),
            "ffa": is_true(cells.get("IsFFARestricted", "")),
            "status": any(cells.get(name, "").strip() for name in ("AddedStatusEffect", "RequiredStatusEffect",
                                                                    "ConsumedStatusEffect", "RequiredSongId", "RequiredWhisperId",
                                                                    "RequireControllingFaction", "ProgressionCurrencyCosts"))}


def record_content(record: dict) -> tuple:
    """@return The values of a record that the chest uses (finding CH-R7: two records of one row at a chest must agree)."""
    return (record["root"], tuple(sorted(set(record["territories"]))), record["type"], record["coatlicue"], record["variant"],
            record["override"], record["encounter"])


def unavailable(gatherable: dict | None, root: str, record: dict | None, lock: str, slice_name: str) -> str:
    """@return The key of the reason of an unavailable chest (UNAVAILABLE), or an empty text for an ordinary chest."""
    if gatherable is None:
        return "noGatherable"
    if not gatherable["container"]:
        return "notContainer"
    if gatherable["table"] in QUEST_GATHERABLE_TABLES or gatherable["questTask"]:
        return "quest"
    if slice_name.lower() in LOCK_SLICES:
        return "lockSlice"
    if lock and lock.lower().replace("\\", "/") != MAP_MARKER:
        return "lock"
    if gatherable["restriction"] not in ("", "NoToolNecessary") and gatherable["restriction"] != "ItemRequired":
        return "tool"
    if gatherable["restriction"] == "ItemRequired" or gatherable["item"]:
        return "item"
    if gatherable["tradeskill"] not in ("", "None") or gatherable["tradeskillLevel"]:
        return "tool"
    if gatherable["status"]:
        return "status"
    if gatherable["openAfterGathering"]:
        return "openAfterGathering"
    if gatherable["ffa"]:
        return "ffa"
    if record is not None and record["encounter"]:
        return "encounter"
    if record is not None and record["type"] != 0:
        return "notPoiLoot"
    if not root or root.lower() == "empty":
        return "emptyRoot"
    return ""


def read_records(sources: Sources, region: str, counts: dict) -> list[dict]:
    """@return The GatherableLocations records of the layer metadata files of a region."""
    records = []
    for path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".metadata"):
        roots = sources.stream(path)
        if roots is None:
            continue
        counts["metadataFiles"] += 1
        layer = path[len(REGIONS_FOLDER):-len(".metadata")]
        for root in roots:
            for element in walk(root):
                if element.name != F_GATHERABLES:
                    continue
                for entry in element.children:
                    record = {"layer": layer, "id": 0, "root": "", "position": None, "territories": [], "type": 0,
                              "coatlicue": 0, "variant": 0, "override": 0, "encounter": 0}
                    for child in entry.children:
                        if child.name == F_GATHERABLE_ID:
                            record["id"] = whole(child)
                        elif child.name == F_LOOT_TABLE:
                            record["root"] = child.value.decode("utf-8", "replace") if child.value is not None else ""
                        elif child.name == F_POSITION:
                            if child.value is None or len(child.value) != 12:
                                raise Failure(f"{layer}: a gatherable record has a position that is not three numbers")
                            record["position"] = struct.unpack(">3f", child.value)
                        elif child.name == F_TERRITORIES:
                            record["territories"] = [whole(node) for node in walk(child) if node.value is not None]
                        elif child.name == F_LOOT_TYPE:
                            record["type"] = whole(child)
                        elif child.name == F_COATLICUE:
                            record["coatlicue"] = whole(child)
                        elif child.name == F_HAS_VARIANT:
                            record["variant"] = whole(child)
                        elif child.name == F_OVERRIDE:
                            record["override"] = whole(child)
                        elif child.name == F_ENCOUNTER:
                            record["encounter"] = whole(child)
                    if record["position"] is None or not all(math.isfinite(value) for value in record["position"]):
                        raise Failure(f"{layer}: a gatherable record has no finite position")
                    counts["layerRecords"] += 1
                    records.append(record)
    return records


def cell_of(x: float, y: float) -> tuple[int, int]:
    return (math.floor(x), math.floor(y))


def use_level(sources: Sources, name: str) -> str:
    """Stage DG2: the layers of an own level (a level or a map id of the map table; game_modes.level_terrain). @return The level."""
    import game_modes  # noqa: PLC0415 (the module imports creature_data)
    global REGIONS_FOLDER, WORLD
    level, terrain, _rows = game_modes.level_terrain(sources, name)
    if not terrain.startswith("coatlicue/") or terrain.count("/") != 1:
        raise Failure(f"the terrain project {terrain!r} of the map row is not a folder coatlicue/NAME")
    WORLD = terrain.split("/", 1)[1]
    REGIONS_FOLDER = f"sharedassets/coatlicue/{WORLD}/regions/"
    return level


def build_document(sources: Sources, regions: list[str] | None = None, center: tuple[float, float] | None = None,
                   radius: float | None = None) -> dict:
    tables = Tables(sources)
    rules = read_rules(sources)
    if regions is None:
        names = set()
        for path in sources.paths(REGIONS_FOLDER, ".capitals.json"):
            parts = path[len(REGIONS_FOLDER):].split("/")
            if len(parts) >= 3 and parts[1] == "capitals":
                names.add(parts[0])
        regions = sorted(names)
        if radius is not None:
            near = set(region_names(center or DEFAULT_CENTER, radius))
            regions = [region for region in regions if region in near]
    counts = {"regions": len(regions), "layers": 0, "placements": 0, "metadataFiles": 0, "layerRecords": 0, "recordsByType": {},
              "kind0": 0, "kind0Positions": 0, "kind0ByRoot": {}, "kind0ByGatherable": {}, "kind0Joined": {"chest": 0, "other": 0,
              "none": 0}, "unmatchedFlags": {"override": 0, "coatlicue": 0, "encounter": 0}, "chests": 0, "chestsWithRecord": 0,
              "chestsWithNoRecord": 0, "ordinary": 0, "unavailable": 0, "ordinaryByKind": {}, "unavailableByReason": {},
              "byVariantTable": {}}
    used_gatherables: dict[str, dict] = {}
    chests: list[dict] = []
    keys: dict[str, str] = {}

    def add(table: dict, key, amount: int = 1) -> None:
        table[str(key)] = table.get(str(key), 0) + amount

    for region in regions:
        records = read_records(sources, region, counts)
        by_layer: dict[str, dict[tuple[int, int], list[int]]] = {}
        for index, record in enumerate(records):
            by_layer.setdefault(record["layer"], {}).setdefault(cell_of(*record["position"][:2]), []).append(index)
        joined: dict[int, str] = {}
        layer_paths = sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".capitals.json")
        for path in layer_paths:
            data = sources.read(path)
            if data is None:
                continue
            counts["layers"] += 1
            try:
                document = json.loads(data.decode("utf-8-sig"))
            except (ValueError, AttributeError) as error:
                raise Failure(f"{path}: the placements of the layer did not parse: {error}") from error
            if not isinstance(document, dict) or not isinstance(document.get("Capitals"), list):
                raise Failure(f"{path}: the layer has no list Capitals")
            layer = path[len(REGIONS_FOLDER):-len(".capitals.json")]
            grid = by_layer.get(layer, {})
            for placement in document["Capitals"]:
                if not isinstance(placement, dict):
                    raise Failure(f"{layer}: a placement is not an object")
                counts["placements"] += 1
                where = placement.get("worldPosition")
                variant = str(placement.get("variantName") or "")
                table_name = next((name for name in VARIATION_TABLES if variant in tables.variations[name]), None)
                if not isinstance(where, dict) or any(axis not in where for axis in "xyz"):
                    if table_name is not None:
                        raise Failure(f"{layer}: the chest placement {placement.get('id')} has no position with x, y, and z")
                    continue
                try:
                    position = tuple(float(where[axis]) for axis in "xyz")
                except (TypeError, ValueError) as error:
                    raise Failure(f"{layer}: a placement has a position that is not a number") from error
                if not all(math.isfinite(value) for value in position):
                    if table_name is not None:
                        raise Failure(f"{layer}: the chest placement {placement.get('id')} has a position that is not finite")
                    continue
                # The join of the records of this layer near the placement (the grid of 1 m cells).
                near: list[int] = []
                cx, cy = cell_of(position[0], position[1])
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for index in grid.get((cx + dx, cy + dy), []):
                            other = records[index]["position"]
                            if sum((other[axis] - position[axis]) ** 2 for axis in range(3)) <= JOIN_DISTANCE ** 2:
                                near.append(index)
                for index in near:
                    joined.setdefault(index, "chest" if table_name is not None else "other")
                    if table_name is not None:
                        joined[index] = "chest"
                if table_name is None:
                    continue
                if center is not None and radius is not None and math.hypot(position[0] - center[0], position[1] - center[1]) > radius:
                    continue
                identifier = str(placement.get("id", ""))
                try:
                    actor = actor_of(identifier)
                    identifier = str(uuid.UUID(identifier))
                except ValueError as error:
                    raise Failure(f"{layer}: the chest placement id {identifier!r} is not a UUID") from error
                rotation = placement.get("rotation") or {}
                normal = json.dumps([variant, [round(value, 4) for value in position], rotation,
                                     str(placement.get("sliceName") or "")], sort_keys=True)
                if identifier in keys:
                    if keys[identifier] != normal:
                        raise Failure(f"the placement {identifier} has two different placements")
                    continue
                keys[identifier] = normal
                row, cells = tables.variations[table_name][variant]
                gatherable_id = cells.get("GatherableEntryID", "").strip()
                gatherable_cells = tables.gatherables.get(gatherable_id)
                if gatherable_cells is None and gatherable_id and gatherable_id not in GATHERABLES_NOT_IN_GAME:
                    raise Failure(f"the variation row {variant} of {table_name} names the gatherable row {gatherable_id}, which no "
                                  f"gatherable table has")
                if gatherable_id in tables.conflicts:
                    raise Failure(f"the variation row {variant} of {table_name} names the gatherable row {gatherable_id}, which two "
                                  f"rows of the gatherable tables have with different content")
                gatherable = gatherable_entry(gatherable_id, gatherable_cells) if gatherable_cells is not None else None
                if gatherable is not None:
                    gatherable["questTask"] = gatherable_id.lower() in tables.quest_named
                if gatherable is not None:
                    used_gatherables[gatherable_id] = gatherable
                # The record of the chest (research K1: 0.06 m): only the records of the gatherable row of the variation (code review
                # of stage 3.5b.1, finding CH-R7: GatherableId is the CRC-32 of the lower-case row id), then the nearest one. Two
                # records of the row at the chest with different content stop the tool: the order of the file does not choose.
                record = None
                crc = zlib.crc32(gatherable_id.lower().encode()) if gatherable_id else None
                matching = sorted((index for index in near if records[index]["id"] == crc),
                                  key=lambda index: sum((records[index]["position"][axis] - position[axis]) ** 2 for axis in range(3)))
                if matching:
                    record = records[matching[0]]
                    for index in matching[1:]:
                        if record_content(records[index]) != record_content(record):
                            raise Failure(f"the chest {identifier} of {layer} has two records of its gatherable row {gatherable_id} "
                                          f"with different content")
                root = (record["root"] if record is not None and record["root"]
                        else cells.get("LootTableComponent", cells.get("LootTableID", ""))).strip()
                territories = sorted(set(record["territories"])) if record is not None else []
                tags: list[str] = []
                levels = []
                for territory in territories:
                    entry = tables.territories.get(territory)
                    if entry is None:
                        # Finding CH-R6: a territory with no row has tags and a level that the tool does not know.
                        raise Failure(f"the record of the chest {identifier} of {layer} names the territory {territory}, which no "
                                      f"TerritoryDefinition table has")
                    tags.extend(tag for tag in entry["tags"] if tag not in tags)
                    if entry["level"] is not None:
                        levels.append(entry["level"])
                lock = cells.get("LootLockSlice", "").strip()
                slice_name = str(placement.get("sliceName") or "")
                reason = unavailable(gatherable, root, record, lock, slice_name)
                if record is None and not reason:
                    # Finding CH-R5: an ordinary chest needs its record (the gate IsEncounter, the type, the root, the territories).
                    raise Failure(f"the chest {identifier} of {layer} has no record of its gatherable row {gatherable_id} in the "
                                  f"layer metadata (the source has no metadata file of the layer, or the file has no such record)")
                kind = KIND_OF_ROOT.get(root.lower(), "elite" if gatherable is not None and gatherable["elite"] else "other")
                state = "unavailable" if reason else "ordinary"
                flags = {"coatlicue": record["coatlicue"], "variant": record["variant"], "override": record["override"],
                         "encounter": record["encounter"], "instancedLootType": record["type"]} if record is not None else {}
                chests.append({"key": identifier, "actor": actor.hex(), "position": [round(value, 3) for value in position],
                               "rotation": [round(float(rotation.get(axis, 0.0 if axis != "w" else 1.0)), 6) for axis in "xyzw"],
                               "slice": slice_name, "variant": variant, "table": table_name, "row": row,
                               "name": cells.get("Name", ""), "gatherable": gatherable_id, "root": root, "kind": kind, "tags": tags,
                               "contentLevel": max(levels) if levels else None, "territories": territories,
                               "metadata": record is not None, "flags": flags, "lock": lock, "state": state,
                               "reason": UNAVAILABLE[reason] if reason else "", "region": region})
                counts["chests"] += 1
                add(counts["byVariantTable"], table_name)
                counts["chestsWithRecord" if record is not None else "chestsWithNoRecord"] += 1
                if reason:
                    counts["unavailable"] += 1
                    add(counts["unavailableByReason"], reason)
                else:
                    counts["ordinary"] += 1
                    add(counts["ordinaryByKind"], kind)
        # The census of the kind-0 records of the layers (research K1): each record one time, with its join.
        positions = set()
        for index, record in enumerate(records):
            add(counts["recordsByType"], record["type"])
            if record["type"] != 0:
                continue
            counts["kind0"] += 1
            positions.add(tuple(round(value, 3) for value in record["position"]))
            add(counts["kind0ByRoot"], record["root"] or "(none)")
            add(counts["kind0ByGatherable"], tables.gatherable_crc.get(record["id"], f"crc {record['id']:08x}"))
            how = joined.get(index, "none")
            counts["kind0Joined"][how] += 1
            if how == "none":
                for flag in ("override", "coatlicue", "encounter"):
                    counts["unmatchedFlags"][flag] += 1 if record[flag] else 0
        counts["kind0Positions"] += len(positions)
    sources.check_complete()
    for name in ("recordsByType", "kind0ByRoot", "kind0ByGatherable", "ordinaryByKind", "unavailableByReason", "byVariantTable"):
        counts[name] = dict(sorted(counts[name].items(), key=lambda item: (-item[1], item[0])))
    chests.sort(key=lambda entry: (entry["region"], entry["position"][0], entry["position"][1], entry["key"]))
    return {"format": FORMAT, "world": WORLD, "counts": counts, "rules": rules, "reasons": UNAVAILABLE,
            "gatherables": dict(sorted(used_gatherables.items())), "chests": chests}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the chests of the world as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="a region (r_+04_+02); more than one is possible")
    parser.add_argument("--radius", type=float, help="only the chests in this circle (meters)")
    parser.add_argument("--center", type=float, nargs=2, metavar=("X", "Y"), help="the center of the circle")
    parser.add_argument("--list", metavar="STATE_OR_KIND", help="print the chests of a state (ordinary, unavailable) or a kind "
                                                                "(supply, provision, alchemy, elite, other), and write no file")
    parser.add_argument("--level", metavar="LEVEL", help="stage DG2: the chests of the layers of an own level (a level or a map id of the "
                                                         "map table; default output GAMEDATA/levels/LEVEL/chest-data.json)")
    arguments = parser.parse_args()
    if arguments.level and (arguments.region or arguments.radius is not None):
        print("chests: --level takes the whole terrain of the level: no --region and no --radius. No file was written.", file=sys.stderr)
        return 1
    if arguments.radius is not None and not (math.isfinite(arguments.radius) and arguments.radius > 0):
        print("chests: --radius must be a number above 0. No file was written.", file=sys.stderr)
        return 1
    center = tuple(arguments.center) if arguments.center else (DEFAULT_CENTER if arguments.radius is not None else None)
    target = Path(arguments.output)
    try:
        # Stage DG2: the layers of a level are below its terrain folder; the map table needs the texts of the modes.
        sources = Sources(game / "assets", WANTED + (("sharedassets/coatlicue/", "localization/") if arguments.level else ()))
        level = use_level(sources, arguments.level) if arguments.level else None
        if level is not None and arguments.output == parser.get_default("output"):
            target = gamedata / "levels" / level / OUTPUT_NAME
        document = build_document(sources, arguments.region, center, arguments.radius)
        if level is not None:
            document["level"] = level
        if not arguments.list:
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".tmp")
            temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"chests: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"chests: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.list:
        for entry in document["chests"]:
            if arguments.list not in (entry["state"], entry["kind"]):
                continue
            x, y, z = entry["position"]
            print(f"{entry['key']}  {x:9.2f} {y:9.2f} {z:8.2f}  {entry['kind']:9s} {entry['variant']:28s} {entry['gatherable']:24s} "
                  f"{entry['root']:28s} {entry['state']}{(': ' + entry['reason']) if entry['reason'] else ''}")
    counts = document["counts"]
    print(f"{'the game copy' if arguments.list else target}: {counts['chests']} chests ({counts['ordinary']} ordinary, "
          f"{counts['unavailable']} unavailable) in {counts['regions']} regions; {counts['chestsWithNoRecord']} with no metadata record")
    print("  ordinary: " + ", ".join(f"{kind} {count}" for kind, count in counts["ordinaryByKind"].items()))
    print("  unavailable: " + ", ".join(f"{reason} {count}" for reason, count in counts["unavailableByReason"].items()))
    print(f"  census: {counts['kind0']} kind-0 records of {counts['metadataFiles']} layer metadata files at {counts['kind0Positions']} "
          f"places; joined to a chest {counts['kind0Joined']['chest']}, to a different placement {counts['kind0Joined']['other']}, "
          f"none {counts['kind0Joined']['none']} (flags of these: " + ", ".join(f"{key} {value}" for key, value in
                                                                              counts["unmatchedFlags"].items()) + ")")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

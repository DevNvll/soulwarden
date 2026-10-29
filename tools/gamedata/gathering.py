#!/usr/bin/env python3
"""Write the gathering data of the world (stage 3.6a, docs/systems/gathering.md): one JSON file and one file for each region.

The backend reads GAMEDATA_DIR/gathering-data.json at its start, and the file of a region (the file that gathering-data.json names:
GAMEDATA_DIR/gathering/SET/REGION.swgn) the first time that it needs a node of that region. Without the files no node can be gathered
(each request is refused).

The sources (all in the pak files of the game copy; research gathering-stage.md and the live checks of stage 3.6a):

  1. The gatherable rows: each table of the type GatherableData (Gatherables, QuestGatherables, GatherablesDunwood,
     GatherablesCatacombs, Gatherables_IsleOfNight). A row has the gathering type (the tool family), the restriction, the trade skill
     and its rank gate, BaseGatherTime (the work amount), the respawn bounds, the loot root (FinalLootTable), the game event (the
     trade skill experience), and the flags. The first row of an id in the byte order of the paths is the row. Stage QG5b: the
     restriction ItemRequired with an item is supported (the backend checks ItemRestrictionQuantity of the item in the bag); the
     KillEnemyType cells of the task tables (ObjectiveTasks) name the events of the quest tasks (a direct quest gimmick: task_event_form).
  2. The variation rows: the variation table of a gathering slice (m_variationTableUniqueName of its VariationDataComponent) has a
     row for each variant (VariantID) with its gatherable row (GatherableEntryID).
  3. The gathering slices: a slice with a GatherableController component (class 0b382af4-...; m_replicationIndex: the slot of the
     node state, type 12), a variation table of ORDINARY_TABLES, and an Interact bound with the option GatherOption. Its root entity,
     the Interact component, the entity of the bound and of its option, the range, and the capacity come from the slice
     (interactions.py, Template).
  4. The natural nodes: each region.distribution file (the reader of the client 0x69DD030; collision_data.py). The client gives each
     record an offline id (client 0x69D5090, called by the record converter 0x6A01D50; the setting javelin.offline-id-version is
     not 1 in the live client): G = ((counter & 0x7FFFF) << 13) | ((regionY & 0x1F) << 8) | ((regionX & 0x1F) << 3) | 1. The counter
     starts at 1 and counts the primary records, then the records of the two tails (live check L2 of stage 3.6a: seven nodes of
     r_+04_+02). The place: x = regionX * 2048 + (packed >> 16) * (2048 / 65535 as a float), y with the low 16 bits; the height is
     the terrain height of the backend (the height files of the game data folder, heights.sh) and, for a height mode that is not 0,
     the water surface (region.waterqt, the ocean level of terrain.json).
  5. The capital nodes: the placements of the layers (regions/REGION/capitals/LAYER/LAYER.capitals.json) whose slice is a gathering
     slice. The backend finds them in the interaction data (interaction-data.json) by the placement UUID; this file gives their
     gatherable row.
  6. The skinning of a creature: each creature slice of the creature data (GAMEDATA_DIR/creatures.json, member "slices") with a
     GatherableController (m_gatherableEntryId: the gatherable row) and an Interact bound with GatherOption.
  7. The tools: each master item (MasterItemDefinitions) whose ItemStatsRef is a weapon row (WeaponItemDefinitions) with a
     gathering type (GatheringTypes): MinGatherEFF, MaxGatherEFF (live check L7: the "Gather Speed" of the tooltip is the linear
     value over the gear score range of the item), GatheringEfficiency, GatheringMultiplier.
  8. The trade skill ranks: the TradeskillRankData tables Mining, Logging, Harvesting, Skinning: RollBonus, GatheringEfficiency,
     GatheringMultiplier of each Level.
  9. The game events of the gatherable rows (GameEventData): CategoricalProgressionId and CategoricalProgressionReward.

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when
a needed table or column is not there, when a value has a wrong form (a time that is not a finite number above 0, a respawn bound
below 0 or a largest bound below the smallest one, a rank that is not a whole number, a range that is not above 0), when a variation
row of a gathering slice names a gatherable row that no table has, when two variation rows of one table have one id and different
content, when a gathering slice has no root, no Interact bound with GatherOption, or not one GatherableController, when a gatherable
row names an event that no event table has, and when a region file has a record that names a slice index outside its list or does
not end at its last byte. From the code review of stage 3.6a (finding GA-R4) it also stops when a GatherableData table has no column of
a gate of its rows (GATE_COLUMNS: the restriction, the item, the gatherer count, the work reset, the causes of UNSUPPORTED_COLUMNS and
UNSUPPORTED_FLAGS; the exceptions of the game data are MISSING_GATE_COLUMNS), when a flag cell has a text that is not "", "0", "1",
"true", or "false" (any case), and when a GameEventData table has no column EventID, CategoricalProgressionId, or
CategoricalProgressionReward. A blank cell is no rule (a blank reward is 0); a missing column is a source that is not complete.
tools/gamedata/check_gathering_tool.py is the check of these rules, with made-up sources.

The publication (finding GA-R6): the region files go into a staging folder (gathering/.staging-PID), the tool checks each one against
the document (its size and its digest), moves the folder to gathering/SET (SET: the first 16 hexadecimal digits of the SHA-256 of the
region digests), and writes gathering-data.json last. A stop before that leaves the published set (the manifest and its region files)
as it was. Each published set stays after a publication (code review of user test 2, finding U2-R6: a backend that runs holds the
manifest of its start and reads its region files later, also after more than one publication; the game of the user can run from Steam
while a stage writes the common game data folder): a publication removes only the staging folders of stopped exports. The option
--prune removes the sets and the region files of the layout of before (gathering/REGION.swgn) that the current manifest does not name;
it refuses (status 1, no change) while a backend process runs (a program soulwarden-backend), because that backend can hold an earlier
manifest. Run one export at a time.

Output:
  gathering-data.json (one line of JSON):
  {"format": 1, "world": TEXT, "option": {"name": "GatherOption", "id": CRC, "action": 14},
   "rows": [{"id", "table", "type", "restriction", "tradeskill", "level", "time", "respawn": [MIN, MAX], "loot", "material", "event",
             "reset", "destroy", "depleted", "required", "item", "itemCount", "waitForImpact", "unsupported": [CAUSE, ...]}, ...],
   "events": {EVENT: [PROGRESSION, AMOUNT], ...},
   "ranks": {SKILL: [[ROLL_BONUS, EFFICIENCY, MULTIPLIER], ...]},          (index: the Level column)
   "tools": {ITEM_ID: {"types": [TYPE, ...], "min", "max", "efficiency", "multiplier"}, ...},
   "templates": [{"slice", "asset", "sub", "root", "controllerSlot", "table", "bound": {"entity", "component", "slot", "rangeSq",
                  "capacity", "translation": [X, Y, Z], "option": OPTION_ENTITY}}, ...],
   "capitals": {PLACEMENT_UUID: [TEMPLATE, ROW] or [TEMPLATE, ROW, INTERACT_TAG, ONLY_ON_QUEST] (a quest object), ...},
   "children" (stage QG5a): {PLACEMENT_UUID/SPAWNER_ENTITY: [TEMPLATE, ROW, INTERACT_TAG, ONLY_ON_QUEST], ...} (a quest object child of a
              spawner of a placement: the key of its object in interaction-data.json),
   "encounterObjects" (stage QG5c2): {SLICE PATH: [TEMPLATE, ROW, INTERACT_TAG, ONLY_ON_QUEST], ...} (the start object of an encounter
              recipe of the quests, a server child of the encounter system; a backend of before does not read it),
   "regions": [{"name", "x", "y", "file", "records", "tails", "version", "digest", "types": [[TEMPLATE, ROW, VARIANT], ...]}, ...],
   "creatures": {ASSET:SUB: {"slice", "template", "row"}, ...},
   "counts": {...}}
  TEMPLATE and ROW are indexes (-1: not a gathering type). `version` is the SHA-256 (first 16 hexadecimal digits) of the
  distribution file: a stored node of an older version is not the same node (the backend drops it). `file` is the region file of the
  set (gathering/SET/REGION.swgn), and `digest` the SHA-256 (first 16 hexadecimal digits) of that file: the backend refuses a region
  file with a different digest (a file of another export).

  gathering/SET/REGION.swgn (little-endian): "SWGN" format:u32 (1) regionX:i32 regionY:i32 records:u32 tails:u32, then for each
  primary record: type:u16 x:f32 y:f32 z:f32 (14 bytes). Record N has the counter N + 1 (its G: the rule above).

Usage: gathering.py [OUTPUT.json] [--region NAME]... [--near X Y METERS]
       gathering.py [OUTPUT.json] --prune
  --region: only these regions (a file for a test); --near: print the gathering nodes near a place (with their G) and write no file;
  --prune: remove the sets that the manifest does not name (no export; only when no backend runs).
Default output: <SOULWARDEN_GAMEDATA_DIR>/gathering-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import uuid
import zlib
from array import array
from itertools import accumulate
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import CATALOG_PATH, REGIONS_FOLDER, TABLES_FOLDER, WORLD, Catalog, Sources  # noqa: E402
from interactions import SPAWNER_CLASS, UI_DATABASE, Template, read_options, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import Element, find_field  # noqa: E402

OUTPUT_NAME = "gathering-data.json"
FOLDER = "gathering"
FORMAT = 1
REGION_FORMAT = 1
REGION_SIZE = 2048
TERRAIN_PATH = f"sharedassets/coatlicue/{WORLD}/terrain.json"
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, "slices/", "coatgen/", UI_DATABASE, CATALOG_PATH, TERRAIN_PATH)
GATHERABLE_TYPE = "GatherableData"
VARIATION_TYPE = "VariationData"
RANK_TYPE = "TradeskillRankData"
EVENT_TYPE = "GameEventData"
ITEM_TYPE = "MasterItemDefinitions"
WEAPON_TYPE = "WeaponItemDefinitions"
# Stage QG5b: the task tables of the quests (their KillEnemyType names the game event of a task: the rule of a direct quest gimmick).
TASK_TYPE = "ObjectiveTasks"
GATHERING_SKILLS = ("Mining", "Logging", "Harvesting", "Skinning")
GATHER_OPTION = "GatherOption"
CONTROLLER_CLASS = uuid.UUID("0b382af4-7946-4698-9223-206d1187b612")
TABLE_FIELD = field_id("m_variationTableUniqueName")
ENTRY_FIELD = field_id("m_gatherableEntryId")
SLOT_FIELD = field_id("m_replicationIndex")
SELECTED_FIELD = field_id("m_selectedVariant")
# The variation tables of the ordinary resource slices of the world (research Q1, the census families: Master_Tree, Master_Bush,
# Master_Minerals, Master_Alchemy, Master_Plants, Master_Log, Master_Stones, and the _Snap slices that use the same tables; and the
# dye plants). The other gathering tables (quests, chests, cyclic, events, holidays, cinematics) belong to their own stages.
ORDINARY_TABLES = ("Gatherable_Trees", "Gatherable_Bushes", "Gatherable_Minerals", "Gatherable_Alchemy", "Gatherable_Plants",
                   "Gatherable_Logs", "Gatherable_Stones", "Gatherable_DyePlants")
# Stage 1.8c (docs/systems/quests.md): the variation table of the quest objects of the slice Master_Quest_Instanced (MSQ_Treasure_A of the
# beach chain). A placement of a layer with such a variant is a quest object: its row gets the member "quest" (its result is the quest
# drops of the tasks of its character and its game event; the loot container of the row with the loot table Empty is not a refusal), and its
# capital entry gets the InteractTag and InteractOnlyOnQuest of the variation row (the client shows the object only during a task with the
# tag: live check G1 of stage 1.8c). The other quest tables (proximity, glyphs, damageable objects) belong to later stages.
# Stage QG5a: also the rescue objects (Gatherables_NPCRescue_01: the trapped animals of 09A_S1_SaveAnimals, the captives of the faction
# introductions; the same columns, GatherableEntryId with a lower-case d) and the glyph locks (Gatherable_Quest_AncientGlyph: the Azoth Staff
# tool, the rank of AzothStaff, and the required achievement of the row: interactions.py BOUND_ACTIONS). A quest placement of any slice path
# (not only slices/gatherables/: the custommqi_ slices of the main story) is a quest object, with the variant of the placement, else the
# m_selectedVariant of its slice.
QUEST_TABLES = ("Gatherable_Quest", "Gatherables_NPCRescue_01", "Gatherable_Quest_AncientGlyph")
# Stage 1.8e (docs/systems/quests.md, quest_objects.py): the variation table of the destructible quest objects (the corruption nodules of
# 06_survivors, the monolith of 06_demonstorm2). This tool reads its rows for quest_objects.py and writes no node of it.
DAMAGEABLE_TABLES = ("Gatherable_Quest_Damageable",)
# Stage QG5a (quest_objects.py): the variation table of the proximity quest objects (trails, footprints, torches: the slice
# master_quest_proximity_instanced with a DetectionVolumeComponent and m_isProximityGatherable). This tool reads its rows for
# quest_objects.py and writes no node of it.
PROXIMITY_TABLES = ("Gatherable_Quest_Proximity",)
# The cause of a loot container that a quest object does not have (its loot is the quest drops).
LOOT_CONTAINER_CAUSE = "a loot container (a chest: stage 3.5b)"
# The causes of a row that the backend refuses (a rule of the data that the backend has no predicate for).
UNSUPPORTED_COLUMNS = (("RequiredSongId", "a song"), ("RequiredWhisperId", "a whisper"), ("UsableEmoteTags", "an emote"),
                       ("AddedStatusEffect", "a status effect"), ("RequiredStatusEffect", "a status effect"),
                       ("ConsumedStatusEffect", "a status effect"), ("ProgressionCurrencyCosts", "a cost"))
# Stage QG5a: RequireControllingFaction is a flag (the client loader 0x440D2A0 reads it with the flag converter 0x5BF6BC0 at 0x440DC07, as
# IsLootContainer and ResetGatherTimeOnEndInteraction): "0" (557 rows of the quest tables) is no rule; before stage QG5a any text of the cell
# was the cause "a faction", and those quest objects were refused.
UNSUPPORTED_FLAGS = (("IsLootContainer", "a loot container (a chest: stage 3.5b)"), ("OpenAfterGathering", "OpenAfterGathering"),
                     ("RequireLootItems", "RequireLootItems"), ("IsFFARestricted", "IsFFARestricted"),
                     ("RequireControllingFaction", "a faction"))
# The columns of the gates of a gatherable row (code review of stage 3.6a, finding GA-R4): each GatherableData table must have them. A
# blank cell is no rule; a missing column is a source that is not complete, and the tool stops (it does not read the gate as false).
GATE_COLUMNS = (("Restriction", "ItemRestrictionId", "NumRequiredToGather", "ResetGatherTimeOnEndInteraction")
                + tuple(column for column, _ in UNSUPPORTED_COLUMNS) + tuple(column for column, _ in UNSUPPORTED_FLAGS))
# The gate columns that a table of the game data does not have (the census of the supported client build): the rows of the table have no
# such rule. GatherablesDunwood has 9 supported rows of the retail data.
MISSING_GATE_COLUMNS = {"IsFFARestricted": ("GatherablesDunwood", "QuestGatherables"),
                        "OpenAfterGathering": ("GatherablesDunwood", "QuestGatherables", "Gatherables_IsleOfNight"),
                        "ProgressionCurrencyCosts": ("GatherablesDunwood", "QuestGatherables", "Gatherables_IsleOfNight")}
# The texts of a flag cell (finding GA-R4; the retail tables have "", "0", "1", and one "FALSE"). A different text stops the tool.
FLAG_TEXTS = {"": False, "0": False, "1": True, "false": False, "true": True}
# The columns of a game event of a gatherable row (finding GA-R4: a missing reward column is not an experience of 0).
EVENT_COLUMNS = ("EventID", "CategoricalProgressionId", "CategoricalProgressionReward")
SUPPORTED_RESTRICTIONS = ("NoToolNecessary", "ToolRequired")
# The restriction of an item (stage QG5b: the backend checks the item in the bag; before, only a row with no item was supported).
ITEM_RESTRICTION = "ItemRequired"
# The gathering types of the four tools (GatheringTypes of the weapon rows; the paperdoll slots 33 to 36 of protocol notes 4.11). A row of
# a different type (AzothStaff: an encounter of a later stage; fishing: stage 3.6b) is refused.
# Stage QG5a: AzothStaff, the Azoth Staff of the glyph locks (paperdoll slot 38: client 0x5CAB873, the query 0x48FF240; static read
# evidence/quests/qg5a/implement/static/ANSWERS.md C2).
TOOL_TYPES = ("Chopping", "Cutting", "Dressing", "Mining", "AzothStaff")
# The folder of a set of region files that the tool writes before it moves the folder to its name (finding GA-R6).
STAGING_PREFIX = ".staging-"
STALE_PREFIX = ".old-"
REGION_HEADER = 24
REGION_RECORD = 14


def flag(text: str, what: str) -> bool:
    """@return The value of a flag cell (FLAG_TEXTS, any case). A different text stops the tool (code review of stage 3.6a, finding
    GA-R4): an unknown text is not false."""
    value = FLAG_TEXTS.get(text.strip().lower())
    if value is None:
        raise Failure(f"{what} has the text {text!r}, which is not a flag (\"\", \"0\", \"1\", \"true\", \"false\")")
    return value


def digest(data: bytes) -> str:
    """@return The digest of a region file: the first 16 hexadecimal digits of its SHA-256 (the backend compares it, finding GA-R6)."""
    return hashlib.sha256(data).hexdigest()[:16]


def finite(text: str, what: str, default: float | None = None) -> float:
    text = text.strip()
    if not text:
        if default is None:
            raise Failure(f"{what} has no value")
        return default
    try:
        value = float(text)
    except ValueError as error:
        raise Failure(f"{what} is not a number: {text!r}") from error
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number: {text!r}")
    return value


def need(sheet: Sheet, path: str, columns: tuple[str, ...]) -> None:
    for column in columns:
        if column not in sheet.columns:
            raise Failure(f"{path}: the table {sheet.name} has no column {column}")


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def offline_id(region_x: int, region_y: int, counter: int) -> int:
    """@return G of a record of a distribution file (client 0x69D5090, offline id version not 1; live check L2 of stage 3.6a)."""
    return (((counter & 0x7FFFF) << 13) | ((region_y & 0x1F) << 8) | ((region_x & 0x1F) << 3) | 1) & 0xFFFFFFFFFFFFFFFF


def region_origin(name: str) -> tuple[int, int]:
    match = re.fullmatch(r"r_([+-]\d+)_([+-]\d+)", name)
    if not match:
        raise Failure(f"{name} is not a region name (r_+XX_+YY)")
    return int(match[1]), int(match[2])


class Tables:
    """The tables of the gathering data."""

    def __init__(self, sources: Sources) -> None:
        self.rows: dict[str, tuple[str, dict]] = {}
        self.variations: dict[str, dict[str, dict]] = {}
        self.ranks: dict[str, list[list[float]]] = {}
        self.events: dict[str, list] = {}
        self.items: dict[str, dict] = {}
        self.weapons: dict[str, dict] = {}
        # Stage QG5b: the game events (lower case) that a quest task names (KillEnemyType of the task tables).
        self.task_events: set[str] = set()
        found = set()
        for path in sources.paths(TABLES_FOLDER, ".datasheet"):
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            if kind not in (GATHERABLE_TYPE, VARIATION_TYPE, RANK_TYPE, EVENT_TYPE, ITEM_TYPE, WEAPON_TYPE, TASK_TYPE):
                continue
            sheet = Sheet(data)
            if kind == TASK_TYPE:
                if "KillEnemyType" in sheet.columns:
                    for row in range(sheet.row_count):
                        for event in str(sheet.cell(row, "KillEnemyType")).split(","):
                            if event.strip():
                                self.task_events.add(event.strip().lower())
                continue
            found.add(kind)
            if kind == GATHERABLE_TYPE:
                self._gatherables(sheet, path)
            elif kind == VARIATION_TYPE:
                self._variations(sheet, path)
            elif kind == RANK_TYPE and sheet.name in GATHERING_SKILLS:
                self._ranks(sheet, path)
            elif kind == EVENT_TYPE:
                self._events(sheet, path)
            elif kind == ITEM_TYPE:
                self._items(sheet, path)
            elif kind == WEAPON_TYPE:
                self._weapons(sheet, path)
        sources.check_complete()
        for kind in (GATHERABLE_TYPE, VARIATION_TYPE, RANK_TYPE, EVENT_TYPE, ITEM_TYPE, WEAPON_TYPE):
            if kind not in found:
                raise Failure(f"the game copy has no table of the type {kind}")
        for skill in GATHERING_SKILLS:
            if skill not in self.ranks:
                raise Failure(f"the game copy has no trade skill rank table {skill}")

    def _gatherables(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ("GatherableID", "GatheringType", "Restriction", "Tradeskill", "RequiredTradeskillLevel", "BaseGatherTime",
                           "MinRespawnRate", "MaxRespawnRate", "FinalLootTable", "GameEventID"))
        for column in GATE_COLUMNS:
            if column not in sheet.columns and sheet.name not in MISSING_GATE_COLUMNS.get(column, ()):
                raise Failure(f"{path}: the table {sheet.name} has no column {column} (a gate of its rows: a blank cell is no rule, a "
                              f"missing column is a source that is not complete)")
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "GatherableID").strip()
            if identifier and identifier.lower() not in self.rows:
                self.rows[identifier.lower()] = (sheet.name, {name: sheet.source(row, name) for name in sheet.columns
                                                              if sheet.source(row, name) != ""})

    def _variations(self, sheet: Sheet, path: str) -> None:
        tagged = sheet.name in QUEST_TABLES or sheet.name in DAMAGEABLE_TABLES or sheet.name in PROXIMITY_TABLES
        if sheet.name not in ORDINARY_TABLES and not tagged:
            return
        # The column of the gatherable row: GatherableEntryID (Gatherables_NPCRescue_01 spells it GatherableEntryId).
        entry_column = next((column for column in sheet.columns if column.lower() == "gatherableentryid"), "GatherableEntryID")
        need(sheet, path, ("VariantID", entry_column) + (("InteractTag", "InteractOnlyOnQuest") if tagged else ()))
        rows = self.variations.setdefault(sheet.name, {})
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "VariantID").strip()
            if not identifier:
                continue
            cells = {"row": row, "gatherable": sheet.source(row, entry_column).strip()}
            if sheet.name in PROXIMITY_TABLES:
                # Stage QG5a: the sphere of the detection volume of a proximity object (Change Sphere Shape Radius of the root entity).
                cells["radius"] = finite(sheet.source(row, "DetectionVolumeSphereRadius"), f"{path}: DetectionVolumeSphereRadius of {identifier}", 0.0)
            if tagged:
                cells["tag"] = sheet.source(row, "InteractTag").strip()
                cells["onlyOnQuest"] = flag(sheet.source(row, "InteractOnlyOnQuest"), f"{path}: InteractOnlyOnQuest of {identifier}")
            if identifier.lower() in rows and rows[identifier.lower()]["gatherable"] != cells["gatherable"]:
                raise Failure(f"{path}: the table {sheet.name} has two rows {identifier} with different content")
            rows.setdefault(identifier.lower(), cells)

    def _ranks(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ("Level", "RollBonus", "GatheringEfficiency", "GatheringMultiplier"))
        ranks: dict[int, list[float]] = {}
        for row in range(sheet.row_count):
            level_text = sheet.source(row, "Level").strip()
            if not level_text:
                continue
            level = finite(level_text, f"{path}: the Level of row {row}")
            if level != int(level) or level < 0:
                raise Failure(f"{path}: the Level of row {row} is not a whole number of 0 or more")
            what = f"{path}: rank {int(level)}"
            ranks[int(level)] = [finite(sheet.source(row, "RollBonus"), f"{what}: RollBonus", 0.0),
                                 round(finite(sheet.source(row, "GatheringEfficiency"), f"{what}: GatheringEfficiency", 0.0), 6),
                                 round(finite(sheet.source(row, "GatheringMultiplier"), f"{what}: GatheringMultiplier", 0.0), 6)]
        if not ranks or sorted(ranks) != list(range(len(ranks))):
            raise Failure(f"{path}: the levels of the table {sheet.name} are not 0, 1, 2, ... with no gap")
        self.ranks[sheet.name] = [ranks[level] for level in range(len(ranks))]

    def _events(self, sheet: Sheet, path: str) -> None:
        # Each event table must have the three columns (finding GA-R4: the retail tables have them; a blank reward cell is 0).
        need(sheet, path, EVENT_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "EventID").strip()
            if not identifier or identifier.lower() in self.events:
                continue
            progression = sheet.source(row, "CategoricalProgressionId").strip()
            reward = finite(sheet.source(row, "CategoricalProgressionReward"), f"the event {identifier}: CategoricalProgressionReward", 0.0)
            self.events[identifier.lower()] = [identifier, progression, reward]

    def _items(self, sheet: Sheet, path: str) -> None:
        if "ItemStatsRef" not in sheet.columns or "ItemClass" not in sheet.columns:
            return
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "ItemID").strip()
            reference = sheet.source(row, "ItemStatsRef").strip()
            if identifier and reference and identifier.lower() not in self.items:
                self.items[identifier.lower()] = {"id": identifier, "ref": reference, "class": sheet.source(row, "ItemClass")}

    def _weapons(self, sheet: Sheet, path: str) -> None:
        if "GatheringTypes" not in sheet.columns:
            return
        need(sheet, path, ("WeaponID", "GatheringTypes", "MinGatherEFF", "MaxGatherEFF"))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "WeaponID").strip()
            types = [part.strip() for part in re.split(r"[+,]", sheet.source(row, "GatheringTypes")) if part.strip()]
            if not identifier or not types or identifier.lower() in self.weapons:
                continue
            what = f"{path}: the weapon {identifier}"
            low = finite(sheet.source(row, "MinGatherEFF"), f"{what}: MinGatherEFF", 0.0)
            high = finite(sheet.source(row, "MaxGatherEFF"), f"{what}: MaxGatherEFF", low)
            if low < 0 or high < low:
                raise Failure(f"{what}: MinGatherEFF {low} and MaxGatherEFF {high} are not a range of 0 or more")
            self.weapons[identifier.lower()] = {
                "types": types, "min": round(low, 6), "max": round(high, 6),
                "efficiency": round(finite(sheet.source(row, "GatheringEfficiency"), f"{what}: GatheringEfficiency", 0.0), 6)
                if "GatheringEfficiency" in sheet.columns else 0.0,
                "multiplier": round(finite(sheet.source(row, "GatheringMultiplier"), f"{what}: GatheringMultiplier", 0.0), 6)
                if "GatheringMultiplier" in sheet.columns else 0.0}


def quest_form(cells: dict, what: str) -> bool:
    """@return True for a gatherable row with the form of a row of the quest tables (stage QG5a, the direct bindings): IsLootContainer, the
    loot table Empty or blank (no roll: the result is the quest drops of the tasks), and a GameEventID (the event of the quests)."""
    return (flag(cells.get("IsLootContainer", ""), f"IsLootContainer of {what}") and cells.get("FinalLootTable", "").strip() in ("", "Empty")
            and bool(cells.get("GameEventID", "").strip()))


def task_event_form(cells: dict, task_events: set[str]) -> bool:
    """@return True for a gatherable row of a quest gimmick (stage QG5b, the direct bindings): the loot table Empty or blank, no tool
    (GatheringType None or blank), and a GameEventID that a quest task names (the cannon Gimmick_BreakingWallsCannon_Vignette of
    12A_MSQ_08_DestroyRune: Handcrafting, the event IO_Gimmick_BreakingWallsCannon_interact). Its result is the event of the task."""
    return (cells.get("FinalLootTable", "").strip() in ("", "Empty") and cells.get("GatheringType", "").strip() in ("", "None")
            and cells.get("GameEventID", "").strip().lower() in task_events)


def row_entry(identifier: str, table: str, cells: dict) -> dict:
    """@return A gatherable row of the file. A value with a wrong form stops the tool."""
    what = f"the gatherable row {identifier}"
    time = finite(cells.get("BaseGatherTime", ""), f"BaseGatherTime of {what}")
    if not time > 0:
        raise Failure(f"BaseGatherTime of {what} is not above 0")
    low = finite(cells.get("MinRespawnRate", ""), f"MinRespawnRate of {what}", 0.0)
    high = finite(cells.get("MaxRespawnRate", ""), f"MaxRespawnRate of {what}", low)
    if low < 0 or high < low:
        raise Failure(f"the respawn bounds of {what} ({low}, {high}) are not a range of 0 or more")
    level = finite(cells.get("RequiredTradeskillLevel", ""), f"RequiredTradeskillLevel of {what}", 0.0)
    if level != int(level) or level < 0:
        raise Failure(f"RequiredTradeskillLevel of {what} is not a whole number of 0 or more")
    required = finite(cells.get("NumRequiredToGather", ""), f"NumRequiredToGather of {what}", 0.0)
    count = finite(cells.get("ItemRestrictionQuantity", ""), f"ItemRestrictionQuantity of {what}", 0.0)
    if count != int(count) or not 0 <= count <= 1000000:
        raise Failure(f"ItemRestrictionQuantity of {what} is not a whole number of 0 to 1,000,000")
    restriction = cells.get("Restriction", "").strip()
    item = cells.get("ItemRestrictionId", "").strip()
    unsupported = []
    # ItemRequired with no item names nothing to check (BushSmall, BushWithered of the game data): the client shows the prompt and sends
    # the request (the stage run of 3.6a). Stage QG5b: with an item the gather checks ItemRestrictionQuantity of the item in the bag (the
    # client: 0x35C2C8E and the Container check 0x48D6200; a blank or 0 quantity needs nothing): the backend has the rule.
    if restriction not in SUPPORTED_RESTRICTIONS and restriction != ITEM_RESTRICTION:
        unsupported.append(f"the restriction {restriction or '(none)'}")
    for column, cause in UNSUPPORTED_COLUMNS:
        if cells.get(column, "").strip() and cause not in unsupported:
            unsupported.append(cause)
    for column, cause in UNSUPPORTED_FLAGS:
        if flag(cells.get(column, ""), f"{column} of {what}"):
            unsupported.append(cause)
    # The loader of the client changes NumRequiredToGather 0 to 1 (0x440D2A0); more than one gatherer: a special node.
    if max(1, int(required)) > 1:
        unsupported.append("more than one gatherer (NumRequiredToGather)")
    kind = cells.get("GatheringType", "").strip() or "None"
    # Stage QG5c4: a type that is not a tool type needs no tool with the restriction NoToolNecessary (the 8 Handcrafting rows of the quest
    # tables: the two chests of the shipwreck encounter of C10A_Q4); the gather has the rule of the type None [data: the restriction; agent:
    # the rule; live check L1 of stage QG5c4: the client sends the gather and completes it].
    if kind not in TOOL_TYPES and kind != "None" and restriction != "NoToolNecessary":
        unsupported.append(f"the gathering type {kind}")
    tradeskill = cells.get("Tradeskill", "").strip()
    return {"id": identifier, "table": table, "type": kind, "restriction": restriction,
            "tradeskill": "" if tradeskill == "None" else tradeskill, "level": int(level), "time": round(time, 6),
            "respawn": [round(low, 4), round(high, 4)], "loot": cells.get("FinalLootTable", "").strip(),
            "material": cells.get("LootTableDefaultMaterial", "").strip(), "event": cells.get("GameEventID", "").strip(),
            "reset": flag(cells.get("ResetGatherTimeOnEndInteraction", ""), f"ResetGatherTimeOnEndInteraction of {what}"),
            "destroy": flag(cells.get("DestroyOnDepletion", ""), f"DestroyOnDepletion of {what}"),
            "depleted": cells.get("DepletedGatherableID", "").strip(), "required": max(1, int(required)),
            "item": item, "itemCount": int(count),
            "waitForImpact": flag(cells.get("WaitForImpact", ""), f"WaitForImpact of {what}"), "unsupported": unsupported}


class Slices:
    """The gathering slices: their template (the bound, the slot of the node state, the variation table)."""

    def __init__(self, sources: Sources, catalog: Catalog, option_id: int) -> None:
        self.sources = sources
        self.catalog = catalog
        self.option_id = option_id
        self.assets = {path: asset for asset, path in catalog.paths.items()}
        self.by_path: dict[str, int | None] = {}
        self.templates: list[dict] = []
        self.extra: list[dict] = []
        self.counts: dict[str, int] = {}

    def template_of(self, path: str, strict: bool = True) -> int | None:
        """@return The index of the template of a gathering slice, or no value for a slice that is not one. strict (the slices of
        slices/gatherables/ and of the distribution files): a slice of an ordinary resource table with a missing part stops the tool;
        stage QG5a reads the other placed slices and spawner children with strict False (such a slice is no gathering slice)."""
        if path in self.by_path:
            return self.by_path[path]
        self.by_path[path] = None
        raw = self.sources.read(path)
        if raw is None:
            self.sources.check_complete()
            return None
        if CONTROLLER_CLASS.bytes not in raw:
            return None
        roots = self.sources.stream(path)
        controllers = []
        tables = []
        entries = []
        selected = []
        for root in roots or []:
            for element in walk(root):
                if element.type_id == CONTROLLER_CLASS:
                    slot = find_field(element, SLOT_FIELD)
                    controllers.append(int.from_bytes(slot.value, "big") if slot is not None and slot.value else 0)
                    entry = find_field(element, ENTRY_FIELD)
                    entries.append(entry.value.decode("utf-8", "replace") if entry is not None and entry.value else "")
                if element.name == TABLE_FIELD and element.value:
                    tables.append(element.value.decode("utf-8", "replace"))
                if element.name == SELECTED_FIELD and element.value:
                    selected.append(element.value.decode("utf-8", "replace").rstrip("\0").strip())
        template = Template(path, roots or [])
        bounds = [bound for bound in template.bounds if any(GATHER_OPTION in names for _, names in bound["options"])]
        if len(controllers) != 1 or not bounds or template.root is None:
            # A slice of an ordinary resource table must have all parts; a different slice (a quest object, a creature with no skinning
            # bound) is not a gathering slice of this stage.
            if strict and any(table in ORDINARY_TABLES for table in tables):
                raise Failure(f"{path}: the gathering slice has {len(controllers)} GatherableController components (not one), or no "
                              f"root entity, or no Interact bound with {GATHER_OPTION}")
            self.counts["notGatheringSlices"] = self.counts.get("notGatheringSlices", 0) + 1
            return None
        bound = bounds[0]
        option_entity = next(entity for entity, names in bound["options"] if GATHER_OPTION in names)
        asset = self.assets.get(path)
        if asset is None:
            raise Failure(f"{path}: the asset catalog has no asset id of the slice")
        if not bound["rangeSq"] > 0:
            raise Failure(f"{path}: the range of the bound is not above 0")
        self.templates.append({
            "slice": path, "asset": asset[0].hex(), "sub": asset[1], "root": f"{template.root:016x}", "controllerSlot": controllers[0],
            "table": tables[0] if tables else "", "entry": entries[0].rstrip("\0").strip(),
            "bound": {"entity": f"{bound['entity']:016x}", "component": f"{bound['component']:016x}", "slot": bound["slot"],
                      "rangeSq": bound["rangeSq"], "capacity": bound["capacity"],
                      "translation": [round(value, 4) for value in bound["translation"]], "option": f"{option_entity:016x}",
                      "lineOfSight": bound["lineOfSight"], "arcRequired": bound["arcRequired"],
                      "requires": sorted(bound["requires"])}})
        # Stage QG5a: the selected variant of the slice, and the tag and InteractOnlyOnQuest of its GatherOption bound (a direct binding
        # has no variation row: its own bound names them). Not written into the file (main removes the members of this tool).
        self.extra.append({"selected": selected[0] if selected else "", "tag": bound["tag"],
                           "onlyOnQuest": bool(bound["requires"].get("onlyOnQuest"))})
        self.by_path[path] = len(self.templates) - 1
        return self.by_path[path]


class Heights:
    """The terrain height of the backend (the height files of the game data folder) and the water surface of a region."""

    TILE = 64
    TILES = 32

    def __init__(self, folder: Path, sources: Sources) -> None:
        self.folder = folder
        self.sources = sources
        self.ocean = None
        data = sources.read(TERRAIN_PATH)
        if data is not None:
            try:
                self.ocean = float(json.loads(data)["oceanLevel"])
            except (ValueError, KeyError, TypeError) as error:
                raise Failure(f"{TERRAIN_PATH}: no ocean level: {error}") from error

    def region(self, name: str) -> tuple:
        """@return (tiles, water, scale): a cache of the decoded tiles of 65 x 65 samples, the water surface raster (or no value),
        and the height of the sample 65535."""
        file = self.folder / f"{name}.swh"
        if not file.is_file():
            raise Failure(f"{file}: no height file of the region (run tools/gamedata/heights.sh)")
        data = file.read_bytes()
        if data[:4] != b"SWHT":
            raise Failure(f"{file} is not a height file")
        scale = struct.unpack_from("<f", data, 24)[0]
        tiles = {"data": data}
        water = None
        path = f"{REGIONS_FOLDER}{name}/region.waterqt"
        if self.sources.has(path):
            from nav_terrain import NO_WATER, WATER_CELL, read_water  # noqa: E402 (only for a region with water)
            surface, _, _ = read_water(self.sources.read(path))
            water = (surface, NO_WATER, WATER_CELL)
        return tiles, water, scale

    def tile(self, tiles: dict, index: int) -> list[int]:
        """@return The samples of one tile (decoded at its first use): each sample is the difference to the sample to its west, and
        the first sample of a row the difference to the first sample of the row to its south (height_maps.py)."""
        samples = tiles.get(index)
        if samples is not None:
            return samples
        data = tiles["data"]
        offset, length = struct.unpack_from("<II", data, 32 + 8 * index)
        deltas = array("H")
        deltas.frombytes(zlib.decompress(data[offset:offset + length]))
        side = self.TILE + 1
        samples = []
        first = 0
        for row in range(side):
            base = row * side
            first = (first + deltas[base]) & 0xFFFF
            running = list(accumulate(deltas[base + 1:base + side], initial=first))
            samples.extend(value & 0xFFFF for value in running)
        tiles[index] = samples
        return samples

    def height(self, region: tuple, local_x: float, local_y: float, mode: int) -> float:
        tiles, water, scale = region
        tile_x = min(int(local_x // self.TILE), self.TILES - 1)
        tile_y = min(int(local_y // self.TILE), self.TILES - 1)
        samples = self.tile(tiles, tile_y * self.TILES + tile_x)
        in_x, in_y = local_x - tile_x * self.TILE, local_y - tile_y * self.TILE
        low_x, low_y = min(int(in_x), self.TILE - 1), min(int(in_y), self.TILE - 1)
        side = self.TILE + 1
        corner, east = samples[low_y * side + low_x], samples[low_y * side + low_x + 1]
        north, diagonal = samples[(low_y + 1) * side + low_x], samples[(low_y + 1) * side + low_x + 1]
        part_x, part_y = in_x - low_x, in_y - low_y
        south_edge = corner + (east - corner) * part_x
        north_edge = north + (diagonal - north) * part_x
        terrain = (south_edge + (north_edge - south_edge) * part_y) * scale / 65535.0
        if mode == 0:
            return terrain
        surface = None
        if water is not None:
            raster, none, cell = water
            cells = REGION_SIZE // cell
            column, row = min(max(int(local_x // cell), 0), cells - 1), min(max(int(local_y // cell), 0), cells - 1)
            value = raster[row * cells + column]
            surface = None if value == none else value / 32.0
        if self.ocean is not None and terrain < self.ocean:
            surface = self.ocean if surface is None else max(surface, self.ocean)
        if surface is None:
            return terrain
        return surface if mode == 2 else max(terrain, surface)


def read_distribution(data: bytes, path: str) -> tuple[list[str], list[str], tuple, tuple, bytes, int]:
    """@return (slice names, variant names, type indexes, packed places, height modes, tail records) of a region.distribution
    (client reader 0x69DD030: an older file has no mark and no height modes)."""
    try:
        version = -1
        position = 0
        if struct.unpack_from("<H", data, 0)[0] == 0xB9D6:
            (version,) = struct.unpack_from("<h", data, 2)
            position = 4
        (count,) = struct.unpack_from("<H", data, position)
        position += 2
        names: list[str] = []
        for _ in range(count * 2):
            size = data[position]
            names.append(data[position + 1:position + 1 + size].decode("utf-8", "replace"))
            position += 1 + size
        (rows,) = struct.unpack_from("<I", data, position)
        position += 4
        indexes = struct.unpack_from(f"<{rows}H", data, position)
        position += rows * 2
        places = struct.unpack_from(f"<{rows}I", data, position)
        position += rows * 4 * 2  # the places, then the packed rotations
        position += rows  # the scales
        modes = bytes(rows)
        if version >= 1:
            modes = data[position:position + rows]
            position += rows
        tails = 0
        for _ in range(2):
            (extra,) = struct.unpack_from("<I", data, position)
            position += 4 + 5 * extra
            tails += extra
        if position != len(data) or len(modes) != rows:
            raise Failure(f"{path}: the file has {len(data)} bytes, and its tables end at {position}")
    except (struct.error, IndexError) as error:
        raise Failure(f"{path}: {error}") from error
    for row, index in enumerate(indexes):
        if index >= count:
            raise Failure(f"{path}: record {row} names the slice {index} of {count}")
    return names[:count], names[count:], indexes, places, modes, tails


def slice_path_of(name: str, sources: Sources) -> str | None:
    key = name.strip().replace("\\", "/").lower()
    if key.endswith(".dynamicslice"):
        key = key[:-len(".dynamicslice")]
    for path in (key + ".dynamicslice", "slices/" + key + ".dynamicslice"):
        if sources.has(path):
            return path
    return None


def build(sources: Sources, gamedata: Path, regions: list[str] | None, out_folder: Path | None,
          near: tuple[float, float, float] | None = None, encounters: bool = True) -> dict:
    tables = Tables(sources)
    options = read_options(sources)
    if GATHER_OPTION not in options:
        raise Failure(f"the user interface database has no option {GATHER_OPTION}")
    option = options[GATHER_OPTION]
    catalog_data = sources.read(CATALOG_PATH)
    if catalog_data is None:
        sources.check_complete()
    catalog = Catalog(catalog_data)
    slices = Slices(sources, catalog, option["id"])
    counts = {"regions": 0, "records": 0, "tails": 0, "gatheringRecords": 0, "capitals": 0, "capitalPlacements": 0,
              "recordsByTable": {}, "capitalsByTable": {}, "unknownVariants": 0, "notGatheringSlices": 0, "creatures": 0}
    row_index: dict[str, int] = {}
    rows: list[dict] = []

    def row_of(identifier: str, what: str) -> int:
        key = identifier.lower()
        if key in row_index:
            return row_index[key]
        if key not in tables.rows:
            raise Failure(f"{what} names the gatherable row {identifier!r}, and no GatherableData table has it")
        table, cells = tables.rows[key]
        row_index[key] = len(rows)
        rows.append(row_entry(cells.get("GatherableID", identifier).strip(), table, cells))
        return row_index[key]

    def variant_row(template: int, variant: str, what: str, proximity: bool = False) -> int:
        table = slices.templates[template]["table"]
        if table not in ORDINARY_TABLES and table not in QUEST_TABLES and not (proximity and table in PROXIMITY_TABLES):
            return -1
        entry = tables.variations.get(table, {}).get(variant.lower())
        if entry is None or not entry["gatherable"]:
            counts["unknownVariants"] += 1
            return -1
        row = row_of(entry["gatherable"], f"{what}: the variation row {table}/{variant}")
        if table in QUEST_TABLES or table in PROXIMITY_TABLES:
            # A quest object (stage 1.8c): its loot is the quest drops; a loot container with the loot table Empty is not a refusal.
            rows[row]["quest"] = True
            rows[row]["unsupported"] = [cause for cause in rows[row]["unsupported"]
                                        if not (cause == LOOT_CONTAINER_CAUSE and rows[row]["loot"] in ("", "Empty"))]
        return row

    # The natural nodes: the distribution file of each region.
    heights = Heights(gamedata / "heights", sources)
    region_list = []
    names = regions or sorted({path[len(REGIONS_FOLDER):].split("/")[0]
                               for path in sources.paths(REGIONS_FOLDER, "/region.distribution")})
    found_near = []
    for name in names:
        path = f"{REGIONS_FOLDER}{name}/region.distribution"
        data = sources.read(path)
        if data is None:
            if regions:
                raise Failure(f"the game copy has no {path}")
            continue
        region_x, region_y = region_origin(name)
        slice_names, variants, indexes, places, modes, tails = read_distribution(data, path)
        types = []
        for slice_name, variant in zip(slice_names, variants):
            slice_path = slice_path_of(slice_name, sources)
            template = slices.template_of(slice_path) if slice_path is not None else None
            row = -1
            if template is not None and slices.templates[template]["table"] in ORDINARY_TABLES:
                row = variant_row(template, variant, f"{path}: the slice {slice_name}")
            else:
                template = None
            types.append([template if template is not None else -1, row, variant])
        terrain = heights.region(name)
        scale = f32(REGION_SIZE / 65535.0)
        origin_x, origin_y = region_x * REGION_SIZE, region_y * REGION_SIZE
        body = bytearray()
        gathering = 0
        for record, (index, packed) in enumerate(zip(indexes, places)):
            local_x = f32((packed >> 16) * scale)
            local_y = f32((packed & 0xFFFF) * scale)
            template, row, variant = types[index]
            z = heights.height(terrain, local_x, local_y, modes[record]) if template >= 0 else 0.0
            body += struct.pack("<Hfff", index, origin_x + local_x, origin_y + local_y, z)
            if template >= 0 and row >= 0:
                gathering += 1
                table = slices.templates[template]["table"]
                counts["recordsByTable"][table] = counts["recordsByTable"].get(table, 0) + 1
                if near is not None:
                    dx, dy = origin_x + local_x - near[0], origin_y + local_y - near[1]
                    if dx * dx + dy * dy <= near[2] * near[2]:
                        found_near.append((math.hypot(dx, dy), name, record, variant, rows[row]["id"],
                                           (origin_x + local_x, origin_y + local_y, z), offline_id(region_x, region_y, record + 1)))
        counts["regions"] += 1
        counts["records"] += len(indexes)
        counts["tails"] += tails
        counts["gatheringRecords"] += gathering
        version = hashlib.sha256(data).hexdigest()[:16]
        file_name = f"{name}.swgn"
        region_data = b"SWGN" + struct.pack("<IiiII", REGION_FORMAT, region_x, region_y, len(indexes), tails) + bytes(body)
        # The file name of the set comes at the publication (publish); the digest binds the file to this document (finding GA-R6).
        region_list.append({"name": name, "x": region_x, "y": region_y, "file": f"{FOLDER}/{file_name}", "records": len(indexes),
                            "tails": tails, "version": version, "digest": digest(region_data), "types": types})
        if out_folder is not None:
            # The staging folder of the set (main): a stop later leaves the published set as it was.
            (out_folder / file_name).write_bytes(region_data)
        print(f"gathering: {name}: {len(indexes)} records, {gathering} gathering nodes", file=sys.stderr)

    def quest_capital(template: int, variant: str, what: str, proximity: bool = False) -> list | None:
        """@return The capital entry of a quest object (stage 1.8c; stage QG5a: any slice path, the variant of the placement or of the
        spawner, else the m_selectedVariant of the slice, and the direct bindings), or no value for a slice that is not one. Stage QG5c3:
        `proximity` also takes a slice of Gatherable_Quest_Proximity (the start object of an ambush: its node state and its row)."""
        table = slices.templates[template]["table"]
        extra = slices.extra[template]
        if table in QUEST_TABLES or (proximity and table in PROXIMITY_TABLES):
            name = variant.strip() or extra["selected"]
            row = variant_row(template, name, what, proximity)
            if row < 0:
                return None
            entry = tables.variations[table][name.lower()]
            return [template, row, entry["tag"], entry["onlyOnQuest"]]
        identifier = slices.templates[template]["entry"]
        if table or not identifier:
            return None
        # A direct binding (stage QG5a): a gathering slice with no variation table names its gatherable row itself. Its row is a quest
        # object when it has the form of a row of the quest tables (quest_form: a loot container with the loot table Empty or blank and a
        # game event; its result is the quest drops and its game event), or (stage QG5b) the form of a quest gimmick (task_event_form: no
        # tool, no loot, and an event that a quest task names); its tag and InteractOnlyOnQuest are the ones of its own GatherOption bound.
        # A different direct row (a body of a creature, a door with no event, a hazard, a fishing spot) is no quest object.
        if identifier.lower() not in tables.rows:
            counts["directNoRow"] = counts.get("directNoRow", 0) + 1
            return None
        cells = tables.rows[identifier.lower()][1]
        gimmick = not quest_form(cells, f"the direct gatherable row {identifier}") and task_event_form(cells, tables.task_events)
        if not gimmick and not quest_form(cells, f"the direct gatherable row {identifier}"):
            counts["directNotQuest"] = counts.get("directNotQuest", 0) + 1
            return None
        if gimmick:
            # Stage QG5b: a quest gimmick (the cannon): a direct row whose game event a quest task names [agent: the result is the event].
            counts["directGimmick"] = counts.get("directGimmick", 0) + 1
        row = row_of(identifier, f"{what}: the direct gatherable row")
        rows[row]["quest"] = True
        rows[row]["unsupported"] = [cause for cause in rows[row]["unsupported"] if cause != LOOT_CONTAINER_CAUSE]
        counts["directQuest"] = counts.get("directQuest", 0) + 1
        return [template, row, extra["tag"], extra["onlyOnQuest"]]

    spawner_cache: dict[str, list[dict]] = {}

    def spawners_of(path: str) -> list[dict]:
        """@return The PrefabSpawners of a placed slice (stage QG5a: the quest object children). Only a slice with the class reads."""
        if path not in spawner_cache:
            raw = sources.read(path)
            spawner_cache[path] = (Template(path, sources.stream(path) or []).spawner_list
                                   if raw is not None and SPAWNER_CLASS.bytes in raw else [])
        return spawner_cache[path]

    # The capital nodes: the placements of the layers whose slice is a gathering slice. Stage QG5a: the quest object children of their
    # spawners in the member "children" (a key "UUID/ENTITY"; a backend of before reads only "capitals").
    capitals: dict[str, list[int]] = {}
    children: dict[str, list] = {}
    capital_regions = regions or sorted({path[len(REGIONS_FOLDER):].split("/")[0]
                                         for path in sources.paths(REGIONS_FOLDER, ".capitals.json")})
    for name in capital_regions:
        for path in sources.paths(f"{REGIONS_FOLDER}{name}/capitals/", ".capitals.json"):
            data = sources.read(path)
            if data is None:
                continue
            try:
                document = json.loads(data)
            except ValueError as error:
                raise Failure(f"{path} is not JSON: {error}") from error
            for placement in document.get("Capitals") or []:
                slice_name = str(placement.get("sliceName", ""))
                if not slice_name or slice_name == "<PLOT>":
                    continue
                counts["capitalPlacements"] += 1
                match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", str(placement.get("sliceAssetId", "")))
                slice_path = slices.catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16))) if match else None
                slice_path = slice_path or slice_path_of(slice_name, sources)
                if slice_path is None:
                    continue
                key = str(placement.get("id", "")).strip().lower()
                variant = str(placement.get("variantName", ""))
                template = slices.template_of(slice_path, slice_path.startswith("slices/gatherables/"))
                table = slices.templates[template]["table"] if template is not None else ""
                if template is not None and slice_path.startswith("slices/gatherables/") and table in ORDINARY_TABLES:
                    if not re.fullmatch(r"[0-9a-f-]{36}", key):
                        raise Failure(f"{path}: a gathering placement has no UUID")
                    row = variant_row(template, variant, f"{path}: the placement {key}")
                    if row >= 0:
                        capitals[key] = [template, row]
                        counts["capitalsByTable"][table] = counts["capitalsByTable"].get(table, 0) + 1
                    continue
                if template is not None:
                    entry = quest_capital(template, variant, f"{path}: the placement {key}")
                    if entry is not None:
                        if not re.fullmatch(r"[0-9a-f-]{36}", key):
                            raise Failure(f"{path}: a gathering placement has no UUID")
                        capitals[key] = entry
                        counts["questCapitals"] = counts.get("questCapitals", 0) + 1
                        counts["capitalsByTable"][table or "(direct)"] = counts["capitalsByTable"].get(table or "(direct)", 0) + 1
                        continue
                # Stage QG5a: the quest object children of the PrefabSpawners of the placed slice (not client-authoritative, made at the
                # init: the client makes them with the id of interactions.py quest_children; the key "UUID/ENTITY" of the spawner).
                for spawner in spawners_of(slice_path):
                    child_path = slices.catalog.path(spawner["asset"]) if spawner["asset"] is not None else None
                    if spawner["client"] or not spawner["onInit"] or child_path is None or not sources.has(child_path):
                        continue
                    child = slices.template_of(child_path, False)
                    entry = quest_capital(child, spawner["variant"], f"{path}: a child of the placement {key}") if child is not None else None
                    if entry is None:
                        continue
                    if not re.fullmatch(r"[0-9a-f-]{36}", key):
                        raise Failure(f"{path}: a gathering placement has no UUID")
                    children[f"{key}/{spawner['entity']:016x}"] = entry
                    counts["questChildren"] = counts.get("questChildren", 0) + 1
                    child_table = slices.templates[child]["table"] or "(direct)"
                    counts["capitalsByTable"][child_table] = counts["capitalsByTable"].get(child_table, 0) + 1
    counts["capitals"] = len(capitals)

    # Stage QG5c2 (docs/systems/encounters.md): the start object of each encounter recipe of the quests (encounter_recipes.py): a quest
    # object (quest_capital: its variant of the variation table, its gatherable row, its tag, InteractOnlyOnQuest) of a server child of the
    # encounter system. A slice that is not a quest object is not written (the backend refuses its recipe).
    encounter_objects: dict[str, list] = {}
    if encounters and catalog is not None:
        import encounter_recipes  # noqa: E402 (encounter_recipes imports the modules of this folder)
        reader, recipes, _ = encounter_recipes.read_recipes(sources, catalog)
        for member in reader.start_objects(recipes):
            template = slices.template_of(member["slice"], False)
            entry = quest_capital(template, member["variant"], f"the start object {member['slice']}", bool(member.get("proximity"))) \
                if template is not None else None
            if entry is None:
                counts["encounterObjectsLeftOut"] = counts.get("encounterObjectsLeftOut", 0) + 1
                continue
            encounter_objects[member["slice"]] = entry
    counts["encounterObjects"] = len(encounter_objects)

    # The skinning of a creature: the creature slices of the creature data with a GatherableController and a GatherOption bound.
    creatures: dict[str, dict] = {}
    creature_file = gamedata / "creatures.json"
    if creature_file.is_file():
        try:
            creature_slices = json.loads(creature_file.read_text(encoding="utf-8")).get("slices") or []
        except ValueError as error:
            raise Failure(f"{creature_file} is not JSON: {error}") from error
        for entry in creature_slices:
            slice_path = str(entry.get("path", "")).lower()
            if not slice_path or not sources.has(slice_path):
                continue
            try:
                template = slices.template_of(slice_path)
            except Failure:
                template = None  # A creature slice with no gathering bound: no skinning.
            if template is None:
                continue
            identifier = slices.templates[template]["entry"]
            if not identifier or identifier.lower() not in tables.rows:
                continue
            asset = slices.templates[template]
            creatures[f"{asset['asset']}:{asset['sub']}"] = {"slice": slice_path, "template": template,
                                                              "row": row_of(identifier, f"the creature slice {slice_path}")}
    counts["creatures"] = len(creatures)

    # The events of the rows, the tools, the ranks.
    events = {}
    for row in rows:
        if row["event"]:
            entry = tables.events.get(row["event"].lower())
            if entry is None and row.get("quest"):
                continue  # A quest object (stage 1.8c): its event is the game event of the quests (no trade skill experience).
            if entry is None:
                raise Failure(f"the gatherable row {row['id']} names the event {row['event']}, and no event table has it")
            events[entry[0]] = [entry[1], entry[2]]
    tools = {}
    for item in tables.items.values():
        weapon = tables.weapons.get(item["ref"].lower())
        if weapon is not None and any(kind in TOOL_TYPES for kind in weapon["types"]):
            tools[item["id"]] = weapon
    # Only the slices that a node or a creature names (a quest slice of a capital placement is read and not used).
    used = sorted({template for region in region_list for template, _, _ in region["types"] if template >= 0}
                  | {entry[0] for entry in capitals.values()} | {entry[0] for entry in children.values()}
                  | {entry["template"] for entry in creatures.values()} | {entry[0] for entry in encounter_objects.values()})
    renumber = {old: new for new, old in enumerate(used)}
    for region in region_list:
        region["types"] = [[renumber[template] if template >= 0 else -1, row, variant] for template, row, variant in region["types"]]
    capitals = {key: [renumber[entry[0]]] + entry[1:] for key, entry in capitals.items()}
    children = {key: [renumber[entry[0]]] + entry[1:] for key, entry in children.items()}
    encounter_objects = {key: [renumber[entry[0]]] + entry[1:] for key, entry in encounter_objects.items()}
    for entry in creatures.values():
        entry["template"] = renumber[entry["template"]]
    templates = [slices.templates[old] for old in used]
    counts["rows"] = len(rows)
    counts["tools"] = len(tools)
    counts["templates"] = len(templates)
    document = {"format": FORMAT, "world": WORLD,
                "option": {"name": GATHER_OPTION, "id": option["id"], "action": option["action"]},
                "rows": rows, "events": events, "ranks": tables.ranks, "tools": dict(sorted(tools.items())),
                "templates": templates, "capitals": dict(sorted(capitals.items())), "children": dict(sorted(children.items())),
                "encounterObjects": dict(sorted(encounter_objects.items())), "regions": region_list,
                "creatures": dict(sorted(creatures.items())), "counts": counts}
    if near is not None:
        document["near"] = sorted(found_near)
    return document


def manifest_files(target: Path) -> set[str]:
    """@return The region files (paths relative to the game data folder) that the manifest `target` names; none when it is not there or
    is not a manifest."""
    try:
        document = json.loads(target.read_text(encoding="utf-8"))
        files = [region.get("file") for region in document.get("regions", []) if isinstance(region, dict)]
        return {file for file in files if isinstance(file, str) and file.startswith(f"{FOLDER}/") and ".." not in file}
    except (OSError, ValueError, AttributeError, TypeError):
        return set()


def same_set(folder: Path, regions: list[dict]) -> bool:
    """@return True when a set folder has each region file with its digest."""
    for region in regions:
        file = folder / f"{region['name']}.swgn"
        if not file.is_file() or digest(file.read_bytes()) != region["digest"]:
            return False
    return True


def publish(target: Path, document: dict, staging: Path, folder: Path) -> list[str]:
    """Publishes one set of files (code review of stage 3.6a, finding GA-R6). The region files of `staging` are checked against the
    document (the size and the digest); the folder becomes the set folder gathering/SET (SET: a digest of the region digests); then the
    manifest names the files of the set (the manifest last). A stop before the manifest leaves the published set whole. Each published
    set stays (code review of user test 2, finding U2-R6: a backend that runs can hold any earlier manifest); only the staging folders of
    stopped exports go. `prune` removes the old sets when no backend runs.
    @return The removed entries of the folder."""
    regions = document["regions"]
    for region in regions:
        staged = staging / f"{region['name']}.swgn"
        data = staged.read_bytes() if staged.is_file() else b""
        if len(data) != REGION_HEADER + REGION_RECORD * region["records"] or digest(data) != region["digest"]:
            raise Failure(f"{staged}: the region file is not the file of the document (its size or its digest)")
    set_name = hashlib.sha256("\n".join(f"{region['name']}:{region['digest']}" for region in regions).encode()).hexdigest()[:16]
    for region in regions:
        region["file"] = f"{FOLDER}/{set_name}/{region['name']}.swgn"
    text = json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n"
    previous = manifest_files(target)
    generation = folder / set_name
    if generation.is_dir() and same_set(generation, regions):
        shutil.rmtree(staging)  # The same files are published (an export of the same sources).
    elif generation.is_dir():
        aside = folder / f"{STALE_PREFIX}{os.getpid()}"
        shutil.rmtree(aside, ignore_errors=True)
        generation.rename(aside)
        try:
            staging.rename(generation)
        except OSError:
            aside.rename(generation)
            raise
        shutil.rmtree(aside, ignore_errors=True)
    else:
        staging.rename(generation)
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(target)
    # The clean up: only the staging folders of stopped exports (no backend reads them). Each set and each region file of the layout of
    # before the review (gathering/REGION.swgn) stays: a backend that runs can hold its manifest (finding U2-R6; `prune` removes them).
    del previous
    removed = []
    for entry in sorted(folder.iterdir()):
        try:
            if entry.is_dir() and entry.name.startswith((STAGING_PREFIX, STALE_PREFIX)):
                shutil.rmtree(entry)
                removed.append(entry.name)
            elif entry.is_file() and entry.name.endswith(".swgn.tmp"):
                entry.unlink()
                removed.append(entry.name)
        except OSError as error:
            print(f"gathering: {entry}: not removed: {error}", file=sys.stderr)
    return removed


def backend_running() -> bool:
    """@return True when a process of a backend runs (the program soulwarden-backend of a start of the user or of a development run): it
    can hold an earlier manifest. A computer with no pgrep: True (no prune)."""
    try:
        result = subprocess.run(["pgrep", "-f", r"^([^ ]*/)?soulwarden-backend( |$)"], capture_output=True, check=False)
    except OSError:
        return True
    return result.returncode == 0


def prune(target: Path, folder: Path) -> list[str]:
    """Removes the sets and the region files of the layout of before that the manifest `target` does not name (finding U2-R6). It
    refuses (a Failure, no change) while a backend runs, and with no manifest. @return The removed entries."""
    if backend_running():
        raise Failure("a backend runs (a program soulwarden-backend): it can hold an earlier manifest. Stop each backend, then prune")
    named = manifest_files(target)
    if not named:
        raise Failure(f"{target} names no region file: nothing to keep, no prune")
    keep_folders = {Path(file).parts[1] for file in named if len(Path(file).parts) == 3}
    keep_files = {Path(file).name for file in named if len(Path(file).parts) == 2}
    removed = []
    for entry in sorted(folder.iterdir()):
        try:
            if entry.is_dir() and entry.name not in keep_folders and (re.fullmatch(r"[0-9a-f]{16}", entry.name)
                                                                        or entry.name.startswith((STAGING_PREFIX, STALE_PREFIX))):
                shutil.rmtree(entry)
                removed.append(entry.name)
            elif entry.is_file() and entry.name.endswith((".swgn", ".swgn.tmp")) and entry.name not in keep_files:
                entry.unlink()
                removed.append(entry.name)
        except OSError as error:
            print(f"gathering: {entry}: not removed: {error}", file=sys.stderr)
    return removed


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the gathering data of the world (one JSON file and one file for each region).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="a region (r_+04_+02); more than one is possible")
    parser.add_argument("--near", type=float, nargs=3, metavar=("X", "Y", "METERS"),
                        help="print the gathering nodes near a place (with their G) and write no file")
    parser.add_argument("--prune", action="store_true",
                        help="remove the sets that the manifest does not name (no export; only when no backend runs)")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    if arguments.prune:
        try:
            removed = prune(target, target.parent / FOLDER)
        except Failure as error:
            print(f"gathering: {error}. Nothing was removed.", file=sys.stderr)
            return 1
        print(f"gathering: removed {len(removed)} entr{'y' if len(removed) == 1 else 'ies'}" + (f": {', '.join(removed)}" if removed else ""))
        return 0
    if arguments.near is not None and not all(math.isfinite(value) for value in arguments.near):
        print("gathering: --near needs three finite numbers. No file was written.", file=sys.stderr)
        return 1
    folder = target.parent / FOLDER
    staging = None
    try:
        sources = Sources(game / "assets", WANTED)
        if not arguments.near:
            # The region files go into a staging folder first (finding GA-R6): the published set changes only at the end.
            folder.mkdir(parents=True, exist_ok=True)
            staging = folder / f"{STAGING_PREFIX}{os.getpid()}"
            shutil.rmtree(staging, ignore_errors=True)
            staging.mkdir()
        document = build(sources, gamedata, arguments.region, staging, tuple(arguments.near) if arguments.near else None)
        if arguments.near:
            for distance, region, record, variant, row, (x, y, z), group in document["near"]:
                print(f"{distance:7.2f} m  {region} record {record:6d}  G {group:016x}  {variant:34s} {row:24s} {x:10.3f} {y:10.3f} {z:8.3f}")
            return 0
        removed = publish(target, document, staging, folder)
        staging = None
    except Failure as error:
        print(f"gathering: {error}. No file was written.", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"gathering: the document has a number that is not finite ({error}). No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"gathering: {target}: {error}. The published files did not change.", file=sys.stderr)
        return 1
    finally:
        if staging is not None:
            shutil.rmtree(staging, ignore_errors=True)
    counts = document["counts"]
    print(f"{target}: {counts['gatheringRecords']} natural nodes of {counts['records']} distribution records in {counts['regions']} "
          f"regions, {counts['capitals']} capital nodes, {counts['creatures']} creature slices with skinning, {counts['rows']} "
          f"gatherable rows, {counts['templates']} slices, {counts['tools']} tools")
    print("  natural nodes: " + ", ".join(f"{table} {count}" for table, count in sorted(counts["recordsByTable"].items())))
    print("  capital nodes: " + ", ".join(f"{table} {count}" for table, count in sorted(counts["capitalsByTable"].items())))
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    print(f"  region files: {document['regions'][0]['file'].rsplit('/', 1)[0] if document['regions'] else FOLDER}"
          + (f"; removed: {', '.join(removed)}" if removed else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

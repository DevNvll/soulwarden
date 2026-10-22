#!/usr/bin/env python3
"""Write the interaction objects of the world as one JSON file (stage 2.5a).

The backend reads this file at its start (protocol notes 4.35, docs/systems/interactions.md). It has each object of the
level data that a player can use with the key of an interaction (a lore page, a person, a storage shed, a gathering node,
a door): its id on the wire, its place, its Interact component, and its options with their rows of the user interface
database. The backend checks each request of a client against this file: the object, the option, the range, and the
requirements (world/interactions.h).

The chain of one object (all files are in the pak files of the game copy; research interaction-stage.md, Q1):

  1. A layer of a region has its placements: regions/REGION/capitals/LAYER/LAYER.capitals.json. A placement has an id (a
     UUID), a world position, a rotation, and a slice (`sliceName`, `sliceAssetId`).
  2. The slice of the placement has its entities. A component of the class Interact (b69045a3-...) is one bound of an
     object: the client shows its prompt. Its fields: m_interactOptionEntities (each entity of an option and the option
     names), m_numberOfInteractorsAllowed (-1: no limit), m_interactRangeSq, m_requiresLineOfSight,
     m_requiredInteractArc, m_interactArc, m_requiredAchievementOverride, m_requiredProgression, m_interactOnlyOnQuest,
     m_interactTagString, m_condition, and its slot (m_replicationIndex: the slot of its state 2930 in a record).
  3. The place of an entity is the translation of its m_worldTM (relative to the root of the slice, not to the parent
     entity; nav_slices.py), turned by the rotation of the placement, plus its position.
  4. The ids of the client (research Q1, live check L1 of stage 2.5a): the actor id of a placement is its UUID as 16
     bytes, the two halves of 8 bytes XOR together, the 8 result bytes in the reverse order, the low 3 bits of the first
     byte set to 3, and 8 zero bytes (a managed actor: the client makes the entity itself). G is the first 8 bytes of the
     actor id, little-endian. K = (the id of the root entity of the slice) XOR G; an instance id is (entity id) XOR K, and
     the facet target of a component is (instance id + component id) mod 2^64. The request of the client names the actor
     id, G, the target of the Interact component, and the instance id of the entity of the option.
  5. An option name is a row of sharedassets/genericassets/ui/uidatabase.uidb (the class f0887e97-...): the action, the
     committed flag, the delay, the hold time, the timeouts, and the requirements. The option id on the wire is the
     CRC-32 of the lower-case name (client hash 0x12F4730).
  6. A lore page (a component with m_loreId in the entity of its option) names a row of javelindata_loreitems
     (AchievementId), and that achievement a row of javelindata_achievements (AchievementIndex: its bit in the achievement
     state of the character, slot 66; the journal of the client reads it, live check L3 of stage 2.5a).
  The same for a required achievement of an option or of the Interact component.

The server children (stage 2.5b, research interaction-stage.md Q1, live check L4 of stage 2.5b). A PrefabSpawner component of
a slice (m_sliceAsset) with m_isClientAuthoratative false has a child that the server makes: the client shows it only after a
record of the server. The file has these children of a placement:
  - "spawned": the station or the camp of a placement: the child of a PrefabSpawner whose slice has an Assembly component
    (520015fd-...) and an Interact bound with OpenCraftingOption, OpenCraftingOptionLevel60, or BindToCampOption (the stations of
    a settlement, Public_Blacksmith and the others; the camp of a permanent camp). Its key is the placement UUID and the entity
    id of the spawner ("UUID/ENTITY"), its place the placement matrix times the m_worldTM of the spawner entity, and its parent
    the managed actor of the placement. The tool writes each such child, also with m_spawnOnInitialize false (the stations of a
    settlement: the server of the game made them from the state of the town, which the game files do not have). Stage 3.7a,
    decision D1 of the orchestrator: a station child whose authored key ends in 2 and has no row of the station database (the master
    slices of Smelting, Carpentry, Masonry, Tanning, and Weaving) gets the slice of the _t2 variant of its family (key ending in 3) as
    its "template"; "authoredTemplate" keeps the authored one and "override" names the change.
  - "templates": the slice of each child (and of each fixture): the asset, the root entity, the slots of the replicated
    components that the backend sends (PositionInTheWorld, BuildableController, Ownership, Vitals), m_initialState and
    m_initialPermissionRule, the Assembly (m_craftingStationEntry, m_chargeCraftingFee, its entity and component, and "resolved":
    the key is a row of sharedassets/genericassets/craftingstations.craftstationdb), the Interact bounds in the frame of the slice
    (the backend makes the ids from G of the child: the XOR rule), and "children": the PrefabSpawner children of the slice with
    m_isClientAuthoratative false and m_spawnOnInitialize true (the forge of a station is such a child: live check L4), with the
    transform of the spawner entity.
  - "fixtures": the slices of the development fixtures (FIXTURES: the powder keg with the timed option Trigger, the player camp
    CampT5, the camp of a permanent camp): a development command of the backend makes them near a character (live checks L5, L6).
  - "lootContainers" (format 3, stage 3.5b.2): the slice of each loot container that the server makes (LOOT_CONTAINERS: "personal",
    the personal bag of a kill and of a discard, slices/instanced_loot_container.dynamicslice; research chests-ground-loot.md Q3): its
    template, the InstancedLoot type (1, AILoot), the slot of its LootDrop component, the lifetime (the DelayedEvent that starts at the
    init and whose event ends the slice: KillSelfDelay, 180 s), the end after the last item (the DelayedEvent that m_completelyLootedEvents
    starts: FastKillSelfDelay, 1.25 s), the hide of the owner after Looted (the DelayedEvent of m_lootedEvents: HideMeshDelay, 1.5 s, a
    value of the client), the count of the rarity particles, and each DelayedEvent of the slice.
The persons (format 4, stage 2.5c; docs/systems/persons.md, live checks L1 and A2 of stage 2.5c). An object with a person has the
member "person":
  - a person child: the child of a PrefabSpawner of a level placement with m_isClientAuthoratative false and m_spawnOnInitialize true whose
    slice has an NPC component (4bdb17d5-...). The client makes it itself at the load of the level (client 0x6160B60, called from
    0x69EE550), with G from its place, its yaw, and its slice asset (child_group: the low 3 bits 7) and the actor G and 8 zero bytes; the
    XOR rule of item 4 gives its targets. The key is "UUID/ENTITY" of the spawner ("~1", "~2", ... for the other ids of a slot near a
    border of the rule). The member: the NPC id of the row of the variant (m_sliceVariant) in the variation table of the slice (NPCId), the
    variant, the table, the row, the presence for one character (PERSON_PRESENCE: "showWhen" or "hideWhen" with an achievement conditional;
    no rule: always there), "clientChild" true, the spawner, and "unresolved" with a cause when the tool cannot bind it (the backend
    refuses its use).
  - a direct person: a placement whose own slice has the NPC component: "npc" (m_npcKey) and "direct" true.
What the file does not have, with the count of each cause in "excluded" (a later stage reads them):
  - The other children of a PrefabSpawner (a trading post, a building), a person child with m_spawnOnInitialize false (the client does
    not make it), and the second slot of a client id that an earlier person slot has: a later stage.
  - The records of the distribution files of a region (the gathering nodes of the terrain): stage 3.6.
  - A placement with no slice (an entry `<PLOT>` with no asset): the level chunks have these objects.
A placement whose slice has no Interact component has no object.

The kind of an object (a table of this project from the option names of the data, research Q4): the first option of the
table KINDS that a bound has gives its kind. The backend registers a handler for a kind and its options.

The tool stops with status 1 and one message, and writes no file, when:
  - a file that it needs has no source (the pak entries have Oodle data: see creature_data.py);
  - a layer has no list "Capitals" (code review of stage 2.5a, finding INT-R5), or a placement with a slice name has no
    correct slice asset id (an asset id with a wrong form is not a plot: a plot has no slice name or the name <PLOT>);
  - a numeric field has a wrong form: a range that is not a finite number above 0, a capacity or a slot that is not a
    number of 4 bytes, a transform that is not 12 numbers, a position that is not finite;
  - two bounds have the same reference (G and target) with different content, or one placement id has two placements with a
    different slice, position, rotation, or asset id (finding INT-R6: only equal copies are one placement);
  - two rows of the achievement table have one AchievementIndex (finding INT-R4: one journal bit for two pages);
  - a fixture slice has no source, no PositionInTheWorld component, or no Interact bound;
  - a loot container slice has no source, no bound with TakeAllOption and OpenStorageOption, no InstancedLoot of the type AILoot, no
    LootDrop with m_isInstancedLoot, no player-unique Storage, or not exactly one DelayedEvent for its lifetime, its end after the last
    item, and the hide after Looted (stage 3.5b.2).
A name of the data with no row does not stop the tool: a required achievement such as
`*17_MSQ_Quest_18020_LynetteDungeonKey`, a lore id with no lore row or no achievement index, and an option name with no
row of the user interface database (the placeholder `None` of some lore slices). A bound with an unresolved requirement
gets "requires": {"unresolved": [TEXT, ...]}, and an option row "unresolved": TEXT: the backend refuses each use of them (a
requirement that it cannot check does not make an object open). An option name with no row is not in the "options" of its
bound but in its "unknownOptions": no request can name it (the client shows no option with no row), and the other options
of the bound stay. The file lists each name in "unresolved" with the first object that names it. The shipped data has such
names: a stop for them would give no file at all.
tools/gamedata/check_interactions_tool.py is the check of these rules, with made-up sources.

Output (one line of JSON):
  {"format": 4, "world": TEXT, "counts": {...}, "excluded": {CAUSE: N, ...},
   "templates": [{"slice", "asset", "sub", "kind", "root", "slots": {"position", "buildable", "ownership", "vitals"},
                  "buildableState", "permissionRule", "assembly": {"key", "fee", "entity", "component", "resolved"} or null,
                  "bounds": [{as the bounds of an object, with "entity" (the Interact entity of the slice), "position" in the
                              frame of the slice, and an option entity of the slice}, ...],
                  "children": [{"template", "position", "rotation"}, ...]}, ...],
   "spawned": [{"key", "parent", "parentActor", "parentSlice", "spawner", "template", "region", "position", "rotation",
                "authoredTemplate" and "override": {"rule": "D1", "from": KEY, "to": KEY, "slice": PATH} for a station of decision D1},
               ...],
   "fixtures": {NAME: template index, ...},
   "lootContainers": {NAME: {"template", "kind", "lootDropSlot", "lifetime", "emptyLifetime", "hideDelay", "rarityParticles",
                             "delays": [{"name", "slot", "seconds", "onInit", "onTrigger"}, ...]}, ...},
   "options": [{"name", "id", "action", "committed", "delay", "hold", "instantCancel", "cooldown", "maxUsage",
                "inactive", "requiredLevel", "requiredItem", "requiredItemCount", "requiredCurrency",
                "requiredAchievement", "statusRules"}, ...],
   "achievements": [[achievement id, index], ...],
   "objects": [{"key", "kind", "slice", "asset", "sub", "actor", "region", "position", "assembly" (or null), "bounds": [
                  {"slot", "component", "target", "position", "rangeSq", "capacity", "lineOfSight", "arcRequired", "arc",
                   "options": [[option index, option entity], ...], "lore": achievement index or -1,
                   "requires": {...}}, ...],
                "person" (format 4, a person only): {"npc", "variant", "table", "row", "presence": {"mode", "condition"},
                            "clientChild", "spawner", "unresolved"} or {"npc", "direct"}}, ...]}
  `key` is the placement UUID (the durable key of the object), `actor` the 32 hexadecimal digits of the actor id,
  `target` and `component` and an option entity 16 hexadecimal digits, `asset` the 32 hexadecimal digits of the slice
  asset id and `sub` its sub id (the metadata state of the record of the object). "options" of a bound are indexes into
  "options" of the file. "lore" is an index into "achievements". "requires" has only the requirements that the data
  gives: "achievement" (an index into "achievements"), "progression" [id, rank], "onlyOnQuest" true, "condition" true,
  "unresolved" [TEXT, ...].
  "tag" of a bound is its m_interactTagString (the tag of a quest task; not a requirement). "statusRules" of an option is true when the option has required, excluded, added, or removed status effects,
  or "Faction Required" or "Check PVP Flag Is Set".

Usage: interactions.py [OUTPUT.json] [--region NAME]... [--radius METERS [--center X Y]] [--list KIND]
  --region selects some regions (r_+04_+02); --radius a circle (default center: the start place of a new character):
  a smaller file for a test. --list KIND prints the objects of a kind (lorePage, npc, ...) and writes no file.
Default output: <SOULWARDEN_GAMEDATA_DIR>/interaction-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import (CATALOG_PATH, DEFAULT_CENTER, REGIONS_FOLDER, TABLES_FOLDER, WORLD, Catalog, Sources,  # noqa: E402
                           asset_id, region_names, spawner_path)
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import (BASE_CLASS_FIELDS, COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, NAME_FIELD, NO_ENTITY,  # noqa: E402
                       Element, component_id, find_field, id_value)

OUTPUT_NAME = "interaction-data.json"
FORMAT = 4
UI_DATABASE = "sharedassets/genericassets/ui/uidatabase.uidb"
CRAFTING_STATIONS = "sharedassets/genericassets/craftingstations.craftstationdb"
# The station slices of the settlements (stage 3.7a, decision D1: the _t2 variant of a master slice whose key has no row).
STATION_FOLDER = "slices/structures/settlements/stations/"
STATION_PREFIX = "settlement_station_"
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, "coatgen/", "slices/", UI_DATABASE, CRAFTING_STATIONS, CATALOG_PATH)
# The development fixtures (stage 2.5b, live checks L5 and L6): a command of the backend makes them near a character.
FIXTURES = {
    "powderKeg": "slices/structures/deployables/deployable_powderkeg.dynamicslice",
    "campT5": "slices/structures/camps/defaultcamps/campt5.dynamicslice",
    "camp": "slices/structures/camps/permanentcamp_master.dynamicslice",
}
# The loot containers (stage 3.5b.2, research chests-ground-loot.md Q3): the slice of the personal loot bag of a kill (AILoot) and of
# the discard of a player. The server makes it; only its eligible characters get its record (live check L5 of stage 3.5b.2).
LOOT_CONTAINERS = {
    "personal": "slices/instanced_loot_container.dynamicslice",
}
LORE_TABLE_TYPE = "LoreData"
ACHIEVEMENT_TABLE_TYPE = "AchievementData"

INTERACT_CLASS = uuid.UUID("b69045a3-a129-4334-bdfb-069c18cc84a2")
OPTION_ROW_CLASS = uuid.UUID("f0887e97-5084-413c-bce7-5c24cecb03c0")
INTERACT_BYTES = INTERACT_CLASS.bytes
# The classes of the components of a server child (live checks L4 to L6 of stage 2.5b; slice reads of the research).
ASSEMBLY_CLASS = uuid.UUID("520015fd-2674-49e1-b862-e7371ae60043")
BUILDABLE_CLASS = uuid.UUID("359f8468-0483-46ee-b1d4-beb75c1048fd")
OWNERSHIP_CLASS = uuid.UUID("2a333c23-a549-bc11-1a9a-ae905170f2f6")
POSITION_CLASS = uuid.UUID("6c2fc842-f451-4b45-9d7d-a6ee954df025")
VITALS_CLASS = uuid.UUID("0c848755-1f41-4408-b2be-0a3627674f50")
SPAWNER_CLASS = uuid.UUID("b52d0d52-99cd-41be-b6f7-d2519c15bf14")
# The classes of the parts of a loot container (stage 3.5b.2; research chests-ground-loot.md Q3, K6, K7).
INSTANCED_LOOT_CLASS = uuid.UUID("4fda5200-13f8-4f21-af8c-19706e74758f")
LOOT_DROP_CLASS = uuid.UUID("de250033-8ed6-4a2f-bdc8-8cd8ebeee9db")
DELAYED_EVENT_CLASS = uuid.UUID("d353f7e8-4266-f749-19a1-aa2c3841fbca")
STORAGE_CLASS = uuid.UUID("f09b20f4-6a64-477e-9690-a099ee1bf746")
STATION_ROW_FIELDS = (field_id("CraftingTypes"),)
# The classes of the elements of an empty m_condition (stage 3.5b.1): the container, the empty condition, and its element. Each chest
# and each gathering node has such a condition with no text and no value.
EMPTY_CONDITION_CLASSES = frozenset({uuid.UUID("9d45d855-d194-5c90-a293-c40eaafd7f0e"), uuid.UUID("41996a3d-446a-48a9-ac29-14d89e67594e"),
                                     uuid.UUID("bebf6d8c-c3da-4812-9277-049d158d8612")})

WORLD_TM = field_id("m_worldTM")
LOCAL_TM = field_id("m_localTM")
PARENT = field_id("m_parentId")
SLICE_ASSET = field_id("m_sliceAsset")
OPTION_ENTITIES = field_id("m_interactOptionEntities")
OPTION_ENTITY = field_id("m_interactEntityRef")
OPTION_LIST = field_id("m_interactOptions")
OPTION_NAME = field_id("m_interactOption")
CAPACITY = field_id("m_numberOfInteractorsAllowed")
RANGE = field_id("m_interactRangeSq")
LINE_OF_SIGHT = field_id("m_requiresLineOfSight")
ARC_REQUIRED = field_id("m_requiredInteractArc")
ARC = field_id("m_interactArc")
ACHIEVEMENT_OVERRIDE = field_id("m_requiredAchievementOverride")
PROGRESSION = field_id("m_requiredProgression")
PROGRESSION_ID = field_id("m_id")
PROGRESSION_TEXT = field_id("m_valueStr")
PROGRESSION_RANK = field_id("m_rank")
ONLY_ON_QUEST = field_id("m_interactOnlyOnQuest")
TAG = field_id("m_interactTagString")
CONDITION = field_id("m_condition")
SLOT = field_id("m_replicationIndex")
LORE_ID = field_id("m_loreId")
STATION_ENTRY = field_id("m_craftingStationEntry")
CHARGE_FEE = field_id("m_chargeCraftingFee")
INITIAL_STATE = field_id("m_initialState")
INITIAL_PERMISSION = field_id("m_initialPermissionRule")
CLIENT_AUTHORITY = field_id("m_isClientAuthoratative")
SPAWN_ON_INIT = field_id("m_spawnOnInitialize")
ALIAS_ASSET = field_id("m_aliasAsset")
INSTANCED_LOOT_TYPE = field_id("m_instancedLootType")
LOOTED_EVENTS = field_id("m_lootedEvents")
COMPLETELY_LOOTED_EVENTS = field_id("m_completelyLootedEvents")
RARITY_PARTICLES = field_id("m_rarityParticleEntityRef")
EVENT_ENTITY = field_id("m_entityRef")
IS_INSTANCED_LOOT = field_id("m_isInstancedLoot")
START_ON_INIT = field_id("m_startDelayOnInit")
START_ON_TRIGGER = field_id("m_startDelayOnTrigger")
DELAY_SECONDS = field_id("m_timeDelaySeconds")
DELAY_OVERRIDE = field_id("m_timeDelaySecondsOverrideConfig")
EVENT_DATA = field_id("m_eventData")
UNIQUE_STORAGE = field_id("m_isPlayerUniqueStorage")
# The persons (stage 2.5c): the NPC component (its key), the variant of a PrefabSpawner, the variation table of a slice and its
# columns, and the default achievement of the GatherableController of a swap slice.
NPC_CLASS = uuid.UUID("4bdb17d5-3b1e-474d-a783-29f47a7acd53")
NPC_KEY = field_id("m_npcKey")
SLICE_VARIANT = field_id("m_sliceVariant")
VARIATION_TABLE = field_id("m_variationTableUniqueName")
VARIATION_COLUMN = field_id("m_colName")
SWAP_ACHIEVEMENT = field_id("m_achievementName")
SWAP_ENABLED = field_id("m_enableUseAchievementForClientState")
VARIATION_TABLE_TYPE = "VariationData"
# The presence of a person child for one character (stage 2.5c; research npc-conversations.md decision 4 and quest-engine-3.md Q2): the
# client shows the child of a swap slice by the achievement of its GatherableController (client 0x3369430 asks the local player for the
# achievement; the variation action "Change Gatherable Achievement" sets it from the column SwapAchievementId), and the child of the
# walkaway slice by the events of its AchievementEvents script (the column AE.3_RequiredAchievementConditional starts the fade-in, AE.0
# the walkaway that deactivates the interaction). A table of this project from the slice names and the columns of the data; live check
# A2 of stage 2.5c. "showWhen": the person is there while the condition is true; "hideWhen": while it is false. A slice with the swap
# column and no row here has a rule that the tool does not know: the backend refuses its use.
PERSON_PRESENCE = {
    "slices/characters/mission/sliceswaps/masternpc_swap_defaultshown.dynamicslice": ("SwapAchievementId", "hideWhen"),
    "slices/characters/mission/sliceswaps/masternpc_swap_defaulthidden.dynamicslice": ("SwapAchievementId", "showWhen"),
    "slices/characters/mission/achievementevents/masternpc_walkaway.dynamicslice": ("AE.3_RequiredAchievementConditional", "showWhen"),
}
# The client id of a child that the client makes for a PrefabSpawner of a level placement (live check L1 of stage 2.5c; client 0x6160B60,
# called from 0x69EE550): the place, the yaw, and the slice asset of the child, with the low three bits 7.
CHILD_GROUP_TAG = 7
CHILD_YAW_BUCKET = 23
# The yaw near a border of the 23 degree buckets, or a position near the half of a step: the float of the client can give the next
# value (the tool has no exact copy of the conversion of the client). The tool then writes each id of the candidates of the slot.
CHILD_ID_MARGIN = 0.001
# The entity of a slice whose event ends the slice (the event target of KillSelfDelay and FastKillSelfDelay of the personal bag).
KILL_SELF_ENTITY = "KillSelf"

# The fields of an option row of the user interface database.
ROW_FIELDS = {name: field_id(name) for name in (
    "Name", "Interact Action Type", "Delay Time", "Is Committed Interaction", "Is Instant Cancel", "Hold action press time",
    "Cooldown Time", "Required Item Name", "Required Item Count", "Required Currency", "Required Achievement Name",
    "Required Level", "Committed Interaction Max Usage Timeout", "Committed Interaction Inactive Timeout",
    "Added Status Effects", "Required Status Effects", "Remove Status Effects", "Excluded Status Effects",
    "Faction Required", "Check PVP Flag Is Set")}

# The kind of an object: the first option name of this list that a bound of the object has (a table of this project
# from the option names of the data; research interaction-stage.md, Q4). An object with no option of the list has the
# kind "other".
KINDS = (
    ("ReadLore", "lorePage"), ("AddLoreToJournal", "lorePage"),
    ("OpenConversation", "npc"), ("OpenMissionBoardOption", "npc"),
    ("TakeAllOption", "lootDrop"), ("OpenStorageOption", "storage"), ("OpenTradingPostOption", "tradingPost"),
    ("OpenMap", "shrine"),
    # A camp has OpenCraftingOption too (permanentcamp_master, CampT5): the camp option comes first (stage 2.5b).
    ("BindToCampOption", "camp"), ("BindToCampOption_17DW", "camp"),
    ("OpenCraftingOption", "craftingStation"), ("OpenCraftingOptionLevel60", "craftingStation"),
    ("OpenLootStorageOption", "lootDrop"), ("GatherOption", "gathering"),
    ("ToggleDoorOption", "door"), ("TeleportToOtherSideOfDoor", "door"), ("TeleportDoorEnter", "door"),
    ("TeleportDoorExit", "door"),
    ("BedInteractionAnimation", "restFurniture"),
)
KIND_OF = dict(KINDS)
KIND_ORDER = {name: index for index, (name, _) in enumerate(KINDS)}

STATION_OPTIONS = ("OpenCraftingOption", "OpenCraftingOptionLevel60", "BindToCampOption", "BindToCampOption_17DW")

# The slice assets of PrefabSpawner children of the retail data that no record of the asset catalog of the game has (stage 2.5b,
# code review finding ST-R7: the full catalog of build 1.400.6031.6004151 has 1,595,260 records and none of these ids; the spawners
# Lotus_A, Lotus_B, and Lotus_C of an enclave of r_+04_+02, five uses). The client cannot make them either: they are not server
# children. Each other concrete asset with no path stops the tool (a broken join).
ASSETS_NOT_IN_GAME = frozenset({uuid.UUID("5f3bb00e-2549-523f-9063-98c2c3c132d0"), uuid.UUID("3e227128-6334-5dd2-8929-c12b15a6aadf"),
                                uuid.UUID("e21df46d-ebfc-57db-82c7-52233ca4ded3")})
CAUSE_SPAWNER = "a child of a PrefabSpawner that is not a station or a camp, or that the client makes: a later stage"
CAUSE_NOT_IN_GAME = "a child of a PrefabSpawner whose slice asset no record of the asset catalog of the game has (ASSETS_NOT_IN_GAME)"
CAUSE_ALIAS = "a child of a PrefabSpawner with an alias asset and no slice asset: not read"
CAUSE_CHILD_NO_PLACE = "a child of a station or a camp slice with no PositionInTheWorld component: the client cannot place it"
CAUSE_DISTRIBUTION = "a record of a distribution file: not read (stage 3.6)"
CAUSE_PLOT = "a placement with no slice (<PLOT>): an object of the level chunks"
CAUSE_NO_INTERACT = ("a placement whose slice has no Interact component (a PrefabSpawner of it makes a server entity: "
                     "stage 2.5b)")
CAUSE_PERSON_ID_TAKEN = ("a person child whose client id an earlier person slot has (one place, one yaw bucket, and the same low bits "
                         "of the slice asset): the first slot keeps the id (stage 2.5c)")
CAUSE_PERSON_NO_INIT = ("a person child of a PrefabSpawner with m_spawnOnInitialize false: the client does not make it at the load "
                        "(stage 2.5c), and the server source of such a person is not known")


def text_of(element: Element | None) -> str:
    if element is None or element.value is None:
        return ""
    return element.value.decode("utf-8", "replace")


def whole_of(element: Element | None, size: int, what: str, signed: bool = False) -> int:
    """@return The number of a field that must have `size` bytes (big-endian)."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    if element is None or element.value is None or len(element.value) != size:
        raise Failure(f"{what} is not a number of {size} byte(s)")
    return int.from_bytes(element.value, "big", signed=signed)


def flag_of(element: Element | None, what: str) -> bool:
    return whole_of(element, 1, what) != 0


def real_of(element: Element | None, what: str) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        raise Failure(f"{what} is not a number of 4 bytes")
    value = struct.unpack(">f", element.value)[0]
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return value


def child_of(element: Element, wanted: int) -> Element | None:
    return next((child for child in element.children if child.name == wanted), None)


def matrix_of(value: bytes | None, what: str) -> tuple[float, ...]:
    """@return The 3 x 4 matrix (three rows) of a transform field: 12 numbers, the basis vectors, then the translation."""
    if value is None or len(value) != 48:
        raise Failure(f"{what} is not a transform of 12 numbers")
    v = struct.unpack(">12f", value)
    if not all(math.isfinite(number) for number in v):
        raise Failure(f"{what} has a number that is not finite")
    return (v[0], v[3], v[6], v[9], v[1], v[4], v[7], v[10], v[2], v[5], v[8], v[11])


def placement_matrix(rotation: dict, position: dict) -> tuple[float, ...]:
    x, y, z, w = (float(rotation.get(axis, 1.0 if axis == "w" else 0.0)) for axis in "xyzw")
    size = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / size, y / size, z / size, w / size
    return ((1 - 2 * (y * y + z * z)), 2 * (x * y - z * w), 2 * (x * z + y * w), float(position["x"]),
            2 * (x * y + z * w), (1 - 2 * (x * x + z * z)), 2 * (y * z - x * w), float(position["y"]),
            2 * (x * z - y * w), 2 * (y * z + x * w), (1 - 2 * (x * x + y * y)), float(position["z"]))


def apply(matrix: tuple[float, ...], point: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = point
    return (matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
            matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
            matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11])


IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def multiply(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    """@return The 3 x 4 matrix a * b (rows of 4: the rotation and the translation)."""
    out = []
    for row in range(3):
        for column in range(4):
            value = sum(a[row * 4 + k] * b[k * 4 + column] for k in range(3))
            out.append(value + (a[row * 4 + 3] if column == 3 else 0.0))
    return tuple(out)


def quaternion_of(m: tuple[float, ...]) -> tuple[float, float, float, float]:
    """@return The unit quaternion (x, y, z, w) of the rotation of a 3 x 4 matrix (its columns made unit: a scale is not a turn),
    with w not negative."""
    columns = []
    for column in range(3):
        size = math.sqrt(sum(m[row * 4 + column] ** 2 for row in range(3))) or 1.0
        columns.append([m[row * 4 + column] / size for row in range(3)])
    r = [[columns[c][r] for c in range(3)] for r in range(3)]
    trace = r[0][0] + r[1][1] + r[2][2]
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        q = ((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s)
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0
        q = (0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s)
    elif r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0
        q = ((r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s)
    else:
        s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0
        q = ((r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[1][0] - r[0][1]) / s)
    size = math.sqrt(sum(value * value for value in q)) or 1.0
    q = tuple(value / size for value in q)
    if q[3] < 0.0:
        q = tuple(-value for value in q)
    if not all(math.isfinite(value) for value in q):
        raise Failure("a transform has a rotation that is not finite")
    return q


def read_station_keys(sources: Sources) -> set[str]:
    """@return The name of each row of the crafting station database (the Assembly key of a station, stage 2.5b)."""
    roots = sources.stream(CRAFTING_STATIONS)
    if roots is None:
        sources.check_complete()
    keys = set()
    for root in roots:
        for element in walk(root):
            if any(child.name in STATION_ROW_FIELDS for child in element.children):
                name = text_of(child_of(element, NAME_FIELD))
                if name:
                    keys.add(name)
    if not keys:
        raise Failure("the crafting station database has no row")
    return keys


def station_overrides(spawned: list[dict], templates: list[dict], has, load) -> tuple[int, int]:
    """Decision D1 of stage 3.7a (a decision of the orchestrator, option B; research crafting-stage.md section 12): each station child gets
    a valid runtime variant. A child whose authored Assembly key ends in 2 and has no row of the station database (the master slices of
    Smelting, Carpentry, Masonry, Tanning, and Weaving) gets the slice of the _t2 variant of its family, whose key ends in 3 (the smallest
    supported replacement; the client has no alias for a missing key: lookup 0x2E804E0). The entry keeps its authored template
    ("authoredTemplate") and names the override ("override"); the backend writes one log line with them. A key with no row of a slice that
    is not such a master slice, or whose family has no variant in the game copy, keeps its authored template (the backend refuses each
    craft there). A variant whose key is not the key ending in 3 with a row stops the tool (the data does not have the form of the rule).
    @param has: a test of a source path; @param load: the index of the template of a slice path (it adds the template).
    @return The number of overrides and the number of children with a key with no row and no variant."""
    overrides = 0
    missing = 0
    for entry in spawned:
        authored = templates[entry["template"]]
        assembly = authored["assembly"] or {}
        key = assembly.get("key", "")
        if assembly.get("resolved", True) or not key.endswith("2"):
            continue
        name = authored["slice"].rsplit("/", 1)[-1]
        if not authored["slice"].startswith(STATION_FOLDER) or not name.startswith(STATION_PREFIX) \
                or not name.endswith("_master.dynamicslice"):
            missing += 1
            continue
        family = name[len(STATION_PREFIX):-len("_master.dynamicslice")]
        variant_path = f"{STATION_FOLDER}{STATION_PREFIX}{family}_t2.dynamicslice"
        if not has(variant_path):
            missing += 1
            continue
        variant_index = load(variant_path)
        variant = templates[variant_index]["assembly"] or {}
        wanted = key[:-1] + "3"
        if not variant.get("resolved") or variant.get("key", "").lower() != wanted.lower():
            raise Failure(f"the variant {variant_path} of the station key {key} has the key {variant.get('key')!r}, not {wanted} with a "
                          f"row of the station database")
        entry["authoredTemplate"] = entry["template"]
        entry["template"] = variant_index
        entry["override"] = {"rule": "D1", "from": key, "to": variant["key"], "slice": variant_path}
        overrides += 1
    return overrides, missing


def actor_of(placement_id: str) -> bytes:
    """@return The managed actor id of a placement (research Q1; interaction.md 4.2; live check L1 of stage 2.5a)."""
    raw = uuid.UUID(placement_id).bytes
    folded = bytes(a ^ b for a, b in zip(raw[:8], raw[8:]))[::-1]
    return bytes([(folded[0] & ~7) | 3]) + folded[1:] + bytes(8)


def group_of(actor: bytes) -> int:
    return int.from_bytes(actor[:8], "little")


def round_half_away(value: float) -> int:
    """@return roundf of the C library (the client import at 0x7ECAC28): a half goes away from 0."""
    return int(math.floor(value + 0.5)) if value >= 0 else -int(math.floor(-value + 0.5))


def f32(value: float) -> float:
    """@return The value as a 32-bit float (the client keeps the place and the yaw of a spawner in floats)."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def compose_group(rx: int, ry: int, rz: int, bucket: int, asset: bytes) -> int:
    """@return G of a child from its rounded fields (client 0x6160B60)."""
    mask = (1 << 64) - 1
    first = int.from_bytes(asset[:8], "little")
    value = (rz & 0xFFF) | ((((bucket & 0xF) | (first << 4)) << 12) & mask)
    value = ((value << 18) | (ry & 0x3FFFF)) & mask
    value = ((value << 18) | (rx & 0x3FFFF)) & mask
    return ((value << 3) | CHILD_GROUP_TAG) & mask


def yaw_bucket(yaw: float) -> int:
    """@return The yaw bucket of the client: the float yaw 0 to 360 degrees, cut toward 0 (cvttss2si; live check L1: -107.4 is 252),
    divided by 23."""
    return ((int(f32(yaw) % 360.0) % 360) // CHILD_YAW_BUCKET) & 0xF


def child_group(x: float, y: float, z: float, yaw: float, asset: bytes) -> int:
    """@return G of the child that the client makes for a PrefabSpawner of a level placement (live check L1 of stage 2.5c; client
    0x6160B60): the low 3 bits 7, then 18 bits of round(x * 8), 18 bits of round(y * 8), 12 bits of round(z * 4), 4 bits of the yaw
    bucket ((int)yaw / 23, the yaw 0 to 360 degrees), and the first 8 bytes of the slice asset of the child (little-endian; only their low
    9 bits stay). The place is a float of the client: x * 8 is f32(x) * 8 (an exact float product), then roundf (code review of stage 2.5c,
    finding PR5C-R6: 10000.0623 is the float 10000.0625, and 80000.5 rounds to 80001)."""
    return compose_group(round_half_away(f32(x) * 8), round_half_away(f32(y) * 8), round_half_away(f32(z) * 4), yaw_bucket(yaw), asset)


def axis_values(value: float, scale: int) -> list[int]:
    """@return The rounded values of one axis: the float path of the client first, then the other rounding when the scaled value is
    CHILD_ID_MARGIN or less from a border of roundf (the tool has no exact copy of the float composition of the client), and the rounding
    of the value in double (the rule of the ids of the trace of gate R)."""
    scaled = f32(value) * scale
    values = [round_half_away(scaled)]
    for other in (round_half_away(scaled + CHILD_ID_MARGIN), round_half_away(scaled - CHILD_ID_MARGIN), round_half_away(value * scale)):
        if other not in values:
            values.append(other)
    return values


def child_groups(x: float, y: float, z: float, yaw: float, asset: bytes) -> list[int]:
    """@return The ids of a child slot: the id of the float path first; with a value near a border of the rule, each combination of the
    roundings of each axis and of the yaw bucket (code review of stage 2.5c, finding PR5C-R6: two axes near their borders at one time)."""
    buckets = [yaw_bucket(yaw)]
    for other in (yaw_bucket(yaw + CHILD_ID_MARGIN), yaw_bucket(yaw - CHILD_ID_MARGIN)):
        if other not in buckets:
            buckets.append(other)
    groups: list[int] = []
    for rx in axis_values(x, 8):
        for ry in axis_values(y, 8):
            for rz in axis_values(z, 4):
                for bucket in buckets:
                    group = compose_group(rx, ry, rz, bucket, asset)
                    if group not in groups:
                        groups.append(group)
    return groups


def yaw_of(matrix: tuple[float, ...]) -> float:
    """@return The turn of a transform about the vertical axis (degrees, -180 to 180)."""
    return math.degrees(math.atan2(matrix[4], matrix[0]))


def read_variation_tables(sources: Sources) -> dict[str, dict[str, dict]]:
    """@return The rows of each variation table by its unique name and the variant id (stage 2.5c): the row index and its cells."""
    tables: dict[str, dict[str, dict]] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        if "variation" not in path.rsplit("/", 1)[-1]:
            continue
        data = sources.read(path)
        if data is None or sheet_type(data) != VARIATION_TABLE_TYPE:
            continue
        try:
            sheet = Sheet(data)
            rows = tables.setdefault(sheet.name, {})
            for row in range(sheet.row_count):
                variant = str(sheet.cell(row, "VariantID"))
                if variant and variant not in rows:
                    rows[variant] = {"row": row, "npc": str(sheet.cell(row, "NPCId")),
                                     **{column: str(sheet.cell(row, column)) for column, _ in PERSON_PRESENCE.values()}}
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    return tables


def walk(element: Element):
    yield element
    for child in element.children:
        yield from walk(child)


def read_options(sources: Sources) -> dict[str, dict]:
    """@return The option rows of the user interface database, by name."""
    roots = sources.stream(UI_DATABASE)
    if roots is None:
        sources.check_complete()
    rows: dict[str, dict] = {}
    for root in roots:
        for element in walk(root):
            if element.type_id != OPTION_ROW_CLASS:
                continue
            fields = {child.name: child for child in element.children}
            name = text_of(fields.get(ROW_FIELDS["Name"]))
            if not name:
                continue
            what = f"the option row {name}"

            def field(label: str) -> Element | None:
                return fields.get(ROW_FIELDS[label])

            def number(label: str, size: int = 4) -> int:
                return whole_of(field(label), size, f"{what}: {label}") if field(label) is not None else 0

            def real(label: str) -> float:
                return real_of(field(label), f"{what}: {label}") if field(label) is not None else 0.0

            def has_list(label: str) -> bool:
                element = field(label)
                return element is not None and len(element.children) > 0

            delay_data = next((child for child in element.children
                               if any(grand.name == ROW_FIELDS["Delay Time"] for grand in child.children)), None)
            delay = real_of(child_of(delay_data, ROW_FIELDS["Delay Time"]), f"{what}: Delay Time") if delay_data else 0.0
            action_element = next((node for node in walk(element) if node.name == ROW_FIELDS["Interact Action Type"]
                                   and node.value is not None), None)
            if action_element is None or len(action_element.value) != 1:
                raise Failure(f"{what} has no action of one byte")
            row = {
                "name": name, "id": zlib.crc32(name.lower().encode()) & 0xFFFFFFFF, "action": action_element.value[0],
                "committed": flag_of(field("Is Committed Interaction"), f"{what}: committed")
                if field("Is Committed Interaction") is not None else False,
                "delay": round(delay, 4), "hold": round(real("Hold action press time"), 4),
                "instantCancel": flag_of(field("Is Instant Cancel"), what) if field("Is Instant Cancel") is not None else False,
                "cooldown": number("Cooldown Time"),
                "maxUsage": round(real("Committed Interaction Max Usage Timeout"), 4),
                "inactive": round(real("Committed Interaction Inactive Timeout"), 4),
                "requiredLevel": number("Required Level"),
                "requiredItem": text_of(field("Required Item Name")),
                "requiredItemCount": number("Required Item Count"),
                "requiredCurrency": number("Required Currency"),
                "requiredAchievement": text_of(field("Required Achievement Name")),
                "statusRules": any(has_list(label) for label in ("Added Status Effects", "Required Status Effects",
                                                                 "Remove Status Effects", "Excluded Status Effects"))
                or (field("Faction Required") is not None and flag_of(field("Faction Required"), what))
                or (field("Check PVP Flag Is Set") is not None and flag_of(field("Check PVP Flag Is Set"), what)),
            }
            if name in rows and rows[name] != row:
                raise Failure(f"the user interface database has two different rows of the option {name}")
            rows[name] = row
    if not rows:
        raise Failure("the user interface database has no option row")
    return rows


def read_lore_tables(sources: Sources) -> tuple[dict[str, str], dict[str, int]]:
    """@return The achievement of each lore id (javelindata_loreitems), and the index of each achievement."""
    lore: dict[str, str] = {}
    achievements: dict[str, int] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        name = path.rsplit("/", 1)[-1]
        if "lore" not in name and "achievement" not in name:
            continue
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        try:
            if kind == LORE_TABLE_TYPE:
                sheet = Sheet(data)
                for row in range(sheet.row_count):
                    lore.setdefault(str(sheet.cell(row, "LoreID")).lower(), str(sheet.cell(row, "AchievementId")))
            elif kind == ACHIEVEMENT_TABLE_TYPE:
                sheet = Sheet(data)
                for row in range(sheet.row_count):
                    index = sheet.cell(row, "AchievementIndex")
                    identifier = str(sheet.cell(row, "AchievementID"))
                    if identifier and isinstance(index, float) and index >= 0 and index == int(index):
                        achievements.setdefault(identifier.lower(), int(index))
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    if not lore:
        raise Failure("the game copy has no lore table (LoreData)")
    if not achievements:
        raise Failure("the game copy has no achievement table (AchievementData)")
    return lore, achievements


class Template:
    """The Interact components of one slice, in the frame of the slice, and the parts of a server child (stage 2.5b)."""

    def __init__(self, path: str, roots: list[Element]) -> None:
        self.path = path
        self.root: int | None = None
        self.bounds: list[dict] = []
        # The PrefabSpawner components of the slice: the entity, the child asset, and the flags of the spawner.
        self.spawner_list: list[dict] = []
        # The slots of the replicated components that the backend sends for a server child (0: the slice has none).
        self.slots = {"position": 0, "buildable": 0, "ownership": 0, "vitals": 0}
        self.buildable_state = -1
        self.permission_rule = -1
        self.assembly: dict | None = None
        self.matrices: dict[int, tuple[float, ...]] = {}
        # The parts of a loot container (stage 3.5b.2): the InstancedLoot, LootDrop, Storage, and DelayedEvent components.
        self.instanced: dict | None = None
        self.loot_drop: dict | None = None
        self.unique_storage = False
        self.delays: list[dict] = []
        # The parts of a person (stage 2.5c): the key of its NPC component, the variation table of the slice and its columns, and the
        # default achievement of the GatherableController of a swap slice.
        self.npc_key = ""
        self.variation_table = ""
        self.variation_columns: list[str] = []
        self.swap_achievement = ""
        self.entity_names: dict[int, str] = {}
        entities: dict[int, dict] = {}
        interacts: list[tuple[int, Element]] = []
        for root in roots:
            for element in walk(root):
                if element.type_id != ENTITY_TYPE:
                    continue
                entity_id = id_value(child_of(element, ID_FIELD))
                if entity_id is None:
                    continue
                name = text_of(child_of(element, NAME_FIELD))
                info = {"name": name, "matrix": None, "parent": None, "lore": ""}
                components = child_of(element, COMPONENTS_FIELD)
                for component in components.children if components is not None else []:
                    world = find_field(component, WORLD_TM)
                    if world is not None or find_field(component, LOCAL_TM) is not None:
                        info["parent"] = id_value(find_field(component, PARENT))
                        source = world if world is not None else find_field(component, LOCAL_TM)
                        info["matrix"] = matrix_of(source.value, f"{path}: the transform of the entity {name}")
                    if component.type_id == INTERACT_CLASS:
                        interacts.append((entity_id, component))
                    lore = find_field(component, LORE_ID)
                    if lore is not None and text_of(lore):
                        info["lore"] = text_of(lore)
                    self._read_parts(path, name, entity_id, component)
                entities[entity_id] = info
        self.entity_names = {identifier: info["name"] for identifier, info in entities.items()}
        roots_of_slice = [identifier for identifier, info in entities.items() if info["parent"] == NO_ENTITY]
        if (interacts or self.spawner_list) and not roots_of_slice:
            raise Failure(f"{path}: the slice has no root entity (an entity whose transform has no parent)")
        self.root = roots_of_slice[-1] if roots_of_slice else None
        self.matrices = {identifier: info["matrix"] for identifier, info in entities.items() if info["matrix"] is not None}
        for entity_id, component in interacts:
            self.bounds.append(self._bound(entity_id, component, entities))

    @property
    def spawners(self) -> int:
        return len(self.spawner_list)

    def _slot(self, component: Element, what: str) -> int:
        element = find_field(component, SLOT)
        return whole_of(element, 4, f"{what}: m_replicationIndex") if element is not None else 0

    def _read_parts(self, path: str, name: str, entity_id: int, component: Element) -> None:
        """Reads the parts of a server child in one component: a slot, the Assembly, an initial state, a spawner."""
        what = f"{path}: the component {component.type_id} of the entity {name}"
        kind = {POSITION_CLASS: "position", BUILDABLE_CLASS: "buildable", OWNERSHIP_CLASS: "ownership",
                VITALS_CLASS: "vitals"}.get(component.type_id)
        if kind is not None and self.slots[kind] == 0:
            self.slots[kind] = self._slot(component, what)
        if component.type_id == BUILDABLE_CLASS and find_field(component, INITIAL_STATE) is not None:
            self.buildable_state = whole_of(find_field(component, INITIAL_STATE), 4, f"{what}: m_initialState")
        if component.type_id == OWNERSHIP_CLASS and find_field(component, INITIAL_PERMISSION) is not None:
            self.permission_rule = whole_of(find_field(component, INITIAL_PERMISSION), 4, f"{what}: m_initialPermissionRule")
        if component.type_id == ASSEMBLY_CLASS and self.assembly is None:
            entry = next((node for node in walk(component) if node.name == STATION_ENTRY and node.value is not None), None)
            fee = next((node for node in walk(component) if node.name == CHARGE_FEE), None)
            identifier = component_id(component)
            if identifier is None:
                raise Failure(f"{what}: the Assembly component has no component id")
            self.assembly = {"key": text_of(entry), "fee": flag_of(fee, f"{what}: m_chargeCraftingFee") if fee is not None else False,
                             "entity": entity_id, "component": identifier}
        if component.type_id in (INSTANCED_LOOT_CLASS, LOOT_DROP_CLASS, DELAYED_EVENT_CLASS, STORAGE_CLASS):
            self._read_loot_part(what, entity_id, name, component)
        if component.type_id == NPC_CLASS and not self.npc_key:
            self.npc_key = text_of(find_field(component, NPC_KEY))
        table = find_field(component, VARIATION_TABLE)
        if table is not None and not self.variation_table:
            self.variation_table = text_of(table)
            self.variation_columns = [text_of(node) for node in walk(component) if node.name == VARIATION_COLUMN and text_of(node)]
        swap = find_field(component, SWAP_ENABLED)
        if swap is not None and flag_of(swap, f"{what}: m_enableUseAchievementForClientState") and not self.swap_achievement:
            self.swap_achievement = text_of(find_field(component, SWAP_ACHIEVEMENT))
        if component.type_id == SPAWNER_CLASS or find_field(component, SLICE_ASSET) is not None:
            authority = find_field(component, CLIENT_AUTHORITY)
            on_init = find_field(component, SPAWN_ON_INIT)
            alias = find_field(component, ALIAS_ASSET)
            self.spawner_list.append({
                "entity": entity_id, "name": name, "asset": asset_id(find_field(component, SLICE_ASSET)),
                "alias": asset_id(alias) is not None,
                "client": flag_of(authority, f"{what}: m_isClientAuthoratative") if authority is not None else False,
                "onInit": flag_of(on_init, f"{what}: m_spawnOnInitialize") if on_init is not None else False,
                "variant": text_of(find_field(component, SLICE_VARIANT))})

    def _read_loot_part(self, what: str, entity_id: int, name: str, component: Element) -> None:
        """Reads a part of a loot container (stage 3.5b.2): the fields are in the component or in its facets."""

        def field(wanted: int) -> Element | None:
            return next((node for node in walk(component) if node.name == wanted), None)

        def events(element: Element | None) -> list[int]:
            targets = []
            for entry in element.children if element is not None else []:
                reference = child_of(entry, EVENT_ENTITY)
                target = id_value(next((node for node in walk(reference) if node.value is not None and len(node.value) == 8), None)) \
                    if reference is not None else None
                if target is not None:
                    targets.append(target)
            return targets

        if component.type_id == INSTANCED_LOOT_CLASS and self.instanced is None:
            kind = field(INSTANCED_LOOT_TYPE)
            particles = field(RARITY_PARTICLES)
            self.instanced = {"kind": whole_of(kind, 4, f"{what}: m_instancedLootType") if kind is not None else -1,
                              "looted": events(field(LOOTED_EVENTS)), "completelyLooted": events(field(COMPLETELY_LOOTED_EVENTS)),
                              "particles": len(particles.children) if particles is not None else 0}
        elif component.type_id == LOOT_DROP_CLASS and self.loot_drop is None:
            instanced = field(IS_INSTANCED_LOOT)
            self.loot_drop = {"slot": self._slot(component, what),
                              "instanced": flag_of(instanced, f"{what}: m_isInstancedLoot") if instanced is not None else False}
        elif component.type_id == STORAGE_CLASS:
            unique = field(UNIQUE_STORAGE)
            self.unique_storage = self.unique_storage or (unique is not None and flag_of(unique, f"{what}: m_isPlayerUniqueStorage"))
        elif component.type_id == DELAYED_EVENT_CLASS:
            on_init, on_trigger, seconds, override = field(START_ON_INIT), field(START_ON_TRIGGER), field(DELAY_SECONDS), field(DELAY_OVERRIDE)
            if seconds is None:
                raise Failure(f"{what}: the DelayedEvent component has no m_timeDelaySeconds")
            data = field(EVENT_DATA)
            reference = child_of(data, EVENT_ENTITY) if data is not None else None
            target = id_value(next((node for node in walk(reference) if node.value is not None and len(node.value) == 8), None)) \
                if reference is not None else None
            self.delays.append({"entity": entity_id, "name": name, "slot": self._slot(component, what),
                                "onInit": flag_of(on_init, f"{what}: m_startDelayOnInit") if on_init is not None else False,
                                "onTrigger": flag_of(on_trigger, f"{what}: m_startDelayOnTrigger") if on_trigger is not None else False,
                                "seconds": real_of(seconds, f"{what}: m_timeDelaySeconds"), "override": text_of(override),
                                "target": target})

    def loot_container(self) -> dict:
        """@return The loot values of a personal loot container (stage 3.5b.2), or a Failure when the slice does not have them all."""
        what = f"the loot container {self.path}"
        if self.instanced is None or self.loot_drop is None:
            raise Failure(f"{what} has no InstancedLoot or no LootDrop component")
        if self.instanced["kind"] != 1 or not self.loot_drop["instanced"] or not self.unique_storage:
            raise Failure(f"{what} is not a personal bag: the loot type is not AILoot (1), m_isInstancedLoot is false, or it has no "
                          f"player-unique Storage")
        by_entity = {delay["entity"]: delay for delay in self.delays}
        kill_self = {identifier for identifier, name in self.entity_names.items() if name == KILL_SELF_ENTITY}
        # The lifetime: the delayed event that starts at the init and ends the slice (KillSelfDelay, 180 s; research K7). A config
        # override name is not proof of an override (the shared bag): the personal bag must have none.
        lives = [delay for delay in self.delays if delay["onInit"] and delay["target"] in kill_self]
        if len(lives) != 1 or lives[0]["override"] or not lives[0]["seconds"] > 0:
            raise Failure(f"{what} has not one delayed event that starts at the init and ends the slice, with no override")
        # The end after the last item: the delayed event that m_completelyLootedEvents of the server facet starts (FastKillSelfDelay).
        empties = [by_entity[target] for target in self.instanced["completelyLooted"] if target in by_entity
                   and by_entity[target]["target"] in kill_self]
        if len(empties) != 1 or empties[0]["seconds"] < 0:
            raise Failure(f"{what}: m_completelyLootedEvents does not start one delayed event that ends the slice")
        # The hide of the owner after Looted (HideMeshDelay; the client runs it): the delayed event that m_lootedEvents starts.
        hides = [by_entity[target] for target in self.instanced["looted"] if target in by_entity]
        if len(hides) != 1:
            raise Failure(f"{what}: m_lootedEvents does not start one delayed event")
        if self.loot_drop["slot"] == 0 or self.slots["position"] == 0:
            raise Failure(f"{what} has no slot of its LootDrop or PositionInTheWorld component")
        return {"kind": self.instanced["kind"], "lootDropSlot": self.loot_drop["slot"], "lifetime": round(lives[0]["seconds"], 4),
                "emptyLifetime": round(empties[0]["seconds"], 4), "hideDelay": round(hides[0]["seconds"], 4),
                "rarityParticles": self.instanced["particles"],
                "delays": [{"name": delay["name"], "slot": delay["slot"], "seconds": round(delay["seconds"], 4), "onInit": delay["onInit"],
                            "onTrigger": delay["onTrigger"]} for delay in self.delays]}

    def option_names(self) -> set[str]:
        return {name for bound in self.bounds for _, names in bound["options"] for name in names}

    def is_station(self) -> bool:
        """@return True for a crafting station or a camp: an Assembly and a bound with a station option (stage 2.5b)."""
        return self.assembly is not None and any(name in STATION_OPTIONS for name in self.option_names())

    def is_person(self) -> bool:
        """@return True for a person (stage 2.5c): an NPC component or a variation table, and a bound with OpenConversation."""
        return bool(self.npc_key or self.variation_table) and "OpenConversation" in self.option_names()

    def _bound(self, entity_id: int, component: Element, entities: dict[int, dict]) -> dict:
        info = entities[entity_id]
        what = f"{self.path}: the Interact component of the entity {info['name']}"
        identifier = component_id(component)
        if identifier is None:
            raise Failure(f"{what} has no component id")
        slot = whole_of(find_field(component, SLOT), 4, f"{what}: m_replicationIndex")
        range_sq = real_of(find_field(component, RANGE), f"{what}: m_interactRangeSq")
        if range_sq <= 0:
            raise Failure(f"{what}: m_interactRangeSq is not above 0")
        capacity = whole_of(find_field(component, CAPACITY), 4, f"{what}: m_numberOfInteractorsAllowed", signed=True)
        options: list[tuple[int, list[str]]] = []
        lore = ""
        list_element = find_field(component, OPTION_ENTITIES)
        for entry in list_element.children if list_element is not None else []:
            option_entity = id_value(next((node for node in walk(child_of(entry, OPTION_ENTITY) or Element())
                                           if node.value is not None and len(node.value) == 8), None))
            names = [text_of(node) for node in walk(child_of(entry, OPTION_LIST) or Element())
                     if node.name == OPTION_NAME and text_of(node)]
            if option_entity is None or option_entity == NO_ENTITY:
                option_entity = entity_id  # No entity of the option: the entity of the Interact component.
            if names:
                options.append((option_entity, names))
            if option_entity in entities and entities[option_entity]["lore"]:
                lore = entities[option_entity]["lore"]
        if not lore and info["lore"]:
            lore = info["lore"]
        matrix = info["matrix"] or (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
        requires: dict = {}
        override = text_of(find_field(component, ACHIEVEMENT_OVERRIDE))
        if override:
            requires["achievement"] = override
        progression = find_field(component, PROGRESSION)
        if progression is not None:
            progression_id = next((text_of(node) for node in walk(progression) if node.name == PROGRESSION_TEXT
                                   and text_of(node)), "")
            rank_element = child_of(progression, PROGRESSION_RANK)
            rank = whole_of(rank_element, 4, f"{what}: m_rank") if rank_element is not None else 0
            if progression_id or rank:
                requires["progression"] = [progression_id, rank]
        if find_field(component, ONLY_ON_QUEST) is not None and flag_of(find_field(component, ONLY_ON_QUEST), what):
            requires["onlyOnQuest"] = True
        # An empty condition is no requirement (stage 3.5b.1: each chest of master_lootcontainer and each gathering node has one): its
        # elements are only of the classes of an empty condition (EMPTY_CONDITION_CLASSES), with no text and no value that is not 0.
        # A condition of a different class (cf33e3aa-...: the doors of the main story quest) is a requirement.
        condition = find_field(component, CONDITION)
        if condition is not None and any((node.value is not None and any(node.value))
                                         or node.type_id not in EMPTY_CONDITION_CLASSES for node in walk(condition)):
            requires["condition"] = True
        tag = text_of(find_field(component, TAG))
        line = find_field(component, LINE_OF_SIGHT)
        arc_required = find_field(component, ARC_REQUIRED)
        arc = find_field(component, ARC)
        return {"entity": entity_id, "component": identifier, "slot": slot, "translation": (matrix[3], matrix[7], matrix[11]),
                "rangeSq": range_sq, "capacity": capacity,
                "lineOfSight": flag_of(line, f"{what}: m_requiresLineOfSight") if line is not None else False,
                "arcRequired": flag_of(arc_required, f"{what}: m_requiredInteractArc") if arc_required is not None else False,
                "arc": real_of(arc, f"{what}: m_interactArc") if arc is not None else 0.0,
                "options": options, "lore": lore, "requires": requires, "tag": tag}


def capital_regions(sources: Sources) -> list[str]:
    """@return The name of each region with a placement file (some regions have placements and no metadata file)."""
    names = set()
    for path in sources.paths(REGIONS_FOLDER, ".capitals.json"):
        parts = path[len(REGIONS_FOLDER):].split("/")
        if len(parts) >= 3 and parts[1] == "capitals":
            names.add(parts[0])
    return sorted(names)


def asset_of(placement: dict) -> tuple[bytes, int] | None:
    match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", str(placement.get("sliceAssetId", "")))
    return (uuid.UUID(match[1]).bytes, int(match[2], 16)) if match else None


def kind_of(bounds: list[dict]) -> str:
    best = None
    for bound in bounds:
        for _, names in bound["options"]:
            for name in names:
                if name in KIND_ORDER and (best is None or KIND_ORDER[name] < KIND_ORDER[best]):
                    best = name
    return KIND_OF[best] if best is not None else "other"


def build_document(sources: Sources, regions: list[str] | None = None, center: tuple[float, float] | None = None,
                   radius: float | None = None) -> dict:
    options = read_options(sources)
    lore_rows, achievement_rows = read_lore_tables(sources)
    catalog_data = sources.read(CATALOG_PATH)
    if catalog_data is None:
        sources.check_complete()
    catalog = Catalog(catalog_data)
    if regions is None:
        regions = capital_regions(sources)
        if radius is not None:
            near = set(region_names(center or DEFAULT_CENTER, radius))
            regions = [region for region in regions if region in near]
    counts = {"regions": len(regions), "layers": 0, "placements": 0, "placementsWithSlice": 0, "objects": 0, "bounds": 0,
              "options": 0, "unresolvedBounds": 0, "unknownOptions": 0, "byKind": {}, "byOption": {}}
    # The names of the data that have no row (a required achievement, a lore id), with the first object that names each one.
    unresolved: dict[str, str] = {}
    excluded: dict[str, int] = {}
    templates: dict[str, Template] = {}
    option_index: dict[str, int] = {}
    option_list: list[dict] = []
    achievement_index: dict[str, int] = {}
    achievement_list: list[list] = []
    references: dict[tuple[int, int], str] = {}
    keys: dict[str, str] = {}
    objects: list[dict] = []

    def exclude(cause: str, count: int = 1) -> None:
        excluded[cause] = excluded.get(cause, 0) + count

    def child_slice(spawner: dict, what: str) -> str | None:
        """@return The slice of the child of a PrefabSpawner that the server makes, or nothing (with its count) for a child that the
        tool does not read: an alias asset. A concrete slice asset that the catalog does not have stops the tool (code review of stage
        2.5b, finding ST-R7: a station or its forge left the file with no error)."""
        child_path = catalog.path(spawner["asset"]) if spawner["asset"] is not None else None
        if child_path is not None:
            return child_path
        if spawner["alias"]:
            exclude(CAUSE_ALIAS)
            return None
        if spawner["asset"] is not None and spawner["asset"][0] != bytes(16):
            if uuid.UUID(bytes=spawner["asset"][0]) in ASSETS_NOT_IN_GAME:
                exclude(CAUSE_NOT_IN_GAME)
                return None
            raise Failure(f"{what}: the PrefabSpawner {spawner.get('name', '')} names the slice asset "
                          f"{uuid.UUID(bytes=spawner['asset'][0])}:{spawner['asset'][1]}, and the catalog of the game has no path of it")
        exclude(CAUSE_SPAWNER)
        return None

    def achievement(identifier: str, what: str) -> int | None:
        """@return The index of an achievement in "achievements", or no value for an id with no row (unresolved)."""
        lower = identifier.lower()
        if lower not in achievement_rows:
            unresolved.setdefault(identifier, what)
            return None
        if lower not in achievement_index:
            achievement_index[lower] = len(achievement_list)
            achievement_list.append([identifier, achievement_rows[lower]])
        return achievement_index[lower]

    def option(name: str, what: str) -> int | None:
        """@return The index of an option in "options", or no value for a name with no row (unresolved)."""
        if name not in options:
            unresolved.setdefault(f"option {name}", what)
            return None
        if name not in option_index:
            row = dict(options[name])
            required = row["requiredAchievement"]
            row["requiredAchievement"] = -1
            if required:
                index = achievement(required, f"the option {name}")
                if index is None:
                    row["unresolved"] = required
                else:
                    row["requiredAchievement"] = index
            option_index[name] = len(option_list)
            option_list.append(row)
        return option_index[name]

    station_keys = read_station_keys(sources)
    variation_tables = read_variation_tables(sources)
    person_groups: set[int] = set()
    counts.update({"persons": 0, "personsDirect": 0, "personsUnresolved": 0, "personSlotsWithTwoIds": 0})
    # The asset id of each slice path (the catalog of the game): the metadata state of a fixture.
    path_assets = {path: asset for asset, path in getattr(catalog, "paths", {}).items()}
    template_index: dict[str, int] = {}
    template_list: list[dict] = []
    spawned: list[dict] = []
    parsed: set[str] = set()

    def load_template(slice_path: str, child: bool = False) -> Template:
        """@param child True for the slice of a server child: it can have only a PositionInTheWorld component (a forge)."""
        if slice_path not in templates or (child and templates[slice_path].root is None and slice_path not in parsed):
            raw = sources.read(slice_path)
            if raw is None:
                sources.check_complete()
            parsed.add(slice_path) if child else None
            if not child and INTERACT_BYTES not in raw and SPAWNER_CLASS.bytes not in raw and ASSEMBLY_CLASS.bytes not in raw:
                # No Interact, PrefabSpawner, or Assembly class in the slice: no object (the class id is in each component of the
                # class).
                templates[slice_path] = Template(slice_path, [])
            else:
                templates[slice_path] = Template(slice_path, sources.stream(slice_path))
        return templates[slice_path]

    def bound_entry(bound: dict, what: str, k: int | None, place: tuple[float, float, float]) -> tuple[dict, list[str]]:
        """@return One bound of the file and its unknown option names. k: the XOR key of an object (root XOR G); no value for a
        template (the ids of the slice; the backend makes the ids from G of the child)."""
        entries = []
        unknown_options = []
        for option_entity, names in bound["options"]:
            for name in names:
                index = option(name, what)
                if index is None:
                    unknown_options.append(name)
                    continue
                entries.append([index, f"{option_entity ^ (k or 0):016x}"])
                counts["byOption"][name] = counts["byOption"].get(name, 0) + 1
        lore = -1
        requires = dict(bound["requires"])
        unresolved_texts = []
        if bound["lore"]:
            achievement_id = lore_rows.get(bound["lore"].lower(), "")
            index = achievement(achievement_id, f"{what}: the lore id {bound['lore']}") if achievement_id else None
            if index is None:
                unresolved.setdefault(f"lore {bound['lore']}", what)
                unresolved_texts.append(f"lore {bound['lore']}")
            else:
                lore = index
        if "achievement" in requires:
            index = achievement(requires["achievement"], what)
            if index is None:
                unresolved_texts.append(requires.pop("achievement"))
            else:
                requires["achievement"] = index
        if unresolved_texts:
            requires["unresolved"] = unresolved_texts
            counts["unresolvedBounds"] += 1
        if not all(math.isfinite(value) for value in place):
            raise Failure(f"{what}: the place of a bound is not finite")
        entry = {"slot": bound["slot"], "component": f"{bound['component']:016x}", "position": [round(value, 3) for value in place],
                 "rangeSq": bound["rangeSq"], "capacity": bound["capacity"], "lineOfSight": bound["lineOfSight"],
                 "arcRequired": bound["arcRequired"], "arc": bound["arc"], "options": entries, "lore": lore, "requires": requires}
        if k is None:
            entry["entity"] = f"{bound['entity']:016x}"
        if bound["tag"]:
            entry["tag"] = bound["tag"]
        if unknown_options:
            entry["unknownOptions"] = unknown_options
            counts["unknownOptions"] += len(unknown_options)
        return entry, unknown_options

    def assembly_entry(template: Template, k: int | None) -> dict | None:
        if template.assembly is None:
            return None
        key = template.assembly["key"]
        return {"key": key, "fee": template.assembly["fee"], "entity": f"{template.assembly['entity'] ^ (k or 0):016x}",
                "component": f"{template.assembly['component']:016x}", "resolved": key in station_keys}

    def person_binding(slice_path: str, template: Template, variant: str | None) -> dict:
        """@return The binding of a person (stage 2.5c): the NPC id, the variant and its row, and the presence rule. A person child gets
        its NPC id from the row of its variant in the variation table of its slice (research npc-conversations.md Q1: not the defaults
        of the generic child slice); a direct person from its NPC component. "unresolved": the backend refuses the use."""
        if variant is None:
            binding = {"npc": template.npc_key, "direct": True}
            if not template.npc_key:
                binding["unresolved"] = "the NPC component has no m_npcKey"
            return binding
        binding: dict = {"npc": "", "variant": variant, "table": template.variation_table}
        rows = variation_tables.get(template.variation_table)
        row = rows.get(variant) if rows is not None and variant else None
        if row is None:
            binding["unresolved"] = f"the variant {variant!r} has no row of the variation table {template.variation_table!r}"
            return binding
        binding["row"] = row["row"]
        binding["npc"] = row["npc"]
        if not row["npc"]:
            binding["unresolved"] = f"the row of the variant {variant!r} has no NPCId"
            return binding
        rule = PERSON_PRESENCE.get(slice_path)
        if rule is not None:
            condition = row.get(rule[0], "")
            if not condition and rule[0] == "SwapAchievementId":
                condition = template.swap_achievement  # No action value: the achievement of the slice stays.
            if not condition:
                binding["unresolved"] = f"the row of the variant {variant!r} has no {rule[0]}"
                return binding
            binding["presence"] = {"mode": rule[1], "condition": condition}
        elif template.swap_achievement:
            # A GatherableController that shows the child by an achievement, with no rule of this tool. A swap column with no action and no
            # such controller (npc_variantmaster) changes nothing that shows: the person is always there.
            binding["unresolved"] = f"the slice {slice_path} has a swap achievement and no presence rule (PERSON_PRESENCE)"
        return binding

    def person_children(layer: str, region: str, identifier: str, matrix: tuple[float, ...], template: Template, spawner: dict,
                        child_path: str, child: Template) -> None:
        """Adds the person child of a PrefabSpawner (stage 2.5c, live check L1): the client makes it at the load of the level, with the
        id of client 0x6160B60 from its place, its yaw, and its slice asset. The backend sends it no record."""
        world = multiply(matrix, template.matrices.get(spawner["entity"], IDENTITY))
        place = (world[3], world[7], world[11])
        if not all(math.isfinite(value) for value in place):
            raise Failure(f"{layer}: the person child of the placement {identifier} has a place that is not finite")
        groups = child_groups(*place, yaw_of(world), spawner["asset"][0])
        binding = person_binding(child_path, child, spawner["variant"])
        binding.update({"clientChild": True, "spawner": identifier})
        counts["persons"] += 1
        counts["personsUnresolved"] += 1 if "unresolved" in binding else 0
        counts["personSlotsWithTwoIds"] += 1 if len(groups) > 1 else 0
        if child.root is None:
            raise Failure(f"{child_path}: the slice of a person child has no root entity")
        for number, group in enumerate(groups):
            if group in person_groups:
                # Two person slots with one client id (one place, one yaw bucket, the same low bits of the slice asset): the client has
                # one entity for the id, and the backend cannot tell the two slots apart. The first slot keeps the id.
                exclude(CAUSE_PERSON_ID_TAKEN)
                continue
            person_groups.add(group)
            k = child.root ^ group
            bounds = []
            for bound in child.bounds:
                target = ((bound["entity"] ^ k) + bound["component"]) & 0xFFFFFFFFFFFFFFFF
                entry, _ = bound_entry(bound, f"{layer}: the person child of {identifier}", k, apply(world, bound["translation"]))
                entry["target"] = f"{target:016x}"
                text = json.dumps(entry, sort_keys=True)
                if (group, target) in references and references[(group, target)] != text:
                    raise Failure(f"two bounds have the reference {group:016x} {target:016x} with different content")
                references[(group, target)] = text
                bounds.append(entry)
            key = f"{identifier}/{spawner['entity']:016x}" + (f"~{number}" if number else "")
            kind = kind_of(child.bounds)
            counts["byKind"][kind] = counts["byKind"].get(kind, 0) + 1
            counts["bounds"] += len(bounds)
            objects.append({"key": key, "kind": kind, "slice": child_path, "asset": spawner["asset"][0].hex(),
                            "sub": spawner["asset"][1], "actor": (group.to_bytes(8, "little") + bytes(8)).hex(), "region": region,
                            "position": [round(value, 3) for value in place], "assembly": None, "bounds": bounds,
                            "person": dict(binding)})

    def template_entry(slice_path: str, asset: tuple[bytes, int] | None, depth: int = 0) -> int:
        """@return The index of the template of a slice in "templates" (with its server children)."""
        if slice_path in template_index:
            return template_index[slice_path]
        template = load_template(slice_path, child=True)
        if asset is None:
            asset = path_assets.get(slice_path)
        if asset is None:
            raise Failure(f"{slice_path}: the catalog of the game has no asset id of this slice")
        if template.root is None:
            raise Failure(f"{slice_path}: the slice of a server child has no root entity")
        index = len(template_list)
        template_index[slice_path] = index
        entry: dict = {"slice": slice_path, "asset": asset[0].hex(), "sub": asset[1], "kind": kind_of(template.bounds),
                       "root": f"{template.root:016x}", "slots": dict(template.slots), "buildableState": template.buildable_state,
                       "permissionRule": template.permission_rule, "assembly": assembly_entry(template, None), "bounds": [],
                       "children": []}
        template_list.append(entry)
        for bound in template.bounds:
            entry["bounds"].append(bound_entry(bound, f"the template {slice_path}", None, bound["translation"])[0])
        for spawner in template.spawner_list:
            # The children that the server makes with the child (the forge of a station: live check L4 of stage 2.5b).
            if spawner["client"] or not spawner["onInit"]:
                continue
            child_path = child_slice(spawner, f"the template {slice_path}")
            if child_path is None:
                continue
            child = load_template(child_path, child=True)
            if child.slots["position"] == 0 or depth >= 3:
                exclude(CAUSE_CHILD_NO_PLACE)
                continue
            matrix = template.matrices.get(spawner["entity"], IDENTITY)
            entry["children"].append({"template": template_entry(child_path, spawner["asset"], depth + 1),
                                      "position": [round(matrix[3], 4), round(matrix[7], 4), round(matrix[11], 4)],
                                      "rotation": [round(value, 6) for value in quaternion_of(matrix)]})
        return index

    for region in regions:
        for path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".capitals.json"):
            data = sources.read(path)
            if data is None:
                continue
            counts["layers"] += 1
            try:
                document = json.loads(data.decode("utf-8-sig"))
            except (ValueError, AttributeError) as error:
                raise Failure(f"{path}: the placements of the layer did not parse: {error}") from error
            # A layer with no list "Capitals" is not a layer with no placement (code review of stage 2.5a, finding INT-R5).
            if not isinstance(document, dict) or not isinstance(document.get("Capitals"), list):
                raise Failure(f"{path}: the layer has no list Capitals")
            placements = document["Capitals"]
            layer = path[len(REGIONS_FOLDER):-len(".capitals.json")]
            for placement in placements:
                if not isinstance(placement, dict):
                    raise Failure(f"{layer}: a placement is not an object")
                counts["placements"] += 1
                slice_name = str(placement.get("sliceName") or "")
                asset_text = str(placement.get("sliceAssetId") or "")
                plot = not slice_name.strip() or "<plot>" in slice_name.lower()
                # The position: an object with three finite numbers (code review of stage 2.5b, finding ST-R6: a missing position, a
                # list, or a missing axis became 0). A named placement with no such position is an incomplete source; a plot with
                # none is not an object.
                where = placement.get("worldPosition")
                if not isinstance(where, dict) or any(axis not in where for axis in "xyz"):
                    if plot:
                        exclude(CAUSE_PLOT)
                        continue
                    raise Failure(f"{layer}: the placement {placement.get('id')} of the slice {slice_name} has no position with x, y, "
                                  f"and z")
                try:
                    position = {axis: float(where[axis]) for axis in "xyz"}
                except (TypeError, ValueError) as error:
                    raise Failure(f"{layer}: a placement has a position that is not a number") from error
                if not all(math.isfinite(value) for value in position.values()):
                    raise Failure(f"{layer}: the placement {placement.get('id')} has a position that is not finite")
                if center is not None and radius is not None \
                        and math.hypot(position["x"] - center[0], position["y"] - center[1]) > radius:
                    continue
                asset = asset_of(placement)
                if plot and (asset is None or asset[0] == bytes(16)):
                    exclude(CAUSE_PLOT)
                    continue
                if asset is None:
                    # A named slice with an asset id of a wrong form is an incomplete source, not a plot (finding INT-R5).
                    raise Failure(f"{layer}: the placement {placement.get('id')} of the slice {slice_name} has no correct slice "
                                  f"asset id ({asset_text!r})")
                slice_path = spawner_path(placement, catalog, sources)
                if slice_path is None:
                    if plot:
                        exclude(CAUSE_PLOT)
                        continue
                    raise Failure(f"{layer}: the placement {placement.get('id')} names a slice that the game copy does not "
                                  f"have ({slice_name})")
                counts["placementsWithSlice"] += 1
                template = load_template(slice_path)
                identifier = str(placement.get("id", ""))
                try:
                    actor = actor_of(identifier)
                    # One form of the UUID for each key and each compare (code review of stage 2.5b, finding ST-R8: two spellings of
                    # one id were two keys, and the backend refused the file).
                    identifier = str(uuid.UUID(identifier))
                except ValueError as error:
                    raise Failure(f"{layer}: the placement id {identifier!r} is not a UUID") from error
                # One placement id in two layers: only an equal copy is one placement (finding INT-R6).
                normal = json.dumps([slice_path, position, placement.get("rotation") or {}, asset_text.lower()], sort_keys=True)
                if identifier in keys:
                    if keys[identifier] != normal:
                        raise Failure(f"the placement {identifier} has two different placements (a slice, a place, a rotation, or "
                                      f"an asset id)")
                    continue
                keys[identifier] = normal
                group = group_of(actor)
                matrix = placement_matrix(placement.get("rotation") or {}, position)
                # The server children of the placement: its stations and its camp (stage 2.5b).
                for spawner in template.spawner_list:
                    if spawner["client"]:
                        exclude(CAUSE_SPAWNER)
                        continue
                    child_path = child_slice(spawner, f"{layer}: the placement {identifier}")
                    if child_path is None:
                        continue
                    child = load_template(child_path)
                    if not child.is_station() and child.is_person():
                        if spawner["onInit"]:
                            person_children(layer, region, identifier, matrix, template, spawner, child_path, child)
                        else:
                            exclude(CAUSE_PERSON_NO_INIT)
                        continue
                    if not child.is_station():
                        exclude(CAUSE_SPAWNER)
                        continue
                    world = multiply(matrix, template.matrices.get(spawner["entity"], IDENTITY))
                    spawned.append({"key": f"{identifier}/{spawner['entity']:016x}", "parent": identifier, "parentActor": actor.hex(),
                                    "parentSlice": slice_path, "spawner": spawner["name"], "spawnOnInitialize": spawner["onInit"],
                                    "template": template_entry(child_path, spawner["asset"]), "region": region,
                                    "position": [round(world[3], 4), round(world[7], 4), round(world[11], 4)],
                                    "rotation": [round(value, 6) for value in quaternion_of(world)]})
                if not template.bounds:
                    exclude(CAUSE_NO_INTERACT)
                    continue
                k = template.root ^ group
                object_position = [round(position[axis], 3) for axis in "xyz"]
                bounds = []
                for bound in template.bounds:
                    instance = bound["entity"] ^ k
                    target = (instance + bound["component"]) & 0xFFFFFFFFFFFFFFFF
                    what = f"{layer}: the object {identifier}"
                    entry, _ = bound_entry(bound, what, k, apply(matrix, bound["translation"]))
                    entry["target"] = f"{target:016x}"
                    reference = (group, target)
                    text = json.dumps(entry, sort_keys=True)
                    if reference in references and references[reference] != text:
                        raise Failure(f"two bounds have the reference {group:016x} {target:016x} with different content")
                    references[reference] = text
                    bounds.append(entry)
                kind = kind_of(template.bounds)
                counts["byKind"][kind] = counts["byKind"].get(kind, 0) + 1
                counts["bounds"] += len(bounds)
                entry = {"key": identifier, "kind": kind, "slice": slice_path, "asset": asset[0].hex(), "sub": asset[1],
                         "actor": actor.hex(), "region": region, "position": object_position, "assembly": assembly_entry(template, k),
                         "bounds": bounds}
                if template.is_person():
                    # A direct person (stage 2.5c): its NPC component names it.
                    entry["person"] = person_binding(slice_path, template, None)
                    counts["personsDirect"] += 1
                    counts["personsUnresolved"] += 1 if "unresolved" in entry["person"] else 0
                objects.append(entry)
    fixtures: dict[str, int] = {}
    for name, fixture_path in FIXTURES.items():
        if not sources.has(fixture_path):
            raise Failure(f"the fixture {name} ({fixture_path}) has no source in the game copy")
        index = template_entry(fixture_path, None)
        if template_list[index]["slots"]["position"] == 0 or not template_list[index]["bounds"]:
            raise Failure(f"the fixture {name} ({fixture_path}) has no PositionInTheWorld component or no Interact bound")
        fixtures[name] = index
    loot_containers: dict[str, dict] = {}
    for name, container_path in LOOT_CONTAINERS.items():
        if not sources.has(container_path):
            raise Failure(f"the loot container {name} ({container_path}) has no source in the game copy")
        index = template_entry(container_path, None)
        options_of = {option_list[entry[0]]["name"] for bound in template_list[index]["bounds"] for entry in bound["options"]}
        if template_list[index]["kind"] != "lootDrop" or not {"TakeAllOption", "OpenStorageOption"} <= options_of:
            raise Failure(f"the loot container {name} ({container_path}) has no bound with TakeAllOption and OpenStorageOption")
        loot_containers[name] = {"template": index, **load_template(container_path, child=True).loot_container()}
    sources.check_complete()
    exclude(CAUSE_DISTRIBUTION, 0)
    # One journal bit for two achievements: the journal cannot tell the pages apart (finding INT-R4).
    bits: dict[int, str] = {}
    for identifier, bit in achievement_list:
        if bit in bits:
            raise Failure(f"the achievements {bits[bit]} and {identifier} have one AchievementIndex ({bit})")
        bits[bit] = identifier
    # Decision D1 of stage 3.7a (a decision of the orchestrator, option B): the station children of a key with no row get the _t2 variant.
    counts["stationOverrides"], counts["stationsWithNoVariant"] = station_overrides(spawned, template_list, sources.has,
                                                                                    lambda path: template_entry(path, None))
    counts["objects"] = len(objects)
    counts["options"] = len(option_list)
    counts["achievements"] = len(achievement_list)
    counts["templates"] = len(template_list)
    counts["spawned"] = len(spawned)
    counts["fixtures"] = len(fixtures)
    counts["lootContainers"] = len(loot_containers)
    counts["byKind"] = dict(sorted(counts["byKind"].items()))
    counts["byOption"] = dict(sorted(counts["byOption"].items()))
    counts["spawnedByKey"] = {}
    for entry in spawned:
        assembly = template_list[entry["template"]]["assembly"] or {}
        label = assembly.get("key", "") + ("" if assembly.get("resolved") else " (no row)")
        counts["spawnedByKey"][label] = counts["spawnedByKey"].get(label, 0) + 1
    counts["spawnedByKey"] = dict(sorted(counts["spawnedByKey"].items()))
    objects.sort(key=lambda entry: (entry["region"], entry["position"][0], entry["position"][1], entry["key"]))
    spawned.sort(key=lambda entry: (entry["region"], entry["position"][0], entry["position"][1], entry["key"]))
    counts["unresolvedNames"] = len(unresolved)
    return {"format": FORMAT, "world": WORLD, "counts": counts, "excluded": dict(sorted(excluded.items())),
            "unresolved": dict(sorted(unresolved.items())), "options": option_list, "achievements": achievement_list,
            "templates": template_list, "spawned": spawned, "fixtures": fixtures, "lootContainers": loot_containers, "objects": objects}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the interaction objects of the world as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="a region (r_+04_+02); more than one is possible")
    parser.add_argument("--radius", type=float, help="only the placements in this circle (meters)")
    parser.add_argument("--center", type=float, nargs=2, metavar=("X", "Y"), help="the center of the circle")
    parser.add_argument("--list", metavar="KIND", help="print the objects of a kind (or 'spawned': the server children), and "
                                                      "write no file")
    arguments = parser.parse_args()
    if arguments.radius is not None and not (math.isfinite(arguments.radius) and arguments.radius > 0):
        print("interactions: --radius must be a number above 0. No file was written.", file=sys.stderr)
        return 1
    center = tuple(arguments.center) if arguments.center else (DEFAULT_CENTER if arguments.radius is not None else None)
    target = Path(arguments.output)
    try:
        sources = Sources(game / "assets", WANTED)
        document = build_document(sources, arguments.region, center, arguments.radius)
        if not arguments.list:
            target.parent.mkdir(parents=True, exist_ok=True)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target.with_name(target.name + ".tmp")
            temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"interactions: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"interactions: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.list == "spawned":
        for entry in document["spawned"]:
            template = document["templates"][entry["template"]]
            assembly = template["assembly"] or {}
            x, y, z = entry["position"]
            print(f"{entry['key']}  {x:9.2f} {y:9.2f} {z:8.2f}  {template['kind']}  {assembly.get('key', '')}"
                  f"{'' if assembly.get('resolved') else ' (no row)'}  {template['slice'].rsplit('/', 1)[-1]}  "
                  f"({entry['spawner']} of {entry['parentSlice'].rsplit('/', 1)[-1]})")
    elif arguments.list:
        for entry in document["objects"]:
            if entry["kind"] != arguments.list:
                continue
            x, y, z = entry["position"]
            names = sorted({document["options"][index]["name"] for bound in entry["bounds"] for index, _ in bound["options"]})
            print(f"{entry['key']}  {x:9.2f} {y:9.2f} {z:8.2f}  actor {entry['actor'][:16]}  {entry['slice'].rsplit('/', 1)[-1]}  "
                  + ",".join(names))
    counts = document["counts"]
    print(f"{'the game copy' if arguments.list else target}: {counts['objects']} objects with {counts['bounds']} Interact "
          f"bounds and {counts['options']} options in {counts['regions']} regions ({counts['placements']} placements)")
    print("  kinds: " + ", ".join(f"{kind} {count}" for kind, count in counts["byKind"].items()))
    print(f"  server children: {counts['spawned']} stations and camps of {counts['templates']} templates; fixtures: "
          + ", ".join(document["fixtures"]))
    print("  station keys: " + ", ".join(f"{key} {count}" for key, count in counts["spawnedByKey"].items()))
    print(f"  persons (stage 2.5c): {counts['persons']} person children that the client makes ({counts['personSlotsWithTwoIds']} slots "
          f"with more than one id), {counts['personsDirect']} direct persons, {counts['personsUnresolved']} with no binding (the backend "
          f"refuses their use)")
    if counts.get("stationOverrides"):
        changes = sorted({f"{entry['override']['from']} -> {entry['override']['to']}" for entry in document["spawned"] if "override" in entry})
        print(f"  decision D1: {counts['stationOverrides']} station children get the _t2 variant ({', '.join(changes)})")
    for cause, count in document["excluded"].items():
        print(f"  not in the file: {count} ({cause})")
    if document["unresolved"]:
        print(f"  {counts['unresolvedNames']} name(s) of the data with no row, in {counts['unresolvedBounds']} bound(s) (the backend "
              f"refuses their use); the first: {next(iter(document['unresolved']))}")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

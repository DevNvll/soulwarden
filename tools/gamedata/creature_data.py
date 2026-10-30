#!/usr/bin/env python3
"""Write the creature data of the game as one JSON file.

The backend reads this file at its start (protocol notes 4.14). It has the usual spawn points
of the creatures in an area of the world, and for each kind of creature what the backend needs
for its record: the slice asset, the slot of each replicated component of the slice, the ids of
its vitals rows, the values for its health, and its name.

The chain of one spawn point (all files are in the pak files of the game copy):

  1. A layer of a region has a list of spawn points: `AiSpawnLocations` of
     regions/REGION/capitals/LAYER/LAYER.metadata (an object stream). An entry has the position,
     the CRC of the vitals row and of the category row, the level, and four flags.
  2. The same layer has a placement at the same position: LAYER.capitals.json. It names the
     spawner slice (`coatgen/.../pointspawner_*.dynamicslice`), and it has the rotation and the
     id of the placement.
  3. The spawner slice names an alias asset (or a slice), and it has the respawn values.
  4. The alias asset has one or more slices with a percent value. The asset catalog
     (`assetcatalog.catalog`) gives the path of each asset id.
  5. The slice of the creature has the entities and the components. A component with a
     replicated state has the field `m_replicationIndex`: its slot in a state record.
  6. The vitals tables give the base row of a vitals row (the family, the creature type, the
     health modifier), the level table gives the health of a level, and the modifier table
     gives the health modifier of a creature type.
  7. The action list component of the slice names a grid file and a controller file. They give
     the action table of the creature (see `read_action_table`): the index of each action and
     of its animation fragment in a movement state (protocol notes 4.14).

Which entries become a spawn point (a rule of this project):
  - The entry has `SpawnedByCoatlicue`, and it has not `IsOverride` and not `IsEncounter`: a
    usual spawn point, not an entry of an event.
  - The base row of its vitals row has a family: a creature, not a siege object.
  - A placement of its layer is at its position (0.06 m or less).
  - Its alias has a slice with no schedule, and that slice has a movement component and a
    vitals component.
  - The client shows that slice correctly with the record of the backend (the list "notShown"
    of creature_slices.json, the result of the picture tours of the slices).
  - Its position is in the area of the file. The default is the whole world: each region of the
    game copy that has a layer with spawn data. --radius selects a circle (with --center, or
    around the start place of a new character): 800 m around that place are the spawn points
    that the research counted and that the live checks of stage 3.4a used.

An entry of a layer can have no category id, or an id that the category table of this build
does not have (608 entries of the supported build: animals and creatures of the newer areas).
The client then shows the name of the vitals row on the name plate (live check of stage 3.4c):
such an entry is a spawn point, and its kind has no category name. A base vitals row with no
creature type has no row in the modifier table: its health modifier is 1.

What the tool leaves out, with the count of each cause in its output (--report FILE writes one
line for each entry):
  - An entry with no creature family: a siege weapon, a training dummy, a barrel, a totem.
  - An entry with no placement at its position in its layer: an object of a trap, creatures of
    an instance of the game (Outpost Rush, the catacombs), and creatures whose placement is a
    part of a larger slice. To find the placement, the tool would have to read each slice of
    each placement of the layer.
  - An entry whose slices have no movement component or no vitals component: an object that can
    take damage (a web, a cocoon, a nest), not a creature.
  - An entry whose slices the client does not show correctly (the list "notShown").

The pak entries of the supported build have the compression method 15 (Oodle), which Python
cannot read. Thus the tool uses the first of these sources that it has for each entry:

  1. The pak entry, when it is stored or deflate data.
  2. The pak entry with an Oodle library of this computer (SOULWARDEN_OODLE_LIB: a shared library
     with the function OodleLZ_Decompress, for example liboo2core.so).
  3. The pak entry with the Oodle helper of tools/research (oodle_loader.c), when
     SOULWARDEN_OODLE_DLL names an Oodle library for Windows (SOULWARDEN_OODLE_DEC: the helper).
  4. The file that tools/research/pakx.py extracted: SOULWARDEN_EXTRACT_DIR, default
     ~/Games/soulwarden/work/assets/extract.
When one entry that the tool needs has no source, the tool stops with one message and writes
no file: a file with a part of the spawn points would give a world with a part of its
creatures and no sign of the fault.

The same for the animation files of step 7. A file that a slice or an animation file names (the
grid file, the controller file, the fragment file and each file that it imports, each action
list of the grid) must be in the pak files and must be correct XML. If not, the tool stops with
one message and writes no file: each index after a part that is not there would be wrong, and
the backend would start a wrong action with no sign of the fault. Two cases of the game copy
are not a fault: a slice that names no grid file has no action table, and a grid file with no
action list gives an empty table.

The tool needs only the standard library of Python and the modules appearance_tables.py,
item_catalog.py, and slice_ids.py of this folder.

Output: see `build_document`. An id of 64 bits is a text of 16 hexadecimal digits.

Usage: creature_data.py [OUTPUT.json] [--radius METERS [--center X Y]] [--report FILE] [--no-attack FILE]
       creature_data.py [OUTPUT.json] --all-slices [--spawn-all]    a file for a picture tour of the slices
       creature_data.py --find TEXT...     print the kinds whose id or name has a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/creatures.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see above)
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import re
import struct
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ElementTree
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id, plain_stream  # noqa: E402
import creature_behavior  # noqa: E402
from collision_data import quaternion_matrix  # noqa: E402
import height_maps  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from nav_shapes import multiply  # noqa: E402
from nav_slices import matrix_of  # noqa: E402
from slice_ids import (COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, LOCAL_TRANSFORM_FIELD, NAME_FIELD,  # noqa: E402
                       NO_ENTITY, PARENT_FIELD, Element, component_id, find_field, id_value, parse)

OUTPUT_NAME = "creatures.json"
WORLD = "newworld_vitaeeterna"
REGIONS_FOLDER = f"sharedassets/coatlicue/{WORLD}/regions/"


# Stage DG2: the folder of the data sets of the own levels in the game data folder (one folder for each level).
LEVELS_FOLDER = "levels"


def use_level_terrain(terrain: str) -> None:
    """Stage DG2: reads the layers of the terrain folder of a map row (CoatlicueName: "coatlicue/NAME")."""
    if not terrain.startswith("coatlicue/") or terrain.count("/") != 1:
        raise Failure(f"the terrain project {terrain!r} of the map row is not a folder coatlicue/NAME")
    use_world(terrain.split("/", 1)[1])


def use_world(name: str) -> None:
    """Stage DG2: the terrain project whose layers the tool reads (`--level`: an own level of a map row, game_modes.level_terrain). The
    default is the main world. WANTED keeps each path below sharedassets/: the index of the pak files has the other terrain projects."""
    global WORLD, REGIONS_FOLDER
    WORLD = name
    REGIONS_FOLDER = f"sharedassets/coatlicue/{name}/regions/"
TABLES_FOLDER = "sharedassets/springboardentitites/datatables/"
CATALOG_PATH = "assetcatalog.catalog"
NAMES_PATH = "localization/en-us/javelindata_vitals.loc.xml"
# The paths that the tool can need: the index of the pak files keeps only these.
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, "coatgen/", "slices/", "sharedassets/", "animations/mannequin/",
          creature_behavior.CREATURE_ANIMATION_FOLDER, CATALOG_PATH, NAMES_PATH, "objects/characters/", "libs/timeline/")
# The start place of a new character (meters), and the radius of the default area.
DEFAULT_CENTER = (8880.0, 4250.0)
DEFAULT_RADIUS = 800.0
# Edge of the square of one region (meters): region r_+XX_+YY starts at XX * 2048, YY * 2048.
REGION_SIZE = 2048.0
# A placement is the placement of a spawn entry when it is this near (meters).
PLACEMENT_TOLERANCE = 0.06
OODLE = 15
STORED = 0
DEFLATE = 8

# Table types of the vitals tables.
VARIANT_TYPE = "VitalsLevelVariantData"
BASE_TYPE = "VitalsBaseData"
CATEGORY_TYPE = "VitalsCategoryData"
LEVEL_TYPE = "VitalsLevelData"
MODIFIER_TYPE = "VitalsModifierData"

# Type ids of the engine classes.
SLICE_COMPONENT_TYPE = uuid.UUID("afd304e4-1773-47c8-855a-8b622398934f")
ASSET_TYPE = uuid.UUID("77a19d40-8731-4d3c-9041-1b43047366a4")
SLICE_ASSET_TYPE = uuid.UUID("78802abf-9595-463a-8d2b-d022f906f9b1")
TEXT_TYPE = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")

# The component classes of a creature slice that have a replicated state: class id, name, and
# the type index of the state type in the supported client build (research files
# creatures.md and enemies-start-area.md, from the constructors of the state types). The
# backend sends the states of the first three. A class that is not here gets the type 0.
COMPONENT_CLASSES = {
    "30ed0ace-51dd-48b9-ba41-2fa6775cd106": ("ActionList", 11),
    "0c848755-1f41-4408-b2be-0a3627674f50": ("Vitals", 15),
    "98021a39-d35a-423d-bdcd-86aac61c6207": ("Marker", 5399),
    "1bd9d601-9de8-4a8b-b115-bb534eb6020c": ("DamageReceiver", 1528),
    "0b382af4-7946-4698-9223-206d1187b612": ("GatherableController", 12),
    "5c822d39-e3ca-73f0-7ab5-922457e9cc9c": ("StatusEffects", 4236),
    "dd164eef-7dbd-4f7c-bcef-b01a344a33f2": ("Ability", 185),
    "1154125c-dc3e-479f-b336-41b76e1963e7": ("AlignToTerrain", 2850),
    "504323cb-f59a-4c94-a32b-ddf174102c58": ("Magic", 2895),
    "a4cf5c98-487e-4db9-ac48-9698f965688d": ("Mana", 1652),
    "d353f7e8-4266-f749-19a1-aa2c3841fbca": ("DelayedEvent", 3139),
    "476b4b9f-9c00-4476-afa2-2d9f1179bad5": ("ItemManagement", 5437),
    "ba93b9ff-7a7e-4028-889e-12d418a11c71": ("Paperdoll", 3183),
    "b69045a3-a129-4334-bdfb-069c18cc84a2": ("Interact", 2930),
    "05e9d829-20e1-40b2-a8ba-ba70b6024921": ("Grit", 17),
    "167eed78-848f-48e4-acf6-141bf356ca7d": ("Stamina", 4297),
    "1e84749a-ae32-4ac1-8391-03955663f3ea": ("LookTargeting", 2443),
    "579d9f45-aa85-4407-ae43-b0c058a60e5a": ("StatMultiplierTable", 1525),
    "32f0711b-235e-43ee-9687-253ae1fcd68a": ("ReactionTracking", 1927),
    "171fa882-c8af-41df-bd19-09cbbfeb1c1c": ("TemporaryAffiliation", 3563),
    # The Projectile component of a projectile slice (stage 3.4f-A, research creature-ranged.md Q2: its state type 16).
    "5637158a-9ae1-41a7-a5c0-d07a76116eaf": ("Projectile", 16),
    # The PositionInTheWorld component (type 13; spells.py, interactions.py): the place of a stationary target (stage QG5c1).
    "6c2fc842-f451-4b45-9d7d-a6ee954df025": ("PositionInTheWorld", 13),
}
PROJECTILE_CLASS = uuid.UUID("5637158a-9ae1-41a7-a5c0-d07a76116eaf")
# The slice of the character of a player (stage 3.4f-A: the capsule of the target of a creature shot).
PLAYER_SLICE = "slices/player.dynamicslice"
# The sub id of the asset of a slice in the catalog (the slices of a projectile have it).
SLICE_SUB_ID = 2
MOVEMENT, VITALS, MARKER, PLACE = "ActionList", "Vitals", "Marker", "PositionInTheWorld"

# The result of the picture tours of the slices (tools/research/creature_tour.py, stages 3.4a
# and 3.4c): a file of this folder with two lists.
#   "spawnAction"  Slices whose body and name plate show only after the spawn action of the slice
#                  (the field m_spawnAction of its action list component). The file of the tool
#                  names that action for these slices, and the backend starts it for each
#                  creature that it makes.
#   "notShown"     Slices that the client does not show correctly with the record of the backend,
#                  with what the picture shows. A spawn point with only such slices is not in
#                  the file of the tool, until a later stage sends what the slice needs.
#   "targets"      Stage QG5c1: the slices of the stationary targets (a totem, a nest) whose record has a live check, with that
#                  check. Such a slice has a Vitals component and a PositionInTheWorld component and no movement component (no
#                  action list): the backend sends its place, its vitals, and its marker, and it never moves or attacks.
# A slice that is in no list shows its body and its name plate with no action of the server.
SLICE_LISTS_PATH = Path(__file__).resolve().parent / "creature_slices.json"


def read_slice_lists() -> tuple[frozenset[str], dict[str, str]]:
    """@return The slices that need their spawn action, and the slices that the client does not show."""
    try:
        lists = json.loads(SLICE_LISTS_PATH.read_text(encoding="utf-8"))
        spawn, hidden = lists["spawnAction"], lists["notShown"]
        if not isinstance(spawn, list) or not isinstance(hidden, dict) or not all(isinstance(path, str) for path in spawn):
            raise ValueError("the two lists do not have their form")
    except (OSError, ValueError, KeyError) as error:
        raise Failure(f"{SLICE_LISTS_PATH.name} (the result of the picture tours) is not there or not correct: {error}") from error
    return frozenset(spawn), {str(path): str(reason) for path, reason in hidden.items()}


def read_area_spell_list() -> dict[str, str]:
    """@return The spells of the ground areas whose slice record has a live check (stage 3.4f-C, the list areaSpells of the slice lists
    file): the tool writes the area attacks of these spells only (docs/systems/spells.md: a new spell entity needs a live check of its
    record first)."""
    try:
        listed = json.loads(SLICE_LISTS_PATH.read_text(encoding="utf-8"))["areaSpells"]
        if not isinstance(listed, dict) or not all(isinstance(name, str) and isinstance(proof, str) for name, proof in listed.items()):
            raise ValueError("the list areaSpells does not have its form")
    except (OSError, ValueError, KeyError) as error:
        raise Failure(f"{SLICE_LISTS_PATH.name} has no correct list areaSpells: {error}") from error
    return {name.lower(): proof for name, proof in listed.items()}


NO_FAMILY = "no creature family (a siege object)"
# Stage QG5c1: the causes of the member keptOut (the entries that are not usual spawn points).
KEPT_ENCOUNTER = "an entry of an encounter of the level data (IsEncounter: the encounter runner of stage QG5c2 and QG5c3)"
KEPT_OVERRIDE = "an override entry of the level data (IsOverride)"
KEPT_NOT_SPAWNED = "an entry that the level data does not spawn (SpawnedByCoatlicue false)"


def is_target(body: dict, targets: dict[str, str]) -> bool:
    """@return True for a slice of a stationary target (stage QG5c1): a slice of the list targets with a vitals component, a
    PositionInTheWorld component, and no movement component."""
    return body["path"] in targets and body["movement"] == 0 and body["vitals"] != 0 and body.get("place", 0) != 0


def read_target_list() -> dict[str, str]:
    """@return The slices of the stationary targets whose record has a live check (stage QG5c1, the list targets of the slice lists file:
    the slice path to its check). A file with no list: none."""
    try:
        listed = json.loads(SLICE_LISTS_PATH.read_text(encoding="utf-8")).get("targets", {})
        if not isinstance(listed, dict) or not all(isinstance(path, str) and isinstance(proof, str) for path, proof in listed.items()):
            raise ValueError("the list targets does not have its form")
    except (OSError, ValueError) as error:
        raise Failure(f"{SLICE_LISTS_PATH.name} has no correct list targets: {error}") from error
    return dict(listed)


def read_status_action_list() -> dict[str, str]:
    """@return The status actions whose marks and statuses have a live check (stage 3.4f-D, the list statusActions of the slice lists file:
    the action name to its check): the tool writes the status actions of these actions only. A file with no list: none."""
    try:
        listed = json.loads(SLICE_LISTS_PATH.read_text(encoding="utf-8")).get("statusActions", {})
        if not isinstance(listed, dict) or not all(isinstance(name, str) and isinstance(proof, str) for name, proof in listed.items()):
            raise ValueError("the list statusActions does not have its form")
    except (OSError, ValueError) as error:
        raise Failure(f"{SLICE_LISTS_PATH.name} has no correct list statusActions: {error}") from error
    return {name.lower(): proof for name, proof in listed.items()}


def read_tree_action_list() -> dict[str, str]:
    """@return The tree actions whose length and statuses have a live check (stage BT3, the list treeActions of the slice lists file: the
    action name to its check): the actions that a behavior tree of the backend plays by their name and that are not an attack, a shot, an
    area attack, or a status action (the phase transition of a boss). The tool writes the tree actions of these actions only. A file with no
    list: none."""
    try:
        listed = json.loads(SLICE_LISTS_PATH.read_text(encoding="utf-8")).get("treeActions", {})
        if not isinstance(listed, dict) or not all(isinstance(name, str) and isinstance(proof, str) for name, proof in listed.items()):
            raise ValueError("the list treeActions does not have its form")
    except (OSError, ValueError) as error:
        raise Failure(f"{SLICE_LISTS_PATH.name} has no correct list treeActions: {error}") from error
    return {name.lower(): proof for name, proof in listed.items()}


REPLICATION_FIELD = field_id("m_replicationIndex")
# Stage 3.4f-D: the low-health part of the Vitals component of a slice (the input LowHealth of the target curves of the AI).
LOW_HEALTH_FIELD = field_id("m_lowHealthPercentThreshold")
FACET_FIELDS = (field_id("m_clientFacetPtr"), field_id("m_serverFacetPtr"))
SPAWN_RADIUS_FIELD = field_id("m_sliceSpawnRadius")
GRID_FIELD = field_id("m_actionGridNew")
SPAWN_ACTION_FIELD = field_id("m_spawnAction")
# The capsule of the character of a slice (stage 3.3b-S3): the CharacterComponent and the QueryShapeCapsule of its m_shape
# (the class ids of the supported build), with m_Radius, m_Height, m_axis, and the m_shapeOffset of the component.
CHARACTER_CLASS = uuid.UUID("15407caa-4970-4d06-8b5c-612fba11bb45")
CAPSULE_CLASS = uuid.UUID("7495c65c-9193-4193-bbb2-de3343b9eb03")
SHAPE_FIELD = field_id("m_shape")
SHAPE_OFFSET_FIELD = field_id("m_shapeOffset")
CAPSULE_RADIUS_FIELD = field_id("m_Radius")
CAPSULE_HEIGHT_FIELD = field_id("m_Height")
CAPSULE_AXIS_FIELD = field_id("m_axis")
CONTROLLER_FIELD = field_id("m_controllerDefinition")
ASSET_PATH_FIELD = field_id("AssetPath")
# An asset field has the id (16 bytes), the sub id and 12 zero bytes, the type id (16 bytes), the
# size of the path text (8 bytes), and the path text of the asset.
ASSET_PATH_OFFSET = 56
SPAWN_LIST_FIELD = field_id("AiSpawnLocations")
ALIAS_FIELD = field_id("m_aliasAsset")
SLICE_FIELD = field_id("m_sliceAsset")
SLICES_FIELD = field_id("Slices")
ALIAS_SLICE_FIELD = field_id("Slice")
PERCENT_FIELD = field_id("Percent")
SCHEDULE_FIELD = field_id("Schedule")
ENTRY_FIELDS = {name: field_id(name) for name in ("VitalsId", "VitalsCategoryId", "VitalsLevel", "WorldPosition",
                                                  "SpawnedByCoatlicue", "IsAlias", "IsOverride", "IsEncounter")}
SPAWNER_FIELDS = {name: field_id(name) for name in ("m_minRespawnCooldown", "m_maxRespawnCooldown",
                                                    "m_respawnAfterDeath", "m_spawnOnInitialize",
                                                    "m_spawnOnlyOnce")}
# The respawn values of an area spawner (stage QG5c1; AreaSpawnerComponentServerFacet, the serializer of the client 0x3C32830: two
# doubles after m_liveCount, before m_ignoreCooldownOverride and the two cooldown flags). A rule of an agent: they are the respawn time
# in seconds, as the two cooldown values of a point spawner (the area spawners of the scorpion nests have 360 and 360, the point
# spawners of the same layer 360 and 360). The tool reads them only for a spawner with no point spawner cooldown.
AREA_RESPAWN_FIELDS = {name: field_id(name) for name in ("m_minRespawnRange", "m_maxRespawnRange")}
# The time constraint of a spawner (stage 2.2, research world-time-stage.md 5.1): the component TimeOfDayConstraintComponent of the
# spawner slice, with the flags of its server facet: m_night, m_dawn, m_day, m_dusk (the phases of the day in which the spawner makes
# its creature) and m_despawnOnTimeEnd (the creature leaves at the end of these phases). The mask of a spawn point has the phase
# numbers of the client as bits (bit 0 night, bit 1 dawn, bit 2 day, bit 3 dusk); a spawner with no constraint has each phase. The two
# cooldown flags of a constrained spawner (m_cooldownAfterDespawn, m_cooldownAfterConstraintFail) are a third value of the point (bit 0,
# bit 1): they are false on each point spawner of the supported build and true on one random encounter spawner (the Mirage Elemental,
# day only, 3,600 s).
TIME_CONSTRAINT_TYPE = uuid.UUID("e949f549-71a8-4914-861c-20a2ca6b75c4")
PHASE_FLAGS = ("m_night", "m_dawn", "m_day", "m_dusk")
PHASE_FIELDS = tuple(field_id(name) for name in PHASE_FLAGS)
DESPAWN_FIELD = field_id("m_despawnOnTimeEnd")
COOLDOWN_FIELDS = {name: field_id(name) for name in ("m_cooldownAfterDespawn", "m_cooldownAfterConstraintFail")}
EACH_PHASE = 0x0F
# The patrol of a spawner (research navigation.md, Q6). The spawner component names the entity of
# its patrol (m_patrolPathEntity); that entity has the component AIPatrol with the ids of its
# waypoint entities in their sequence (m_waypointIds) and, in its server facet, the policy of the
# route (m_pathTravesalPolicy: 0 LOOP, 1 BOUNCE, 2 RANDOM, 3 UNDEFINED, client reflection 0x3B6E9F0).
# A waypoint entity has a transform (m_worldTM: its place in the slice) and the component
# AIWaypoint with a task name, a wait time range in seconds (m_minDuration, m_maxDuration; -1: the
# creature stays), and m_matchAlignment (the creature takes the direction of the waypoint).
PATROL_TYPE = uuid.UUID("fa7fa996-f8c3-4cd0-8313-042fb6a66578")
WAYPOINT_TYPE = uuid.UUID("3d1526bf-ac9c-4a5d-8b0d-00abfccd19f1")
PATROL_PATH_FIELD = field_id("m_patrolPathEntity")
WAYPOINT_IDS_FIELD = field_id("m_waypointIds")
POLICY_FIELD = field_id("m_pathTravesalPolicy")
TASK_FIELD = field_id("m_taskName")
MIN_WAIT_FIELD = field_id("m_minDuration")
MAX_WAIT_FIELD = field_id("m_maxDuration")
ALIGNMENT_FIELD = field_id("m_matchAlignment")
WORLD_TRANSFORM_FIELD = field_id("m_worldTM")
# An entity reference with one of these values names no entity (the 11 references of the
# research that did not resolve have the first one).
NO_ENTITIES = frozenset({0x00000000FFFFFFFF, 0, 0xFFFFFFFFFFFFFFFF})
POLICIES = 4
# A waypoint farther than this from its spawn point is not a waypoint of a usual patrol (meters).
LONGEST_PATROL = 200.0
# The patrols of the retail data that the tool leaves out, each with its cause (the counts are in the
# member "routesLeftOut" of the file). Each other fault of a patrol source stops the tool.
ROUTE_NO_WAYPOINT_COMPONENT = "a waypoint of the patrol has no waypoint component or no transform"
ROUTE_EMPTY = "the patrol has no waypoint"
ROUTE_TOO_LONG = f"a waypoint is more than {LONGEST_PATROL:g} m from its spawn point"
RETAIL_ROUTE_CAUSES = (ROUTE_NO_WAYPOINT_COMPONENT, ROUTE_EMPTY, ROUTE_TOO_LONG)


def name_crc(text: str) -> int:
    """@return The id of a table row on the wire: the CRC-32 of its name in lower case."""
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def walk(element: Element):
    yield element
    for child in element.children:
        yield from walk(child)


def first(element: Element, wanted: int) -> Element | None:
    """@return The first element below `element` with this field number, at each depth."""
    for node in walk(element):
        if node.name == wanted:
            return node
    return None


def number(element: Element | None) -> int:
    """@return The whole number of a field: its own value, or the value of its one child."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    return int.from_bytes(element.value, "big") if element is not None and element.value is not None else 0


def real(element: Element | None) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        return 0.0
    return struct.unpack(">f", element.value)[0]


def double(element: Element | None) -> float:
    """@return The value of a field of 64 bits with a fraction (stage QG5c1: the respawn range of an area spawner). The tool stops for a
    value that is not finite or is negative: a cut field is a fault of the source, not the value 0."""
    if element is None or element.value is None:
        return 0.0
    value = struct.unpack(">d", element.value)[0] if len(element.value) == 8 else math.nan
    if not (math.isfinite(value) and value >= 0.0):
        raise Failure(f"a respawn range of an area spawner is not a finite number of 0 or more ({element.value.hex()})")
    return round(value, 3)


class Sources:
    """The pak files of the game copy, and the content of one entry from the first source that has it."""

    def __init__(self, assets: Path, wanted: tuple[str, ...] = WANTED) -> None:
        """@param wanted The start of each path that the tool can need: the index keeps only these entries."""
        if not assets.is_dir():
            raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
        paks = sorted(assets.rglob("*.pak"))
        if not paks:
            raise Failure(f"no game copy: {assets} has no pak file")
        self.entries: dict[str, tuple[Path, zipfile.ZipInfo]] = {}
        for pak in paks:
            try:
                with zipfile.ZipFile(pak) as archive:
                    for info in archive.infolist():
                        name = info.filename.lower().replace("\\", "/")
                        if name.startswith(wanted):
                            self.entries.setdefault(name, (pak, info))
            except (OSError, zipfile.BadZipFile) as error:
                raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
        home = Path.home()
        self.extracted = Path(os.environ.get("SOULWARDEN_EXTRACT_DIR") or home / "Games/soulwarden/work/assets/extract")
        self.windows_library = os.environ.get("SOULWARDEN_OODLE_DLL", "")
        self.helper = Path(os.environ.get("SOULWARDEN_OODLE_DEC") or home / "Games/soulwarden/work/assets/bin/oodle_dec")
        self.native = None
        library = os.environ.get("SOULWARDEN_OODLE_LIB", "")
        if library:
            try:
                self.native = ctypes.CDLL(library)
                self.native.OodleLZ_Decompress.restype = ctypes.c_ssize_t
                self.native.OodleLZ_Decompress.argtypes = [
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int, ctypes.c_int,
                    ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_void_p,
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int]
            except (OSError, AttributeError) as error:
                raise Failure(f"SOULWARDEN_OODLE_LIB ({library}) is not an Oodle library: {error}") from error
        self.used: dict[str, int] = {}
        self.absent: list[str] = []

    def has(self, path: str) -> bool:
        return path in self.entries

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return sorted(name for name in self.entries if name.startswith(prefix) and name.endswith(suffix))

    def _raw(self, pak: Path, info: zipfile.ZipInfo) -> bytes:
        with open(pak, "rb") as handle:
            handle.seek(info.header_offset)
            header = handle.read(30)
            name_size, extra_size = struct.unpack_from("<HH", header, 26)
            handle.seek(info.header_offset + 30 + name_size + extra_size)
            return handle.read(info.compress_size)

    def read(self, path: str) -> bytes | None:
        """@return The plain content of one entry. No value, with a mark, when the entry has no source."""
        entry = self.entries.get(path)
        if entry is None:
            self.absent.append(path)
            return None
        pak, info = entry
        try:
            if info.compress_type == STORED:
                return self._done("pak files", plain_stream(self._raw(pak, info)))
            if info.compress_type == DEFLATE:
                return self._done("pak files", plain_stream(zlib.decompress(self._raw(pak, info), -15)))
            if info.compress_type != OODLE:
                raise Failure(f"compression method {info.compress_type} is not supported")
            if self.native is not None:
                data = self._raw(pak, info)
                out = ctypes.create_string_buffer(info.file_size)
                size = self.native.OodleLZ_Decompress(data, len(data), out, info.file_size, 1, 1, 0, None, 0, None,
                                                      None, None, 0, 3)
                if size != info.file_size:
                    raise Failure(f"the Oodle library gave {size} bytes, not {info.file_size}")
                return self._done("pak files (Oodle library)", plain_stream(out.raw))
            if self.windows_library and self.helper.is_file():
                result = subprocess.run([str(self.helper), self.windows_library, str(info.file_size)],
                                        input=self._raw(pak, info), capture_output=True, check=False)
                if result.returncode != 0:
                    raise Failure(f"the Oodle helper failed: {result.stderr.decode(errors='replace').strip()}")
                return self._done("pak files (Oodle helper)", plain_stream(result.stdout))
            file = self.extracted / path
            if file.is_file():
                return self._done("extracted files", plain_stream(file.read_bytes()))
        except (Failure, OSError, zlib.error, struct.error) as error:
            raise Failure(f"{pak.name}: {path}: {error}") from error
        self.absent.append(path)
        return None

    def _done(self, source: str, data: bytes) -> bytes:
        self.used[source] = self.used.get(source, 0) + 1
        return data

    def stream(self, path: str) -> list[Element] | None:
        data = self.read(path)
        if data is None:
            return None
        try:
            return parse(data)
        except (Failure, struct.error, IndexError, ValueError) as error:
            raise Failure(f"{path}: not an object stream: {error}") from error

    def check_complete(self) -> None:
        if self.absent:
            names = sorted(set(self.absent))
            raise Failure(f"{len(names)} file(s) of the game copy have no source (first: {names[0]}). The pak entries "
                          f"have Oodle data: set SOULWARDEN_OODLE_LIB or SOULWARDEN_OODLE_DLL to an Oodle library, or extract "
                          f"the files with tools/research/pakx.py (SOULWARDEN_EXTRACT_DIR, default {self.extracted})")


class Catalog:
    """The asset catalog of the game: the path of each asset id.

    header := "RASC" version:u32 size:u32 u32 guids:u32 types:u32 folders:u32 files:u32 end:u32 count:u32
    record := guid:u32 sub:u32 u32 u32 type:u32 u32 size:u64 folder:u32 file:u32     (40 bytes, from offset 40)
    `guid` and `type` are indexes into two tables of 16 bytes for each entry, and `folder` and
    `file` are offsets into two blocks of texts that end with a zero byte.
    """

    def __init__(self, data: bytes) -> None:
        if len(data) < 40 or data[:4] != b"RASC":
            raise Failure("the asset catalog has no RASC header")
        _, _, size, _, guids, _, folders, files, _, count = struct.unpack_from("<4s9I", data)
        if size != len(data) or 40 + 40 * count > len(data):
            raise Failure("the asset catalog does not have the size of its header")
        self.paths: dict[tuple[bytes, int], str] = {}
        folder_names: dict[int, str | None] = {}
        view = memoryview(data)
        for guid, sub, _, _, _, _, _, folder, file in struct.iter_unpack("<6IQ2I", view[40:40 + 40 * count]):
            name = folder_names.get(folder, "")
            if name == "":
                start = folders + folder
                name = data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
                # Only the folders of slices and of alias assets: the catalog has more than 2 million records.
                name = name if name.startswith("slices/") or "/aliasassets/" in name or name.startswith("coatgen/") else None
                folder_names[folder] = name
            if name is None:
                continue
            start = files + file
            text = name + data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
            self.paths.setdefault((data[guids + 16 * guid:guids + 16 * guid + 16], sub), text)

    def path(self, asset: tuple[bytes, int] | None) -> str | None:
        return self.paths.get(asset) if asset is not None else None


def asset_id(element: Element | None) -> tuple[bytes, int] | None:
    """@return The asset id of an asset field (16 bytes and the sub id), or no value for an empty field."""
    if element is None or element.value is None or len(element.value) < 20:
        return None
    guid, sub = element.value[:16], int.from_bytes(element.value[16:20], "big")
    return None if guid == bytes(16) else (guid, sub)


def all_region_names(sources: "Sources") -> list[str]:
    """@return The name of each region of the world that has a layer with spawn data."""
    names = set()
    for path in sources.paths(REGIONS_FOLDER, ".metadata"):
        parts = path[len(REGIONS_FOLDER):].split("/")
        if len(parts) >= 3 and parts[1] == "capitals" and re.fullmatch(r"r_\+\d\d_\+\d\d", parts[0]):
            names.add(parts[0])
    return sorted(names)


def region_names(center: tuple[float, float], radius: float) -> list[str]:
    """@return The names of the regions whose square touches the circle."""
    names = []
    low_x, high_x = int((center[0] - radius) // REGION_SIZE), int((center[0] + radius) // REGION_SIZE)
    low_y, high_y = int((center[1] - radius) // REGION_SIZE), int((center[1] + radius) // REGION_SIZE)
    for x in range(max(low_x, 0), high_x + 1):
        for y in range(max(low_y, 0), high_y + 1):
            near_x = min(max(center[0], x * REGION_SIZE), (x + 1) * REGION_SIZE)
            near_y = min(max(center[1], y * REGION_SIZE), (y + 1) * REGION_SIZE)
            if math.hypot(near_x - center[0], near_y - center[1]) <= radius:
                names.append(f"r_+{x:02d}_+{y:02d}")
    return names


def read_tables(sources: Sources) -> dict:
    """@return The rows of the vitals tables, with the lower-case CRC of the row id as the key."""
    variants: dict[int, dict] = {}
    bases: dict[int, dict] = {}
    categories: dict[int, dict] = {}
    levels: dict[int, tuple[float, float, float]] = {}
    modifiers: dict[str, tuple[float, float]] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        if "vitals" not in path.rsplit("/", 1)[-1]:
            continue
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind not in (VARIANT_TYPE, BASE_TYPE, CATEGORY_TYPE, LEVEL_TYPE, MODIFIER_TYPE):
            continue
        try:
            sheet = Sheet(data)
            for row in range(sheet.row_count):
                if kind == VARIANT_TYPE:
                    name = str(sheet.cell(row, "VitalsID"))
                    variants.setdefault(name_crc(name), {"id": name, "base": str(sheet.cell(row, "BaseVitalsId")),
                                                         "level": int(sheet.cell(row, "Level") or 0),
                                                         "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == BASE_TYPE:
                    name = str(sheet.cell(row, "VitalsID"))
                    bases.setdefault(name_crc(name), {"id": name, "family": str(sheet.cell(row, "Family")),
                                                      "creatureType": str(sheet.cell(row, "CreatureType")),
                                                      "buffBuckets": str(sheet.cell(row, "BuffBuckets") or ""),
                                                      "healthMod": float(sheet.cell(row, "HealthMod") or 0.0),
                                                      "damageMod": float(sheet.cell(row, "DamageMod") or 0.0),
                                                      "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == CATEGORY_TYPE:
                    name = str(sheet.cell(row, "VitalsCategoryID"))
                    categories.setdefault(name_crc(name), {"id": name, "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == LEVEL_TYPE:
                    # The health, the base damage, and the gear score of a creature of the level.
                    levels[int(sheet.cell(row, "Level"))] = (float(sheet.cell(row, "BaseMaxHealth")),
                                                             float(sheet.cell(row, "BaseDamage") or 0.0),
                                                             float(sheet.cell(row, "GearScore") or 0.0))
                else:
                    modifiers[str(sheet.cell(row, "CategoryId"))] = (float(sheet.cell(row, "CategoryHealthMod")),
                                                                     float(sheet.cell(row, "CategoryDamageMod") or 0.0))
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    for what, rows in (("level variant vitals", variants), ("base vitals", bases), ("vitals category", categories),
                       ("vitals level", levels), ("vitals modifier", modifiers)):
        if not rows:
            raise Failure(f"the game copy has no {what} table")
    return {"variants": variants, "bases": bases, "categories": categories, "levels": levels, "modifiers": modifiers}


# Stage U3 (research creature-combat-feel.md Q5): the status effects of the buff buckets of a base vitals row that the reaction actions of
# the creature action lists test (the named conditions of baseai_reaction.actionlist): PromotedElite (IfHasStatusEffect
# AI_Elite_Unbreakable_Grit_Buff: the push of a full reaction x 0.15, of an additive reaction x 0) and the NoReaction part AI_NoReactions (no
# reaction and no additive reaction). The member of a kind is the mark with the value true.
BUFF_BUCKET_TYPE = "BuffBucketData"
KIND_MARK_STATUSES = {"AI_Elite_Unbreakable_Grit_Buff": "promotedElite", "AI_NoReactions": "noReactions"}


def read_buff_buckets(sources: Sources) -> dict[str, dict]:
    """@return The buff buckets of the game data (BuffBucketData): the bucket id to {"type": TableType (AND or OR), "buffs": [(id,
    BuffType), ...]}. No table: no bucket (each kind has no mark)."""
    buckets: dict[str, dict] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        if "buffbucket" not in path.rsplit("/", 1)[-1]:
            continue
        data = sources.read(path)
        if data is None or sheet_type(data) != BUFF_BUCKET_TYPE:
            continue
        try:
            sheet = Sheet(data)
            for row in range(sheet.row_count):
                buffs = []
                for index in range(1, 7):
                    buff = str(sheet.cell(row, f"Buff{index}") or "").strip()
                    if buff and buff != "0":
                        buffs.append((buff, str(sheet.cell(row, f"BuffType{index}") or "").strip().lower()))
                buckets.setdefault(str(sheet.cell(row, "BuffBucketId")),
                                   {"type": str(sheet.cell(row, "TableType") or "").strip().upper(), "buffs": buffs})
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    return buckets


def bucket_statuses(buckets: dict[str, dict], bucket: str, depth: int = 0) -> set[str]:
    """@return The status effects that a buff bucket gives for certain: each status of an AND bucket (and of its inner buckets), and of an
    OR bucket only the statuses that each of its choices gives (the server of the game rolls one choice; the backend has no roll: a status
    of only some choices is not certain; a decision of an agent)."""
    entry = buckets.get(bucket)
    if entry is None or depth > 6:
        return set()
    parts = []
    for buff, kind in entry["buffs"]:
        parts.append({buff} if kind == "statuseffect" else bucket_statuses(buckets, buff, depth + 1) if kind == "buffbucket" else set())
    if not parts:
        return set()
    if entry["type"] == "OR":
        return set.intersection(*parts)
    return set.union(*parts)


def kind_marks(base: dict, buckets: dict[str, dict]) -> list[str]:
    """@return The marks of KIND_MARK_STATUSES that the buff buckets of a base vitals row give for certain (bucket_statuses)."""
    statuses: set[str] = set()
    for bucket in base.get("buffBuckets", "").split(","):
        if bucket.strip():
            statuses |= bucket_statuses(buckets, bucket.strip())
    return sorted(KIND_MARK_STATUSES[status] for status in statuses if status in KIND_MARK_STATUSES)


def read_names(sources: Sources) -> dict[str, str]:
    """@return The English text of each key of the vitals text file. A key has no "@" here."""
    data = sources.read(NAMES_PATH)
    if data is None:
        return {}
    text = data.decode("utf-8", "replace")
    names = {}
    for key, value in re.findall(r'<string key="([^"]+)"[^>]*>([^<]*)</string>', text):
        names[key.lower()] = (value.replace("&amp;", "&").replace("&apos;", "'").replace("&quot;", '"')
                              .replace("&lt;", "<").replace("&gt;", ">"))
    return names


def read_spawn_entries(roots: list[Element]) -> list[dict]:
    entries = []
    for root in roots:
        spawn_list = first(root, SPAWN_LIST_FIELD)
        if spawn_list is None:
            continue
        for element in spawn_list.children:
            fields = {child.name: child for child in element.children}
            position = fields.get(ENTRY_FIELDS["WorldPosition"])
            if position is None or position.value is None or len(position.value) != 12:
                continue
            entries.append({
                "vitals": number(fields.get(ENTRY_FIELDS["VitalsId"])),
                "category": number(fields.get(ENTRY_FIELDS["VitalsCategoryId"])),
                "level": number(fields.get(ENTRY_FIELDS["VitalsLevel"])),
                "position": struct.unpack(">fff", position.value),
                "coatlicue": number(fields.get(ENTRY_FIELDS["SpawnedByCoatlicue"])) != 0,
                "override": number(fields.get(ENTRY_FIELDS["IsOverride"])) != 0,
                "encounter": number(fields.get(ENTRY_FIELDS["IsEncounter"])) != 0,
            })
    return entries


def spawner_path(placement: dict, catalog: Catalog, sources: Sources) -> str | None:
    """@return The pak path of the spawner slice of a placement."""
    name = str(placement.get("sliceName", "")).replace("\\", "/").lower()
    while "//" in name:
        name = name.replace("//", "/")
    if name.endswith(".slice"):
        name = name[:-6] + ".dynamicslice"
    if sources.has(name):
        return name
    match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", str(placement.get("sliceAssetId", "")))
    return catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16))) if match else None


def flag(element: Element | None, name: str) -> bool:
    """@return The value of a bool field of a time constraint. The tool stops for a field that is not there or that is not one byte of
    0 or 1: a cut flag is a fault of the source, not the value false."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    if element is None or element.value is None or len(element.value) != 1 or element.value[0] > 1:
        raise Failure(f"a time constraint of a spawner has no correct flag {name}")
    return element.value[0] == 1


def read_time_constraint(roots: list[Element], cooldowns: dict[str, Element]) -> tuple[int, bool, int]:
    """@return The phase mask, the despawn flag, and the cooldown flags (bit 0 m_cooldownAfterDespawn, bit 1
    m_cooldownAfterConstraintFail) of a spawner slice (stage 2.2): each phase, False, and 0 for a spawner with no time constraint. The
    tool stops for two constraints in one spawner and for a flag that is not correct."""
    constraints = [node for root in roots for node in walk(root) if node.type_id == TIME_CONSTRAINT_TYPE]
    if not constraints:
        return EACH_PHASE, False, 0
    if len(constraints) > 1:
        raise Failure("a spawner slice has more than one time constraint")
    constraint = constraints[0]
    mask = 0
    for bit, (name, field) in enumerate(zip(PHASE_FLAGS, PHASE_FIELDS)):
        if flag(first(constraint, field), name):
            mask |= 1 << bit
    leaves = flag(first(constraint, DESPAWN_FIELD), "m_despawnOnTimeEnd")
    # A cooldown flag that the spawner does not have is false (the defaults of the spawner component); one that is there must be a
    # correct bool.
    cooldown = 0
    for bit, name in enumerate(COOLDOWN_FIELDS):
        if cooldowns.get(name) is not None and flag(cooldowns[name], name):
            cooldown |= 1 << bit
    return mask, leaves, cooldown


def read_spawner(roots: list[Element]) -> dict:
    """@return The asset of the spawn (an alias or a slice), the respawn values, the patrol, and the time constraint of a spawner
    slice."""
    alias = None
    direct = None
    values: dict[str, Element] = {}
    cooldowns: dict[str, Element] = {}
    for root in roots:
        for node in walk(root):
            if node.name == ALIAS_FIELD and alias is None:
                alias = asset_id(node)
            elif node.name == SLICE_FIELD and direct is None:
                direct = asset_id(node)
            else:
                for name, wanted in SPAWNER_FIELDS.items():
                    if node.name == wanted:
                        values.setdefault(name, node)
                for name, wanted in COOLDOWN_FIELDS.items():
                    if node.name == wanted:
                        cooldowns.setdefault(name, node)
                for name, wanted in AREA_RESPAWN_FIELDS.items():
                    if node.name == wanted:
                        values.setdefault(name, node)
    patrol, cause = read_patrol(roots)
    phases, leaves, cooldown = read_time_constraint(roots, cooldowns)
    respawn = (real(values.get("m_minRespawnCooldown")), real(values.get("m_maxRespawnCooldown")))
    if "m_minRespawnCooldown" not in values and "m_maxRespawnCooldown" not in values:
        # An area spawner (stage QG5c1): its two respawn values are doubles (AREA_RESPAWN_FIELDS).
        respawn = (double(values.get("m_minRespawnRange")), double(values.get("m_maxRespawnRange")))
    return {"alias": alias, "slice": direct, "patrol": patrol, "patrolCause": cause, "phases": phases, "leavesAtPhaseEnd": leaves,
            "phaseCooldowns": cooldown,
            "respawnMin": respawn[0],
            "respawnMax": respawn[1],
            "respawnAfterDeath": number(values.get("m_respawnAfterDeath")) != 0,
            "spawnOnInitialize": number(values.get("m_spawnOnInitialize")) != 0,
            "spawnOnlyOnce": number(values.get("m_spawnOnlyOnce")) != 0}


def reference_of(element: Element | None) -> int | None:
    """@return The 64-bit id of an entity reference (the value is some levels below the field), or no value."""
    if element is None:
        return None
    return next((int.from_bytes(node.value, "big") for node in walk(element)
                 if node.value is not None and len(node.value) == 8), None)


def read_patrol(roots: list[Element]) -> tuple[dict | None, str]:
    """@return The patrol of a spawner slice in the coordinates of the slice, and a cause for a spawner with no
    patrol or with a patrol that is not complete ("": the spawner has a patrol, or names none).

    The patrol: {"policy": N, "points": [(matrix, task, min wait, max wait, alignment), ...]}, with the 3 x 4
    matrix of each waypoint in the slice (m_worldTM: the place relative to the root of the slice, nav_slices.py)."""
    entities: dict[int, dict] = {}
    reference = None
    for root in roots:
        for element in walk(root):
            if element.name == PATROL_PATH_FIELD and reference is None:
                reference = reference_of(element)
            if element.type_id != ENTITY_TYPE:
                continue
            identifier = id_value(next((child for child in element.children if child.name == ID_FIELD), None))
            listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
            if identifier is None or listed is None:
                continue
            entry = entities.setdefault(identifier, {})
            for component in listed.children:
                transform = find_field(component, WORLD_TRANSFORM_FIELD)
                if transform is not None and "matrix" not in entry:
                    entry["matrix"] = matrix_of(transform.value)
                if component.type_id == PATROL_TYPE:
                    ids = find_field(component, WAYPOINT_IDS_FIELD)
                    entry["patrol"] = ([reference_of(item) for item in ids.children] if ids is not None else [],
                                       number(first(component, POLICY_FIELD)))
                elif component.type_id == WAYPOINT_TYPE:
                    task = first(component, TASK_FIELD)
                    entry["waypoint"] = (task.value.decode("utf-8", "replace") if task is not None and task.value else "",
                                         real(first(component, MIN_WAIT_FIELD)), real(first(component, MAX_WAIT_FIELD)),
                                         number(first(component, ALIGNMENT_FIELD)) != 0)
    if reference is None or reference in NO_ENTITIES:
        return None, ""
    # A reference that the slice does not have, a transform with a number that is not finite, and
    # a policy that the client does not have are faults of the source: the tool stops (code review
    # of stage 3.4e, findings F5 and F6). The cases of the retail data that the tool leaves out are
    # the explicit causes of RETAIL_ROUTE_CAUSES.
    if reference not in entities:
        raise Failure("a spawner names a patrol entity that its slice does not have")
    owner = entities[reference]
    if "patrol" not in owner:
        raise Failure("the patrol entity of a spawner has no patrol component")
    ids, policy = owner["patrol"]
    if policy >= POLICIES:
        raise Failure(f"a patrol has the policy {policy}, which the client does not have")
    points = []
    for identifier in ids:
        if identifier is None or identifier in NO_ENTITIES:
            continue  # An empty place of the list (an invalid id of the game data).
        if identifier not in entities:
            raise Failure("a patrol names a waypoint entity that its slice does not have")
        waypoint = entities[identifier]
        if "waypoint" not in waypoint or waypoint.get("matrix") is None:
            return None, ROUTE_NO_WAYPOINT_COMPONENT
        if not all(math.isfinite(value) for value in waypoint["matrix"]):
            raise Failure("the transform of a waypoint has a number that is not finite")
        points.append((waypoint["matrix"], *waypoint["waypoint"]))
    if not points:
        return None, ROUTE_EMPTY
    return {"policy": policy, "points": points}, ""


def world_route(patrol: dict, placement: dict, spawn: tuple[float, float, float]) -> tuple[list | None, str]:
    """@return The route of a spawn point in world coordinates: [policy, [[x, y, z, yaw, min wait, max wait, task,
    alignment], ...]], or no value and a cause. The place of a waypoint is the matrix of the placement (its
    position, rotation, and scale: collision_data.quaternion_matrix) times the matrix of the waypoint in the slice;
    its yaw (degrees, 0 north, 90 west) is the angle of that matrix about the vertical axis."""
    try:
        placed = quaternion_matrix(placement.get("rotation") or {}, placement["worldPosition"],
                                   float(placement.get("scale", 1.0)))
    except (KeyError, TypeError, ValueError):
        return None, "the placement of the spawner has no position"
    points = []
    for matrix, task, low, high, alignment in patrol["points"]:
        world = multiply(placed, matrix)
        x, y, z = world[3], world[7], world[11]
        yaw = math.degrees(math.atan2(world[4], world[0])) % 360.0
        if not all(math.isfinite(value) for value in (*world, low, high, yaw)):
            raise Failure("a waypoint has a number that is not finite (its place, its rotation, or its wait)")
        if math.hypot(x - spawn[0], y - spawn[1]) > LONGEST_PATROL:
            return None, ROUTE_TOO_LONG
        points.append([round(x, 3), round(y, 3), round(z, 3), round(yaw, 2), round(low, 3), round(high, 3), task,
                       1 if alignment else 0])
    return [patrol["policy"], points], ""


def read_alias(roots: list[Element]) -> list[tuple[tuple[bytes, int], int]]:
    """@return The slices of an alias asset that have no schedule: (asset id, percent)."""
    slices = []
    for root in roots:
        for node in walk(root):
            if node.name != SLICES_FIELD:
                continue
            for element in node.children:
                fields = {child.name: child for child in element.children}
                asset = asset_id(fields.get(ALIAS_SLICE_FIELD))
                schedule = fields.get(SCHEDULE_FIELD)
                if asset is None or (schedule is not None and schedule.children):
                    continue
                slices.append((asset, number(fields.get(PERCENT_FIELD))))
    return slices


def game_path(text: str) -> str:
    return text.replace("\\", "/").lower().lstrip("/")


def read_xml(sources: Sources, path: str, named_by: str) -> ElementTree.Element | None:
    """@return The root of an XML file of the game that a slice or an animation file names (`named_by`: that
    file, for the message). No value, with a mark, when the pak entry of the file has no source on this
    computer: `Sources.check_complete` then stops the tool. A file that is not in the pak files, and a file
    that is not correct XML, stop the tool here: a part of an action table that is not there makes each later
    index wrong."""
    name = game_path(path)
    if not sources.has(name):
        raise Failure(f"{named_by} names the file {name or '(no name)'}, which is not in the pak files of the "
                      f"game copy")
    data = sources.read(name)
    if data is None:
        return None
    try:
        return ElementTree.fromstring(data.decode("utf-8-sig", "replace"))
    except ElementTree.ParseError as error:
        raise Failure(f"{name} (a file that {named_by} names) is not correct XML: {error}") from error


def read_tag_names(sources: Sources, path: str, named_by: str, depth: int = 0) -> list[str] | None:
    """@return The names of a tag definition file, in the sequence of the client: the names of each imported file
    first, then the tags of the file in their sequence (the tags of a group at the place of the group). A name
    that is in the list already is not added again. The position of a fragment name in the list of the fragment
    file of a controller is the fragment index on the wire (live proof: 117 fragments of three kinds). No value
    when a file has no source on this computer (see `read_xml`). An imported file that is not there stops the
    tool: without its names, each fragment index of the file that imports it would be too small."""
    if depth > 8:
        raise Failure(f"{game_path(path)}: the tag definition files import each other more than 8 times")
    root = read_xml(sources, path, named_by)
    if root is None:
        return None
    names: list[str] = []
    seen: set[str] = set()

    def add(name: str | None) -> None:
        if name and name.lower() not in seen:
            seen.add(name.lower())
            names.append(name)

    for imported in root.iter("Import"):
        for name in read_tag_names(sources, imported.get("filename", ""), game_path(path), depth + 1) or []:
            add(name)
    tags = root.find("Tags")
    for child in (tags if tags is not None else []):
        if child.tag == "Tag":
            add(child.get("name"))
        else:
            for tag in child.iter("Tag"):
                add(tag.get("name"))
    return names


def controller_path_of(roots: list[Element]) -> str:
    """@return The path of the animation controller file that the action list component of a slice names."""
    for root in roots:
        for node in walk(root):
            if node.name == CONTROLLER_FIELD:
                found = first(node, ASSET_PATH_FIELD)
                if found is not None and found.value is not None:
                    return found.value.decode("utf-8", "replace")
    return ""


# The reaction fragments of the five power levels (creature_behavior.REACTION_FRAGMENTS). Stage 3.3a: the knockdown of an ability plays
# power 4, a stagger power 2 (live check L6: the fragment file of the Grey Wolf has them).
REACTION_FRAGMENTS = creature_behavior.REACTION_FRAGMENTS


def read_reaction_fragments(sources: Sources, roots: list[Element]) -> list[int] | None:
    """@return The fragment index of each reaction of REACTION_FRAGMENTS (-1: the fragment file has none), or no value
    when the slice names no controller with a fragment file. The rule of the index is the rule of the action table."""
    controller_path = controller_path_of(roots)
    controller = read_xml(sources, controller_path, "the action list component of the slice") if controller_path else None
    fragment_file = controller.find("Fragments") if controller is not None else None
    if fragment_file is None:
        return None
    names = read_tag_names(sources, fragment_file.get("filename", ""), game_path(controller_path)) or []
    positions: dict[str, int] = {}
    for position, name in enumerate(names):
        positions.setdefault(name.lower(), position)
    return [positions.get(name.lower(), -1) for name in REACTION_FRAGMENTS]


def marker_statuses(action: ElementTree.Element) -> list[tuple[str, list[str]]]:
    """@return The statuses that an action definition gives to its creature at a mark (stage 3.4f-D): each IfThenElse whose IfCondition is
    one IfMannequinMarker and whose ThenActivity has ModifyStatusEffects with EffectsToApplyOnEnter: (the MarkerName, [EffectId, ...])."""
    found = []
    for branch in action.iter("IfThenElse"):
        condition = branch.find("IfCondition")
        then = branch.find("ThenActivity")
        if condition is None or then is None or len(list(condition)) != 1 or list(condition)[0].tag != "IfMannequinMarker":
            continue
        mark = list(condition)[0].get("MarkerName", "")
        statuses = [effect.get("EffectId", "") for modify in then.iter("ModifyStatusEffects") for listed in modify.iter("EffectsToApplyOnEnter")
                    for effect in listed.iter("EffectId") if effect.get("EffectId")]
        if mark and statuses:
            found.append((mark, statuses))
    return found


def direct_statuses(action: ElementTree.Element) -> tuple[list[str], list[str]]:
    """@return The statuses of the ModifyStatusEffects activities of an action definition that are not in a condition (stage BT3): (the
    EffectId of each EffectsToApplyOnEnter, the EffectId of each EffectsToRemoveOnExit). An activity inside an IfThenElse (a mark, a tag) is
    not one of them."""
    activities = action.find("Activities")
    enter: list[str] = []
    removed: list[str] = []
    for modify in (activities.findall("ModifyStatusEffects") if activities is not None else []):
        for listed in modify.iter("EffectsToApplyOnEnter"):
            enter.extend(effect.get("EffectId", "") for effect in listed.iter("EffectId") if effect.get("EffectId"))
        for listed in modify.iter("EffectsToRemoveOnExit"):
            removed.extend(effect.get("EffectId", "") for effect in listed.iter("EffectId") if effect.get("EffectId"))
    return enter, removed


def reaction_condition(element: ElementTree.Element | None, named: dict[str, ElementTree.Element], depth: int = 0) -> tuple:
    """@return The condition of a row of a reaction table (stage U3): ("always",), ("angles", [[min, max, tolerance], ...]) for an
    IfDamagedByAngle or an Or of them (degrees; client code 0x2C07B70: true when min - tolerance < angle < max + tolerance), ("tag",
    name) for an IfMannequinTag (the tags of the slice decide), or ("never", text) for a condition that the backend does not have (a
    blocked hit, a marker, a hit volume): such a row does not hold. An IfExternalCondition names a condition of the <Conditions> of the
    table file."""
    if element is None:
        return ("always",)
    if element.tag == "IfExternalCondition":
        target = named.get(element.get("ConditionName", ""))
        if target is None or depth > 4:
            return ("never", f"the condition {element.get('ConditionName', '')} is not in the table")
        return reaction_condition(target, named, depth + 1)
    if element.tag == "IfDamagedByAngle":
        values = [creature_behavior.strict_number(element.get(name)) for name in ("Min", "Max", "Tolerance")]
        if any(value is None for value in values):
            return ("never", "an angle that is not a finite number")
        return ("angles", [[creature_behavior.rounded(value) for value in values]])
    if element.tag == "Or" and len(list(element)) > 0 and all(child.tag == "IfDamagedByAngle" for child in element):
        ranges = []
        for child in element:
            found = reaction_condition(child, named, depth + 1)
            if found[0] != "angles":
                return found
            ranges.extend(found[1])
        return ("angles", ranges)
    if element.tag == "IfMannequinTag" and element.get("TagName"):
        return ("tag", element.get("TagName", ""))
    return ("never", element.tag)


def read_reaction_table(sources: Sources, path: str, named_by: str) -> list[list] | None:
    """@return The rows of a reaction table file (stage U3, research Q2; the client walks them in this order, client code 0x2B9F1C0):
    [power (-1: "*"), condition (reaction_condition), fragment name, AlignToDirection, AlignOffsetAngle] for each ReactionTableData of
    <Reactions>. A row with a power that is not "*" or a whole number from 0 to 9 does not hold (("never", ...)). No value: the action
    names a table file that is not in the pak files of the game copy (the retail data has such a name: the withered beetle); the slice
    then gets no table member, and the backend keeps the rule of user test 2 for it."""
    if not sources.has(game_path(path)):
        return None
    table = read_xml(sources, path, named_by)
    if table is None:
        return []
    named: dict[str, ElementTree.Element] = {}
    conditions = table.find("Conditions")
    for element in (list(conditions) if conditions is not None else []):
        if element.get("Name"):
            named.setdefault(element.get("Name", ""), element)
    rows = []
    reactions = table.find("Reactions")
    for row in (reactions.findall("ReactionTableData") if reactions is not None else []):
        text = row.get("PowerLevel", "").strip()
        children = list(row)
        condition = reaction_condition(children[0] if children else None, named)
        if len(children) > 1:
            condition = ("never", "more than one condition")
        if text == "*":
            power = -1
        elif text.isdigit() and int(text) <= 9:
            power = int(text)
        else:
            power, condition = -1, ("never", f"the power {text}")
        offset = creature_behavior.strict_number(row.get("AlignOffsetAngle"))
        rows.append([power, condition, row.get("Reaction", ""), row.get("AlignToDirection", "").strip().lower() == "true",
                     offset if offset is not None else 0.0])
    return rows


def reaction_motion(action: ElementTree.Element) -> list[list]:
    """@return The entries of the activity ReactionMotion of an action (stage U3, research Q5): [[name, Scale, PushBackScale], ...],
    "Default" first (Scale and PushBackScale 1.0 when the attribute is not there: client code 0x48A0690). No activity: the default only."""
    entries = [["Default", 1.0, 1.0]]
    for motion in action.iter("ReactionMotion"):
        for entry in motion:
            if entry.tag not in ("Default", "Conditional"):
                continue
            scale = creature_behavior.strict_number(entry.get("Scale")) if entry.get("Scale") is not None else 1.0
            push = creature_behavior.strict_number(entry.get("PushBackScale")) if entry.get("PushBackScale") is not None else 1.0
            if scale is None or push is None:
                raise Failure(f"the action {action.get('Name', '')} has a ReactionMotion value that is not a finite number")
            name = "Default" if entry.tag == "Default" else entry.get("Name", "")
            if entry.tag == "Default":
                entries[0] = [name, creature_behavior.rounded(scale), creature_behavior.rounded(push)]
            elif name:
                entries.append([name, creature_behavior.rounded(scale), creature_behavior.rounded(push)])
        break
    return entries


def read_reactions(sources: Sources, roots: list[Element], definitions: dict) -> dict | None:
    """@return The reaction facts of a slice (stage U3) for creature_behavior.Behavior.read: {"positions": fragment name (lower case) to
    its index in the fragment list of the controller, "actions": {name: {"index", "fragment", "fragmentIndex", "table" (rows of
    read_reaction_table), "motion" (reaction_motion), "exit" (ExitOffsetSeconds of IfFragmentDone), "scope" (the index of the first scope
    of the fragment of the action in the ScopeDefs of the controller; -1: not known)}}}. The scope of the additive fragment r_R0_F is
    "Additive" (scope 4 of the wolf controller): the client plays a record of that scope over the action of scope 0 (client code
    0x2A2ACD0). No controller or no definition: no value."""
    controller_path = controller_path_of(roots)
    controller = read_xml(sources, controller_path, "the action list component of the slice") if controller_path else None
    if controller is None or not definitions:
        return None
    fragment_file = controller.find("Fragments")
    names = read_tag_names(sources, fragment_file.get("filename", ""), game_path(controller_path)) if fragment_file is not None else []
    positions: dict[str, int] = {}
    for position, name in enumerate(names or []):
        positions.setdefault(name.lower(), position)
    scope_defs = controller.find("ScopeDefs")
    scope_index = {child.tag.lower(): index for index, child in enumerate(list(scope_defs) if scope_defs is not None else [])}
    fragment_defs = controller.find("FragmentDefs")
    fragment_scopes = {child.tag.lower(): child.get("scopes", "") for child in (list(fragment_defs) if fragment_defs is not None else [])}
    actions = {}
    for name, found in definitions.items():
        element = found["element"]
        table_path = ""
        for activity in element.iter("SetReactionFragment"):
            default = activity.find("Default")
            table_path = default.get("ReactionTablePath", "") if default is not None else ""
            break
        exit_offset = 0.0
        for done in element.iter("IfFragmentDone"):
            value = creature_behavior.strict_number(done.get("ExitOffsetSeconds"))
            exit_offset = max(value, 0.0) if value is not None else 0.0
            break
        scopes = [scope.strip().lower() for scope in fragment_scopes.get(found["fragment"].lower(), "").split("+") if scope.strip()]
        actions[name] = {"index": found["index"], "fragment": found["fragment"], "fragmentIndex": found["fragmentIndex"],
                         "table": (read_reaction_table(sources, table_path, found["list"]) if table_path else []),
                         "tablePath": game_path(table_path),
                         "motion": reaction_motion(element), "exit": exit_offset,
                         "scope": scope_index.get(scopes[0], -1) if scopes else -1}
    return {"positions": positions, "actions": actions}


def read_action_table(sources: Sources, roots: list[Element], markers: dict | None = None,
                      reaction_definitions: dict | None = None, effects: dict | None = None) -> list[list] | None:
    """@return The action table of a creature slice: [action name, fragment name, fragment index, server started]
    for each action, or no value when the slice names no grid file. The position of an action in the list is its
    index on the wire: the scope record of a movement state has the index plus 1. A file that the slice, the
    grid, or the controller names and that is not there, or that is not correct, stops the tool (`read_xml`).

    The rule of the client (builder at image RVA 0x4840D90, live proof for the tables of three kinds: 50, 62,
    and 19 actions): the table has the action lists of the grid file in their sequence, and the action
    definitions of each list in their sequence. An action definition is in the table only when the alias map
    of its list in the grid file names it. A definition with the mark Disabled is not in the table.
    """
    grid_path = ""
    controller_path = ""
    for root in roots:
        for node in walk(root):
            if node.name == GRID_FIELD and node.value is not None and len(node.value) > ASSET_PATH_OFFSET and not grid_path:
                grid_path = node.value[ASSET_PATH_OFFSET:].split(b"\0")[0].decode("utf-8", "replace")
            elif node.name == CONTROLLER_FIELD and not controller_path:
                found = first(node, ASSET_PATH_FIELD)
                if found is not None and found.value is not None:
                    controller_path = found.value.decode("utf-8", "replace")
    component = "the action list component of the slice"
    grid = read_xml(sources, grid_path, component) if grid_path else None
    if grid is None:
        return None
    fragments: dict[str, int] = {}
    controller = read_xml(sources, controller_path, component) if controller_path else None
    fragment_file = controller.find("Fragments") if controller is not None else None
    if fragment_file is not None:
        names = read_tag_names(sources, fragment_file.get("filename", ""), game_path(controller_path))
        for position, name in enumerate(names or []):
            fragments.setdefault(name.lower(), position)
    actions: list[list] = []
    # Only the lists of the element ActionLists: a grid file names action lists in other elements too.
    lists = grid.find("ActionLists")
    for entry in (lists.findall("ActionList") if lists is not None else []):
        listed = {action.get("ActionName", "").lower() for action in entry.iter("Action")}
        definitions = read_xml(sources, entry.get("Path", ""), game_path(grid_path))
        if definitions is None:
            # The list has no source on this computer: the tool stops at the end (check_complete).
            return None
        container = definitions.find("Actions")
        if container is None:
            # A list of the grid that is not an action list file: each index after it would be wrong.
            raise Failure(f"{game_path(entry.get('Path', ''))} (an action list of {game_path(grid_path)}) has no "
                          f"element Actions")
        for action in container.findall("Action"):
            name = action.get("Name", "")
            if action.get("Disabled", "").lower() in ("true", "1") or name.lower() not in listed:
                continue
            fragment = action.get("FragmentName", "")
            actions.append([name, fragment, fragments.get(fragment.lower(), -1),
                            action.get("ServerStarted", "").lower() in ("true", "1")])
            if (reaction_definitions is not None and name in (creature_behavior.REACTION_ACTION, creature_behavior.ADDITIVE_ACTION)
                    and name not in reaction_definitions and actions[-1][2] >= 0):
                # Stage U3: the definition of the first reaction action with a fragment (the action that the backend plays).
                reaction_definitions[name] = {"element": action, "index": len(actions) - 1, "fragment": fragment,
                                     "fragmentIndex": actions[-1][2], "list": game_path(entry.get("Path", ""))}
            if markers is not None:
                # Stage 3.4f-D: the statuses of the action at its marks (the status actions of creature_behavior.py).
                linked = marker_statuses(action)
                if linked:
                    markers.setdefault(name, linked)
            if effects is not None:
                # Stage BT3: the statuses of the action at its enter and at its exit (the tree actions of creature_behavior.py).
                direct = direct_statuses(action)
                if direct[0] or direct[1]:
                    effects.setdefault(name, direct)
    return actions


def vector_of(element: Element | None, what: str) -> list[float]:
    """@return The three floats of an AZ::Vector3 field (12 bytes, big-endian)."""
    if element is None or element.value is None or len(element.value) not in (12, 16):
        raise Failure(f"{what} is not a vector of three floats")
    values = struct.unpack(">3f", element.value[:12])
    if not all(math.isfinite(value) for value in values):
        raise Failure(f"{what} has a number that is not finite")
    return [round(value, 6) for value in values]


def read_capsule(path: str, roots: list[Element]) -> list | None:
    """@return The capsule of the CharacterComponent of a slice (stage 3.3b-S3): [radius, height, [axis], [offset]], or
    nothing when the slice has none. A radius or a height that is not a finite 32-bit float stops the tool. An axis of 0
    (some slices of the game have it) makes the capsule a sphere of its radius."""
    for root in roots:
        for element in walk(root):
            if element.type_id != CHARACTER_CLASS:
                continue
            shape = find_field(element, SHAPE_FIELD)
            capsule = next((node for node in walk(shape) if node.type_id == CAPSULE_CLASS), None) if shape is not None else None
            if capsule is None:
                return None
            values = []
            for field, label in ((CAPSULE_RADIUS_FIELD, "m_Radius"), (CAPSULE_HEIGHT_FIELD, "m_Height")):
                node = find_field(capsule, field)
                if node is None or node.value is None or len(node.value) != 4 or not math.isfinite(struct.unpack(">f", node.value)[0]):
                    raise Failure(f"{path}: the {label} of the capsule of the character is not a finite 32-bit float")
                values.append(round(struct.unpack(">f", node.value)[0], 6))
            if values[0] <= 0.0 or values[1] < 0.0:
                raise Failure(f"{path}: the capsule of the character has a radius or a height that is not a size")
            offset = find_field(element, SHAPE_OFFSET_FIELD)
            return [values[0], values[1], vector_of(find_field(capsule, CAPSULE_AXIS_FIELD), f"{path}: the axis of the capsule"),
                    vector_of(offset, f"{path}: the offset of the capsule") if offset is not None else [0.0, 0.0, 0.0]]
    return None


# The hit facts of a creature slice (stage 3.3b-S5): the two angles of its DamageReceiver (a hit from behind and a critical
# hit from behind: the angle between the front of the creature and the direction to the source of the hit, radians), and
# its head volume (the HitVolume with m_isHeadshot: a shape on a bone of its skeleton).
BEHIND_FIELD = field_id("m_isHitFromBehindMinAngle")
BEHIND_CRIT_FIELD = field_id("m_isHitByCritFromBehindMinAngle")
HEADSHOT_FIELD = field_id("m_isHeadshot")
TARGET_BONE_FIELD = field_id("m_targetBoneName")
VOLUME_CENTER_FIELD = field_id("m_center")
CDF_FIELD = field_id("m_cdfPath")
# The skeleton file of a character (CryEngine chunk file "CrCh" 0x746): the chunk CompiledBones (type 0x2000, version
# 0x800) has 32 reserved bytes and one record of 584 bytes for each bone: the controller id (4), two physics records (208),
# the mass (4), the matrices world to bone and bone to world (48 each: 3 rows of 4 floats, little-endian), the name (256
# bytes), the limb id, the parent offset, the child count, and the child offset (4 each).
SKELETON_CHUNK = 0x2000
SKELETON_VERSION = 0x800
BONE_RECORD = 584
BONE_TO_WORLD = 264
BONE_NAME = 312


def read_skeleton(data: bytes, path: str) -> dict[str, tuple]:
    """@return The bind pose of each bone of a skeleton file: name to (rotation rows, translation) in the model frame."""
    if len(data) < 16 or data[:4] != b"CrCh":
        raise Failure(f"{path} is not a chunk file (CrCh)")
    version, count, table = struct.unpack_from("<III", data, 4)
    if version != 0x746 or table + count * 16 > len(data):
        raise Failure(f"{path} has the chunk version {version:#x} or a chunk table out of the file")
    for index in range(count):
        kind, chunk_version, _, size, offset = struct.unpack_from("<HHiII", data, table + index * 16)
        if kind != SKELETON_CHUNK:
            continue
        if chunk_version != SKELETON_VERSION or size < 32 or (size - 32) % BONE_RECORD != 0 or offset + size > len(data):
            raise Failure(f"{path}: the bone chunk has the version {chunk_version:#x} or a size of {size} bytes")
        bones: dict[str, tuple] = {}
        for bone in range((size - 32) // BONE_RECORD):
            start = offset + 32 + bone * BONE_RECORD
            matrix = struct.unpack_from("<12f", data, start + BONE_TO_WORLD)
            name = data[start + BONE_NAME:start + BONE_NAME + 256].split(b"\0")[0].decode("utf-8", "replace")
            if not all(math.isfinite(value) for value in matrix):
                raise Failure(f"{path}: the bone {name} has a matrix with a number that is not finite")
            rows = (matrix[0:3], matrix[4:7], matrix[8:11])
            bones.setdefault(name.lower(), (rows, (matrix[3], matrix[7], matrix[11])))
        return bones
    raise Failure(f"{path} has no bone chunk")


def text_value(element: Element | None) -> str:
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    return element.value.decode("utf-8", "replace") if element is not None and element.value is not None else ""


def read_hit_facts(sources: Sources, path: str, roots: list[Element], skeletons: dict[str, dict | None],
                   counts: dict[str, int]) -> dict:
    """@return The members "behind" ([hit angle, critical angle]) and "head" ([[x, y, z], [x, y, z], radius]: the segment of
    the head capsule in the frame of the creature at its feet, x to its right, y to its front, z up; the bind pose of the
    bone of the volume) of a slice, each one only when the slice has it. A slice whose character file or skeleton is not in
    the game has no head (counts["noSkeleton"]); a file that is in the game and not correct stops the tool."""
    facts: dict = {}
    head_volume = None
    cdf_path = ""
    for root in roots:
        for element in walk(root):
            children = {child.name: child for child in element.children}
            if BEHIND_FIELD in children and BEHIND_CRIT_FIELD in children and "behind" not in facts:
                # Each angle field is exactly one 32-bit float (code review of stage 3.3b-S5, finding F12): a cut field is a fault
                # of the source, not the angle 0 (real() gives 0 for a value of a different size).
                for field in (BEHIND_FIELD, BEHIND_CRIT_FIELD):
                    value = children[field].value
                    if value is None or len(value) != 4:
                        raise Failure(f"{path}: an angle field of the DamageReceiver has {0 if value is None else len(value)} "
                                      f"bytes, not one 32-bit float")
                angles = [real(children[BEHIND_FIELD]), real(children[BEHIND_CRIT_FIELD])]
                if not all(math.isfinite(angle) and 0.0 <= angle <= math.pi + 1e-4 for angle in angles):
                    raise Failure(f"{path}: an angle of the DamageReceiver is not an angle of 0 to pi")
                facts["behind"] = [round(angle, 6) for angle in angles]
            flag = children.get(HEADSHOT_FIELD)
            if flag is not None and flag.value == b"\x01" and head_volume is None:
                head_volume = element
            if element.name == CDF_FIELD and not cdf_path:
                cdf_path = text_value(first(element, ASSET_PATH_FIELD)).strip().lower()
    if head_volume is None:
        return facts
    bone = text_value(find_field(head_volume, TARGET_BONE_FIELD)).strip().lower()
    center = vector_of(find_field(head_volume, VOLUME_CENTER_FIELD), f"{path}: the center of the head volume")
    shape = find_field(head_volume, SHAPE_FIELD)
    radius_node = first(shape, CAPSULE_RADIUS_FIELD) if shape is not None else None
    if radius_node is None:
        # A head volume of a different shape (a box of a plant): no head (counts["headOtherShape"]).
        counts["headOtherShape"] = counts.get("headOtherShape", 0) + 1
        return facts
    radius = real(radius_node)
    height_node = first(shape, CAPSULE_HEIGHT_FIELD)
    height = real(height_node) if height_node is not None else 0.0
    axis_node = first(shape, CAPSULE_AXIS_FIELD)
    axis = vector_of(axis_node, f"{path}: the axis of the head volume") if axis_node is not None else [0.0, 0.0, 0.0]
    if not (math.isfinite(radius) and radius > 0.0 and math.isfinite(height) and height >= 0.0):
        raise Failure(f"{path}: the head volume has a radius or a height that is not a size")
    if not cdf_path or not sources.has(cdf_path):
        counts["noSkeleton"] = counts.get("noSkeleton", 0) + 1
        return facts
    if cdf_path not in skeletons:
        skeleton = None
        # The field names a character definition (.cdf, XML with the skeleton file of its Model) or the skeleton itself (.chr).
        definition = sources.read(cdf_path) if not cdf_path.endswith(".chr") else None
        if cdf_path.endswith(".chr"):
            data = sources.read(cdf_path)
            skeleton = read_skeleton(data, cdf_path) if data is not None else None
        elif definition is not None:
            try:
                model = ElementTree.fromstring(definition.decode("utf-8", "replace")).find("Model")
            except ElementTree.ParseError as error:
                raise Failure(f"{cdf_path} is not a character definition: {error}") from error
            chr_path = (model.get("File", "") if model is not None else "").strip().replace("\\", "/").lower()
            if chr_path and sources.has(chr_path):
                data = sources.read(chr_path)
                skeleton = read_skeleton(data, chr_path) if data is not None else None
        skeletons[cdf_path] = skeleton
    skeleton = skeletons[cdf_path]
    if skeleton is None or bone not in skeleton:
        counts["noSkeleton"] = counts.get("noSkeleton", 0) + 1
        return facts
    rows, translation = skeleton[bone]
    def turn(vector):
        return [sum(rows[row][column] * vector[column] for column in range(3)) for row in range(3)]
    middle = [value + offset for value, offset in zip(turn(center), translation)]
    direction = turn(axis)
    size = math.sqrt(sum(value * value for value in direction))
    half = height * 0.5 / size if size > 1e-6 else 0.0
    a = [round(middle[index] - direction[index] * half, 4) for index in range(3)]
    b = [round(middle[index] + direction[index] * half, 4) for index in range(3)]
    facts["head"] = [a, b, round(radius, 4)]
    counts["heads"] = counts.get("heads", 0) + 1
    return facts


def read_creature_slice(path: str, asset: tuple[bytes, int], roots: list[Element]) -> dict:
    """@return The data of one creature slice: its root entity, and each component with a replicated state or a facet."""
    entities: list[tuple[int, int | None]] = []
    slots: list[list] = []
    components: list[list] = []
    radius = 0.0
    for root in roots:
        for element in walk(root):
            if element.name == SPAWN_RADIUS_FIELD and radius == 0.0:
                radius = real(element)
            if element.type_id != ENTITY_TYPE:
                continue
            listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
            entity_id = next((id_value(child) for child in element.children if child.name == ID_FIELD), None)
            name = next((child.value.decode("utf-8", "replace") for child in element.children
                         if child.name == NAME_FIELD and child.value is not None and child.type_id == TEXT_TYPE), "")
            # The outer entity of the file has the slice component: it is not an entity of an instance.
            if listed is None or entity_id is None or any(c.type_id == SLICE_COMPONENT_TYPE for c in listed.children):
                continue
            parent = None
            for component in listed.children:
                if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                    parent = id_value(find_field(component, PARENT_FIELD))
                class_name, state_type = COMPONENT_CLASSES.get(str(component.type_id), ("", 0))
                identifier = component_id(component)
                slot = number(find_field(component, REPLICATION_FIELD))
                faceted = any(find_field(component, facet) is not None for facet in FACET_FIELDS)
                if identifier is None or (slot == 0 and not faceted):
                    continue
                label = f"{name}/{class_name or component.type_id}"
                if slot != 0:
                    slots.append([slot, class_name or str(component.type_id), state_type])
                if faceted:
                    components.append([f"{entity_id:016X}", f"{identifier:016X}", label])
            entities.append((entity_id, parent))
    slice_roots = [entity for entity, parent in entities if parent == NO_ENTITY]
    if not slice_roots:
        raise Failure(f"{path}: the slice has no root entity")
    # The action that the original server starts when it makes the creature (a text field of the
    # action list component).
    spawn_action = ""
    for root in roots:
        found = first(root, SPAWN_ACTION_FIELD)
        if found is not None and found.value is not None and not spawn_action:
            spawn_action = found.value.decode("utf-8", "replace")
    slots.sort()
    numbers = [slot[0] for slot in slots]
    if len(set(numbers)) != len(numbers):
        raise Failure(f"{path}: two components of the slice have the same slot")
    by_name = {slot[1]: slot[0] for slot in reversed(slots)}
    entry = {"path": path, "asset": str(uuid.UUID(bytes=asset[0])).upper(), "sub": asset[1],
             "root": f"{slice_roots[-1]:016X}", "interestRadius": round(radius, 3),
             "movement": by_name.get(MOVEMENT, 0), "vitals": by_name.get(VITALS, 0), "marker": by_name.get(MARKER, 0),
             "slots": slots, "components": components, "gameSpawnAction": spawn_action}
    # Stage QG5c1: the slot of the PositionInTheWorld component (the place of a stationary target), only for a slice that has one.
    if by_name.get(PLACE, 0) != 0:
        entry["place"] = by_name[PLACE]
    capsule = read_capsule(path, roots)
    if capsule is not None:
        entry["capsule"] = capsule
    # Stage 3.4f-D: m_lowHealthPercentThreshold of the Vitals component (0.15 for the Inquisitor Cleric). A field that is not one finite 32-bit
    # float from 0 to 1 stops the tool.
    for root in roots:
        found = first(root, LOW_HEALTH_FIELD)
        if found is None or found.value is None:
            continue
        value = struct.unpack(">f", found.value)[0] if len(found.value) == 4 else math.nan
        if not (math.isfinite(value) and 0.0 <= value <= 1.0):
            raise Failure(f"{path}: m_lowHealthPercentThreshold is not a part of the maximum health ({found.value.hex()})")
        entry["lowHealth"] = round(value, 4)
        break
    # Stage BT2: the server trees that the slice names (research BT0: one in 500 slices, two in the 3 bombers): a binding key of the
    # backend. A member of a slice that names one or more.
    trees = creature_behavior.read_server_trees(roots, path)
    if trees:
        entry["serverTrees"] = trees
    return entry


def yaw_of(rotation: dict) -> float:
    """@return The angle about the vertical axis of a rotation quaternion, in degrees (0 to 360)."""
    x, y, z = (float(rotation.get(axis, 0.0)) for axis in "xyz")
    w = float(rotation.get("w", 1.0))
    return math.degrees(math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))) % 360.0


def build_document(sources: Sources, center: tuple[float, float] | None = None, radius: float | None = None,
                   all_slices: bool = False, details: list[dict] | None = None, spawn_all: bool = False,
                   no_attack: list[dict] | None = None, ground: bool = True, members: bool = True,
                   level: tuple[str, str] | None = None) -> dict:
    """@param center, radius The area of the file: a circle. No radius: the whole world.
    @param members True: the kinds of the creature members of the encounter recipes (stage QG5c2, encounter_recipes.py) are in the file
           with no spawn point ("member" names the member slice; the list "members" of the document).
    @param level Stage QG9e: the level and its terrain folder of an own level: the members are the creature members of the instance recipes
           of the mode roots of that level (encounter_recipes.Reader); a level with member kinds and no spawn point has a file too.
    @param all_slices True: the file also has the spawn points of the slices of the list "notShown" (a file for a
           picture tour of the slices, not for a start of the user).
    @param details When not null: gets one entry for each spawn entry that is left out (the cause, the layer,
           the position, the vitals row, and what the tool found for it).
    @param spawn_all With `all_slices`: each slice with a spawn action in its action table gets it in the file (a
           file for a tour that tests the spawn action).
    @param no_attack When not null: gets one entry for each kind that does not attack, with the cause.
    @param ground False: no read of the height maps, and each spawn point has no terrain height.

    Output:
    {"format": 2, "world": TEXT, "center": [x, y], "radius": METERS, "regions": [...],
     "counts": {...},
     "levels": [[level, health, damage, gear score], ...]   the values of each level (the level table)
     "slices": [{"path", "asset", "sub", "root", "interestRadius", "movement", "vitals", "marker",
                 "slots": [[slot, component name, state type], ...],
                 "components": [[entity id, component id, name], ...],
                 "actions": [[action name, fragment name, fragment index, server started], ...],
                 "reactions": [r_R0_F, ..., r_R4_F],   the fragment index of the reaction of each
                                                       power level (-1: none); only for a slice whose
                                                       fragment file has one of them (stage 3.3a)
                 "reactionLengths": [seconds x 5],     the length of the variant of each reaction fragment of
                                                       the slice (its end blend, else its animation; -1: not
                                                       known); only for a slice with one length (user test 2)
                 "spawnAction": NAME,                  only for a slice that needs its spawn action
                 "serverTrees": [NAME, ...],           the server trees that the slice names (stage BT2: m_behaviorTreeAssets of
                                                       its BehaviorTreeComponent, lower case, with no folder
                                                       sharedassets/ai/behaviortrees/ and no suffix: "wolf/wolf_base"); a
                                                       binding key of the backend; only for a slice that names one
                 "behavior": {"sight": [angle, distance], "combatSight": [angle, distance],
                              "leash": [at rest, in a fight], "speed": [walk, run]},
                 "tags": HEX,                          the tag state of its animation fragments (12 bytes)
                 "capsule": [radius, height, [x, y, z] axis, [x, y, z] offset],   the capsule of the
                                                       CharacterComponent of the slice (stage 3.3b-S3); only
                                                       for a slice that has one
                 "behind": [hit angle, critical angle],  the angles of its DamageReceiver (radians, stage
                                                       3.3b-S5): m_isHitFromBehindMinAngle and
                                                       m_isHitByCritFromBehindMinAngle; only for a slice that has them
                 "head": [[x, y, z], [x, y, z], radius],  the segment and the radius of its head volume (the
                                                       HitVolume with m_isHeadshot) in the frame of the creature at
                                                       its feet (x right, y front, z up), with the bind pose of its
                                                       bone from the skeleton of its character (stage 3.3b-S5); only
                                                       for a slice with a head volume and a skeleton in the game
                 "attacks": [[action, time, [[contact time, reach, damage factor, added damage,
                                              damage type, type number, type category, damage row, rune charge,
                                              attack type], ...]], ...]},
                                              (stage B4: the attack type of the damage row, Light, Heavy, Magic, or empty; also
                                              the member "attackType" of a ranged release and of an area strike)
                ...]
     "kinds":  [{"id", "vitals", "category", "vitalsId", "categoryId", "name", "vitalsName",
                 "family", "creatureType", "health", "damage", "level", "bodies": [[slice, percent], ...],
                 "points": N}, ...]
     "spawnPoints": [[placement id, kind, level, x, y, z, yaw, respawnMin, respawnMax, ground, route, phases, leaves, cooldowns],
                     ...]}
    Format 4 (stage 2.2): "phases" of a spawn point is the phase mask of the time constraint of its spawner (bit 0 night, bit 1 dawn,
    bit 2 day, bit 3 dusk; 15: no constraint), "leaves" is its m_despawnOnTimeEnd (true: the creature leaves at the end of its
    phases), and "cooldowns" its two cooldown flags (bit 0 m_cooldownAfterDespawn, bit 1 m_cooldownAfterConstraintFail; 0 with no
    constraint).
    The members of the behavior (creature_behavior.py, protocol notes 4.17): "behavior" has the two
    vision profiles (degrees and meters), the two leash distances (meters), and the two speeds (m/s)
    of the slice; "attacks" has one entry for each attack: the index of its action in "actions", its
    time in seconds, and its contacts. "damage" of a kind is its damage as a part of the base damage
    of its level (the damage modifier of its base row times the modifier of its creature type).
    "ground" of a spawn point is the terrain height at its place (null: the world has no height map
    there).
    `slices`: "movement", "vitals", and "marker" are the slots of these three components (0: the
    slice has none), "components" are the components with a facet (the targets of client
    messages), and "actions" is the action table: the position of an action is its index in a
    movement state, and a fragment index of -1 is "no fragment" (an empty list: the slice names
    no grid file, or its grid file has no action list). "spawnAction" is the action that the
    backend starts when it makes a creature with this slice: the client shows the body of such
    a slice only after that action. `kinds`: "vitalsId" and "categoryId" are
    the ids on the wire, "health" is the health of the kind as a part of the health of its level
    (the health modifier of its base row times the modifier of its creature type), "level" is
    the level of the most spawn points, and "bodies" are the slices of its alias (index into
    `slices`). A spawn point has an index into `kinds`.
    """
    tables = read_tables(sources)
    buckets = read_buff_buckets(sources)
    names = read_names(sources)
    catalog_data = sources.read(CATALOG_PATH)
    if catalog_data is None:
        sources.check_complete()
    catalog = Catalog(catalog_data)
    whole_world = radius is None
    center = center or DEFAULT_CENTER
    needs_spawn_action, not_shown = read_slice_lists()
    # Stage QG5c1: the slices of the stationary targets with a live check of their record (the list targets).
    targets = read_target_list()
    # The ranged attacks (stage 3.4f-A): the attacks of the ranged attack data with their index on the wire, and the slices of
    # the projectiles.
    ranged_attacks = read_ranged_attacks(sources)
    projectile_list: list[list] = []
    projectile_index: dict[str, int | None] = {}
    by_path = slice_assets(catalog)

    def projectile_of(path: str) -> int | str | None:
        """@return The index of a projectile slice in the list of the file, or the cause with no slice: the catalog has no asset of the
        path (an authored reference that the game copy does not have), or the slice has no Projectile component or no flight values.
        An asset of the catalog with no source stops the tool (code review of stage 3.4f-A, finding CR-R5: a broken source)."""
        if path not in projectile_index:
            entry = None
            asset = by_path.get(path)
            if asset is None:
                projectile_index[path] = "the projectile slice is not in the asset catalog of the game copy"
                return projectile_index[path]
            roots = sources.stream(path) if sources.has(path) else None
            if roots is None:
                sources.check_complete()
                raise Failure(f"the projectile slice {path} of the asset catalog has no source")
            entry = read_projectile(path, asset, roots, flight_of(path, roots))
            projectile_index[path] = "the projectile slice has no Projectile component or no flight values"
            if entry is not None:
                projectile_index[path] = len(projectile_list)
                projectile_list.append(entry)
        return projectile_index[path]

    # The spells of the casts of the creatures (stage 3.4f-C): the ground areas of the backend (spells.area_shape).
    import spells as spell_tool  # noqa: E402 (the module imports this one: the import is here, after the load)
    behavior = creature_behavior.Behavior(sources, TABLES_FOLDER, ranged_attacks, projectile_of,
                                          spell_tool.AreaSpells(sources, read_area_spell_list()), read_status_action_list())
    # Stage BT3: the tree actions of the list treeActions (the phase transitions of the boss trees).
    behavior.tree_actions = read_tree_action_list()
    regions = all_region_names(sources) if whole_world else region_names(center, radius)
    counts = {"entries": 0, "inArea": 0, "usual": 0, "creatures": 0}
    left_out: dict[str, int] = {}
    slices: list[dict] = []
    slice_index: dict[str, int] = {}
    skeletons: dict[str, dict | None] = {}
    hit_counts: dict[str, int] = {}
    spawners: dict[str, dict | None] = {}
    aliases: dict[tuple[bytes, int], list] = {}
    kinds: dict[tuple, dict] = {}
    points: list[list] = []
    placed: dict[str, int] = {}
    # The patrol route of each spawn point (an index into `routes`, or no value), in the sequence of `points`.
    routes: list[list] = []
    point_routes: list[int | None] = []
    # The phase mask, the despawn flag, and the cooldown flags of each spawn point (stage 2.2, format 4).
    point_phases: list[tuple[int, bool, int]] = []
    route_left_out: dict[str, int] = {}
    # The entry and the layer of the loop below, for the report of an entry that is left out.
    current: dict = {}
    # Stage QG5c1: the names of the vitals rows and of the spawn categories of the entries that the file does not have, with the cause (the
    # member keptOut; the support manifest names it in the reason of a kill task). The rank decides between two causes of one name.
    kept_out: dict[str, tuple[int, str]] = {}

    def keep_out(entry: dict, rank: int, cause: str) -> None:
        variant = tables["variants"].get(entry["vitals"])
        base = tables["bases"].get(name_crc(variant["base"])) if variant else tables["bases"].get(entry["vitals"])
        category = tables["categories"].get(entry["category"])
        for name in ((variant or {}).get("id"), (base or {}).get("id"), (category or {}).get("id")):
            if name and (name.lower() not in kept_out or kept_out[name.lower()][0] > rank):
                kept_out[name.lower()] = (rank, cause)

    def leave_out(cause: str, **found) -> None:
        left_out[cause] = left_out.get(cause, 0) + 1
        if details is not None:
            entry = current.get("entry") or {}
            variant, base = current.get("variant"), current.get("base")
            details.append({"cause": cause, "layer": current.get("layer", ""),
                            "position": [round(value, 3) for value in entry.get("position", ())],
                            "vitals": (variant or base or {}).get("id", f"{entry.get('vitals', 0):08x}"),
                            "category": f"{entry.get('category', 0):08x}", "level": entry.get("level", 0),
                            "family": (base or {}).get("family", ""), "creatureType": (base or {}).get("creatureType", ""),
                            **found})

    def body_of(asset: tuple[bytes, int]) -> int | None:
        path = catalog.path(asset)
        if path is None:
            return None
        if path not in slice_index:
            roots = sources.stream(path)
            if roots is None:
                return None
            slice_index[path] = len(slices)
            slices.append(read_creature_slice(path, asset, roots))
            markers: dict = {}
            definitions: dict = {}
            effects: dict = {}
            try:
                slices[-1]["actions"] = read_action_table(sources, roots, markers, definitions, effects) or []
                reactions = read_reaction_fragments(sources, roots)
                if reactions is not None and any(index >= 0 for index in reactions):
                    slices[-1]["reactions"] = reactions
                # Stage U3: the reaction tables, the push scales, and the scope of the additive reaction.
                reaction_facts = read_reactions(sources, roots, definitions)
                # The hit facts (stage 3.3b-S5): the angles of a hit from behind and the head volume.
                slices[-1].update(read_hit_facts(sources, path, roots, skeletons, hit_counts))
            except Failure as error:
                raise Failure(f"the action table of {path} is not complete: {error}") from error
            # What the behavior of a creature with this slice needs: its sense and speed values, the
            # tags of its animation fragments, and its attacks.
            try:
                slices[-1].update(behavior.read(roots, controller_path_of(roots), slices[-1]["actions"], path, markers, reaction_facts, effects))
            except Failure as error:
                raise Failure(f"the behavior data of {path} is not complete: {error}") from error
        return slice_index[path]

    def bodies_of(spawner: dict) -> list[tuple[int, int]]:
        if spawner["alias"] is None:
            direct = body_of(spawner["slice"]) if spawner["slice"] is not None else None
            return [(direct, 100)] if direct is not None else []
        if spawner["alias"] not in aliases:
            path = catalog.path(spawner["alias"])
            roots = sources.stream(path) if path is not None else None
            alternatives = []
            for asset, percent in (read_alias(roots) if roots is not None else []):
                body = body_of(asset)
                if body is not None:
                    alternatives.append((body, percent))
            aliases[spawner["alias"]] = alternatives
        return aliases[spawner["alias"]]

    # Stage QG5c2: the levels of the IsEncounter entries of each vitals row (the level of a creature member with no level of its own).
    encounter_levels: dict[int, dict[int, int]] = {}
    for region in regions:
        for metadata_path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".metadata"):
            roots = sources.stream(metadata_path)
            if roots is None:
                continue
            entries = read_spawn_entries(roots)
            counts["entries"] += len(entries)
            current["layer"] = metadata_path[len(REGIONS_FOLDER):-len(".metadata")]
            selected = []
            for entry in entries:
                if not whole_world \
                        and math.hypot(entry["position"][0] - center[0], entry["position"][1] - center[1]) > radius:
                    continue
                counts["inArea"] += 1
                if not entry["coatlicue"] or entry["override"] or entry["encounter"]:
                    if entry["encounter"]:
                        keep_out(entry, 1, KEPT_ENCOUNTER)
                        encounter_levels.setdefault(entry["vitals"], {}).setdefault(entry["level"], 0)
                        encounter_levels[entry["vitals"]][entry["level"]] += 1
                    elif entry["override"]:
                        keep_out(entry, 2, KEPT_OVERRIDE)
                    else:
                        keep_out(entry, 3, KEPT_NOT_SPAWNED)
                    continue
                counts["usual"] += 1
                variant = tables["variants"].get(entry["vitals"])
                base = tables["bases"].get(name_crc(variant["base"])) if variant else tables["bases"].get(entry["vitals"])
                current.update(entry=entry, variant=variant, base=base)
                familyless = base is None or not base["family"]
                if familyless and (base is None or not targets):
                    leave_out(NO_FAMILY)
                    continue
                if not familyless:
                    counts["creatures"] += 1
                # Stage QG5c1: an entry with no family is a stationary target only when its placement names a slice of the list targets
                # (below); else it keeps the cause of before.
                selected.append((entry, variant, base, familyless))
            if not selected:
                continue
            capitals = sources.read(metadata_path[:-len(".metadata")] + ".capitals.json")
            if capitals is None:
                continue
            try:
                placements = json.loads(capitals.decode("utf-8-sig")).get("Capitals") or []
            except (ValueError, AttributeError) as error:
                raise Failure(f"{metadata_path}: the placements of the layer did not parse: {error}") from error
            for entry, variant, base, familyless in selected:
                current.update(entry=entry, variant=variant, base=base)
                placement = None
                nearest = None
                for candidate in placements:
                    where = candidate.get("worldPosition") or {}
                    distance = math.dist(entry["position"], tuple(float(where.get(axis, 0.0)) for axis in "xyz"))
                    if distance <= PLACEMENT_TOLERANCE:
                        placement = candidate
                        break
                    if nearest is None or distance < nearest[0]:
                        nearest = (distance, candidate)
                if placement is None:
                    if familyless:
                        leave_out(NO_FAMILY)
                        continue
                    leave_out("no placement at the position", placements=len(placements),
                              nearest=round(nearest[0], 3) if nearest else None,
                              nearestSlice=str(nearest[1].get("sliceName", "")) if nearest else "")
                    continue
                path = spawner_path(placement, catalog, sources)
                if path is None:
                    leave_out(NO_FAMILY if familyless else "no spawner slice", placement=str(placement.get("sliceName", "")))
                    continue
                if path not in spawners:
                    roots = sources.stream(path)
                    spawners[path] = read_spawner(roots) if roots is not None else None
                spawner = spawners[path]
                if spawner is None:
                    if familyless:
                        leave_out(NO_FAMILY)
                    continue
                candidates = bodies_of(spawner)
                # Stage QG5c1: the slices of the list targets with a vitals component, a PositionInTheWorld component, and no movement
                # component: a stationary target (a totem, a nest). Its kind has only such bodies.
                stationary = [(body, percent) for body, percent in candidates if is_target(slices[body], targets)]
                if familyless and not stationary:
                    leave_out(NO_FAMILY)
                    continue
                if stationary:
                    bodies = stationary
                else:
                    bodies = [(body, percent) for body, percent in candidates
                              if slices[body]["movement"] != 0 and slices[body]["vitals"] != 0]
                    if not bodies:
                        leave_out("no slice with a movement component and a vitals component", spawner=path,
                                  slices=[slices[body]["path"] for body, _ in candidates],
                                  alias=catalog.path(spawner["alias"]) or "")
                        continue
                    shown = [(body, percent) for body, percent in bodies
                             if all_slices or slices[body]["path"] not in not_shown]
                    if not shown:
                        leave_out("the client does not show its slice correctly ("
                                  + slices[bodies[0][0]]["path"].rsplit("/", 1)[-1] + ")", spawner=path,
                                  slices=[slices[body]["path"] for body, _ in bodies])
                        keep_out(entry, 0, "the client does not show its slice " + slices[bodies[0][0]]["path"].rsplit("/", 1)[-1]
                                 + " (the list notShown of the picture tours)")
                        continue
                    bodies = shown
                # An entry can have no category id, or an id that the category table of this build does
                # not have. The client then shows the name of the vitals row on the name plate (live
                # check of stage 3.4c): the kind has no category name, and the id of the entry.
                category = tables["categories"].get(entry["category"])
                if category is None:
                    counts["noCategoryRow"] = counts.get("noCategoryRow", 0) + 1
                # A base row with no creature type has no row in the modifier table: the modifier 1.
                modifier = tables["modifiers"].get(base["creatureType"]) if base["creatureType"] else (1.0, 1.0)
                if modifier is None:
                    leave_out("no modifier row of the creature type", spawner=path,
                              slices=[slices[body]["path"] for body, _ in bodies])
                    continue
                vitals_text = variant["id"] if variant else base["id"]
                key = (vitals_text, category["id"] if category else entry["category"], tuple(bodies))
                kind = kinds.get(key)
                if kind is None:
                    vitals_name = names.get((variant or base)["name"].lstrip("@").lower(), "")
                    kind = kinds[key] = {
                        "vitals": vitals_text, "category": category["id"] if category else "",
                        "vitalsId": entry["vitals"], "categoryId": entry["category"],
                        "name": names.get(category["name"].lstrip("@").lower(), "") if category else vitals_name,
                        "vitalsName": vitals_name,
                        "family": base["family"], "creatureType": base["creatureType"],
                        "marks": kind_marks(base, buckets),
                        "health": round(base["healthMod"] * modifier[0], 6),
                        "damage": round(base["damageMod"] * modifier[1], 6),
                        "bodies": [list(body) for body in bodies], "levels": {}, "index": len(kinds)}
                # The metadata of a layer has one entry for each slice of an alias with more than
                # one slice: the entries of one placement are one spawn point.
                identifier = str(placement.get("id", ""))
                if identifier in placed:
                    leave_out("a second entry of one placement" if placed[identifier] == kind["index"]
                              else "a second entry of one placement, with a different kind")
                    continue
                placed[identifier] = kind["index"]
                kind["levels"][entry["level"]] = kind["levels"].get(entry["level"], 0) + 1
                points.append([identifier, kind["index"], entry["level"],
                               round(entry["position"][0], 4), round(entry["position"][1], 4),
                               round(entry["position"][2], 4), round(yaw_of(placement.get("rotation") or {}), 2),
                               spawner["respawnMin"], spawner["respawnMax"]])
                # The patrol route of the spawner, in world coordinates (the transforms of the placement). A stationary target has no
                # route (stage QG5c1: it never moves).
                route, cause = (world_route(spawner["patrol"], placement, entry["position"]) if spawner["patrol"] and not stationary
                                else (None, "" if stationary else spawner["patrolCause"]))
                if cause:
                    if cause not in RETAIL_ROUTE_CAUSES:
                        raise Failure(f"the patrol of the spawner of placement {placement.get('id', '')} is not complete: {cause}")
                    route_left_out[cause] = route_left_out.get(cause, 0) + 1
                point_routes.append(len(routes) if route is not None else None)
                point_phases.append((spawner["phases"], spawner["leavesAtPhaseEnd"], spawner["phaseCooldowns"]))
                if route is not None:
                    routes.append(route)
    # Stage QG5c2 (docs/systems/encounters.md): a kind for each creature member of the encounter recipes of the quests, with no spawn
    # point. Its vitals row is m_vitalsTableRowId of the AIVariantProviderComponent of the member slice, its category m_vitalsCategoryTableRowId
    # (none: the client shows the name of the vitals row), its level m_vitalsLevel, else the Level of its level variant row, else the level
    # of the most IsEncounter entries of its vitals row; its bodies are the slice or the alias of the spawner of the member slice, with the
    # rules of a spawn point (a movement and a vitals component, the list notShown).
    member_kinds: list[dict] = []
    member_list: list[list] = []
    incomplete: set[int] = set()
    if members:
        import encounter_recipes  # noqa: E402 (encounter_recipes imports this module)
        reader, recipes, _ = encounter_recipes.read_recipes(sources, catalog, level)
        # Stage QG5c5: a group member (more than one creature spawner in its slice: the portal waves of the Darkness, the boat landings)
        # has one kind for each part, with the key "SLICE#N" (N from 0, in the order of the slice); the member slice keeps the kind of
        # its first part. A stationary member (the core shells of the monoliths: a Vitals component and no movement) takes its body by the
        # rule of a stationary target (is_target: the list targets).
        entries = []
        for member in reader.creature_members(recipes):
            entries.append((member["slice"], member))
            for number, part in enumerate(member.get("group") or []):
                entries.append((f"{member['slice']}#{number}", {**part, "slice": member["slice"]}))
        for path, member in entries:
            vitals_crc = name_crc(member["vitals"])
            variant = tables["variants"].get(vitals_crc)
            base = tables["bases"].get(name_crc(variant["base"])) if variant else tables["bases"].get(vitals_crc)
            category = tables["categories"].get(name_crc(member["category"])) if member["category"] else None
            cause = ""
            # Stage QG5c5: a stationary member (a core shell) keeps the rule of a stationary target: no family is needed (stage QG5c1: a
            # totem has no family).
            if base is None or (not base["family"] and not member.get("stationary")):
                cause = "its vitals row has no base row with a family"
            spawner = {"alias": member["_asset"], "slice": None} if member["bodyField"] == "alias" \
                else {"alias": None, "slice": member["_asset"]}
            bodies = []
            if not cause:
                before = len(slices)
                try:
                    candidates = bodies_of(spawner)
                    if any(body in incomplete for body, _ in candidates):
                        raise Failure("an earlier member found the data of its slice not complete")
                    bodies = [(body, percent) for body, percent in candidates
                              if (slices[body]["movement"] != 0 and slices[body]["vitals"] != 0)
                              or (member.get("stationary") and is_target(slices[body], targets))]
                except Failure as error:
                    # A member does not stop the tool: the encounter runner refuses its recipe (the rules of a spawn point stay strict).
                    # The slices that the read added are not complete: no member uses them.
                    incomplete.update(range(before, len(slices)))
                    cause = f"the data of its slice is not complete: {error}"
            if not cause and not bodies:
                cause = "no slice of its spawner has a movement component and a vitals component"
            shown = [(body, percent) for body, percent in bodies if all_slices or slices[body]["path"] not in not_shown]
            if not cause and not shown:
                cause = ("the client does not show its slice " + slices[bodies[0][0]]["path"].rsplit("/", 1)[-1]
                         + " (the list notShown of the picture tours)")
            modifier = None
            if not cause:
                modifier = tables["modifiers"].get(base["creatureType"]) if base["creatureType"] else (1.0, 1.0)
                if modifier is None:
                    cause = "no modifier row of the creature type"
            levels = encounter_levels.get(vitals_crc, {})
            level = member["level"] or (variant["level"] if variant else 0) \
                or (max(levels.items(), key=lambda item: (item[1], -item[0]))[0] if levels else 0)
            if not cause and level not in tables["levels"]:
                cause = f"no row of the level table for its level {level}"
            if cause:
                member_list.append([path, -1, cause])
                continue
            vitals_name = names.get((variant or base)["name"].lstrip("@").lower(), "")
            member_kinds.append({
                "id": path.rsplit("/", 1)[-1].replace(".dynamicslice", ""), "vitals": variant["id"] if variant else base["id"],
                "category": category["id"] if category else "", "vitalsId": vitals_crc,
                "categoryId": name_crc(member["category"]) if member["category"] else 0,
                "name": names.get(category["name"].lstrip("@").lower(), "") if category else vitals_name, "vitalsName": vitals_name,
                "family": base["family"], "creatureType": base["creatureType"], "marks": kind_marks(base, buckets),
                "health": round(base["healthMod"] * modifier[0], 6), "damage": round(base["damageMod"] * modifier[1], 6),
                "level": level, "bodies": [list(body) for body in shown], "member": path})
            member_list.append([path, len(member_kinds) - 1, ""])
        counts["memberKinds"] = len(member_kinds)
        counts["membersLeftOut"] = sum(1 for _, index, _ in member_list if index < 0)
    sources.check_complete()
    # Stage QG9e: a file of an own level with the kinds of the creature members of its instance recipes and no spawn point (QuestLiang: its
    # creatures are the members of its encounter) is a file too.
    if not points and not member_kinds:
        area = "the world" if whole_world else f"the area (center {center[0]:g} {center[1]:g}, radius {radius:g} m)"
        raise Failure(f"{area} has no spawn point of a creature in the regions {', '.join(regions) or 'none'}")
    if "" in placed:
        raise Failure("a placement of a spawn point has no id")
    # The kinds in the sequence of their first spawn point. A kind id is the vitals row; when
    # more than one kind has the row, the kinds after the one with the most spawn points get a number.
    kind_list = sorted((kind for kind in kinds.values() if kind["levels"]), key=lambda kind: kind["index"])
    kind_number = {kind["index"]: position for position, kind in enumerate(kind_list)}
    for point, route in zip(points, point_routes):
        point[1] = kind_number[point[1]]
    by_row: dict[str, list[dict]] = {}
    for kind in kind_list:
        by_row.setdefault(kind["vitals"], []).append(kind)
    for row, group in by_row.items():
        group.sort(key=lambda kind: (-sum(kind["levels"].values()), kind["index"]))
        for position, kind in enumerate(group):
            kind["id"] = row if position == 0 else f"{row}:{position + 1}"
    used = sorted({body for kind in kind_list for body, _ in kind["bodies"]} | {body for kind in member_kinds for body, _ in kind["bodies"]})
    renumber = {old: new for new, old in enumerate(used)}
    document_kinds = []
    for kind in kind_list:
        total = sum(kind["levels"].values())
        level = max(kind["levels"].items(), key=lambda item: (item[1], -item[0]))[0]
        document_kinds.append({"id": kind["id"], "vitals": kind["vitals"], "category": kind["category"],
                               "vitalsId": kind["vitalsId"], "categoryId": kind["categoryId"],
                               "name": kind["name"], "vitalsName": kind["vitalsName"], "family": kind["family"],
                               "creatureType": kind["creatureType"], "health": kind["health"],
                               "damage": kind["damage"], "level": level,
                               "bodies": [[renumber[body], percent] for body, percent in kind["bodies"]],
                               "points": total})
        # Stage U3 (research Q5): the marks of the buff buckets of the base vitals row, only when they hold.
        for mark in kind["marks"]:
            document_kinds[-1][mark] = True
    point_kinds = len(document_kinds)
    for kind in member_kinds:
        document_kinds.append({key: value for key, value in kind.items() if key != "marks"} | {
            "bodies": [[renumber[body], percent] for body, percent in kind["bodies"]], "points": 0})
        for mark in kind["marks"]:
            document_kinds[-1][mark] = True
    for entry in member_list:
        if entry[1] >= 0:
            entry[1] += point_kinds
    # The spawn action of the game data goes into the file only for a slice of the list: the
    # backend starts it for these, and it starts no action for a slice that shows without it.
    for body in used:
        entry = slices[body]
        name = entry.pop("gameSpawnAction", "")
        has_action = any(action[0] == name and action[2] >= 0 for action in entry["actions"])
        if entry["path"] in needs_spawn_action or (all_slices and spawn_all and has_action):
            if not has_action:
                raise Failure(f"{entry['path']} is in the list spawnAction of {SLICE_LISTS_PATH.name}, and its action table "
                              f"has no action \"{name}\" with a fragment")
            entry["spawnAction"] = name
    counts["slicesWithSpawnAction"] = sum(1 for body in used if "spawnAction" in slices[body])
    counts["slicesWithNoActions"] = sum(1 for body in used if not slices[body]["actions"])
    # The cause of a slice with no attack is for the report of the tool, not for the file.
    causes = {body: slices[body].pop("noAttack", "") for body in used}
    # The value Enable_Wander of the slice is for the counts of the tool, not for the file.
    wander_flags = {body: slices[body].get("behavior", {}).pop("wanderFlag", None) for body in used}
    counts["slicesByEnableWander"] = {"true": sum(1 for flag in wander_flags.values() if flag is True),
                                      "false": sum(1 for flag in wander_flags.values() if flag is False),
                                      "notInTheSlice": sum(1 for body in used if "behavior" in slices[body]
                                                           and wander_flags[body] is None)}
    counts["slicesWithBehavior"] = sum(1 for body in used if "behavior" in slices[body])
    # Stage BT2: the server trees of the slices (a binding key of the backend).
    counts["slicesWithServerTree"] = sum(1 for body in used if "serverTrees" in slices[body])
    counts["serverTrees"] = len({name for body in used for name in slices[body].get("serverTrees", [])})
    counts["slicesWithAttacks"] = sum(1 for body in used if slices[body].get("attacks"))
    counts["slicesWithTags"] = sum(1 for body in used if "tags" in slices[body])
    # The hit facts (stage 3.3b-S5).
    counts["slicesWithBehindAngles"] = sum(1 for body in used if "behind" in slices[body])
    counts["slicesWithHead"] = sum(1 for body in used if "head" in slices[body])
    counts["headsWithNoSkeleton"] = hit_counts.get("noSkeleton", 0)
    counts["headsWithOtherShape"] = hit_counts.get("headOtherShape", 0)
    # A kind attacks when one of its slices has an attack (a melee attack, or from stage 3.4f-A the basic shot of the backend:
    # creature_behavior.BASIC_RANGED_ACTIONS, the cast of a caster from stage 3.4f-B) and the values for the way to a target.
    quiet = 0
    quiet_points = 0

    def shoots(body: dict) -> bool:
        # From stage 3.4f-C an area attack is an attack too (the Undead Shaman: only the lightning strike).
        return bool(body.get("areas")) or any(body["actions"][attack[0]][0] in creature_behavior.BASIC_RANGED_ACTIONS
                                              for attack in body.get("ranged", []))

    # Stage QG5c1: a stationary target never moves and never attacks. The tool stops for a slice of the list targets with an attack, a
    # shot, an area, a status action, or a spawn action: the list would name a slice that is not a target.
    target_slices = [body for body in used if is_target(slices[body], targets)]
    for body in target_slices:
        entry = slices[body]
        for member in ("attacks", "ranged", "areas", "statusActions", "treeActions", "spawnAction", "basicShot"):
            if entry.get(member):
                raise Failure(f"{entry['path']} is in the list targets of {SLICE_LISTS_PATH.name}, and it has the member {member}")
        for member in ("behavior", "serverTrees", "tags", "reactionTable", "additive", "reactions", "reactionLengths"):
            entry.pop(member, None)
    target_kinds = [entry for kind, entry in zip(kind_list, document_kinds)
                    if all(is_target(slices[body], targets) for body, _ in kind["bodies"])]
    counts["targetSlices"] = len(target_slices)
    counts["targetKinds"] = len(target_kinds)
    counts["targetSpawnPoints"] = sum(entry["points"] for entry in target_kinds)
    for kind, entry in zip(kind_list, document_kinds):
        bodies = [slices[body] for body, _ in kind["bodies"]]
        if entry in target_kinds:
            continue  # A stationary target is not a creature that does not attack (stage QG5c1).
        if any((body.get("attacks") or shoots(body)) and body.get("behavior", {}).get("speed", [0, 0])[1] > 0 for body in bodies):
            continue
        quiet += 1
        quiet_points += entry["points"]
        if no_attack is not None:
            cause = next((causes[body] for body, _ in kind["bodies"] if causes.get(body)), "")
            no_attack.append({"kind": entry["id"], "name": entry["name"], "family": entry["family"],
                              "creatureType": entry["creatureType"], "spawnPoints": entry["points"],
                              "slices": [body["path"] for body in bodies],
                              "cause": cause or "its slice has no speed for the way to a target"})
    counts["kindsWithNoAttack"] = quiet
    counts["spawnPointsWithNoAttack"] = quiet_points
    # The terrain height at each spawn point: a creature that walks stands on the terrain, and a
    # creature whose spawn point is not at the terrain height (a floor, a bridge, a cave) does not
    # leave its place until the navigation data has the floors (protocol notes 4.17).
    near = [0, 0]
    if ground:
        terrain = height_maps.Terrain(sources)
        terrain.load(sorted({f"r_+{int(point[3] // REGION_SIZE):02d}_+{int(point[4] // REGION_SIZE):02d}"
                             for point in points}))
        for point in points:
            height = terrain.height(point[3], point[4])
            point.append(None if height is None else round(height, 3))
            if height is not None:
                near[0] += abs(point[5] - height) <= 0.3
                near[1] += abs(point[5] - height) <= 1.0
    else:
        for point in points:
            point.append(None)
    counts["spawnPointsAtTerrainHeight"] = {"0.3": near[0], "1.0": near[1]}
    # Value 11 of a spawn point: the index of its patrol route (format 3), or null. Values 12 to 14 (format 4, stage 2.2): the phase
    # mask, the despawn flag, and the cooldown flags of its time constraint.
    for point, route, (phases, leaves, cooldown) in zip(points, point_routes, point_phases):
        point.append(route)
        point.append(phases)
        point.append(leaves)
        point.append(cooldown)
    counts.update(phase_counts(point_phases))
    counts.update(patrol_counts(routes, point_routes, points, kind_list, document_kinds, slices, used))
    counts["routesLeftOut"] = dict(sorted(route_left_out.items()))
    counts.update({"spawnPoints": len(points), "kinds": len(document_kinds), "pointKinds": point_kinds, "slices": len(used),
                   "leftOut": dict(sorted(left_out.items()))})
    counts.update(ranged_counts([slices[body] for body in used], points, document_kinds, behavior.ranged_left_out,
                                projectile_list))
    counts.update(area_counts([slices[body] for body in used], points, document_kinds, behavior.areas_left_out))
    counts.update(status_counts([slices[body] for body in used], points, document_kinds, getattr(behavior, "status_left_out", {})))
    # Stage BT3: the tree actions (the slices in the file that have one, and the causes of a listed action that is not one).
    counts["slicesWithTreeActions"] = sum(1 for body in used if slices[body].get("treeActions"))
    counts["treeActions"] = sorted({slices[body]["actions"][action[0]][0] for body in used for action in slices[body].get("treeActions", [])})
    counts["treeActionsLeftOut"] = dict(sorted(getattr(behavior, "tree_left_out", {}).items()))
    # User test 2 (UT2-7): the sources of the lengths of the attacks and the reaction fragments with a length (each slice that the tool read).
    counts.update({"attackLengths": dict(getattr(behavior, "attack_lengths", {"endBlend": 0, "animation": 0, "guess": 0})),
                   "reactionLengths": dict(getattr(behavior, "reaction_lengths", {"known": 0, "unknown": 0})),
                   "slicesWithReactionLengths": sum(1 for body in used if "reactionLengths" in slices[body]),
                   # Stage U3: the Homing clips of the attacks, the reaction tables and the additive reactions, the kind marks.
                   "homing": dict(getattr(behavior, "homing_counts", {})),
                   "reactionTables": dict(getattr(behavior, "reaction_table_counts", {})),
                   "slicesWithReactionTable": sum(1 for body in used if "reactionTable" in slices[body]),
                   "slicesWithAdditive": sum(1 for body in used if "additive" in slices[body]),
                   "kindsPromotedElite": sum(1 for kind in document_kinds if kind.get("promotedElite")),
                   "kindsNoReactions": sum(1 for kind in document_kinds if kind.get("noReactions")),
                   "spawnPointsPromotedElite": sum(kind["points"] for kind in document_kinds if kind.get("promotedElite"))})
    # The capsule of the character of a player (stage 3.4f-A): the target of a creature shot. No capsule: the backend uses its
    # default capsule.
    player_roots = sources.stream(PLAYER_SLICE)
    player_capsule = read_capsule(PLAYER_SLICE, player_roots) if player_roots is not None else None
    # The whole world: the radius 0, and the center has no meaning.
    document = {"format": 4, "world": WORLD, "center": list(center), "radius": 0.0 if whole_world else radius,
                "regions": regions,
                "counts": counts,
                "levels": [[level, health, damage, gear_score]
                           for level, (health, damage, gear_score) in sorted(tables["levels"].items())],
                "slices": [slices[body] for body in used], "kinds": document_kinds, "spawnPoints": points,
                "routes": routes, "projectiles": projectile_list}
    if player_capsule is not None:
        document["playerCapsule"] = player_capsule
    if members:
        # Stage QG5c2: each creature member slice of the encounter recipes: its kind (an index into "kinds"; -1: left out) and the cause.
        document["members"] = member_list
    # Stage QG5c1: the names of the entries that are not in the file, with their cause (a name of a kind of the file is not kept out).
    exported = {kind["vitals"].lower() for kind in document_kinds if kind["points"]} \
        | {kind["category"].lower() for kind in document_kinds if kind["category"] and kind["points"]}
    document["keptOut"] = {name: cause for name, (_, cause) in sorted(kept_out.items()) if name not in exported}
    counts["keptOutNames"] = len(document["keptOut"])
    return document


def read_ranged_attacks(sources: Sources) -> dict:
    """@return The dictionary of the attacks of the ranged attack data of the game copy (ranged_dictionary). The tool stops when the
    game copy has no ranged attack data: a file with no ranged attack must not replace a file that has them."""
    import projectiles as projectile_tool  # noqa: E402 (the module imports this one: the import is here, after the load)
    radb = sources.stream(projectile_tool.RADB_PATH)
    sources.check_complete()
    if radb is None:
        raise Failure(f"the game copy has no {projectile_tool.RADB_PATH}")
    return ranged_dictionary(projectile_tool.read_attacks(radb))


def slice_assets(catalog: Catalog) -> dict[str, tuple[bytes, int]]:
    """@return The asset of each slice path of the catalog with the sub id of a slice (the projectile slices, stage 3.4f-A)."""
    return {path: asset for asset, path in catalog.paths.items() if asset[1] == SLICE_SUB_ID}


def flight_of(path: str, roots: list[Element]) -> list | None:
    """@return The flight values of the Projectile component of a slice (projectiles.read_projectile_slice), or nothing."""
    import projectiles as projectile_tool  # noqa: E402
    return projectile_tool.read_projectile_slice(path, roots)


def ranged_dictionary(rows: list[list]) -> dict:
    """@return The attacks of the ranged attack data (the rows of projectiles.read_attacks) by (lower-case profile, lower-case
    attack), with the index of each attack on the wire (stage 3.4f-A, client code 0x5BF9F30): the profiles in the order of their
    names, and the attacks of each profile in the order of their names, as case-sensitive bytes (the keys of two maps of the
    client)."""
    ordered = sorted(rows, key=lambda row: (row[0].encode("utf-8"), row[1].encode("utf-8")))
    dictionary = {}
    for index, row in enumerate(ordered):
        dictionary.setdefault((row[0].lower(), row[1].lower()), {
            "profile": row[0], "attack": row[1], "index": index, "hitScan": row[2], "range": row[3], "radius": row[4],
            "accelDelay": row[7]})
    return dictionary


def read_projectile(path: str, asset: tuple[bytes, int], roots: list[Element], flight: list | None) -> list | None:
    """@return One projectile slice of the file (stage 3.4f-A): [path, asset, sub, root, slot of the Projectile component,
    baseSpeed, extraSpeed, timeForMaxSpeed, [acceleration], flyingLifetime, stuckLifetime], or nothing for a slice with no
    Projectile component with a slot, or with no flight values (the slice cannot be a server projectile)."""
    if flight is None:
        return None
    slot = 0
    entities: list[tuple[int, int | None]] = []
    for root in roots:
        for element in walk(root):
            if element.type_id != ENTITY_TYPE:
                continue
            listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
            entity_id = next((id_value(child) for child in element.children if child.name == ID_FIELD), None)
            if listed is None or entity_id is None or any(c.type_id == SLICE_COMPONENT_TYPE for c in listed.children):
                continue
            parent = None
            for component in listed.children:
                if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                    parent = id_value(find_field(component, PARENT_FIELD))
                if component.type_id == PROJECTILE_CLASS and slot == 0:
                    slot = number(find_field(component, REPLICATION_FIELD))
            entities.append((entity_id, parent))
    roots_found = [entity for entity, parent in entities if parent == NO_ENTITY]
    if slot == 0 or not roots_found:
        return None
    return [path, str(uuid.UUID(bytes=asset[0])).upper(), asset[1], f"{roots_found[-1]:016X}", slot, *flight[1:]]


def ranged_counts(slices: list[dict], points: list[list], kinds: list[dict], left_out: dict[str, int],
                  projectile_list: list[list]) -> dict:
    """@return The counts of the ranged attacks (stage 3.4f-A) for the output of the tool and the file."""
    ranged_slices = {index for index, entry in enumerate(slices) if entry.get("ranged")}
    ranged_kinds = {index for index, kind in enumerate(kinds) if any(body[0] in ranged_slices for body in kind["bodies"])}
    releases = sum(len(attack[2]) for entry in slices for attack in entry.get("ranged", []))
    basic = sum(1 for entry in slices if any(entry["actions"][attack[0]][0] in creature_behavior.BASIC_RANGED_ACTIONS
                                             for attack in entry.get("ranged", [])))
    return {"slicesWithRangedAttacks": len(ranged_slices), "slicesWithBasicShot": basic,
            "rangedAttacks": sum(len(entry.get("ranged", [])) for entry in slices),
            "rangedReleases": releases, "kindsWithRangedAttacks": len(ranged_kinds),
            "spawnPointsWithRangedAttacks": sum(1 for point in points if point[1] in ranged_kinds),
            "projectileSlices": len(projectile_list), "rangedLeftOut": dict(sorted(left_out.items()))}


def area_counts(slices: list[dict], points: list[list], kinds: list[dict], left_out: dict[str, int]) -> dict:
    """@return The counts of the area attacks (stage 3.4f-C) for the output of the tool and the file."""
    area_slices = {index for index, entry in enumerate(slices) if entry.get("areas")}
    area_kinds = {index for index, kind in enumerate(kinds) if any(body[0] in area_slices for body in kind["bodies"])}
    spells = sorted({cast["spell"] for entry in slices for attack in entry.get("areas", []) for cast in attack[2]})
    return {"slicesWithAreaAttacks": len(area_slices), "areaAttacks": sum(len(entry.get("areas", [])) for entry in slices),
            "areaCasts": sum(len(attack[2]) for entry in slices for attack in entry.get("areas", [])),
            "areaSpells": spells, "kindsWithAreaAttacks": len(area_kinds),
            "spawnPointsWithAreaAttacks": sum(1 for point in points if point[1] in area_kinds),
            "areasLeftOut": dict(sorted(left_out.items()))}


def status_counts(slices: list[dict], points: list[list], kinds: list[dict], left_out: dict[str, int]) -> dict:
    """@return The counts of the status actions (stage 3.4f-D) for the output of the tool and the file."""
    status_slices = {index for index, entry in enumerate(slices) if entry.get("statusActions")}
    status_kinds = {index for index, kind in enumerate(kinds) if any(body[0] in status_slices for body in kind["bodies"])}
    statuses = sorted({status for entry in slices for action in entry.get("statusActions", []) for mark in action[2] for status in mark[1]})
    return {"slicesWithStatusActions": len(status_slices), "statusActions": sum(len(entry.get("statusActions", [])) for entry in slices),
            "statusActionStatuses": statuses, "kindsWithStatusActions": len(status_kinds),
            "spawnPointsWithStatusActions": sum(1 for point in points if point[1] in status_kinds),
            "slicesWithLowHealth": sum(1 for entry in slices if "lowHealth" in entry),
            "statusActionsLeftOut": dict(sorted(left_out.items()))}


def phase_counts(point_phases: list[tuple[int, bool, int]]) -> dict:
    """@return The counts of the time constraints (stage 2.2): the spawn points of each phase mask that is not each phase (the names of
    its phases), the points whose creature leaves at the end of its phases, and the points with a cooldown flag."""
    masks: dict[str, int] = {}
    for phases, _, _ in point_phases:
        if phases != EACH_PHASE:
            name = ",".join(flag_name[2:] for bit, flag_name in enumerate(PHASE_FLAGS) if phases & (1 << bit)) or "none"
            masks[name] = masks.get(name, 0) + 1
    return {"spawnPointsWithPhases": sum(masks.values()), "phaseMasks": dict(sorted(masks.items())),
            "spawnPointsLeavingAtPhaseEnd": sum(1 for phases, leaves, _ in point_phases if phases != EACH_PHASE and leaves),
            "spawnPointsWithPhaseCooldown": sum(1 for _, _, cooldown in point_phases if cooldown)}


def patrol_counts(routes: list[list], point_routes: list[int | None], points: list[list], kind_list: list[dict],
                  document_kinds: list[dict], slices: list[dict], used: list[int]) -> dict:
    """@return The counts of the patrols and of the wander values for the output of the tool and the file."""
    routed_kinds: set[int] = set()
    tasks: dict[str, int] = {}
    tasks_with_action = 0
    task_waypoints = 0
    policies = [0] * POLICIES
    sizes: dict[str, int] = {}
    for point, route in zip(points, point_routes):
        if route is None:
            continue
        routed_kinds.add(point[1])
        policy, waypoints = routes[route]
        policies[policy] += 1
        sizes[str(len(waypoints))] = sizes.get(str(len(waypoints)), 0) + 1
        bodies = [slices[body] for body, _ in kind_list[point[1]]["bodies"]]
        for waypoint in waypoints:
            if waypoint[6]:
                task_waypoints += 1
                tasks[waypoint[6]] = tasks.get(waypoint[6], 0) + 1
                tasks_with_action += any(action[0] == waypoint[6] and action[2] >= 0 for body in bodies
                                         for action in body["actions"])
    wandering = 0
    neither = 0
    for position, kind in enumerate(kind_list):
        bodies = [slices[body] for body, _ in kind["bodies"]]
        wanders = any(body.get("behavior", {}).get("wander", [0, 0, 0])[0] == 1
                      and body["behavior"]["wander"][2] > 0 for body in bodies)
        wandering += wanders
        neither += not wanders and position not in routed_kinds
    return {"routes": len(routes), "spawnPointsWithRoute": sum(1 for route in point_routes if route is not None),
            "kindsWithRoute": len(routed_kinds), "kindsThatWander": wandering, "kindsWithNoRouteAndNoWander": neither,
            "routePolicies": {"loop": policies[0], "bounce": policies[1], "random": policies[2], "undefined": policies[3]},
            "routeWaypoints": dict(sorted(sizes.items(), key=lambda item: int(item[0]))),
            "waypointsWithTask": task_waypoints, "waypointTasksInTheActionTable": tasks_with_action,
            "tasks": dict(sorted(tasks.items(), key=lambda item: -item[1])[:40])}


def health_of(document: dict, kind: dict, level: int) -> float:
    levels = dict((int(row[0]), row[1]) for row in document["levels"])
    return levels.get(level, 0.0) * kind["health"]


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the creature data of the game as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--center", nargs=2, type=float, default=None, metavar=("X", "Y"),
                        help="the center of a smaller area (default: the start place of a new character)")
    parser.add_argument("--radius", type=float, default=None, metavar="METERS",
                        help="only the spawn points in a circle (default: the whole world)")
    parser.add_argument("--all-slices", action="store_true",
                        help="also the slices that the client does not show correctly (a file for a picture tour)")
    parser.add_argument("--spawn-all", action="store_true",
                        help="with --all-slices: name the spawn action of each slice (a tour that tests this action)")
    parser.add_argument("--report", metavar="FILE", help="write one line for each spawn entry that is left out")
    parser.add_argument("--no-attack", metavar="FILE", help="write one line for each kind that does not attack, with the cause")
    parser.add_argument("--find", nargs="+", metavar="TEXT", help="print the kinds with a text, and write no file")
    parser.add_argument("--level", metavar="LEVEL",
                        help="stage DG2: the spawn points of an own level (a level or a map id of the map table; default output "
                             "GAMEDATA/levels/LEVEL/creatures.json)")
    arguments = parser.parse_args()
    if arguments.level and (arguments.radius is not None or arguments.center is not None):
        print("creatures: --level takes the whole terrain of the level: no --radius and no --center. No file was written.", file=sys.stderr)
        return 1
    target = Path(arguments.output)
    if arguments.center is not None and arguments.radius is None:
        arguments.radius = DEFAULT_RADIUS
    if arguments.radius is not None and not (math.isfinite(arguments.radius) and 0.0 < arguments.radius <= 20000.0):
        print("creatures: the radius must be a number from 1 to 20000. No file was written.", file=sys.stderr)
        return 1
    center = tuple(arguments.center) if arguments.center is not None else DEFAULT_CENTER
    details: list[dict] | None = [] if arguments.report else None
    quiet: list[dict] | None = [] if arguments.no_attack else None
    try:
        sources = Sources(game / "assets")
        level = None
        if arguments.level:
            # Stage DG2: the terrain project of the level comes from its map row (game_modes.level_terrain).
            import game_modes  # noqa: PLC0415 (the module imports this one)
            level, terrain, _rows = game_modes.level_terrain(sources, arguments.level)
            use_level_terrain(terrain)
            # The terrain height of each spawn point: the height maps of the level (and its terrain after the cookies).
            height_maps.use_level(level, terrain)
            if arguments.output == parser.get_default("output"):
                target = gamedata / LEVELS_FOLDER / level / OUTPUT_NAME
        # Stage DG2: the member kinds of the encounter recipes of the main world are kinds of the main world file. Stage QG9e: a file of an
        # own level has the kinds of the creature members of the instance recipes of its mode roots (encounter_recipes.Reader with the level).
        document = build_document(sources, center, arguments.radius, arguments.all_slices, details,
                                  arguments.spawn_all and arguments.all_slices, quiet, arguments.find is None, members=True,
                                  level=(level, terrain) if level is not None else None)
        if level is not None:
            document["level"] = level
        if quiet is not None:
            listing = Path(arguments.no_attack)
            listing.parent.mkdir(parents=True, exist_ok=True)
            listing.write_text("".join(json.dumps(entry, separators=(",", ":")) + "\n" for entry in quiet),
                               encoding="utf-8")
        if details is not None:
            report = Path(arguments.report)
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text("".join(json.dumps(entry, separators=(",", ":")) + "\n" for entry in details),
                              encoding="utf-8")
        if arguments.find is None:
            target.parent.mkdir(parents=True, exist_ok=True)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target.with_name(target.name + ".tmp")
            # A number that is not finite is not JSON: the backend would refuse the whole file
            # (code review of stage 3.4e, finding F6). json.dumps then stops with ValueError.
            temporary.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"creatures: {error}. No file was written.", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"creatures: the document has a number that is not finite ({error}). No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"creatures: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.find is not None:
        for kind in document["kinds"]:
            text = f"{kind['id']} {kind['name']} {kind['vitalsName']} {kind['category']}".lower()
            if any(word.lower() in text for word in arguments.find):
                body = document["slices"][kind["bodies"][0][0]]
                print(f"{kind['id']}: {kind['name'] or '(no name)'}, category {kind['category']}, {kind['family']}, "
                      f"{kind['creatureType']}, level {kind['level']}, health {health_of(document, kind, kind['level']):.3f}, "
                      f"{kind['points']} spawn point(s), slice {body['path']} (asset {body['asset']}:{body['sub']}, "
                      f"slots: movement {body['movement']}, vitals {body['vitals']}, marker {body['marker']}, "
                      f"interest radius {body['interestRadius']:g} m)")
        return 0
    counts = document["counts"]
    area = (f"in the whole world ({len(document['regions'])} regions)" if arguments.radius is None
            else f"in {arguments.radius:g} m around {center[0]:g} {center[1]:g} (regions {', '.join(document['regions'])})")
    print(f"{target}: {counts['spawnPoints']} spawn points of {counts['kinds']} kinds with {counts['slices']} slices, "
          f"{area}")
    print(f"  entries of the layers: {counts['entries']}; in the area: {counts['inArea']}; usual (not an event): "
          f"{counts['usual']}; with a creature family: {counts['creatures']}")
    print(f"  stationary targets (the list targets): {counts.get('targetSpawnPoints', 0)} spawn points of "
          f"{counts.get('targetKinds', 0)} kinds with {counts.get('targetSlices', 0)} slices")
    for cause, count in counts["leftOut"].items():
        print(f"  left out, {cause}: {count}")
    print(f"  in the file with no category row (the name plate has the name of the vitals row): "
          f"{counts.get('noCategoryRow', 0)}")
    print(f"  slices with no action table: {counts['slicesWithNoActions']}; slices whose spawn action the backend starts: "
          f"{counts['slicesWithSpawnAction']}")
    print(f"  slices with behavior values: {counts['slicesWithBehavior']}; with an attack: {counts['slicesWithAttacks']}; "
          f"with animation tags: {counts['slicesWithTags']}")
    print(f"  kinds that do not attack: {counts['kindsWithNoAttack']} of {counts['kinds']} "
          f"({counts['spawnPointsWithNoAttack']} spawn points; --no-attack FILE lists them)")
    print(f"  ranged attacks: {counts['rangedAttacks']} of {counts['slicesWithRangedAttacks']} slices ({counts['rangedReleases']} "
          f"releases, {counts['projectileSlices']} projectile slices; {counts['kindsWithRangedAttacks']} kinds, "
          f"{counts['spawnPointsWithRangedAttacks']} spawn points); slices with the basic shot "
          f"{' or '.join(creature_behavior.BASIC_RANGED_ACTIONS)}: {counts['slicesWithBasicShot']}; player capsule: "
          f"{document.get('playerCapsule', 'none')}")
    for cause, count in counts["rangedLeftOut"].items():
        print(f"  no ranged attack, {cause}: {count}")
    print(f"  area attacks: {counts['areaAttacks']} of {counts['slicesWithAreaAttacks']} slices ({counts['areaCasts']} casts of "
          f"{len(counts['areaSpells'])} spells; {counts['kindsWithAreaAttacks']} kinds, {counts['spawnPointsWithAreaAttacks']} spawn "
          f"points): {', '.join(counts['areaSpells'])}")
    for cause, count in counts["areasLeftOut"].items():
        print(f"  no area cast, {cause}: {count}")
    print(f"  status actions: {counts['statusActions']} of {counts['slicesWithStatusActions']} slices ({counts['kindsWithStatusActions']} "
          f"kinds, {counts['spawnPointsWithStatusActions']} spawn points): {', '.join(counts['statusActionStatuses'])}; slices with a "
          f"low-health part: {counts['slicesWithLowHealth']}")
    for cause, count in counts["statusActionsLeftOut"].items():
        print(f"  no status action, {cause}: {count}")
    print(f"  tree actions (stage BT3): {counts.get('slicesWithTreeActions', 0)} slices: {', '.join(counts.get('treeActions', []))}")
    for cause, count in counts.get("treeActionsLeftOut", {}).items():
        print(f"  no tree action, {cause}: {count}")
    lengths = counts["attackLengths"]
    print(f"  the lengths of the attacks: {lengths['endBlend']} from an end blend, {lengths['animation']} from the animation file, "
          f"{lengths['guess']} the last contact plus {creature_behavior.TIME_AFTER_CONTACT} s; reaction fragments with a length: "
          f"{counts['reactionLengths']['known']} ({counts['reactionLengths']['unknown']} with none), "
          f"{counts['slicesWithReactionLengths']} slices")
    homing = counts.get("homing", {})
    tables = counts.get("reactionTables", {})
    print(f"  homing (stage U3): {homing.get('attacks', 0)} attacks with {homing.get('clips', 0)} Homing clips "
          f"({homing.get('moving', 0)} that move), {homing.get('leftOut', 0)} attacks with a clip that is not complete; reaction tables: "
          f"{counts.get('slicesWithReactionTable', 0)} slices ({tables.get('rows', 0)} rows, {tables.get('neverRows', 0)} rows that never "
          f"hold, {tables.get('noVariant', 0)} side fragments with no variant, {tables.get('hitStunClips', 0)} rows with a HitStun clip), "
          f"additive reaction: {counts.get('slicesWithAdditive', 0)} slices; promoted elites: {counts.get('kindsPromotedElite', 0)} kinds "
          f"({counts.get('spawnPointsPromotedElite', 0)} spawn points), no reactions: {counts.get('kindsNoReactions', 0)} kinds")
    near = counts["spawnPointsAtTerrainHeight"]
    print(f"  spawn points 0.3 m or less from the terrain height: {near['0.3']}; 1.0 m or less: {near['1.0']}")
    policies = counts["routePolicies"]
    print(f"  spawn points with a patrol route: {counts['spawnPointsWithRoute']} ({counts['kindsWithRoute']} kinds; "
          f"loop {policies['loop']}, back and forth {policies['bounce']}, random {policies['random']}, "
          f"undefined {policies['undefined']}; waypoints: "
          + ", ".join(f"{size}: {count}" for size, count in counts["routeWaypoints"].items()) + ")")
    print(f"  spawn points with a time constraint: {counts['spawnPointsWithPhases']} "
          f"({counts['spawnPointsLeavingAtPhaseEnd']} leave at the end of their phases, {counts['spawnPointsWithPhaseCooldown']} with a "
          f"cooldown flag; "
          + ", ".join(f"{mask}: {count}" for mask, count in counts["phaseMasks"].items()) + ")")
    for cause, count in counts["routesLeftOut"].items():
        print(f"  no route, {cause}: {count}")
    print(f"  waypoints with a task: {counts['waypointsWithTask']}; the action table of the slice has the task: "
          f"{counts['waypointTasksInTheActionTable']}")
    flags = counts["slicesByEnableWander"]
    print(f"  kinds that wander: {counts['kindsThatWander']}; kinds with no route and no wander: "
          f"{counts['kindsWithNoRouteAndNoWander']} (slices with Enable_Wander true {flags['true']}, false "
          f"{flags['false']}, not in the slice {flags['notInTheSlice']})")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

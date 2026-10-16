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

What the file does not have, with the count of each cause in "excluded" (a later stage reads them):
  - The children of a PrefabSpawner of a slice (m_sliceAsset): the server makes these entities (a crafting station, a
    person of a quest swap). Stage 2.5b.
  - The records of the distribution files of a region (the gathering nodes of the terrain): stage 3.6.
  - A placement with no slice (an entry `<PLOT>` with no asset): the level chunks have these objects.
A placement whose slice has no Interact component has no object.

The kind of an object (a table of this project from the option names of the data, research Q4): the first option of the
table KINDS that a bound has gives its kind. The backend registers a handler for a kind and its options.

The tool stops with status 1 and one message, and writes no file, when:
  - a file that it needs has no source (the pak entries have Oodle data: see creature_data.py);
  - a numeric field has a wrong form: a range that is not a finite number above 0, a capacity or a slot that is not a
    number of 4 bytes, a transform that is not 12 numbers, a position that is not finite;
  - two bounds have the same reference (G and target) with different content, or one placement id has two different
    slices.
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
  {"format": 1, "world": TEXT, "counts": {...}, "excluded": {CAUSE: N, ...},
   "options": [{"name", "id", "action", "committed", "delay", "hold", "instantCancel", "cooldown", "maxUsage",
                "inactive", "requiredLevel", "requiredItem", "requiredItemCount", "requiredCurrency",
                "requiredAchievement", "statusRules"}, ...],
   "achievements": [[achievement id, index], ...],
   "objects": [{"key", "kind", "slice", "asset", "sub", "actor", "region", "position", "bounds": [
                  {"slot", "component", "target", "position", "rangeSq", "capacity", "lineOfSight", "arcRequired", "arc",
                   "options": [[option index, option entity], ...], "lore": achievement index or -1,
                   "requires": {...}}, ...]}, ...]}
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
                           region_names, spawner_path)
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import (BASE_CLASS_FIELDS, COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, NAME_FIELD, NO_ENTITY,  # noqa: E402
                       Element, component_id, find_field, id_value)

OUTPUT_NAME = "interaction-data.json"
UI_DATABASE = "sharedassets/genericassets/ui/uidatabase.uidb"
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, "coatgen/", "slices/", UI_DATABASE, CATALOG_PATH)
LORE_TABLE_TYPE = "LoreData"
ACHIEVEMENT_TABLE_TYPE = "AchievementData"

INTERACT_CLASS = uuid.UUID("b69045a3-a129-4334-bdfb-069c18cc84a2")
OPTION_ROW_CLASS = uuid.UUID("f0887e97-5084-413c-bce7-5c24cecb03c0")
INTERACT_BYTES = INTERACT_CLASS.bytes

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
    ("OpenStorageOption", "storage"), ("OpenTradingPostOption", "tradingPost"),
    ("OpenMap", "shrine"),
    ("OpenCraftingOption", "craftingStation"), ("OpenCraftingOptionLevel60", "craftingStation"),
    ("BindToCampOption", "camp"),
    ("OpenLootStorageOption", "lootDrop"), ("GatherOption", "gathering"),
    ("ToggleDoorOption", "door"), ("TeleportToOtherSideOfDoor", "door"), ("TeleportDoorEnter", "door"),
    ("TeleportDoorExit", "door"),
    ("BedInteractionAnimation", "restFurniture"),
)
KIND_OF = dict(KINDS)
KIND_ORDER = {name: index for index, (name, _) in enumerate(KINDS)}

CAUSE_SPAWNER = "a PrefabSpawner in the slice of an object: its child is a server entity (stage 2.5b)"
CAUSE_DISTRIBUTION = "a record of a distribution file: not read (stage 3.6)"
CAUSE_PLOT = "a placement with no slice (<PLOT>): an object of the level chunks"
CAUSE_NO_INTERACT = ("a placement whose slice has no Interact component (a PrefabSpawner of it makes a server entity: "
                     "stage 2.5b)")


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


def actor_of(placement_id: str) -> bytes:
    """@return The managed actor id of a placement (research Q1; interaction.md 4.2; live check L1 of stage 2.5a)."""
    raw = uuid.UUID(placement_id).bytes
    folded = bytes(a ^ b for a, b in zip(raw[:8], raw[8:]))[::-1]
    return bytes([(folded[0] & ~7) | 3]) + folded[1:] + bytes(8)


def group_of(actor: bytes) -> int:
    return int.from_bytes(actor[:8], "little")


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
    """The Interact components of one slice, in the frame of the slice."""

    def __init__(self, path: str, roots: list[Element]) -> None:
        self.path = path
        self.root: int | None = None
        self.bounds: list[dict] = []
        self.spawners = 0
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
                    if find_field(component, SLICE_ASSET) is not None:
                        self.spawners += 1
                entities[entity_id] = info
        roots_of_slice = [identifier for identifier, info in entities.items() if info["parent"] == NO_ENTITY]
        if interacts and not roots_of_slice:
            raise Failure(f"{path}: the slice has no root entity (an entity whose transform has no parent)")
        self.root = roots_of_slice[-1] if roots_of_slice else None
        for entity_id, component in interacts:
            self.bounds.append(self._bound(entity_id, component, entities))

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
        condition = find_field(component, CONDITION)
        if condition is not None and (condition.children or condition.value):
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

    for region in regions:
        for path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".capitals.json"):
            data = sources.read(path)
            if data is None:
                continue
            counts["layers"] += 1
            try:
                placements = json.loads(data.decode("utf-8-sig")).get("Capitals") or []
            except (ValueError, AttributeError) as error:
                raise Failure(f"{path}: the placements of the layer did not parse: {error}") from error
            layer = path[len(REGIONS_FOLDER):-len(".capitals.json")]
            for placement in placements:
                counts["placements"] += 1
                where = placement.get("worldPosition") or {}
                try:
                    position = {axis: float(where.get(axis, 0.0)) for axis in "xyz"}
                except (TypeError, ValueError) as error:
                    raise Failure(f"{layer}: a placement has a position that is not a number") from error
                if not all(math.isfinite(value) for value in position.values()):
                    raise Failure(f"{layer}: the placement {placement.get('id')} has a position that is not finite")
                if center is not None and radius is not None \
                        and math.hypot(position["x"] - center[0], position["y"] - center[1]) > radius:
                    continue
                asset = asset_of(placement)
                slice_path = spawner_path(placement, catalog, sources) if asset is not None else None
                if asset is None or slice_path is None:
                    if "plot" in str(placement.get("sliceName", "")).lower() or asset is None:
                        exclude(CAUSE_PLOT)
                        continue
                    raise Failure(f"{layer}: the placement {placement.get('id')} names a slice that the game copy does not "
                                  f"have ({placement.get('sliceName')})")
                counts["placementsWithSlice"] += 1
                if slice_path not in templates:
                    raw = sources.read(slice_path)
                    if raw is None:
                        sources.check_complete()
                    if INTERACT_BYTES not in raw:
                        # No Interact class in the slice: no object (the class id is in each component of the class).
                        templates[slice_path] = Template(slice_path, [])
                    else:
                        templates[slice_path] = Template(slice_path, sources.stream(slice_path))
                template = templates[slice_path]
                if template.spawners:
                    exclude(CAUSE_SPAWNER, template.spawners)
                if not template.bounds:
                    exclude(CAUSE_NO_INTERACT)
                    continue
                identifier = str(placement.get("id", ""))
                try:
                    actor = actor_of(identifier)
                except ValueError as error:
                    raise Failure(f"{layer}: the placement id {identifier!r} is not a UUID") from error
                if identifier in keys:
                    if keys[identifier] != slice_path:
                        raise Failure(f"the placement {identifier} has two different slices")
                    continue
                keys[identifier] = slice_path
                group = group_of(actor)
                k = template.root ^ group
                matrix = placement_matrix(placement.get("rotation") or {}, position)
                object_position = [round(position[axis], 3) for axis in "xyz"]
                bounds = []
                for bound in template.bounds:
                    instance = bound["entity"] ^ k
                    target = (instance + bound["component"]) & 0xFFFFFFFFFFFFFFFF
                    what = f"{layer}: the object {identifier}"
                    entries = []
                    unresolved_texts = []
                    unknown_options = []
                    for option_entity, names in bound["options"]:
                        for name in names:
                            index = option(name, what)
                            if index is None:
                                unknown_options.append(name)
                                continue
                            entries.append([index, f"{option_entity ^ k:016x}"])
                            counts["byOption"][name] = counts["byOption"].get(name, 0) + 1
                    lore = -1
                    requires = dict(bound["requires"])
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
                    world = apply(matrix, bound["translation"])
                    if not all(math.isfinite(value) for value in world):
                        raise Failure(f"{what}: the place of a bound is not finite")
                    entry = {"slot": bound["slot"], "component": f"{bound['component']:016x}", "target": f"{target:016x}",
                             "position": [round(value, 3) for value in world], "rangeSq": bound["rangeSq"],
                             "capacity": bound["capacity"], "lineOfSight": bound["lineOfSight"],
                             "arcRequired": bound["arcRequired"], "arc": bound["arc"], "options": entries, "lore": lore,
                             "requires": requires}
                    if bound["tag"]:
                        entry["tag"] = bound["tag"]
                    if unknown_options:
                        entry["unknownOptions"] = unknown_options
                        counts["unknownOptions"] += len(unknown_options)
                    reference = (group, target)
                    text = json.dumps(entry, sort_keys=True)
                    if reference in references and references[reference] != text:
                        raise Failure(f"two bounds have the reference {group:016x} {target:016x} with different content")
                    references[reference] = text
                    bounds.append(entry)
                kind = kind_of(template.bounds)
                counts["byKind"][kind] = counts["byKind"].get(kind, 0) + 1
                counts["bounds"] += len(bounds)
                objects.append({"key": identifier, "kind": kind, "slice": slice_path, "asset": asset[0].hex(),
                                "sub": asset[1], "actor": actor.hex(), "region": region, "position": object_position,
                                "bounds": bounds})
    sources.check_complete()
    exclude(CAUSE_DISTRIBUTION, 0)
    counts["objects"] = len(objects)
    counts["options"] = len(option_list)
    counts["achievements"] = len(achievement_list)
    counts["byKind"] = dict(sorted(counts["byKind"].items()))
    counts["byOption"] = dict(sorted(counts["byOption"].items()))
    objects.sort(key=lambda entry: (entry["region"], entry["position"][0], entry["position"][1], entry["key"]))
    counts["unresolvedNames"] = len(unresolved)
    return {"format": 1, "world": WORLD, "counts": counts, "excluded": dict(sorted(excluded.items())),
            "unresolved": dict(sorted(unresolved.items())), "options": option_list, "achievements": achievement_list,
            "objects": objects}


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
    parser.add_argument("--list", metavar="KIND", help="print the objects of a kind, and write no file")
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
    if arguments.list:
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
    for cause, count in document["excluded"].items():
        print(f"  not in the file: {count} ({cause})")
    if document["unresolved"]:
        print(f"  {counts['unresolvedNames']} name(s) of the data with no row, in {counts['unresolvedBounds']} bound(s) (the backend "
              f"refuses their use); the first: {next(iter(document['unresolved']))}")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

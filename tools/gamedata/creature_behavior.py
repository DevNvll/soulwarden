#!/usr/bin/env python3
"""Read what the behavior of a creature needs from the files of its slice.

The creature data tool (creature_data.py) uses this module. For one creature slice it gives
(protocol notes 4.17):

  1. The behavior values of the slice components: the two vision profiles of the Perception
     component (an angle and a distance for a creature at rest and for a creature in a fight),
     and the leash distances and the speeds of the Blackboard component (the Pathing component
     gives the speeds when the blackboard does not have them).
  2. The tags of the animation fragments. A creature with a weapon has a variant of each attack
     fragment for each weapon, and a tag state selects the variant. The slice names its weapons
     (item ids with a slot), and the weapon table gives the tags of each weapon. The tag
     definition file of the animation controller gives the bit of each tag: the movement state
     of a creature has the tag state as 12 bytes.
  3. The attacks: each action of the action table whose animation fragment has a damage event.
     The event has the time of the contact, a shape, and the name of a damage row. The damage
     table of the slice gives the damage factor and the damage type of the row.

The files, all in the pak files of the game copy:

  slice                      m_animationDatabase   the animation database of the creature (.adb, XML)
                             m_damageTable         the damage table of the creature
                             m_slotType, m_itemName   its weapons: slot 24 is the main hand, slot 27
                                                   the off hand
  weapon table               MannequinTag, OffHandMannequinTag, AnimDbPath (the animation database
                             of the weapon: the damage events of a weapon attack are there)
  animation database         FragmentList/NAME/Fragment: one variant of the fragment NAME, with its
                             tags. ProcLayer/Procedural type="CAGE-Damage": a damage event; the sum
                             of the times `ExitTime` of the Blend elements before it is its time.
                             SubADBs/SubADB: one more file, whose variants have its tags too.
  tag definition file        Tags/Group/Tag and Tags/Tag, after the tags of each file of Imports

The bit of a tag (client code at image RVA 0x6D87130; live proof for the Ancient Guardian
Reaver, stage 3.4b): the tag state has 12 bytes. Each byte gets, in the sequence of the file,
each group that has no place yet and that fits in the bits that the byte has left (a group
with N tags needs the number of bits of the number N; the value 0 is "no tag of the group", and
the tag K of the group has the value K, from 1), and then one bit for each tag with no group
that has no place yet. The first bit of a byte is its lowest bit.

Which variant of a fragment a creature plays (the rule of the animation system, as this tool
uses it): the variant with the most tags whose tags are all tags of the creature. The variants
of the off hand have the tag "Offhand" too.

The rule of the tool for an attack (a project rule):
  - The action has a fragment, and the variant of the creature has one damage event or more
    in the animation database of the creature or of one of its weapons.
  - Each damage event names a row of the damage table of the slice. The row is not a ranged
    row, its damage type is a type of the damage type table, and its damage factor is 0.05 or
    more.
  - The action is not a part of a chain, not a reaction, not an attack on a structure, and not
    an attack that needs a run or a special body (no word of its name is "Combo", "Breakout",
    "Reaction", "Structure", "Run", "Running", "Spray", "Throw", or "Summon").
The reach of a contact is a project value: the game has the shape of the contact at a joint of
the animated body, and the tool does not animate a body. For a shape at the root of the body
the reach is the far end of the shape plus a margin. For a shape at a different joint (an arm,
a weapon) it is one constant.
"""

from __future__ import annotations

import math
import struct
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

WEAPON_TYPE = "WeaponItemDefinitions"
DAMAGE_TYPE_TYPE = "DamageTypeData"
TEXT_TYPE_ID = "03aaab3f-5c47-5a66-9ebc-d5fa4db353c9"
BOOL_TYPE_ID = "a0ca880c-afe4-43cb-926c-59ac48496112"

ANIMATION_FIELD = field_id("m_animationDatabase")
DAMAGE_TABLE_FIELD = field_id("m_damageTable")
ASSET_PATH_FIELD = field_id("AssetPath")
SLOT_TYPE_FIELD = field_id("m_slotType")
ITEM_NAME_FIELD = field_id("m_itemName")
AMBIENT_VISION_FIELD = field_id("m_ambientVisionProfile")
COMBAT_VISION_FIELD = field_id("m_combatVisionProfile")
VISION_ANGLE_FIELD = field_id("m_visionAngle")
VISION_DISTANCE_FIELD = field_id("m_visionDistance")
NAV_TYPE_FIELD = field_id("m_navConfigType")
MAX_SPEED_FIELD = field_id("m_maxSpeed")
NAME_FIELD = field_id("m_name")
VALUE_FIELD = field_id("m_value")

# The slots of the weapons of a creature: the slot numbers of the paperdoll.
MAIN_HAND_SLOT = 24
OFF_HAND_SLOT = 27
OFFHAND_TAG = "offhand"
TAG_BYTES = 12
# The rule of the tool for an attack (see the head of the file).
SMALLEST_FACTOR = 0.05
LEFT_OUT_WORDS = frozenset({"combo", "breakout", "reaction", "structure", "run", "running", "spray", "throw", "summon"})
# The reach of a contact, in meters (project values).
REACH_MARGIN = 0.5
REACH_AT_JOINT = 2.2
REACH_SMALLEST = 1.5
REACH_LARGEST = 6.0
ROOT_JOINTS = ("", "bind_xform", "xform", "master_root", "root")
# The time of an attack whose animation has no end time in the database: its last contact plus this (seconds).
TIME_AFTER_CONTACT = 0.9
LONGEST_ATTACK = 12.0


def walk(element):
    yield element
    for child in element.children:
        yield from walk(child)


def real(element) -> float | None:
    if element is None or element.value is None or len(element.value) != 4:
        return None
    value = struct.unpack(">f", element.value)[0]
    return value if math.isfinite(value) else None


def text(element) -> str:
    if element is None or element.value is None or str(element.type_id) != TEXT_TYPE_ID:
        return ""
    return element.value.decode("utf-8", "replace")


def child(element, wanted: int):
    """@return The first element below `element` with this field number, at each depth, or no value."""
    for node in walk(element):
        if node is not element and node.name == wanted:
            return node
    return None


def game_path(name: str) -> str:
    return name.replace("\\", "/").lower().lstrip("/")


def rounded(value: float) -> float:
    return float(f"{value:.6g}")


def read_behavior(roots: list) -> dict | None:
    """@return The behavior values of a slice: {"sight": [angle, distance], "combatSight": [angle, distance],
    "leash": [at rest, in a fight], "speed": [walk, run], "ambient": speed, "wander": [on, nearest, farthest]}, or
    no value for a slice with no vision profile and no speed (an object, not a creature that goes to a target). A
    value that the slice does not have is 0.

    "ambient" is the speed of a creature with no target (a patrol, a wander): the blackboard value
    AmbientNavMaxSpeed, or the largest speed of the navigation configuration "Wander" or "AmbientLeash" of the
    Pathing component, in this sequence. "wander" is the blackboard value Enable_Wander (1: true; 0: false, or not
    in the slice: a project rule, a slice with no value does not wander) and the distance range WanderDistanceMin,
    WanderDistanceMax."""
    sight = [0.0, 0.0]
    combat = [0.0, 0.0]
    board: dict[str, float] = {}
    flags: dict[str, bool] = {}
    speeds: dict[str, float] = {}
    for root in roots:
        for node in walk(root):
            if node.name == AMBIENT_VISION_FIELD and sight == [0.0, 0.0]:
                sight = [real(child(node, VISION_ANGLE_FIELD)) or 0.0, real(child(node, VISION_DISTANCE_FIELD)) or 0.0]
                continue
            if node.name == COMBAT_VISION_FIELD and combat == [0.0, 0.0]:
                combat = [real(child(node, VISION_ANGLE_FIELD)) or 0.0, real(child(node, VISION_DISTANCE_FIELD)) or 0.0]
                continue
            fields = {entry.name: entry for entry in reversed(node.children)}
            # One navigation configuration: its type name and its values are fields of one element.
            kind = text(fields.get(NAV_TYPE_FIELD))
            speed = real(fields.get(MAX_SPEED_FIELD))
            if kind and speed is not None:
                speeds.setdefault(kind, speed)
            # One value of the blackboard: an element with a name (a text below it) and a value (a
            # number, or a value of one byte with the type of a bool).
            value = fields.get(VALUE_FIELD)
            number = real(value)
            flag = (value.value[0] != 0 if value is not None and value.value is not None and len(value.value) == 1
                    and str(value.type_id) == BOOL_TYPE_ID else None)
            if number is None and flag is None:
                continue
            for entry in node.children:
                if entry.name == VALUE_FIELD:
                    continue
                name = text(entry) if entry.name == NAME_FIELD else text(child(entry, NAME_FIELD))
                if name:
                    if number is not None:
                        board.setdefault(name, number)
                    else:
                        flags.setdefault(name, flag)
                    break
    walk_speed = board.get("CombatWalkMaxSpeed") or speeds.get("WalkToTarget") or board.get("AmbientNavMaxSpeed") or 0.0
    run_speed = board.get("CombatRunMaxSpeed") or speeds.get("RunToTarget") or walk_speed
    if run_speed <= 0.0 and sight[1] <= 0.0:
        return None
    ambient = board.get("AmbientNavMaxSpeed") or speeds.get("Wander") or speeds.get("AmbientLeash") or 0.0
    nearest = max(board.get("WanderDistanceMin", 0.0), 0.0)
    farthest = max(board.get("WanderDistanceMax", 0.0), nearest)
    return {"sight": [rounded(max(sight[0], 0.0)), rounded(max(sight[1], 0.0))],
            "combatSight": [rounded(max(combat[0], 0.0)), rounded(max(combat[1], 0.0))],
            "leash": [rounded(max(board.get("AmbientLeashDistance", 0.0), 0.0)),
                      rounded(max(board.get("CombatLeashDistance", 0.0), 0.0))],
            "speed": [rounded(max(walk_speed, 0.0)), rounded(max(run_speed, 0.0))],
            "ambient": rounded(max(ambient, 0.0)),
            "wander": [1 if flags.get("Enable_Wander") else 0, rounded(nearest), rounded(farthest)],
            # For the counts of the tool only (not a member of the file): the value of the slice.
            "wanderFlag": flags.get("Enable_Wander")}


def read_references(roots: list) -> dict:
    """@return What a slice names for its attacks: {"animations": path, "damageTable": path, "weapons": {slot: item id}}."""
    animations = ""
    table = ""
    weapons: dict[int, str] = {}
    for root in roots:
        for node in walk(root):
            if node.name == ANIMATION_FIELD and not animations:
                animations = game_path(text(child(node, ASSET_PATH_FIELD)))
            elif node.name == DAMAGE_TABLE_FIELD and not table:
                table = game_path(text(child(node, ASSET_PATH_FIELD)))
            # One weapon: its slot and its item id are fields of one element.
            fields = {entry.name: entry for entry in reversed(node.children)}
            slot, name = fields.get(SLOT_TYPE_FIELD), text(fields.get(ITEM_NAME_FIELD))
            if name and slot is not None and slot.value is not None:
                weapons.setdefault(int.from_bytes(slot.value, "big"), name)
    if table.endswith(".xml"):
        table = table[:-4] + ".datasheet"
    return {"animations": animations, "damageTable": table, "weapons": weapons}


class Tables:
    """The tables of the game that the attacks need: the weapons, the damage types, and the damage tables."""

    def __init__(self, sources, tables_folder: str) -> None:
        self.sources = sources
        self.weapons: dict[str, dict] = {}
        self.types: dict[str, tuple[int, str]] = {}
        self._damage: dict[str, dict | None] = {}
        for path in sources.paths(tables_folder, ".datasheet"):
            name = path.rsplit("/", 1)[-1]
            if "itemdefinitions_weapons" not in name and "damagetypes" not in name:
                continue
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            try:
                sheet = Sheet(data)
                for row in range(sheet.row_count):
                    if kind == WEAPON_TYPE:
                        self.weapons.setdefault(str(sheet.cell(row, "WeaponID")).lower(), {
                            "tags": tag_names(str(sheet.cell(row, "MannequinTag"))),
                            "offhandTags": tag_names(str(sheet.cell(row, "OffHandMannequinTag"))),
                            "animations": game_path(str(sheet.cell(row, "AnimDbPath")))})
                    elif kind == DAMAGE_TYPE_TYPE:
                        wire = sheet.cell(row, "IntID")
                        if str(sheet.cell(row, "TypeID")) and isinstance(wire, float) and 0 < wire < 256:
                            self.types.setdefault(str(sheet.cell(row, "TypeID")),
                                                  (int(wire), str(sheet.cell(row, "Category"))))
            except (struct.error, ValueError, IndexError, TypeError) as error:
                raise Failure(f"{path}: the table did not parse: {error}") from error
        if not self.weapons or not self.types:
            raise Failure("the game copy has no weapon table or no damage type table")

    def damage_rows(self, path: str) -> dict | None:
        """@return The rows of one damage table by their lower-case name, or no value for a table that is not there."""
        if path not in self._damage:
            rows = None
            if path and self.sources.has(path):
                data = self.sources.read(path)
                if data is not None:
                    try:
                        sheet = Sheet(data)
                        rows = {}
                        for row in range(sheet.row_count):
                            factor, added = sheet.cell(row, "DmgCoef"), sheet.cell(row, "AddDmg")
                            rows.setdefault(str(sheet.cell(row, "DamageID")).lower(), {
                                "id": str(sheet.cell(row, "DamageID")),
                                "type": str(sheet.cell(row, "DamageType")),
                                "ranged": sheet.cell(row, "IsRanged") in (True, 1.0, "1", "TRUE", "true"),
                                "factor": factor if isinstance(factor, float) and math.isfinite(factor) else 0.0,
                                "added": added if isinstance(added, float) and math.isfinite(added) else 0.0})
                    except (struct.error, ValueError, IndexError, TypeError) as error:
                        raise Failure(f"{path}: the damage table did not parse: {error}") from error
            self._damage[path] = rows
        return self._damage[path]


def tag_names(value: str) -> list[str]:
    """@return The tags of a text of the game ("1H_Melee,Sickle" or "1H_Melee+Sickle")."""
    return [name.strip() for name in value.replace("+", ",").split(",") if name.strip()]


def read_xml(sources, path: str, named_by: str) -> ElementTree.Element | None:
    """@return The root of an XML file that a file of the game names. No value when the file is not in the pak files
    (the tool then has no attack for the slice). A file that is not correct XML stops the tool."""
    name = game_path(path)
    if not name or not sources.has(name):
        return None
    data = sources.read(name)
    if data is None:
        return None
    try:
        return ElementTree.fromstring(data.decode("utf-8-sig", "replace"))
    except ElementTree.ParseError as error:
        raise Failure(f"{name} (a file that {named_by} names) is not correct XML: {error}") from error


class TagBits:
    """The bit of each tag of one tag definition file, by the rule of the client (see the head of the file)."""

    def __init__(self, sources, path: str) -> None:
        self.path = game_path(path)
        # (group name or None, tag names). The groups and the tags of an imported file come first.
        groups: list[tuple[str, list[str]]] = []
        single: list[str] = []
        seen: set[str] = set()
        self._read(sources, self.path, groups, single, seen, 0)
        self.masks: dict[str, tuple[int, int]] = {}
        placed_groups: set[int] = set()
        placed_single: set[int] = set()
        for byte in range(TAG_BYTES):
            used = 0
            for index, (_, names) in enumerate(groups):
                bits = len(names).bit_length()
                if index in placed_groups or used + bits > 8:
                    continue
                placed_groups.add(index)
                for number, name in enumerate(names, start=1):
                    self.masks[name.lower()] = (byte, number << used)
                used += bits
            for index, name in enumerate(single):
                if index in placed_single or used >= 8:
                    continue
                placed_single.add(index)
                self.masks[name.lower()] = (byte, 1 << used)
                used += 1
        self.complete = len(placed_groups) == len(groups) and len(placed_single) == len(single)

    def _read(self, sources, path: str, groups: list, single: list, seen: set, depth: int) -> None:
        if depth > 8:
            raise Failure(f"{path}: the tag definition files import each other more than 8 times")
        root = read_xml(sources, path, "an animation controller")
        if root is None:
            return
        for imported in root.iter("Import"):
            self._read(sources, imported.get("filename", ""), groups, single, seen, depth + 1)
        tags = root.find("Tags")
        for entry in (tags if tags is not None else []):
            if entry.tag == "Tag":
                name = entry.get("name", "")
                if name and name.lower() not in seen:
                    seen.add(name.lower())
                    single.append(name)
            else:
                names = []
                for tag in entry.iter("Tag"):
                    name = tag.get("name", "")
                    if name and name.lower() not in seen:
                        seen.add(name.lower())
                        names.append(name)
                if names:
                    groups.append((entry.get("name", ""), names))

    def state(self, tags: list[str]) -> tuple[str, list[str]]:
        """@return The tag state of these tags as 24 hexadecimal digits, and the tags that the file does not have."""
        state = bytearray(TAG_BYTES)
        unknown = []
        for name in tags:
            place = self.masks.get(name.lower())
            if place is None:
                unknown.append(name)
            else:
                state[place[0]] |= place[1]
        return state.hex(), unknown


class Animations:
    """The variants of the fragments of the animation databases of the game, read when a slice names a file."""

    def __init__(self, sources) -> None:
        self.sources = sources
        self._files: dict[str, dict[str, list[dict]]] = {}

    def variants(self, path: str, extra_tags: tuple[str, ...] = (), depth: int = 0) -> dict[str, list[dict]]:
        """@return The variants of each fragment of one database: lower-case fragment name to a list of
        {"tags": set, "contacts": [(time, parameters)], "end": time or no value}, in the sequence of the file."""
        key = game_path(path)
        if not extra_tags and key in self._files:
            return self._files[key]
        fragments: dict[str, list[dict]] = {}
        if not extra_tags:
            self._files[key] = fragments
        root = read_xml(self.sources, key, "a slice or a weapon") if depth <= 8 else None
        if root is None:
            return fragments
        listed = root.find("FragmentList")
        for fragment in (listed if listed is not None else []):
            for variant in fragment.findall("Fragment"):
                tags = {name.lower() for name in tag_names(variant.get("Tags", ""))} | {name.lower() for name in extra_tags}
                entry = {"tags": tags, "contacts": [], "end": None}
                for layer in variant.findall("AnimLayer"):
                    time = 0.0
                    last = None
                    items = list(layer)
                    for position, item in enumerate(items):
                        if item.tag != "Blend":
                            continue
                        step = number_of(item.get("ExitTime"))
                        if step > 0.0:
                            time += step
                        following = items[position + 1] if position + 1 < len(items) else None
                        # The blend to an empty animation is the end of the fragment.
                        if following is not None and following.tag == "Animation" and not following.get("name"):
                            last = time if step > 0.0 else None
                    if last is not None and (entry["end"] is None or last > entry["end"]):
                        entry["end"] = last
                for layer in variant.findall("ProcLayer"):
                    time = 0.0
                    for item in layer:
                        if item.tag == "Blend":
                            step = number_of(item.get("ExitTime"))
                            if step > 0.0:
                                time += step
                        elif item.tag == "Procedural" and item.get("type") == "CAGE-Damage":
                            parameters = item.find("ProceduralParams")
                            values = {value.tag: value.get("value", "") for value in parameters} if parameters is not None else {}
                            entry["contacts"].append((time, values))
                fragments.setdefault(fragment.tag.lower(), []).append(entry)
        included = root.find("SubADBs")
        for sub in (included.iter("SubADB") if included is not None else []):
            more = tuple(extra_tags) + tuple(tag_names(sub.get("Tags", "")))
            for name, entries in self.variants(sub.get("File", ""), more, depth + 1).items():
                fragments.setdefault(name, []).extend(entries)
        return fragments


def number_of(value: str | None) -> float:
    try:
        number = float(value or "0")
    except ValueError:
        return 0.0
    return number if math.isfinite(number) else 0.0


def select(variants: list[dict], tags: set[str], offhand: bool) -> dict | None:
    """@return The variant that a creature with these tags plays: the one with the most tags whose tags are all
    tags of the creature (the first one of the file for an equal number). The variants of the off hand have the
    tag "Offhand"."""
    best = None
    for variant in variants:
        own = variant["tags"]
        if (OFFHAND_TAG in own) != offhand or not (own - {OFFHAND_TAG}) <= tags:
            continue
        if best is None or len(own) > len(best["tags"]):
            best = variant
    return best


def reach_of(parameters: dict) -> float:
    """@return The reach of one contact from the shape of its damage event, in meters (a project value)."""
    joint = parameters.get("JointName", "").lower()
    if joint not in ROOT_JOINTS or joint == "master_root":
        # The shape is at an arm or at a weapon: the tool does not know the place of the joint.
        return REACH_AT_JOINT

    def value(name: str, fallback: float = 0.0) -> float:
        number = number_of(parameters.get(name))
        return number if number != 0.0 else fallback

    scale = abs(value("ScaleY", 1.0))
    kind = parameters.get("MeleeAttackShapeCastType", "")
    if kind.endswith("Box"):
        half = abs(value("MeleeAttackBoxDimensionsY")) / 2.0
    elif kind.endswith("Capsule"):
        half = abs(value("MeleeAttackShapeRadius")) + abs(value("MeleeAttackCapsuleHalfHeight"))
    else:
        half = abs(value("MeleeAttackShapeRadius"))
    reach = (abs(value("OffsetY")) + half) * scale + REACH_MARGIN
    return min(max(reach, REACH_SMALLEST), REACH_LARGEST)


class Behavior:
    """The reader of the behavior data of the slices of one creature file."""

    def __init__(self, sources, tables_folder: str) -> None:
        self.sources = sources
        self.tables = Tables(sources, tables_folder)
        self.animations = Animations(sources)
        self._tags: dict[str, TagBits] = {}

    def tag_bits(self, controller_path: str) -> TagBits | None:
        controller = read_xml(self.sources, controller_path, "the action list component of a slice")
        entry = controller.find("Tags") if controller is not None else None
        path = game_path(entry.get("filename", "")) if entry is not None else ""
        if not path:
            return None
        if path not in self._tags:
            self._tags[path] = TagBits(self.sources, path)
        return self._tags[path]

    def read(self, roots: list, controller_path: str, actions: list[list]) -> dict:
        """@return The members of a slice for its behavior: "behavior" (when the slice has the values), "tags" (when
        the slice has a tag), "attacks", and "noAttack" (the cause, for a slice with no attack: not a member of
        the file). `actions` is the action table of the slice."""
        result: dict = {}
        behavior = read_behavior(roots)
        if behavior is not None:
            result["behavior"] = behavior
        references = read_references(roots)
        main_hand = self.tables.weapons.get(references["weapons"].get(MAIN_HAND_SLOT, "").lower())
        off_hand = self.tables.weapons.get(references["weapons"].get(OFF_HAND_SLOT, "").lower())
        names: list[str] = []
        for weapon, member in ((main_hand, "tags"), (off_hand, "offhandTags")):
            for name in (weapon[member] if weapon is not None else []):
                if name.lower() not in (known.lower() for known in names):
                    names.append(name)
        bits = self.tag_bits(controller_path) if controller_path else None
        if names and bits is not None:
            state, unknown = bits.state(names)
            names = [name for name in names if name not in unknown]
            if int(state, 16) != 0:
                result["tags"] = state
        tags = {name.lower() for name in names}
        rows = self.tables.damage_rows(references["damageTable"])
        if not references["animations"]:
            result["noAttack"] = "the slice names no animation database"
            return result
        if rows is None:
            result["noAttack"] = "the slice names no damage table"
            return result
        databases = [references["animations"]]
        for weapon in (main_hand, off_hand):
            if weapon is not None and weapon["animations"] and weapon["animations"] not in databases:
                databases.append(weapon["animations"])
        attacks = []
        causes: set[str] = set()
        for index, (name, fragment, fragment_index, _) in enumerate(actions):
            if fragment_index < 0:
                continue
            events = []
            end = None
            for position, database in enumerate(databases):
                variants = self.animations.variants(database).get(fragment.lower(), [])
                for offhand in (False, True):
                    variant = select(variants, tags, offhand)
                    if variant is None:
                        continue
                    events.extend(variant["contacts"])
                    if position == 0 and not offhand:
                        end = variant["end"]
            events = [event for event in events if event[1].get("DamageSelf", "").lower() != "true"]
            if not events:
                continue
            if LEFT_OUT_WORDS & set(name.lower().split("_")):
                causes.add("its attacks are parts of a chain, reactions, attacks in a run, or attacks on a structure")
                continue
            contacts = []
            usable = True
            for time, parameters in sorted(events, key=lambda event: event[0]):
                row = rows.get(parameters.get("DamageTableRow", "").lower())
                if row is None:
                    causes.add("an attack names a damage row that the damage table of the slice does not have")
                    usable = False
                elif row["ranged"]:
                    causes.add("its attacks are ranged attacks")
                    usable = False
                elif row["type"] not in self.tables.types:
                    causes.add("an attack has a damage row with no damage type")
                    usable = False
                elif row["factor"] < SMALLEST_FACTOR:
                    causes.add("an attack has a damage factor below 0.05")
                    usable = False
                if not usable:
                    break
                wire, category = self.tables.types[row["type"]]
                contacts.append([rounded(time), rounded(reach_of(parameters)), rounded(row["factor"]), rounded(row["added"]),
                                 row["type"], wire, category, row["id"]])
            if not usable or not contacts:
                continue
            last = contacts[-1][0]
            length = end if end is not None and end > last else last + TIME_AFTER_CONTACT
            attacks.append([index, rounded(min(length, LONGEST_ATTACK)), contacts])
        if attacks:
            result["attacks"] = attacks
        else:
            result["noAttack"] = "; ".join(sorted(causes)) or "no action of its action table has a damage event"
        return result

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

The ranged attacks (stage 3.4f-A, research creature-ranged.md Q2, R0, and Q5): each action of the
action table whose selected variant has one CAGE-RangedAttack event or more (the release of a
projectile; its time is the sum of the Blend times before it, as for a damage event). Each
release gets its joins (the slice of the creature, its weapons and ammunition, and the tables):
  - the profile: RangedAttackProfile of the weapon of the main hand (WeaponItemDefinitions), and
    the attack: the RangedAttackName of the event in that profile of the ranged attack data. The
    index of the attack on the wire is its place in the list of all attacks of all profiles, the
    profiles and then the attacks of a profile in the order of their names (case-sensitive bytes:
    client code 0x5BF9F30; the caller gives the list);
  - the projectile slice: the SliceOverride of the event, or (UseAmmo) the AmmoPrefabPath of the
    ammunition in the AmmoSlot of the slice (AmmoItemDefinitions); the ammunition key is the
    CRC-32 of the lower-case AmmoID (0 with an override or with no ammunition);
  - the damage table: the unique name of the damage table of the slice and its CRC-32 of the
    lower-case name, and the row: its place among the rows with a DamageID (client code 0x677CEC0)
    and its values (the factor, the added damage, the type, the rune charge);
  - the aim values of the event: AILeadTarget, ProjectileSpeed (a speed for the aim, not of the
    flight), AILeadTargetMaxAngle, AIAimMaxAngle, AIMissMinDistance, AIMissMaxDistance.
A release with a join that the data does not have makes no ranged attack of its action (the
count of its cause): the tool does not make a melee attack or a hit with no flight from it.

The ground areas (stage 3.4f-C, research creature-ranged.md Q4, live check L8): each action of the action table whose selected
variant (the tags of the ranged attacks) has one CAGE-CastSpell event or more (the cast of a spell; its time is the sum of the
Blend times before it). Each cast names its spell (SpellName), whose row and slice must have an area shape of the backend
(spells.area_shape: "status", the mushroom of the Dryad Shaman; "strike", the lightning strike of the Undead Shaman), and has the
aim values of the event (AIAimAtTarget, AILeadTarget, ProjectileSpeed, AILeadTargetMaxAngle, AIAimMaxAngle, AIMissMinDistance,
AIMissMaxDistance, UseAllAvailableTargets). A "strike" needs the row of its spell in the damage table of the slice (the damage of the
hit). An action with a cast of a different spell, or with a cast whose join fails, makes no area attack (the count `areasLeftOut`).
"""

from __future__ import annotations

import math
import struct
import sys
import xml.etree.ElementTree as ElementTree
import zlib
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
DEFAULT_TAGS_FIELD = field_id("m_defaultTags")
VALUE_FIELD = field_id("m_value")
# Stage BT2: the server trees of a slice (the BehaviorTreeComponent of its entity "AI Functionality", research BT0). Each element below
# the field is one asset value: the asset id (16 bytes), 16 bytes, the asset type (16 bytes), the length of the path (8 bytes, big
# endian), and the path. Each path of the 503 slices of the supported build is in the folder SERVER_TREE_FOLDER (research BT0).
TREE_ASSETS_FIELD = field_id("m_behaviorTreeAssets")
SERVER_TREE_FOLDER = "sharedassets/ai/behaviortrees/"
SERVER_TREE_SUFFIX = ".behaviortree"

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
# The ranged attacks (stage 3.4f-A). The paperdoll slots of the AmmoSlot of a clip (client enum PaperdollSlotTypes at image
# RVA 0x5D19E40; QUICK_SLOT_1 is slot 14: the slices of the skeleton archer have their Arcane_AI_Arrow there).
RANGED_EVENT = "CAGE-RangedAttack"
# Stage 3.4f-C: the cast of a spell in a fragment (the ground areas of the creatures).
CAST_EVENT = "CAGE-CastSpell"
# Stage 3.4f-D: a mark of a fragment (the status actions: the action list gives statuses at the mark).
NAMED_EVENT = "CAGE-Named"
AMMO_SLOTS = {"PaperdollSlotTypes::ARROW_AMMO_SLOT": 12, "PaperdollSlotTypes::CARTRIDGE_AMMO_SLOT": 13,
              "PaperdollSlotTypes::QUICK_SLOT_1": 14}
AMMO_TYPE = "AmmoItemDefinitions"
# The basic shot of an archer (stage 3.4f-A, a project rule of the backend: gamedata::kBasicShotAction) and the basic cast of a caster
# (stage 3.4f-B: gamedata::kBasicCastAction): the ranged attacks that a creature uses. The other ranged attacks of the file (a charged or
# a fast shot, a rolling spell) wait for later stages.
BASIC_SHOT_ACTION = "FireBow"
BASIC_CAST_ACTION = "Cast_One_Projectile"
BASIC_RANGED_ACTIONS = (BASIC_SHOT_ACTION, BASIC_CAST_ACTION)
# A release of a basic ranged action whose join fails stops the tool (code review of stage 3.4f-A, finding CR-R5: a broken source must
# not replace a complete export with fewer attacks), but for these authored cases of the retail data, each with its reason: the slice
# keeps no such basic attack in the file. Key: (the slice path, the action, the cause). Empty: the retail data has none.
BASIC_RANGED_EXCEPTIONS: dict[tuple[str, str, str], str] = {}
# A cast of an area spell of the list (a supported spell: creature_slices.json, areaSpells) with a malformed event or a broken join stops
# the tool (code review of stage 3.4f-C, finding AR-R7: the cast went to `areasLeftOut`, and an export with status 0 lost the area attack
# of an area-only creature), but for these authored cases of the retail data, each with its reason: the slice keeps no such area attack.
# Key: (the slice path, the action, the cause). The count `areasLeftOut` keeps only the casts of spells and actions with no support. A
# spell of the list whose spell row, slice asset, slice, or shape is not there is a broken join too (code review of stage 3.4f-D, finding
# SU-R4: the tool replaced the export with no area attack).
AREA_EXCEPTIONS: dict[tuple[str, str, str], str] = {
    ("slices/characters/ghost.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/ghost_charred.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/ghost_frozen.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/ghost_shackled.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/ghost_shackled_boss.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/ghost_shipwrecked.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/seasons/season_04/ghost_s4q2.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/seasons/season_04/ghost_s4q2_charred.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/seasons/season_04/ghost_s4q2_shackled.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/worldevent_ai_ghost_charred_20325.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/worldevent_ai_ghost_frozen_20325.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/worldevent_ai_ghost_shipwrecked_20325.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/worldevent_ai_ghost_starved_20325.dynamicslice", "Spell_Plagued_Plague_Nova",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the ghosts share one action list with the plague nova; only the plagued ghost has its row in its damage table",
    ("slices/characters/corrupted_dynasty_summoner.dynamicslice", "AOE_Ranged_Spell",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the action list of the summoner has the area cast; its damage table has no row of the spell (stage 3.4f-C: left out)",
    ("slices/pois/wexvariants/r_+02_+02_ebonscalereach/dynasty_summoner_towerbeam_08qp08_elite.dynamicslice", "AOE_Ranged_Spell",
     "the damage row of the spell is not a row of the damage table of the slice"):
        "the action list of the summoner has the area cast; its damage table has no row of the spell (stage 3.4f-C: left out)",
}
# An action of a ranged event that is a movement or an attack on a structure is not a ranged attack of the file (a project
# rule): the backend has no rule for a shot in a walk (WalkBack of the skeleton archer) or for a structure.
RANGED_LEFT_OUT_WORDS = LEFT_OUT_WORDS | {"walkback", "walk", "move"}
# The time of an attack whose fragment has no end blend and whose animation length the tool cannot read: its last contact plus
# this (seconds; user test 2: the tool reads the animation length first, UT2-7).
TIME_AFTER_CONTACT = 0.9
LONGEST_ATTACK = 12.0
# The reaction fragments of the five power levels (r_R0_F to r_R4_F: the reaction to a hit from the front). Stage 3.3a: the knockdown of an
# ability plays power 4, a stagger power 2; user test 2: a hit plays the power of its damage row (PowerLevel_AI).
REACTION_FRAGMENTS = ("r_R0_F", "r_R1_F", "r_R2_F", "r_R3_F", "r_R4_F")
# The animation files of the creatures (user test 2, UT2-7): the length of an animation clip of a fragment, by the name of the clip (the
# file name with no suffix). The motion parameters chunk of a CAF file has the frames and the time of a frame (the reader of
# weapon_actions.py for the player).
CREATURE_ANIMATION_FOLDER = "animations/gameplay/character/npc/"
MOTION_CHUNK = 0x3002
# Stage U3 (research creature-combat-feel.md Q1): the Homing clips of an attack fragment (ProcLayer/Procedural type="Homing"). The
# start of a clip is the sum of the Blend ExitTime values before it in its layer; its own duration is the ExitTime of the next Blend (no
# next Blend: to the end of the fragment). The values of a clip in the file, in this order (the member `homing` of an attack). The client
# applies a clip only to its own character (client code 0x255A140, 0x2564FC0, 0x2565210; live check LC-Q1-2): the server moves a creature.
HOMING_CLIP = "Homing"
HOMING_NUMBERS = ("MoveToDistance", "MoveToDuration", "MoveToVelocity", "MaxMoveDistance", "TurnRate", "Radius", "MaxAngle", "Height")
HOMING_FLAGS = ("MoveToTarget", "UseExponential", "LockInitialTargetPos", "AllowMovementAfterReachingTargetPos", "AllowUpwardsMovement",
                "UseRadiusforAITarget", "AllowRotationAfterReachingTargetAngle")
# Stage U3 (Q3): the HitStun clip of a reaction fragment (client code 0x2559640: the rest of the reaction plays so that it ends at the hit
# stun of the hit). Its start is the sum of the Blend ExitTime values before it.
HIT_STUN_CLIP = "HitStun"
# Stage U3: the two reaction actions of an action table (the full reaction and the additive one).
REACTION_ACTION = "Reaction"
ADDITIVE_ACTION = "Reaction_Additive"


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


def slice_path(prefab: str) -> str:
    """@return The slice file of a prefab path of a table ("Items/Weapons/Basic_AI_Arrow")."""
    path = game_path(prefab)
    if not path.startswith("slices/"):
        path = "slices/" + path
    return path if path.endswith(".dynamicslice") else path + ".dynamicslice"


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


def server_tree_name(value: bytes, slice_path: str) -> str:
    """@return The name of a server tree from one asset value of m_behaviorTreeAssets: its path in lower case, with no folder
    SERVER_TREE_FOLDER and no suffix SERVER_TREE_SUFFIX ("wolf/wolf_base"). A value that is not an asset with a path of a tree stops the
    tool."""
    length = int.from_bytes(value[48:56], "big") if len(value) >= 56 else -1
    path = value[56:].decode("utf-8", "replace").replace("\\", "/").lower() if length == len(value) - 56 else ""
    if not path.startswith(SERVER_TREE_FOLDER) or not path.endswith(SERVER_TREE_SUFFIX) or len(path) <= len(SERVER_TREE_FOLDER) + len(
            SERVER_TREE_SUFFIX):
        raise Failure(f"{slice_path}: a value of m_behaviorTreeAssets is not the asset of a behavior tree ({value[:64].hex()})")
    return path[len(SERVER_TREE_FOLDER):-len(SERVER_TREE_SUFFIX)]


def read_server_trees(roots: list, slice_path: str) -> list[str]:
    """@return The server trees that a slice names (stage BT2), in the sequence of the slice, each one time: the names of
    server_tree_name. The tree files are not in the client (research BT0): a name is a binding key of the backend
    (apps/backend/behavior/bindings.jsonc), not a tree that the backend runs."""
    names: list[str] = []
    for root in roots:
        for node in walk(root):
            if node.name != TREE_ASSETS_FIELD:
                continue
            for entry in walk(node):
                if entry is not node and entry.value is not None and len(entry.value) > 0:
                    name = server_tree_name(entry.value, slice_path)
                    if name not in names:
                        names.append(name)
    return names


def read_default_tags(roots: list) -> set[str]:
    """@return The lower-case default fragment tags of a slice (the texts below its field m_defaultTags; stage 3.4f-A)."""
    tags: set[str] = set()
    for root in roots:
        for node in walk(root):
            if node.name == DEFAULT_TAGS_FIELD:
                tags.update(text(entry).lower() for entry in walk(node) if entry is not node and text(entry))
    return tags


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
        # The ammunition (stage 3.4f-A): the lower-case AmmoID to its AmmoID and the slice of its AmmoPrefabPath.
        self.ammo: dict[str, dict] = {}
        self._damage: dict[str, dict | None] = {}
        for path in sources.paths(tables_folder, ".datasheet"):
            name = path.rsplit("/", 1)[-1]
            if "itemdefinitions_weapons" not in name and "damagetypes" not in name and "itemdefinitions_ammo" not in name:
                continue
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            try:
                sheet = Sheet(data)
                for row in range(sheet.row_count):
                    if kind == WEAPON_TYPE:
                        profile = str(sheet.cell(row, "RangedAttackProfile")) if "RangedAttackProfile" in sheet.columns else ""
                        self.weapons.setdefault(str(sheet.cell(row, "WeaponID")).lower(), {
                            "tags": tag_names(str(sheet.cell(row, "MannequinTag"))),
                            "offhandTags": tag_names(str(sheet.cell(row, "OffHandMannequinTag"))),
                            "animations": game_path(str(sheet.cell(row, "AnimDbPath"))),
                            "profile": profile.strip()})
                    elif kind == AMMO_TYPE:
                        ammo = str(sheet.cell(row, "AmmoID")).strip()
                        prefab = str(sheet.cell(row, "AmmoPrefabPath")).strip() if "AmmoPrefabPath" in sheet.columns else ""
                        if ammo:
                            self.ammo.setdefault(ammo.lower(), {"id": ammo, "slice": slice_path(prefab) if prefab else ""})
                    elif kind == DAMAGE_TYPE_TYPE:
                        wire = sheet.cell(row, "IntID")
                        if str(sheet.cell(row, "TypeID")) and isinstance(wire, float) and 0 < wire < 256:
                            self.types.setdefault(str(sheet.cell(row, "TypeID")),
                                                  (int(wire), str(sheet.cell(row, "Category"))))
            except (struct.error, ValueError, IndexError, TypeError) as error:
                raise Failure(f"{path}: the table did not parse: {error}") from error
        if not self.weapons or not self.types:
            raise Failure("the game copy has no weapon table or no damage type table")
        if not self.ammo:
            raise Failure("the game copy has no ammunition table (AmmoItemDefinitions)")

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
                        # The unique name of the table (its header, not the file name) and the place of each row among the
                        # rows with a DamageID (stage 3.4f-A: the client finds a row of a server projectile by this place).
                        table = {"name": sheet.name, "next": 0}
                        for row in range(sheet.row_count):
                            factor, added = sheet.cell(row, "DmgCoef"), sheet.cell(row, "AddDmg")
                            # The heart rune charge that a player gets who takes the hit (stage 1.3b). A table
                            # with no such column gives no charge.
                            charge = sheet.cell(row, "HitRuneCharge") if "HitRuneCharge" in sheet.columns else 0.0
                            identifier = str(sheet.cell(row, "DamageID"))
                            ordinal = table["next"]
                            if identifier:
                                table["next"] += 1
                            rows.setdefault(identifier.lower(), {
                                "ordinal": ordinal,
                                "table": table["name"],
                                "charge": charge if isinstance(charge, float) and math.isfinite(charge) and charge > 0.0 else 0.0,
                                "id": str(sheet.cell(row, "DamageID")),
                                "type": str(sheet.cell(row, "DamageType")),
                                # Stage B4: the attack type of the row (Light, Heavy, Magic, or empty; research bow-build-2.md Q5): the
                                # condition AttackType of a rule of a hit that a player takes (Enchanted Ward). A table with no column: empty.
                                "attackType": str(sheet.cell(row, "AttackType")) if "AttackType" in sheet.columns else "",
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


def caf_length(data: bytes, path: str) -> float:
    """@return The length of an animation file (seconds): (end frame - start frame) x ticks per frame x seconds per tick of its motion
    parameters chunk."""
    if data[:4] != b"CrCh" or len(data) < 16:
        raise Failure(f"{path} is not an animation chunk file")
    count, table = struct.unpack_from("<II", data, 8)
    for index in range(count):
        if table + 16 * (index + 1) > len(data):
            break
        kind, _, _, size, offset = struct.unpack_from("<HHIII", data, table + 16 * index)
        if kind == MOTION_CHUNK and size >= 24 and offset + 24 <= len(data):
            _, _, ticks, seconds, start, end = struct.unpack_from("<IIifii", data, offset)
            length = (end - start) * ticks * seconds
            if not math.isfinite(length) or length <= 0.0:
                raise Failure(f"{path} has a motion parameters chunk with no length")
            return length
    raise Failure(f"{path} has no motion parameters chunk")


def animation_clips(layer) -> list[dict]:
    """@return The animation clips of one AnimLayer: the name, the start (the sum of the ExitTime values before it), the flags, the speed,
    the StartTime of its blend, and "afterPrevious" (ExitTime -1: the clip starts at the end of the clip before it)."""
    clips = []
    time = 0.0
    blend: dict = {}
    for item in layer:
        if item.tag == "Blend":
            blend = dict(item.attrib)
            time += max(number_of(blend.get("ExitTime")), 0.0)
        elif item.tag == "Animation":
            clips.append({"name": item.get("name", ""), "start": time, "flags": item.get("flags", ""),
                          "speed": number_of(item.get("speed")) or 1.0, "startTime": number_of(blend.get("StartTime")),
                          "afterPrevious": number_of(blend.get("ExitTime")) < 0.0})
    return clips


class Animations:
    """The variants of the fragments of the animation databases of the game, read when a slice names a file."""

    def __init__(self, sources) -> None:
        self.sources = sources
        self._files: dict[str, dict[str, list[dict]]] = {}
        # The animation files of the creatures by the lower-case name of a clip (user test 2), read at the first length.
        self._caf: dict[str, list[str]] | None = None
        self._lengths: dict[str, float | None] = {}

    def clip_length(self, name: str) -> float | None:
        """@return The length of the animation of a clip (seconds), or no value: no file of that name, or two files of different lengths."""
        if self._caf is None:
            self._caf = {}
            for path in self.sources.paths(CREATURE_ANIMATION_FOLDER, ".caf"):
                self._caf.setdefault(path.rsplit("/", 1)[-1][:-4], []).append(path)
        key = name.lower()
        if key not in self._lengths:
            values = set()
            for path in self._caf.get(key, []):
                data = self.sources.read(path)
                if data is not None:
                    values.add(round(caf_length(data, path), 6))
            self._lengths[key] = values.pop() if len(values) == 1 else None
        return self._lengths[key]

    def fragment_length(self, variant: dict | None) -> float | None:
        """@return The length of a variant of a fragment (seconds): its end blend (the blend to an empty animation), else the end of the last
        animation clip of its first animation layer (the lengths of the animation files; the rule of weapon_actions.py). No value: the
        variant has neither, a clip has no file, or its last clip loops (the fragment ends only at a transition)."""
        if variant is None:
            return None
        if variant["end"] is not None:
            return variant["end"]
        clips = variant.get("clips") or []
        if not clips:
            return None
        end = 0.0
        previous_name = None
        animation_time = 0.0
        previous_start = 0.0
        previous_speed = 1.0
        shift = 0.0
        for index, clip in enumerate(clips):
            length = self.clip_length(clip["name"]) if clip["name"] else None
            if length is None or clip["speed"] <= 0.0:
                return None
            if "Loop" in clip["flags"].split("+") and index == len(clips) - 1:
                return None
            if clip["afterPrevious"] and index > 0:
                shift = end - clip["start"]
            start = clip["start"] + shift
            if clip["name"] == previous_name:
                animation_time += (start - previous_start) * previous_speed
            else:
                animation_time = clip["startTime"]
            end = start + max(length - animation_time, 0.0) / clip["speed"]
            previous_name, previous_start, previous_speed = clip["name"], start, clip["speed"]
        return end if end > 0.0 else None

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
                entry = {"tags": tags, "contacts": [], "ranged": [], "casts": [], "named": [], "end": None, "clips": [], "homing": [],
                         "hitStun": None}
                for layer_number, layer in enumerate(variant.findall("AnimLayer")):
                    if layer_number == 0:
                        entry["clips"] = animation_clips(layer)
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
                    # A time of a ranged event needs each ExitTime before it as a finite number (code review of stage 3.4f-A, finding
                    # CR-R6): a text that is not a number gives the event no time (None), not the time 0.
                    broken = False
                    items = list(layer)
                    for position, item in enumerate(items):
                        if item.tag == "Procedural" and item.get("type") == HOMING_CLIP:
                            # Stage U3 (Q1): a Homing clip of the fragment: its start, its own duration, and its values.
                            entry["homing"].append(homing_clip(items, position, None if broken else time))
                        elif item.tag == "Procedural" and item.get("type") == HIT_STUN_CLIP and entry["hitStun"] is None and not broken:
                            # Stage U3 (Q3): the HitStun clip (its start; client code 0x2559640 scales the rest of the reaction).
                            entry["hitStun"] = time
                        if item.tag == "Blend":
                            strict = strict_number(item.get("ExitTime"))
                            broken = broken or strict is None
                            step = number_of(item.get("ExitTime"))
                            if step > 0.0:
                                time += step
                        elif item.tag == "Procedural" and item.get("type") == NAMED_EVENT:
                            # A mark of the fragment (stage 3.4f-D): the action list reads it with IfMannequinMarker.
                            parameters = item.find("ProceduralParams")
                            named = parameters.find("Name") if parameters is not None else None
                            if named is not None and named.get("value"):
                                entry["named"].append((None if broken else time, named.get("value")))
                        elif item.tag == "Procedural" and item.get("type") in ("CAGE-Damage", RANGED_EVENT, CAST_EVENT):
                            parameters = item.find("ProceduralParams")
                            values = {value.tag: value.get("value", "") for value in parameters} if parameters is not None else {}
                            if item.get("type") == "CAGE-Damage":
                                entry["contacts"].append((time, values))
                            elif item.get("type") == CAST_EVENT:
                                entry["casts"].append((None if broken else time, values))
                            else:
                                entry["ranged"].append((None if broken else time, values))
                fragments.setdefault(fragment.tag.lower(), []).append(entry)
        included = root.find("SubADBs")
        for sub in (included.iter("SubADB") if included is not None else []):
            more = tuple(extra_tags) + tuple(tag_names(sub.get("Tags", "")))
            for name, entries in self.variants(sub.get("File", ""), more, depth + 1).items():
                fragments.setdefault(name, []).extend(entries)
        return fragments


def homing_clip(items: list, position: int, start: float | None) -> dict:
    """@return One Homing clip of a ProcLayer (stage U3): its start (no value: a time before it is not a finite number), its own duration
    (the ExitTime of the next Blend; no value: to the end of the fragment), the numbers of HOMING_NUMBERS (no value: not a finite number),
    and the flags of HOMING_FLAGS."""
    following = next((later for later in items[position + 1:] if later.tag == "Blend"), None)
    duration: float | None = -1.0  # No next Blend: the clip lasts to the end of the fragment.
    if following is not None:
        duration = strict_number(following.get("ExitTime"))
        duration = max(duration, 0.0) if duration is not None else None
    parameters = items[position].find("ProceduralParams")
    values = {value.tag: value.get("value", "") for value in parameters} if parameters is not None else {}
    clip = {"start": start, "duration": duration}
    for name in HOMING_NUMBERS:
        clip[name] = strict_number(values.get(name))
    for name in HOMING_FLAGS:
        clip[name] = values.get(name, "").strip().lower() == "true"
    return clip


def homing_member(clips: list[dict]) -> tuple[list[list] | None, str]:
    """@return The member `homing` of an attack (stage U3): one list for each clip of the selected variant, [start, duration (-1: to the
    end of the fragment), MoveToTarget, MoveToDistance, MoveToDuration, MoveToVelocity, MaxMoveDistance, TurnRate, UseExponential, Radius,
    MaxAngle, Height, LockInitialTargetPos, AllowMovementAfterReachingTargetPos, AllowUpwardsMovement, UseRadiusforAITarget,
    AllowRotationAfterReachingTargetAngle] (flags as 0 or 1), in the sequence of their starts; or no value and the cause (a time or a
    value that is not a finite number: the attack gets no homing)."""
    rows = []
    for clip in clips:
        if clip["start"] is None or clip["duration"] is None or any(clip[name] is None for name in HOMING_NUMBERS):
            return None, "a Homing clip has a time or a value that is not a finite number"
        flag = {name: 1 if clip[name] else 0 for name in HOMING_FLAGS}
        rows.append([rounded(clip["start"]), rounded(clip["duration"]) if clip["duration"] >= 0.0 else -1.0, flag["MoveToTarget"],
                     rounded(clip["MoveToDistance"]), rounded(clip["MoveToDuration"]), rounded(clip["MoveToVelocity"]),
                     rounded(clip["MaxMoveDistance"]), rounded(clip["TurnRate"]), flag["UseExponential"], rounded(clip["Radius"]),
                     rounded(clip["MaxAngle"]), rounded(clip["Height"]), flag["LockInitialTargetPos"],
                     flag["AllowMovementAfterReachingTargetPos"], flag["AllowUpwardsMovement"], flag["UseRadiusforAITarget"],
                     flag["AllowRotationAfterReachingTargetAngle"]])
    rows.sort(key=lambda row: row[0])
    return rows, ""


def number_of(value: str | None) -> float:
    try:
        number = float(value or "0")
    except ValueError:
        return 0.0
    return number if math.isfinite(number) else 0.0


def strict_number(value: str | None) -> float | None:
    """@return The number of an attribute: 0 for no text (the default of the format), no value for a text that is not a finite number
    (code review of stage 3.4f-A, finding CR-R6)."""
    if value is None or not value.strip():
        return 0.0
    try:
        number = float(value)
    except ValueError:
        return None
    return number if math.isfinite(number) else None


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

    def __init__(self, sources, tables_folder: str, ranged_attacks: dict | None = None, projectile_of=None, area_spells=None,
                 status_actions: dict | None = None) -> None:
        """@param ranged_attacks The attacks of the ranged attack data (stage 3.4f-A): (lower-case profile, lower-case attack) to
               {"profile", "attack", "index", "hitScan", "range", "radius", "accelDelay"}. None: the file has no ranged attack.
        @param projectile_of A function: the path of a projectile slice to its index in the list "projectiles" of the file, or
               None for a slice with no asset, no Projectile component, or no flight values."""
        self.sources = sources
        self.tables = Tables(sources, tables_folder)
        self.animations = Animations(sources)
        self._tags: dict[str, TagBits] = {}
        self.ranged_attacks = ranged_attacks
        self.projectile_of = projectile_of
        # The causes of the ranged attacks that are not in the file, with their counts (a member of the counts of the file).
        self.ranged_left_out: dict[str, int] = {}
        # Stage 3.4f-C: the spells of the casts (spells.AreaSpells), and the causes of the area attacks that are not in the file.
        self.area_spells = area_spells
        self.areas_left_out: dict[str, int] = {}
        # Stage 3.4f-D: the status actions of the list statusActions of creature_slices.json (lower-case action name to its live check),
        # and the causes of a listed action of a slice that is not a status action of the file.
        self.status_actions = status_actions
        self.status_left_out: dict[str, int] = {}
        # User test 2 (UT2-7): the source of the length of each attack (its end blend, the length of its animation, or the last contact plus
        # TIME_AFTER_CONTACT), and the reaction fragments with a length and with none.
        self.attack_lengths = {"endBlend": 0, "animation": 0, "guess": 0}
        self.reaction_lengths = {"known": 0, "unknown": 0}
        # Stage U3: the attacks with Homing clips, the clips, the clips that move (MoveToTarget), and the attacks whose clips have a value
        # that is not a finite number (no homing).
        self.homing_counts = {"attacks": 0, "clips": 0, "moving": 0, "leftOut": 0}
        # Stage U3: the reaction tables of the slices (the rows in the file, the rows that never hold in the backend, the side fragments
        # with no variant that take the front fragment of their power), and the slices with an additive reaction.
        self.reaction_table_counts = {"slices": 0, "rows": 0, "neverRows": 0, "noVariant": 0, "additive": 0, "hitStunClips": 0,
                                      "missingTables": 0}

    def release(self, values: dict, references: dict, main_hand: dict | None, rows: dict) -> tuple[dict | None, str]:
        """@return The joins of one release of a ranged attack (see the head of the file), or no value and the cause."""
        row = rows.get(values.get("DamageTableRow", "").lower())
        if row is None:
            return None, "a ranged attack names a damage row that the damage table of the slice does not have"
        if row["type"] not in self.tables.types:
            return None, "a ranged attack has a damage row with no damage type"
        profile = main_hand["profile"] if main_hand is not None else ""
        if not profile:
            return None, "the weapon of the main hand has no ranged attack profile"
        attack = self.ranged_attacks.get((profile.lower(), values.get("RangedAttackName", "").lower()))
        if attack is None:
            return None, "the profile of the weapon has no attack with the RangedAttackName of the clip"
        if attack["hitScan"]:
            return None, "the attack of the profile is a hit scan"
        override = values.get("SliceOverride", "").strip()
        ammo_id = ""
        if override:
            path = slice_path(override)
        elif values.get("UseAmmo", "").lower() == "true":
            slot = AMMO_SLOTS.get(values.get("AmmoSlot", ""))
            item = references["weapons"].get(slot, "") if slot is not None else ""
            ammo = self.tables.ammo.get(item.lower()) if item else None
            if ammo is None or not ammo["slice"]:
                return None, "the ammunition of the AmmoSlot of the clip is not in the slice or has no prefab"
            path, ammo_id = ammo["slice"], ammo["id"]
        else:
            return None, "the clip uses no ammunition and names no slice"
        projectile = self.projectile_of(path) if self.projectile_of is not None else None
        if projectile is None:
            return None, "the projectile slice has no asset, no Projectile component, or no flight values"
        if isinstance(projectile, str):
            return None, projectile
        aim = {name: strict_number(values.get(name)) for name in ("ProjectileSpeed", "AILeadTargetMaxAngle", "AIAimMaxAngle",
                                                                  "AIMissMinDistance", "AIMissMaxDistance")}
        if any(value is None for value in aim.values()):
            return None, "a ranged attack has an aim value that is not a finite number"

        def number(name: str) -> float:
            return rounded(aim[name])

        wire, category = self.tables.types[row["type"]]
        return {"profile": attack["profile"], "attack": attack["attack"], "index": attack["index"], "ammo": ammo_id,
                "ammoKey": zlib.crc32(ammo_id.lower().encode()) if ammo_id else 0, "projectile": projectile,
                "override": bool(override), "table": row["table"], "tableKey": zlib.crc32(row["table"].lower().encode()),
                "row": row["id"], "rowIndex": row["ordinal"], "factor": rounded(row["factor"]), "added": rounded(row["added"]),
                "type": row["type"], "wire": wire, "category": category, "charge": rounded(row["charge"]),
                "attackType": row.get("attackType", ""), "lead": values.get("AILeadTarget", "").lower() == "true", "aimSpeed": number("ProjectileSpeed"),
                "leadAngle": number("AILeadTargetMaxAngle"), "aimAngle": number("AIAimMaxAngle"),
                "miss": [number("AIMissMinDistance"), number("AIMissMaxDistance")], "range": attack["range"],
                "radius": attack["radius"], "accelDelay": attack["accelDelay"]}, ""

    def attack_length(self, chosen: dict | None, last: float) -> float:
        """@return The length of an attack (user test 2, UT2-7): the end blend of its fragment, else the end of its animation (the CAF file),
        else its last contact plus TIME_AFTER_CONTACT. The server ends the action at this time, and each observer gets the idle record then."""
        end = chosen["end"] if chosen is not None else None
        animated = self.animations.fragment_length(chosen) if end is None else None
        if end is not None and end > last:
            self.attack_lengths["endBlend"] += 1
            return end
        if animated is not None and animated > last:
            self.attack_lengths["animation"] += 1
            return animated
        self.attack_lengths["guess"] += 1
        return last + TIME_AFTER_CONTACT

    def reaction_lengths_of(self, databases: list[str], tags: set[str]) -> list[float]:
        """@return The length of each reaction fragment of REACTION_FRAGMENTS (user test 2, UT2-4 and UT2-7): the variant of the tags (the
        weapons and the default tags of the slice) in the first database that has one, its end blend or the end of its animation; -1: not
        known. The reaction to a hit and its push last this time (protocol notes 4.26)."""
        lengths = []
        for name in REACTION_FRAGMENTS:
            length = None
            for database in databases:
                variant = select(self.animations.variants(database).get(name.lower(), []), tags, False)
                if variant is not None:
                    length = self.animations.fragment_length(variant)
                    break
            self.reaction_lengths["known" if length is not None else "unknown"] += 1
            lengths.append(rounded(min(length, LONGEST_ATTACK)) if length is not None else -1.0)
        return lengths

    def fragment_facts(self, databases: list[str], tags: set[str], name: str) -> tuple[bool, float | None, float | None]:
        """@return For one fragment name: a variant of the tags in one database (True), the length of that variant (no value: not known),
        and the start of its HitStun clip (no value: none). The first database that has a variant gives it."""
        for database in databases:
            variant = select(self.animations.variants(database).get(name.lower(), []), tags, False)
            if variant is not None:
                return True, self.animations.fragment_length(variant), variant["hitStun"]
        return False, None, None

    def table_member(self, action: dict, positions: dict[str, int], databases: list[str], tags: set[str]) -> dict:
        """@return The member of one reaction action (stage U3, research Q2, Q3, Q5): {"rows": [[power (-1: each power), angle ranges
        ([[min, max, tolerance], ...] in degrees; no value: each angle), fragment index, length (-1: not known), start of the HitStun clip
        (-1: none), AlignToDirection (0 or 1), AlignOffsetAngle (degrees)], ...] in the sequence of the table (a row whose condition never
        holds in the backend is not in the list: the first row that holds wins, client code 0x2B9F1C0), "motion": [[name, Scale,
        PushBackScale], ...] (the entries of ReactionMotion: Default first), "exit": ExitOffsetSeconds of IfFragmentDone}.

        The fragment of a row: its index in the fragment list of the slice. A name that the list does not have keeps the fragment of the
        action (client code 0x2B9F1C0). A name of the list with no variant of the tags in the databases of the slice takes the fragment
        of the front row of the same power (a project rule until live check LC-Q2-2: what the client shows for a fragment with no variant
        is not known)."""
        rows = []
        raw = action["table"]

        def holds(condition) -> bool | None:
            kind = condition[0]
            if kind == "tag":
                return condition[1].lower() in tags
            return None if kind == "angles" else kind == "always"

        def front_of(power: int) -> str | None:
            for row in raw:
                if row[0] == power and row[1][0] == "angles" and any(low - tol <= 0.0 <= high + tol for low, high, tol in row[1][1]):
                    return row[2]
            return None

        for power, condition, name, align, offset in raw:
            state = holds(condition)
            if state is False:
                self.reaction_table_counts["neverRows"] += 1
                continue
            angles = condition[1] if condition[0] == "angles" else None
            fragment = name
            index = positions.get(fragment.lower(), -1)
            if index < 0:
                fragment = action["fragment"]
                index = action["fragmentIndex"]
            found, length, stun = self.fragment_facts(databases, tags, fragment)
            if not found:
                front = front_of(power)
                if front is not None and front.lower() != fragment.lower() and positions.get(front.lower(), -1) >= 0:
                    self.reaction_table_counts["noVariant"] += 1
                    fragment = front
                    index = positions[front.lower()]
                    found, length, stun = self.fragment_facts(databases, tags, fragment)
            if stun is not None:
                self.reaction_table_counts["hitStunClips"] += 1
            rows.append([power, angles, index, rounded(min(length, LONGEST_ATTACK)) if length is not None else -1.0,
                         rounded(stun) if stun is not None else -1.0, 1 if align else 0, rounded(offset)])
            self.reaction_table_counts["rows"] += 1
        return {"rows": rows, "motion": action["motion"], "exit": rounded(action["exit"])}

    def tag_bits(self, controller_path: str) -> TagBits | None:
        controller = read_xml(self.sources, controller_path, "the action list component of a slice")
        entry = controller.find("Tags") if controller is not None else None
        path = game_path(entry.get("filename", "")) if entry is not None else ""
        if not path:
            return None
        if path not in self._tags:
            self._tags[path] = TagBits(self.sources, path)
        return self._tags[path]

    def read(self, roots: list, controller_path: str, actions: list[list], slice_path: str = "", markers: dict | None = None,
             reactions: dict | None = None) -> dict:
        """@return The members of a slice for its behavior: "behavior" (when the slice has the values), "tags" (when
        the slice has a tag), "attacks", and "noAttack" (the cause, for a slice with no attack: not a member of
        the file). `actions` is the action table of the slice. `reactions` (stage U3, creature_data.read_reactions): the fragment list,
        the scopes, and the reaction actions of the slice; with it the members "reactionTable" and "additive"."""
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
        databases = [references["animations"]]
        for weapon in (main_hand, off_hand):
            if weapon is not None and weapon["animations"] and weapon["animations"] not in databases:
                databases.append(weapon["animations"])
        # The length of each reaction fragment (user test 2, UT2-4 and UT2-7): the variant of the tags of the weapons and the default tags of
        # the slice (the Inquisitor Cleric plays the variant 1H_Caster+Staff+Inquisitor), in the first database that has one. The reaction to a
        # hit and its push last this time (protocol notes 4.26: the client pushes a creature to the end of the reaction fragment). -1: no length.
        reaction_lengths = self.reaction_lengths_of(databases, tags | read_default_tags(roots))
        if any(length > 0.0 for length in reaction_lengths):
            result["reactionLengths"] = reaction_lengths
        # Stage U3: the reaction table of the action Reaction of the slice (the side, the power, the length, the HitStun clip, the push
        # scales) and the additive reaction (the action Reaction_Additive in its own scope). The tags of the reaction lengths select the
        # variants.
        if reactions:
            reaction_tags = tags | read_default_tags(roots)
            for action in reactions["actions"].values():
                if action["table"] is None:
                    self.reaction_table_counts["missingTables"] += 1
            full = reactions["actions"].get(REACTION_ACTION)
            if full is not None and full["table"]:
                result["reactionTable"] = self.table_member(full, reactions["positions"], databases, reaction_tags)
                self.reaction_table_counts["slices"] += 1
            additive = reactions["actions"].get(ADDITIVE_ACTION)
            # The scope of the additive fragment: 2 or more (the client reads the time of such a scope from scopeTimeBlobEx, client
            # code 0x2A2ACD0; the time fields of scope 1 are not known). Scope 0 is the full body: no additive reaction.
            if additive is not None and additive["table"] and 2 <= additive["scope"] < 16:
                member = self.table_member(additive, reactions["positions"], databases, reaction_tags)
                member["action"] = additive["index"]
                member["scope"] = additive["scope"]
                result["additive"] = member
                self.reaction_table_counts["additive"] += 1
        if rows is None:
            result["noAttack"] = "the slice names no damage table"
            return result
        attacks = []
        causes: set[str] = set()
        for index, (name, fragment, fragment_index, _) in enumerate(actions):
            if fragment_index < 0:
                continue
            events = []
            chosen = None
            for position, database in enumerate(databases):
                variants = self.animations.variants(database).get(fragment.lower(), [])
                for offhand in (False, True):
                    variant = select(variants, tags, offhand)
                    if variant is None:
                        continue
                    events.extend(variant["contacts"])
                    if position == 0 and not offhand:
                        chosen = variant
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
                                 row["type"], wire, category, row["id"], rounded(row["charge"]), row.get("attackType", "")])
            if not usable or not contacts:
                continue
            attack = [index, rounded(min(self.attack_length(chosen, contacts[-1][0]), LONGEST_ATTACK)), contacts]
            # Stage U3 (Q1): the Homing clips of the selected variant (the variant of the creature in its own database). The server moves
            # and turns the creature by them (the client does not: live check LC-Q1-2). No clip: no member (the old form).
            homing, cause = homing_member(chosen["homing"] if chosen is not None else [])
            if homing is None:
                self.homing_counts["leftOut"] += 1
            elif homing:
                attack.append(homing)
                self.homing_counts["attacks"] += 1
                self.homing_counts["clips"] += len(homing)
                self.homing_counts["moving"] += sum(1 for row in homing if row[2] == 1)
            attacks.append(attack)
        if attacks:
            result["attacks"] = attacks
        # The variant of a ranged attack: the tags of the weapons and the default tags of the slice (stage 3.4f-A, research
        # creature-ranged.md Q1: the skeleton archer plays Bow+Skeleton+Bowman, the dryad archer Bow+Dryad+Bowman). The melee
        # attacks keep the tags of the weapons (their selection has the live proof of stage 3.4b).
        ranged_tags = tags | read_default_tags(roots)
        ranged = (self.read_ranged(actions, databases, ranged_tags, references, main_hand, rows, slice_path)
                  if self.ranged_attacks is not None else [])
        if ranged:
            result["ranged"] = ranged
        areas = self.read_areas(actions, databases, ranged_tags, rows, slice_path) if self.area_spells is not None else []
        if areas:
            result["areas"] = areas
        status_actions = (self.read_status_actions(actions, databases, ranged_tags, markers or {}, slice_path)
                          if self.status_actions is not None else [])
        if status_actions:
            result["statusActions"] = status_actions
        if not attacks and not ranged and not areas:
            result["noAttack"] = "; ".join(sorted(causes)) or "no action of its action table has a damage event"
        return result

    def read_ranged(self, actions: list[list], databases: list[str], tags: set[str], references: dict, main_hand: dict | None,
                    rows: dict, slice_path: str = "") -> list[list]:
        """@return The ranged attacks of a slice (stage 3.4f-A): [action, time, [release, ...]], a release with its "time". A release of
        a basic ranged action (BASIC_RANGED_ACTIONS) whose join fails stops the tool (finding CR-R5), but for BASIC_RANGED_EXCEPTIONS."""
        ranged = []
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
                    events.extend(variant["ranged"])
                    if position == 0 and not offhand:
                        end = variant["end"]
            if not events:
                continue
            words = set(name.lower().split("_"))
            if RANGED_LEFT_OUT_WORDS & words or any(word in name.lower() for word in ("walkback", "move")):
                self.count("a ranged action is a part of a chain, a reaction, a movement, or an attack on a structure")
                continue
            releases = []
            basic = name in BASIC_RANGED_ACTIONS
            for time, values in sorted(events, key=lambda event: (event[0] is None, event[0] or 0.0)):
                if time is None:
                    release, cause = None, "a ranged event has a time that is not a finite number"
                else:
                    release, cause = self.release(values, references, main_hand, rows)
                if release is None:
                    if basic and (slice_path, name, cause) not in BASIC_RANGED_EXCEPTIONS:
                        raise Failure(f"the basic ranged action {name} has a release whose join fails: {cause}")
                    self.count(cause)
                    releases = []
                    break
                releases.append({"time": rounded(time), **release})
            if not releases:
                continue
            last = releases[-1]["time"]
            length = end if end is not None and end > last else last + TIME_AFTER_CONTACT
            ranged.append([index, rounded(min(length, LONGEST_ATTACK)), releases])
        return ranged

    def count(self, cause: str) -> None:
        self.ranged_left_out[cause] = self.ranged_left_out.get(cause, 0) + 1

    def read_areas(self, actions: list[list], databases: list[str], tags: set[str], rows: dict, slice_path: str = "") -> list[list]:
        """@return The area attacks of a slice (stage 3.4f-C): [action, length, [cast, ...]], each cast with its "time", "spell", "shape", the
        aim values, and for a "strike" the damage of the row of its spell (see the head of the file). A cast of a supported spell with a
        malformed event or a broken join stops the tool (finding AR-R7), but for AREA_EXCEPTIONS."""
        areas = []
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
                    events.extend(variant["casts"])
                    if position == 0 and not offhand:
                        end = variant["end"]
            if not events:
                continue
            if LEFT_OUT_WORDS & set(name.lower().split("_")):
                self.count_area("a cast action is a part of a chain, a reaction, a run, a throw, or a summon")
                continue
            casts = []
            for time, values in sorted(events, key=lambda event: (event[0] is None, event[0] or 0.0)):
                cast, cause, broken = self.area_cast(time, values, rows)
                if cast is None and broken:
                    reason = AREA_EXCEPTIONS.get((slice_path, name, cause))
                    if reason is None:
                        raise Failure(f"{slice_path}: the action {name} casts the supported spell {values.get('SpellName', '').strip()}, "
                                      f"and {cause} (code review of stage 3.4f-C, finding AR-R7; an authored case of the retail data "
                                      f"goes into AREA_EXCEPTIONS with its reason)")
                    self.count_area(f"{cause} (an authored case of the retail data: {reason})")
                    casts = []
                    break
                if cast is None:
                    self.count_area(cause)
                    casts = []
                    break
                casts.append(cast)
            if not casts:
                continue
            last = casts[-1]["time"]
            length = end if end is not None and end > last else last + TIME_AFTER_CONTACT
            areas.append([index, rounded(min(length, LONGEST_ATTACK)), casts])
        return areas

    def area_cast(self, time: float | None, values: dict, rows: dict) -> tuple[dict | None, str, bool]:
        """@return The joins of one cast of an area attack (stage 3.4f-C), or no value, the cause, and True for a cast of a supported spell
        (a malformed event or a broken join: finding AR-R7)."""
        spell = values.get("SpellName", "").strip()
        if not spell:
            return None, "a cast event names no spell", False
        found, cause = self.area_spells.shape(spell)
        if found is None:
            # A spell of the list with no spell row, no slice asset, no slice, or no supported shape is a broken join too (code review of
            # stage 3.4f-D, finding SU-R4); a spell that is not in the list is a count of `areasLeftOut`.
            return None, cause, self.area_spells.listed(spell)
        if time is None:
            return None, "a cast event has a time that is not a finite number", True
        aim = {name: strict_number(values.get(name)) for name in ("ProjectileSpeed", "AILeadTargetMaxAngle", "AIAimMaxAngle",
                                                                  "AIMissMinDistance", "AIMissMaxDistance")}
        if any(value is None for value in aim.values()):
            return None, "a cast event has an aim value that is not a finite number", True
        cast = {"time": rounded(time), "spell": found["spell"], "shape": found["shape"],
                "aimAtTarget": values.get("AIAimAtTarget", "").lower() == "true", "lead": values.get("AILeadTarget", "").lower() == "true",
                "aimSpeed": rounded(aim["ProjectileSpeed"]), "leadAngle": rounded(aim["AILeadTargetMaxAngle"]),
                "aimAngle": rounded(aim["AIAimMaxAngle"]), "miss": [rounded(aim["AIMissMinDistance"]), rounded(aim["AIMissMaxDistance"])],
                "allTargets": values.get("UseAllAvailableTargets", "").lower() == "true"}
        if found["shape"] == "strike":
            row = rows.get(found["row"].lower())
            if row is None or row["table"].lower() != found["table"].lower():
                return None, "the damage row of the spell is not a row of the damage table of the slice", True
            if row["type"] not in self.tables.types:
                return None, "the damage row of the spell has no damage type", True
            wire, category = self.tables.types[row["type"]]
            cast.update({"row": row["id"], "factor": rounded(row["factor"]), "added": rounded(row["added"]), "type": row["type"],
                         "wire": wire, "category": category, "charge": rounded(row["charge"]), "attackType": row.get("attackType", "")})
        return cast, "", False

    def read_status_actions(self, actions: list[list], databases: list[str], tags: set[str], markers: dict, slice_path: str = "") -> list[list]:
        """@return The status actions of a slice (stage 3.4f-D): [action, length, [[time, [status, ...]], ...]] for each action of the list
        statusActions whose action list gives statuses at a mark (`markers`: the action name to [(mark, [status, ...])], read_action_table of
        creature_data.py) and whose selected variant has the CAGE-Named event of the mark (its time). A mark with a time that is not a finite
        number stops the tool (a listed action: the rule of finding AR-R7)."""
        found = []
        for index, (name, fragment, fragment_index, _) in enumerate(actions):
            if fragment_index < 0 or name.lower() not in self.status_actions:
                continue
            links = markers.get(name, [])
            if not links:
                self.count_status("the action list of the action gives no status at a mark")
                continue
            named = []
            end = None
            for position, database in enumerate(databases):
                variants = self.animations.variants(database).get(fragment.lower(), [])
                variant = select(variants, tags, False)
                if variant is None:
                    continue
                named.extend(variant["named"])
                if position == 0:
                    end = variant["end"]
            marks = []
            for mark, statuses in links:
                times = [time for time, label in named if label == mark]
                if not times:
                    continue
                if any(time is None for time in times):
                    raise Failure(f"{slice_path}: the mark {mark} of the status action {name} has a time that is not a finite number")
                marks.append([rounded(min(times)), sorted(statuses)])
            if not marks:
                self.count_status("the variant of the fragment of the slice has no CAGE-Named event of the mark of the action list")
                continue
            marks.sort(key=lambda entry: entry[0])
            last = marks[-1][0]
            length = end if end is not None and end > last else last + TIME_AFTER_CONTACT
            found.append([index, rounded(min(length, LONGEST_ATTACK)), marks])
        return found

    def count_status(self, cause: str) -> None:
        self.status_left_out[cause] = self.status_left_out.get(cause, 0) + 1

    def count_area(self, cause: str) -> None:
        self.areas_left_out[cause] = self.areas_left_out.get(cause, 0) + 1

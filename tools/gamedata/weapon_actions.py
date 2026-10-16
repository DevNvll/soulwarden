#!/usr/bin/env python3
"""Write the basic attacks of each melee weapon of the game as one JSON file (stage 3.3b-M1).

The backend reads this file and follows the basic attacks of a character as the client plays them (protocol notes 4.32): the
light chain, the partial and the full charge of a heavy attack, and the fast sub-hits. For each fragment of the attack graph
it gets the transitions (the condition, the window of the fragment clock, the buffered or instant kind, the next fragment), the
strikes (the damage key, the window, the damage row of each clip with the learned nodes that the clip needs, the shapes), the
payments, the dodge windows, and the length of the fragment. The damage rows are in the combat tables
(tools/gamedata/combat.sh). Without the file the backend starts and works: each melee hit then uses the light or the heavy row of
the weapon table (the rule of stage 3.2a).

Input: the game copy, from the same sources as the creature data tool (creature_data.py).

  Source                                                  What the file gets
  WeaponItemDefinitions (datatables)                      the tag set (MannequinTag) of each melee weapon (PrimaryUse "Combat
                                                          Melee", a tag 1H_Melee or 2H_Melee); the dagger is excluded (the
                                                          client cannot start its six abilities: research abilities-melee.md K11)
  animations/mannequin/adb/player/player_<weapon>.adb     the body fragments of the weapon, and the common fragments of
  and player_1h_melee.adb or player_2h_melee.adb          player_1h_melee.adb or player_2h_melee.adb: the transitions
                                                          (CAGE-TransitionBuffered, CAGE-TransitionInstant), the damage clips
                                                          (CAGE-Damage), the payments (PayStaminaCost, CAGE-PayManaCost), the dodge
                                                          windows (CAGE-Named CanDodgeCancel, CanDodgeBuffer), the animation clips
  animations/mannequin/adb/playermaleanims_1h_meleeattach.adb and _2h_meleeattach.adb
                                                          the damage clips of the weapon attachment (the same fragment names)
  playeractions_*.actionlist (cage folder of the player)  the fragment of each action (Action FragmentName), and the named
                                                          conditions of the damage clips (IfAbilityActive tests)
  animations/gameplay/character/player/male/**.caf        the length of each animation (the motion parameters chunk 0x3002:
                                                          (end - start) x ticks per frame x seconds per tick)

The fragment of a weapon: the variant of the fragment name with no FragTags (a FragTag is a weapon oil) whose tags are all in
the tag set of the weapon, the one with the most tags (the weapon file first at a tie). The time of a clip is the sum of the
ExitTime values of the Blend elements before it in its layer; its end is the start of the next clip of the layer (research
abilities-melee.md, Appendix C). The graph of a weapon starts at Attack_Start_Charge and follows the transitions with a
condition of the basic attacks:

  Condition (action lists)        Kind in the file   The input of the step (protocol notes 4.32)
  CanAttack_Heavy, CanAttack_Primary  "virtual"      the attack input (bit 555) or its hold (191); a buffered window takes a press
  CanAttack_Primary_Rapier        "press"            the attack input (attack_primary, bit 555) only
  Attack_Heavy_NotHeld            "notHeld"          neither the attack input nor its hold
  Attack_Primary_Released         "released"         the release (bit 567)
  (none), Test_AlwaysTrue         "always"           no condition

A transition with a different condition (an ability, a sprint, a block, an unsheathe) is not in the graph: the list
"transitionsNotExported" counts them by condition.

The length of a fragment: the end of the last animation clip of its first animation layer. A clip starts at the time of its
animation given by the StartTime of its Blend (seconds); a clip of the same animation as the clip before it continues the time
of that animation (live check I1 of stage 3.3b-M1: the first swing of the war hammer, 44 input steps). The clip ends when the
animation ends, at its speed. A Blend with a negative ExitTime (-1) starts its clip at the end of the clip before it (the
Mannequin rule; the sprint attack of the hatchet). A fragment whose last clip loops (the flag Loop) has no end of its own, and a
fragment with an animation that the game copy does not have: the length 0 (not known).

Output: {"format": 1, "counts": {...},
"weapons": [{"tags": [tag, ...], "files": [body file, base file, attachment file], "start": "Attack_Start_Charge",
  "fragments": [{"name": N, "tags": "2H_Melee+GreatAxe", "length": S, "canTransition": S or null,
    "transitions": [[kind ("buffered"|"instant"), start, end or null, condition kind, next fragment, condition name], ...],
    "strikes": [[key, start, end, [[row, [needed node, ...], [forbidden node, ...]], ...], [shape, ...]], ...],
    "payments": [[time, "stamina"|"mana", cost id, multiplier, named condition or ""], ...],
    "dodge": [[start, end or null, buffered], ...]}, ...]}, ...],
"transitionsNotExported": {condition: count, ...}, "clipsNotExported": [[weapon, fragment, key, row, cause], ...],
"weaponsNotExported": [[tags, cause], ...], "animationsNotFound": [name, ...]}
A shape is [cast type, radius, half height, box x, box y, box z, offset x, offset y, offset z, scale x, scale y, scale z, joint].
A strike groups the clips of one damage key of one fragment (body and attachment, each shape): one hit of each target. A
clip whose named condition is not an AND of learned-node tests is not exported (the list "clipsNotExported").

A tag set with no Attack_Start_Charge is not exported (the list "weaponsNotExported"). The tool writes no file (status 1) when a
file is not in the game copy, when a
transition names an action that no action list has, when a damage clip has no row, or when a number is not finite.

Usage: weapon_actions.py [OUTPUT.json]
       weapon_actions.py --find TEXT...   print the fragments of the weapons whose tags have a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/weapon-actions.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys
from pathlib import Path
from xml.etree import ElementTree

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "weapon-actions.json"
ADB_FOLDER = "animations/mannequin/adb/"
BODY_FOLDER = ADB_FOLDER + "player/"
ACTION_FOLDER = "sharedassets/springboardentitites/playerdata/cage/"
ANIMATION_FOLDER = "animations/gameplay/character/player/male/"
WANTED = (TABLES_FOLDER, ADB_FOLDER, ACTION_FOLDER, ANIMATION_FOLDER)
WEAPON_TYPE = "WeaponItemDefinitions"
START = "Attack_Start_Charge"
# The body file of each weapon tag (the file names of the game: the hatchet has the tag Axe).
BODY_FILES = {"sword": "player_sword.adb", "rapier": "player_rapier.adb", "axe": "player_axe.adb", "flail": "player_flail.adb",
              "spear": "player_spear.adb", "greataxe": "player_greataxe.adb", "warhammer": "player_warhammer.adb",
              "greatsword": "player_greatsword.adb"}
BASE_FILES = {"1h_melee": ("player_1h_melee.adb", "playermaleanims_1h_meleeattach.adb"),
              "2h_melee": ("player_2h_melee.adb", "playermaleanims_2h_meleeattach.adb")}
# The dagger: the client cannot start its abilities (research abilities-melee.md, K11); stage 3.3b excludes it.
EXCLUDED_TAGS = frozenset({"dagger"})
CONDITIONS = {"canattack_heavy": "virtual", "canattack_primary": "virtual", "canattack_primary_rapier": "press",
              "attack_heavy_notheld": "notHeld", "attack_primary_released": "released", "": "always", "test_alwaystrue": "always"}
SHAPE_FIELDS = ("MeleeAttackShapeCastType", "MeleeAttackShapeRadius", "MeleeAttackCapsuleHalfHeight", "MeleeAttackBoxDimensionsX",
                "MeleeAttackBoxDimensionsY", "MeleeAttackBoxDimensionsZ", "OffsetX", "OffsetY", "OffsetZ", "ScaleX", "ScaleY", "ScaleZ",
                "JointName")
MOTION_CHUNK = 0x3002


def rounded(value: float) -> float:
    if not math.isfinite(value):
        raise Failure(f"a number is not finite: {value}")
    return round(value, 6)


def read_xml(sources: Sources, path: str) -> ElementTree.Element:
    data = sources.read(path)
    if data is None:
        sources.check_complete()
        raise Failure(f"{path} has no source")
    try:
        return ElementTree.fromstring(data.decode("utf-8-sig", "replace"))
    except ElementTree.ParseError as error:
        raise Failure(f"{path} is not correct XML: {error}") from error


def weapon_tag_sets(sources: Sources) -> list[tuple[str, ...]]:
    """@return Each tag set of a melee weapon of the weapon tables (PrimaryUse Combat Melee, a tag 1H_Melee or 2H_Melee)."""
    # One entry for each tag set with no regard to the case of a letter (the tables have GreatAxe and Greataxe): the first
    # spelling of the tables.
    found: dict[tuple[str, ...], tuple[str, ...]] = {}
    tables = 0
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None or sheet_type(data) != WEAPON_TYPE:
            continue
        sheet = Sheet(data)
        tables += 1
        for column in ("PrimaryUse", "MannequinTag"):
            if column not in sheet.columns:
                raise Failure(f"{path}: the weapon table has no column {column}")
        for row in range(sheet.row_count):
            if str(sheet.cell(row, "PrimaryUse")) != "Combat Melee":
                continue
            tags = tuple(sorted({tag.strip() for tag in str(sheet.cell(row, "MannequinTag")).split(",") if tag.strip()}, key=str.lower))
            lower = {tag.lower() for tag in tags}
            if not lower & set(BASE_FILES) or lower & EXCLUDED_TAGS or not lower & set(BODY_FILES):
                continue
            found.setdefault(tuple(sorted(lower)), tags)
    sources.check_complete()
    if not tables:
        raise Failure(f"the game copy has no data table of the type {WEAPON_TYPE}")
    return [found[key] for key in sorted(found)]


def layer_clips(layer: ElementTree.Element, kind: str) -> list[dict]:
    """@return The clips of one layer with their start (the sum of the ExitTime values before them) and end."""
    clips: list[dict] = []
    time = 0.0
    blend: dict = {}
    for node in layer:
        if node.tag == "Blend":
            blend = dict(node.attrib)
            # A negative ExitTime (-1): the clip starts at the end of the clip before it (fragment_length has the animation lengths).
            time += max(float(blend.get("ExitTime") or 0.0), 0.0)
        elif node.tag == kind:
            clip = {"start": time, "blend": blend, "attrib": dict(node.attrib),
                    "afterPrevious": float(blend.get("ExitTime") or 0.0) < 0.0}
            if kind == "Procedural":
                parameters = node.find("ProceduralParams")
                clip["params"] = {child.tag: child.get("value", "") for child in parameters} if parameters is not None else {}
                clip["type"] = node.get("type", "")
            clips.append(clip)
    for index, clip in enumerate(clips):
        clip["end"] = clips[index + 1]["start"] if index + 1 < len(clips) else None
    return clips


def read_fragments(sources: Sources, path: str) -> dict[str, list[dict]]:
    """@return The variants of each fragment name of an animation database, in file order."""
    root = read_xml(sources, path)
    fragment_list = root.find("FragmentList")
    result: dict[str, list[dict]] = {}
    if fragment_list is None:
        return result
    for group in fragment_list:
        for fragment in group.findall("Fragment"):
            tags = {tag for tag in fragment.get("Tags", "").split("+") if tag}
            variant = {"name": group.tag, "tags": tags, "tagText": fragment.get("Tags", ""), "fragTags": fragment.get("FragTags", ""),
                       "file": path.rsplit("/", 1)[-1],
                       "procedural": [clip for layer in fragment.findall("ProcLayer") for clip in layer_clips(layer, "Procedural")],
                       "animation": [layer_clips(layer, "Animation") for layer in fragment.findall("AnimLayer")]}
            result.setdefault(group.tag, []).append(variant)
    return result


def select(variants: list[list[dict]], weapon: set[str]) -> dict | None:
    """@return The variant with no FragTags whose tags are in the weapon tags, with the most tags (the first file first at a tie)."""
    best = None
    best_size = -1
    for file_variants in variants:
        for variant in file_variants:
            if variant["fragTags"] or not {tag.lower() for tag in variant["tags"]} <= weapon:
                continue
            if len(variant["tags"]) > best_size:
                best, best_size = variant, len(variant["tags"])
    return best


class Animations:
    """The length of each player animation of the game copy, by the name of the animation (the file name with no suffix)."""

    def __init__(self, sources: Sources) -> None:
        self.sources = sources
        self.paths: dict[str, list[str]] = {}
        for path in sources.paths(ANIMATION_FOLDER, ".caf"):
            self.paths.setdefault(path.rsplit("/", 1)[-1][:-4], []).append(path)
        self.lengths: dict[str, float | None] = {}
        self.missing: set[str] = set()

    def length(self, name: str) -> float | None:
        key = name.lower()
        if key in self.lengths:
            return self.lengths[key]
        values = set()
        for path in self.paths.get(key, []):
            data = self.sources.read(path)
            if data is None:
                continue
            values.add(caf_length(data, path))
        # A name with two files of different lengths is not known: no value.
        self.lengths[key] = values.pop() if len(values) == 1 else None
        if self.lengths[key] is None:
            self.missing.add(name)
        return self.lengths[key]


def caf_length(data: bytes, path: str) -> float:
    if data[:4] != b"CrCh":
        raise Failure(f"{path} is not an animation chunk file")
    count, table = struct.unpack_from("<II", data, 8)
    for index in range(count):
        kind, _, _, size, offset = struct.unpack_from("<HHIII", data, table + 16 * index)
        if kind == MOTION_CHUNK and size >= 24:
            _, _, ticks, seconds, start, end = struct.unpack_from("<IIifii", data, offset)
            return rounded((end - start) * ticks * seconds)
    raise Failure(f"{path} has no motion parameters chunk")


def fragment_length(variant: dict, animations: Animations) -> float:
    """@return The end of the last animation clip of the first animation layer (seconds), or 0 when it is not known."""
    if not variant["animation"] or not variant["animation"][0]:
        return 0.0
    clips = variant["animation"][0]
    end = 0.0
    previous_name = None
    animation_time = 0.0
    previous_start = 0.0
    previous_speed = 1.0
    shift = 0.0
    for index, clip in enumerate(clips):
        name = clip["attrib"].get("name", "")
        speed = float(clip["attrib"].get("speed") or 1.0)
        length = animations.length(name) if name else None
        if length is None or speed <= 0.0:
            return 0.0
        if "Loop" in clip["attrib"].get("flags", "").split("+") and index == len(clips) - 1:
            return 0.0  # The last clip loops: the fragment ends only at a transition.
        if clip.get("afterPrevious") and index > 0:
            # ExitTime -1: the clip starts at the end of the clip before it; the later clips move with it.
            shift = end - clip["start"]
        start = clip["start"] + shift
        if name == previous_name:
            # The same animation as the clip before: its time continues (live check I1).
            animation_time += (start - previous_start) * previous_speed
        else:
            animation_time = float(clip["blend"].get("StartTime") or 0.0)
        end = start + max(length - animation_time, 0.0) / speed
        previous_name, previous_start, previous_speed = name, start, speed
    return rounded(end)


def read_actions(sources: Sources) -> tuple[dict[str, str], dict[str, ElementTree.Element]]:
    """@return The fragment of each action (by its name in lower case), and each named condition (by its name in lower case)."""
    fragments: dict[str, str] = {}
    conditions: dict[str, ElementTree.Element] = {}
    for path in sources.paths(ACTION_FOLDER, ".actionlist"):
        root = read_xml(sources, path)
        for element in root.iter():
            name = element.get("Name", "")
            if element.tag == "Action" and name:
                fragments.setdefault(name.lower(), element.get("FragmentName", "") or name)
            elif element.tag in ("And", "Or", "Not") and name:
                conditions.setdefault(name.lower(), element)
    return fragments, conditions


def node_tests(element: ElementTree.Element, negated: bool, tests: list[tuple[str, bool]]) -> bool:
    """Adds the IfAbilityActive tests of a condition (with their sign) to `tests`. @return False for a condition that is not an
    AND of learned-node tests: a test of a different kind, an Or that is not negated with more than one part, or a Not of an And
    with more than one part."""
    if element.tag == "IfAbilityActive":
        name = element.get("AbilityName", "").strip()
        if not name:
            return False
        tests.append((name, negated))
        return True
    if element.tag == "Not":
        children = list(element)
        return len(children) == 1 and node_tests(children[0], not negated, tests)
    if element.tag in ("And", "Or"):
        children = list(element)
        conjunction = (element.tag == "And") != negated
        if not conjunction and len(children) != 1:
            return False
        return all(node_tests(child, negated, tests) for child in children)
    return False


# The input of a condition of a transition inside an ability (the input names of the action lists of the player).
ABILITY_INPUTS = {"attack_primary_virtual": "attack", "block_press_virtual": "block", "character_collided": "collided"}
# The weapons whose abilities the backend plays as fragments (stage 3.3b-M2), and their abilities whose damage is the area of a spell of
# the server (no strike: Shockwave, Path of Destiny). For these weapons the sources must have the fragment of each ability, each fragment
# that its action selects, and a strike of each other ability (code review of stage 3.3b-M2, finding M2-R7): else the tool stops and the
# old file stays.
FRAGMENT_WEAPONS = {"greataxe", "warhammer"}
AREA_ABILITIES = {"ability_warhammer_shockwave", "ability_warhammer_pathofdestiny"}
# The fragments of an ability of these weapons that do no damage with a strike in the game data (a choice whose damage is a spell of the
# server, or none). The list of the supported build is empty: each choice of the other abilities has a strike (finding M2b-R4).
NO_STRIKE_FRAGMENTS: set[str] = set()
# The ability action list of each weapon tag (the hatchet has the tag Axe).
ABILITY_LISTS = {"sword": "playeractions_ability_sword", "rapier": "playeractions_ability_rapier", "axe": "playeractions_ability_hatchet",
                 "flail": "playeractions_ability_flail", "spear": "playeractions_ability_spear",
                 "greataxe": "playeractions_ability_greataxe", "warhammer": "playeractions_ability_warhammer",
                 "greatsword": "playeractions_ability_greatsword"}


def literal_of(element: ElementTree.Element) -> tuple[str, object] | None:
    """@return One literal of a condition of an ability transition ([kind, value]), or None for a test that the backend has no
    source for."""
    if element.tag == "IfAbilityActive":
        name = element.get("AbilityName", "").strip()
        if not name:
            return None
        return ("perk" if element.get("External", "").lower() == "true" else "node", name)
    if element.tag == "IfInput":
        name = ABILITY_INPUTS.get(element.get("InputName", "").strip().lower())
        return ("input", name) if name else None
    if element.tag == "IfAbilityNumHits":
        if element.get("ComparisonType", "").strip() != "LessThan":
            return None
        try:
            return ("hits", int(element.get("NumHits", "")))
        except ValueError:
            return None
    if element.tag == "IfFragmentPlaying":
        name = element.get("FragmentName", "").strip()
        return ("playing", name) if name else None
    if element.tag == "IfWaterDepth":
        return ("water", "")
    if element.tag == "IfMannequinMarker":
        name = element.get("MarkerName", "").strip()
        return ("marker", name) if name else None
    return None


def dnf(element: ElementTree.Element, negated: bool = False) -> list[list[list]] | None:
    """@return A condition as an OR of ANDs of literals [kind, value, negated] (no term: never true; one empty term: always true), or
    None when a test of it has no source in the backend. The backend has no water: a test of the water depth is false."""
    if element.tag == "Not":
        children = list(element)
        return dnf(children[0], not negated) if len(children) == 1 else None
    if element.tag == "True":
        return [] if negated else [[]]
    if element.tag in ("And", "Or"):
        parts = [dnf(child, negated) for child in element]
        if any(part is None for part in parts):
            return None
        if (element.tag == "And") != negated:
            terms: list[list[list]] = [[]]
            for part in parts:
                terms = [left + right for left in terms for right in part]
            return terms
        return [term for part in parts for term in part]
    literal = literal_of(element)
    if literal is None:
        return None
    kind, value = literal
    if kind == "water":
        return [[]] if negated else []
    return [[[kind, value, negated]]]


def condition_terms(name: str, named: dict[str, ElementTree.Element]) -> list[list[list]] | None:
    """@return The terms of a named condition of the action lists (an empty name: always true), or None (not known)."""
    if not name:
        return [[]]
    element = named.get(name.lower())
    return dnf(element) if element is not None else None


def read_abilities(sources: Sources) -> dict[str, dict]:
    """@return The actions of the ability action lists (by the action name in lower case): the list, the fragment, the ability of its
    cooldown (SetCooldownTimer AbilityID; none for a follow-up action), the fragment selections (ChangeActionFragment below an
    IfThenElse), and the spell casts (CastSpell below an IfThenElse, with the marker of its condition)."""
    found: dict[str, dict] = {}
    for path in sources.paths(ACTION_FOLDER, ".actionlist"):
        list_name = path.rsplit("/", 1)[-1][:-len(".actionlist")]
        if not list_name.startswith("playeractions_ability_"):
            continue
        root = read_xml(sources, path)
        for action in root.iter("Action"):
            name = action.get("Name", "")
            fragment = action.get("FragmentName", "") or name
            ability = None
            for timer in action.iter("SetCooldownTimer"):
                for default in timer.iter("Default"):
                    ability = ability or default.get("AbilityID", "").strip() or None
            entry = {"name": name, "fragment": fragment, "ability": ability, "list": list_name, "selections": [], "casts": [],
                     "selectionsNotExported": 0, "castsNotExported": 0,
                     # The named conditions of the conditionals of the movement of the action (the speed of Whirlwind with Gusting
                     # Winds): the client moves its character with them.
                     "motion": sorted({conditional.get("Name", "") for kind in ("AnimationDrivenMotion", "CameraRelativeMotion")
                                       for activity in action.iter(kind) for conditional in activity.iter("Conditional")} - {""})}
            for branch in action.iter("IfThenElse"):
                condition = branch.find("IfCondition")
                then = branch.find("ThenActivity")
                if condition is None or then is None:
                    continue
                wrapper = ElementTree.Element("And")
                wrapper.extend(list(condition))
                terms = dnf(wrapper)
                for change in then.iter("ChangeActionFragment"):
                    for default in change.iter("Default"):
                        target = default.get("FragmentName", "").strip()
                        if default.get("UseDefaultFragment", "").lower() == "true" or not target:
                            continue
                        selection = node_selection(terms)
                        if selection is None:
                            entry["selectionsNotExported"] += 1
                            continue
                        entry["selections"].append([target, *selection])
                for cast in then.iter("CastSpell"):
                    for default in cast.iter("Default"):
                        spell = default.get("SpellName", "").strip()
                        if not spell:
                            continue
                        marker = next((element.get("MarkerName", "") for element in condition.iter("IfMannequinMarker")), "")
                        selection = node_selection(terms, allow_marker=True)
                        if selection is None:
                            entry["castsNotExported"] += 1
                            continue
                        entry["casts"].append([spell, *selection, marker])
            found[name.lower()] = entry
    return found


def node_selection(terms: list[list[list]] | None, allow_marker: bool = False) -> list | None:
    """@return [needs, forbids, perks, perk forbids] of a condition that is one AND of node and perk tests (a test of the playing
    fragment is left out: it is true at the start of the action), or None."""
    if terms is None or len(terms) != 1:
        return None
    needs, forbids, perks, perk_forbids = [], [], [], []
    for kind, value, negated in terms[0]:
        if kind == "node":
            (forbids if negated else needs).append(value)
        elif kind == "perk":
            (perk_forbids if negated else perks).append(value)
        elif kind == "playing" or (kind == "marker" and allow_marker and not negated):
            continue
        else:
            return None
    return [sorted(set(needs), key=str.lower), sorted(set(forbids), key=str.lower), sorted(set(perks), key=str.lower),
            sorted(set(perk_forbids), key=str.lower)]


def build_weapon(tags: tuple[str, ...], body: dict, common: dict, attachment: dict, action_fragments: dict[str, str],
                 named: dict[str, ElementTree.Element], animations, not_exported: dict[str, int], clips_not_exported: list[list],
                 counts: dict, abilities: dict[str, dict] | None = None) -> tuple[list[dict], list[dict]] | None:
    """@return The fragments of the attack graph of one tag set and its abilities, or None when the tag set has no
    Attack_Start_Charge. The graph: from Attack_Start_Charge the basic attacks (the transitions with a condition of a basic attack),
    and from each ability of the ability action list of the weapon its fragment, the fragments that its action selects, and the
    fragments of its transitions inside the ability (a condition of the inputs, nodes, perks, and hits that the backend has).
    `body`, `common`, and `attachment` are the fragments of the body file of the weapon, of the common file, and of the attachment
    file (read_fragments). `abilities` is read_abilities (None: no ability)."""
    lower = {tag.lower() for tag in tags}
    label = "+".join(tags)
    abilities = abilities or {}

    def body_variant(name: str) -> dict | None:
        return select([body.get(name, []), common.get(name, [])], lower)

    if body_variant(START) is None:
        return None
    weapon_tag = next((tag for tag in BODY_FILES if tag in lower), "")
    ability_list = ABILITY_LISTS.get(weapon_tag)
    weapon_abilities = [entry for entry in abilities.values() if entry["list"] == ability_list and entry["ability"]]
    # The ability actions by name, for the transitions into an ability (CanAttack_<ability>): their target is the fragment of the
    # action of the ability.
    ability_actions = {entry["name"].lower(): entry for entry in weapon_abilities}
    markers_wanted = {cast[5] for entry in weapon_abilities for cast in entry["casts"] if cast[5]}

    fragments = []
    grits: list[list] = []
    seen: dict[str, str] = {START: "basic"}
    queue: list[tuple[str, str]] = [(START, "basic")]
    exported_abilities = []
    strict = weapon_tag in FRAGMENT_WEAPONS
    for entry in sorted(weapon_abilities, key=lambda item: item["name"].lower()):
        if strict:
            for name in [entry["fragment"]] + [selection[0] for selection in entry["selections"]]:
                if body_variant(name) is None:
                    raise Failure(f"the ability {entry['ability']} of {label} plays the fragment {name}, which the animation databases do not "
                                  "have: the sources are not complete")
        if body_variant(entry["fragment"]) is None:
            continue
        roots = [entry["fragment"]] + [selection[0] for selection in entry["selections"]]
        roots = [root for root in roots if body_variant(root) is not None]
        for root in roots:
            if root not in seen:
                seen[root] = "ability"
                queue.append((root, "ability"))
        exported_abilities.append({"ability": entry["ability"], "action": entry["name"], "fragment": entry["fragment"],
                                   "selections": [selection for selection in entry["selections"] if body_variant(selection[0]) is not None],
                                   "casts": entry["casts"]})
    while queue:
        name, mode = queue.pop(0)
        variant = body_variant(name)
        if variant is None:
            # A transition of the graph names a fragment that the animation databases do not have: the sources are not complete (code
            # review of stage 3.3b-M1, finding M1-R6). The tool writes no file: the old file stays.
            raise Failure(f"the graph of {label} goes to the fragment {name}, which the animation databases do not have")
        transitions = []
        payments = []
        dodge = []
        markers = []
        can_transition = None
        for clip in variant["procedural"]:
            kind, params = clip["type"], clip["params"]
            if kind in ("CAGE-TransitionInstant", "CAGE-TransitionBuffered"):
                condition = params.get("Condition", "").strip()
                target = params.get("NewFragment", "").strip()
                action = params.get("NewAction", "").strip()
                window = ["buffered" if kind.endswith("Buffered") else "instant", rounded(clip["start"]),
                          None if clip["end"] is None else rounded(clip["end"])]
                if action:
                    if action.lower() not in action_fragments:
                        if CONDITIONS.get(condition.lower()) is None:
                            # A transition of a different kind to an action that no list has (an action name of the game can be wrong
                            # there: Ability_Rapier_Aerial_Lunge_Introa): not exported.
                            not_exported[condition or "(none)"] = not_exported.get(condition or "(none)", 0) + 1
                            continue
                        raise Failure(f"{variant['file']}: the fragment {name} names the action {action}, which no action list has")
                    starts = ability_actions.get(action.lower())
                    if starts is not None and condition.lower().startswith("canattack"):
                        # The start of an ability (its press in this window). An ability whose fragment the animation databases do
                        # not have for these tags (the evade of the rapier selects its fragment by the direction) is not exported.
                        if body_variant(starts["fragment"]) is None:
                            not_exported[condition] = not_exported.get(condition, 0) + 1
                            continue
                        transitions.append(window + ["ability", starts["fragment"], condition, starts["ability"]])
                        if starts["fragment"] not in seen:
                            seen[starts["fragment"]] = "ability"
                            queue.append((starts["fragment"], "ability"))
                        continue
                    target = action_fragments[action.lower()]
                if target.startswith("Attack_"):
                    condition_kind = CONDITIONS.get(condition.lower())
                    if condition_kind is None:
                        # The transition to a sprint, a block, an unsheathe: not a basic attack (an action name of the game can be
                        # wrong there: Ability_Rapier_Aerial_Lunge_Introa).
                        not_exported[condition] = not_exported.get(condition, 0) + 1
                        continue
                    transitions.append(window + [condition_kind, target, condition, None])
                    if target not in seen:
                        seen[target] = "basic"
                        queue.append((target, "basic"))
                    continue
                if mode != "ability" or not target:
                    not_exported[condition or "(none)"] = not_exported.get(condition or "(none)", 0) + 1
                    continue
                # A transition inside an ability (the exit of Whirlwind, the uppercut of Charge): its condition as terms.
                terms = condition_terms(condition, named)
                if terms is None:
                    not_exported[condition or "(none)"] = not_exported.get(condition or "(none)", 0) + 1
                    continue
                transitions.append(window + ["terms", target, condition, terms])
                if target not in seen:
                    seen[target] = "ability"
                    queue.append((target, "ability"))
            elif kind in ("PayStaminaCost", "CAGE-PayManaCost"):
                cost = params.get("CostID", "").strip()
                if cost:
                    payments.append([rounded(clip["start"]), "stamina" if kind == "PayStaminaCost" else "mana", cost,
                                     rounded(float(params.get("CostMultiplier") or 1.0)), params.get("Condition", "").strip()])
            elif kind == "CAGE-Named":
                named_clip = params.get("Name", "")
                if named_clip in ("CanDodgeCancel", "CanDodgeBuffer"):
                    dodge.append([rounded(clip["start"]), None if clip["end"] is None else rounded(clip["end"]),
                                  named_clip == "CanDodgeBuffer"])
                elif named_clip == "CanTransition":
                    can_transition = rounded(clip["start"])
                elif named_clip in markers_wanted:
                    markers.append([named_clip, rounded(clip["start"])])
        # The damage clips of the body and of the attachment of the weapon: one strike for each damage key. Each row of a strike
        # keeps the index of the shape of its clip (the pull of Reap: three capsules with three rows at three distances).
        strikes: dict[str, dict] = {}
        casts = []
        # The grit of the fragment with a node condition (ActivateGrit: Unstoppable Greed, Indomitable): the backend has no grit; the
        # list of the uses of the nodes names it.
        for clip in variant["procedural"]:
            condition = clip["params"].get("Condition", "").strip() if clip["type"] == "ActivateGrit" else ""
            element = named.get(condition.lower()) if condition else None
            tests = []
            if element is not None and node_tests(element, False, tests):
                for node, negated in tests:
                    if not negated:
                        grits.append([node, "grit", name])
        sources_of_damage = [variant]
        attach_variant = select([attachment.get(name, [])], lower)
        if attach_variant is not None:
            sources_of_damage.append(attach_variant)
        for source in sources_of_damage:
            for clip in source["procedural"]:
                params = clip["params"]
                if clip["type"] == "CAGE-CastSpell":
                    spell = params.get("SpellName", "").strip()
                    tests: list[tuple[str, bool]] = []
                    condition = params.get("Condition", "").strip()
                    element = named.get(condition.lower()) if condition else None
                    if spell and (not condition or (element is not None and node_tests(element, False, tests))):
                        casts.append([rounded(clip["start"]), spell, sorted({node for node, negated in tests if not negated}, key=str.lower),
                                      sorted({node for node, negated in tests if negated}, key=str.lower)])
                    elif spell:
                        clips_not_exported.append([label, name, "cast", spell, f"the condition {condition} is not an AND of node tests"])
                    continue
                if clip["type"] != "CAGE-Damage":
                    continue
                key = params.get("DamageKey", "").strip().lower()
                row = params.get("DamageTableRow", "").strip()
                if not row:
                    raise Failure(f"{source['file']}: a damage clip of {name} ({label}) has no damage row")
                if params.get("UseOffhandWeapon", "false").lower() == "true" or params.get("DamageSelf", "false").lower() == "true":
                    clips_not_exported.append([label, name, key, row, "the off hand or the attacker itself"])
                    continue
                condition = params.get("Condition", "").strip()
                needs: list[str] = []
                forbids: list[str] = []
                if condition:
                    element = named.get(condition.lower())
                    tests = []
                    if element is None or not node_tests(element, False, tests):
                        clips_not_exported.append([label, name, key, row, f"the condition {condition} is not an AND of node tests"])
                        continue
                    needs = sorted({node for node, negated in tests if not negated}, key=str.lower)
                    forbids = sorted({node for node, negated in tests if negated}, key=str.lower)
                end = clip["end"] if clip["end"] is not None else clip["start"]
                strike = strikes.setdefault(key, {"start": clip["start"], "end": end, "rows": [], "shapes": []})
                strike["start"] = min(strike["start"], clip["start"])
                strike["end"] = max(strike["end"], end)
                shape = [params.get(field, "") if field in ("MeleeAttackShapeCastType", "JointName")
                         else rounded(float(params.get(field) or 0.0)) for field in SHAPE_FIELDS]
                if shape not in strike["shapes"]:
                    strike["shapes"].append(shape)
                entry = [row, needs, forbids, strike["shapes"].index(shape)]
                if entry not in strike["rows"]:
                    strike["rows"].append(entry)
                counts["clips"] += 1
        length = fragment_length(variant, animations)
        counts["lengthsNotKnown"] += 1 if length == 0.0 else 0
        fragments.append({"name": name, "tags": variant["tagText"], "file": variant["file"], "length": length,
                          "canTransition": can_transition, "transitions": transitions,
                          "strikes": [[key, rounded(value["start"]), rounded(value["end"]), value["rows"], value["shapes"]]
                                      for key, value in sorted(strikes.items(), key=lambda item: (item[1]["start"], item[0]))],
                          "payments": payments, "dodge": dodge, "casts": casts, "markers": markers})
        counts["transitions"] += len(transitions)
        counts["strikes"] += len(strikes)
        counts["payments"] += len(payments)
        counts["casts"] = counts.get("casts", 0) + len(casts)
    # The uses of the learned nodes in the graph (format 2): [node, use, place]; use: strike (a row of a strike), transition (a term of a
    # transition inside an ability), selection (a fragment of an action), cast (a spell of an action or of a fragment), grit (ActivateGrit),
    # motion (a conditional of the movement of an action).
    uses: list[list] = []
    def use(node: str, kind: str, place: str) -> None:
        if [node, kind, place] not in uses:
            uses.append([node, kind, place])
    for fragment in fragments:
        for strike in fragment["strikes"]:
            for row in strike[3]:
                for node in row[1] + row[2]:
                    use(node, "strike", f"{fragment['name']} {strike[0]}")
        for transition in fragment["transitions"]:
            if transition[3] == "terms":
                for term in transition[6]:
                    for literal in term:
                        if literal[0] == "node":
                            use(literal[1], "transition", f"{fragment['name']} -> {transition[4]}")
        for cast in fragment["casts"]:
            for node in cast[2] + cast[3]:
                use(node, "cast", f"{fragment['name']} {cast[1]}")
    for entry in exported_abilities:
        for selection in entry["selections"]:
            for node in selection[1] + selection[2]:
                use(node, "selection", f"{entry['action']} {selection[0]}")
        for cast in entry["casts"]:
            for node in cast[1] + cast[2]:
                use(node, "cast", f"{entry['action']} {cast[0]}")
    for node, kind, place in grits:
        use(node, kind, place)
    for entry in weapon_abilities:
        for condition in entry.get("motion", []):
            for term in condition_terms(condition, named) or []:
                for kind, value, negated in term:
                    if kind == "node" and not negated:
                        use(value, "motion", f"{entry['name']} {condition}")
    if strict:
        # These weapons need their ability action list: with no list, the file would have no ability of the weapon and look complete
        # (code review of stage 3.3b-M2b, finding M2b-R4).
        if not weapon_abilities:
            raise Failure(f"the game copy has no ability of {label} in the action list {ability_list}: the sources are not complete")
        # Each choice of an ability of these weapons that does damage with its strikes has a strike in the fragments that it can play:
        # the fragment of the action and each fragment that the action selects, each one with the fragments of its own transitions
        # inside the ability (finding M2-R7: an attachment file with no clip of the ability gave a contact ability with no strike;
        # finding M2b-R4: a strike of one choice does not prove a strike of a different choice, which plays alone).
        by_name = {fragment["name"]: fragment for fragment in fragments}
        for entry in exported_abilities:
            if entry["ability"].lower() in AREA_ABILITIES:
                continue
            for root in [entry["fragment"]] + [selection[0] for selection in entry["selections"]]:
                if root.lower() in NO_STRIKE_FRAGMENTS:
                    continue
                open_names = [root]
                reached: set[str] = set()
                strikes = 0
                while open_names:
                    name = open_names.pop()
                    if name in reached or name not in by_name:
                        continue
                    reached.add(name)
                    strikes += len(by_name[name]["strikes"])
                    for transition in by_name[name]["transitions"]:
                        if transition[3] != "ability" and not transition[4].startswith("Attack_"):
                            open_names.append(transition[4])
                if strikes == 0:
                    raise Failure(f"the fragment {root} of the ability {entry['ability']} of {label} has no damage clip: the attachment "
                                  "database of the weapon is not complete")
    return fragments, exported_abilities, uses


def build_data(sources: Sources) -> dict:
    tag_sets = weapon_tag_sets(sources)
    action_fragments, named = read_actions(sources)
    abilities = read_abilities(sources)
    animations = Animations(sources)
    databases: dict[str, dict[str, list[dict]]] = {}

    def database(name: str, folder: str) -> dict[str, list[dict]]:
        path = folder + name
        if path not in databases:
            if not sources.has(path):
                raise Failure(f"the game copy has no file {path}")
            databases[path] = read_fragments(sources, path)
            # An animation database with no fragment is not a source of a graph (finding M1-R6: an empty attachment file gave a graph
            # with no strike).
            if not databases[path]:
                raise Failure(f"{path} has no fragment")
        return databases[path]

    not_exported: dict[str, int] = {}
    weapons_not_exported: list[list] = []
    clips_not_exported: list[list] = []
    weapons = []
    counts = {"weapons": 0, "fragments": 0, "transitions": 0, "strikes": 0, "clips": 0, "payments": 0, "lengthsNotKnown": 0,
              "abilities": 0, "casts": 0}
    for tags in tag_sets:
        lower = {tag.lower() for tag in tags}
        base = next(tag for tag in BASE_FILES if tag in lower)
        weapon_tag = next(tag for tag in BODY_FILES if tag in lower)
        built = build_weapon(tags, database(BODY_FILES[weapon_tag], BODY_FOLDER), database(BASE_FILES[base][0], BODY_FOLDER),
                             database(BASE_FILES[base][1], ADB_FOLDER), action_fragments, named, animations, not_exported,
                             clips_not_exported, counts, abilities)
        fragments, weapon_abilities, node_uses = built if built is not None else (None, [], [])
        if fragments is None:
            # A tag set with no attack graph (1H_Melee with the tag of a two-handed weapon: an item of a test of the game).
            weapons_not_exported.append(["+".join(tags), f"no fragment {START}"])
            continue
        # The graph of a melee weapon is complete: each transition goes to a fragment of the graph, and the graph has a strike (finding
        # M1-R6). Else the tool writes no file.
        names = {fragment["name"] for fragment in fragments}
        missing = sorted({transition[4] for fragment in fragments for transition in fragment["transitions"]} - names)
        if missing:
            raise Failure(f"the graph of {'+'.join(tags)} goes to {', '.join(missing)}, which it does not have")
        if not any(fragment["strikes"] for fragment in fragments):
            raise Failure(f"the graph of {'+'.join(tags)} has no strike: the attachment database of the weapon is not complete")
        counts["weapons"] += 1
        counts["fragments"] += len(fragments)
        counts["abilities"] += len(weapon_abilities)
        weapons.append({"tags": list(tags), "files": [BODY_FILES[weapon_tag], BASE_FILES[base][0], BASE_FILES[base][1]], "start": START,
                        "fragments": fragments, "abilities": weapon_abilities, "nodeUses": node_uses})
    sources.check_complete()
    return {"format": 2, "counts": counts, "weapons": weapons,
            "transitionsNotExported": dict(sorted(not_exported.items())), "clipsNotExported": clips_not_exported,
            "weaponsNotExported": weapons_not_exported, "animationsNotFound": sorted(animations.missing, key=str.lower)}


def print_found(data: dict, texts: list[str]) -> None:
    wanted = [text.lower() for text in texts]
    for weapon in data["weapons"]:
        label = "+".join(weapon["tags"])
        if not any(text in label.lower() for text in wanted):
            continue
        print(f"== {label} ({', '.join(weapon['files'])})")
        for fragment in weapon["fragments"]:
            print(f"  {fragment['name']} [{fragment['tags']}] length {fragment['length']} canTransition {fragment['canTransition']}")
            for transition in fragment["transitions"]:
                extra = f" {transition[6]}" if transition[6] is not None else ""
                print(f"    {transition[0]:8s} {transition[1]}..{transition[2]} {transition[3]:8s} -> {transition[4]} ({transition[5]}){extra}")
            for strike in fragment["strikes"]:
                print(f"    strike {strike[0]} {strike[1]}..{strike[2]} rows {strike[3]}")
            for payment in fragment["payments"]:
                print(f"    payment {payment}")
            for cast in fragment["casts"]:
                print(f"    cast {cast}")
            for marker in fragment["markers"]:
                print(f"    marker {marker}")
        for ability in weapon["abilities"]:
            print(f"  ability {ability['ability']} action {ability['action']} fragment {ability['fragment']} selections "
                  f"{ability['selections']} casts {ability['casts']}")


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        data = build_data(sources)
        if find is not None:
            print_found(data, find)
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(data, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"weapon-actions: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"weapon-actions: {target}: {error}", file=sys.stderr)
        return 1
    counts = data["counts"]
    print(f"{target}: {counts['weapons']} melee weapon tag sets, {counts['fragments']} fragments, {counts['transitions']} transitions, "
          f"{counts['strikes']} strikes ({counts['clips']} damage clips), {counts['payments']} payments, {counts['abilities']} abilities, "
          f"{counts['casts']} spell casts of a fragment, "
          f"{counts['lengthsNotKnown']} fragments with no known length, {len(data['clipsNotExported'])} clips not exported "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

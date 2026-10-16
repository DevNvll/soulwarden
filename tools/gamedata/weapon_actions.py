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
animation ends, at its speed. A fragment with an animation that the game copy does not have gets the length 0 (not known).

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
            time += float(blend.get("ExitTime") or 0.0)
        elif node.tag == kind:
            clip = {"start": time, "blend": blend, "attrib": dict(node.attrib)}
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
    for clip in clips:
        name = clip["attrib"].get("name", "")
        speed = float(clip["attrib"].get("speed") or 1.0)
        length = animations.length(name) if name else None
        if length is None or speed <= 0.0:
            return 0.0
        if name == previous_name:
            # The same animation as the clip before: its time continues (live check I1).
            animation_time += (clip["start"] - previous_start) * previous_speed
        else:
            animation_time = float(clip["blend"].get("StartTime") or 0.0)
        end = clip["start"] + max(length - animation_time, 0.0) / speed
        previous_name, previous_start, previous_speed = name, clip["start"], speed
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


def build_weapon(tags: tuple[str, ...], body: dict, common: dict, attachment: dict, action_fragments: dict[str, str],
                 named: dict[str, ElementTree.Element], animations, not_exported: dict[str, int], clips_not_exported: list[list],
                 counts: dict) -> list[dict] | None:
    """@return The fragments of the attack graph of one tag set (from Attack_Start_Charge), or None when the tag set has no
    Attack_Start_Charge. `body`, `common`, and `attachment` are the fragments of the body file of the weapon, of the common file,
    and of the attachment file (read_fragments)."""
    lower = {tag.lower() for tag in tags}
    label = "+".join(tags)

    def body_variant(name: str) -> dict | None:
        return select([body.get(name, []), common.get(name, [])], lower)

    if body_variant(START) is None:
        return None
    fragments = []
    queue = [START]
    seen = {START}
    while queue:
        name = queue.pop(0)
        variant = body_variant(name)
        if variant is None:
            continue
        transitions = []
        payments = []
        dodge = []
        can_transition = None
        for clip in variant["procedural"]:
            kind, params = clip["type"], clip["params"]
            if kind in ("CAGE-TransitionBuffered", "CAGE-TransitionInstant"):
                condition = params.get("Condition", "").strip()
                condition_kind = CONDITIONS.get(condition.lower())
                if condition_kind is None:
                    # The transition to an ability, a sprint, a block: not a basic attack (an action name of the game can be wrong
                    # there: Ability_Rapier_Aerial_Lunge_Introa).
                    not_exported[condition] = not_exported.get(condition, 0) + 1
                    continue
                target = params.get("NewFragment", "").strip()
                action = params.get("NewAction", "").strip()
                if action:
                    if action.lower() not in action_fragments:
                        raise Failure(f"{variant['file']}: the fragment {name} names the action {action}, which no action list has")
                    target = action_fragments[action.lower()]
                if not target or not target.startswith("Attack_"):
                    not_exported[condition or "(none)"] = not_exported.get(condition or "(none)", 0) + 1
                    continue
                transitions.append(["buffered" if kind.endswith("Buffered") else "instant", rounded(clip["start"]),
                                    None if clip["end"] is None else rounded(clip["end"]), condition_kind, target, condition])
                if target not in seen:
                    seen.add(target)
                    queue.append(target)
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
        # The damage clips of the body and of the attachment of the weapon: one strike for each damage key.
        strikes: dict[str, dict] = {}
        sources_of_damage = [variant]
        attach_variant = select([attachment.get(name, [])], lower)
        if attach_variant is not None:
            sources_of_damage.append(attach_variant)
        for source in sources_of_damage:
            for clip in source["procedural"]:
                if clip["type"] != "CAGE-Damage":
                    continue
                params = clip["params"]
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
                    tests: list[tuple[str, bool]] = []
                    if element is None or not node_tests(element, False, tests):
                        clips_not_exported.append([label, name, key, row, f"the condition {condition} is not an AND of node tests"])
                        continue
                    needs = sorted({node for node, negated in tests if not negated}, key=str.lower)
                    forbids = sorted({node for node, negated in tests if negated}, key=str.lower)
                end = clip["end"] if clip["end"] is not None else clip["start"]
                strike = strikes.setdefault(key, {"start": clip["start"], "end": end, "rows": [], "shapes": []})
                strike["start"] = min(strike["start"], clip["start"])
                strike["end"] = max(strike["end"], end)
                entry = [row, needs, forbids]
                if entry not in strike["rows"]:
                    strike["rows"].append(entry)
                shape = [params.get(field, "") if field in ("MeleeAttackShapeCastType", "JointName")
                         else rounded(float(params.get(field) or 0.0)) for field in SHAPE_FIELDS]
                if shape not in strike["shapes"]:
                    strike["shapes"].append(shape)
                counts["clips"] += 1
        length = fragment_length(variant, animations)
        counts["lengthsNotKnown"] += 1 if length == 0.0 else 0
        fragments.append({"name": name, "tags": variant["tagText"], "file": variant["file"], "length": length,
                          "canTransition": can_transition, "transitions": transitions,
                          "strikes": [[key, rounded(value["start"]), rounded(value["end"]), value["rows"], value["shapes"]]
                                      for key, value in sorted(strikes.items(), key=lambda item: (item[1]["start"], item[0]))],
                          "payments": payments, "dodge": dodge})
        counts["transitions"] += len(transitions)
        counts["strikes"] += len(strikes)
        counts["payments"] += len(payments)
    return fragments


def build_data(sources: Sources) -> dict:
    tag_sets = weapon_tag_sets(sources)
    action_fragments, named = read_actions(sources)
    animations = Animations(sources)
    databases: dict[str, dict[str, list[dict]]] = {}

    def database(name: str, folder: str) -> dict[str, list[dict]]:
        path = folder + name
        if path not in databases:
            if not sources.has(path):
                raise Failure(f"the game copy has no file {path}")
            databases[path] = read_fragments(sources, path)
        return databases[path]

    not_exported: dict[str, int] = {}
    weapons_not_exported: list[list] = []
    clips_not_exported: list[list] = []
    weapons = []
    counts = {"weapons": 0, "fragments": 0, "transitions": 0, "strikes": 0, "clips": 0, "payments": 0, "lengthsNotKnown": 0}
    for tags in tag_sets:
        lower = {tag.lower() for tag in tags}
        base = next(tag for tag in BASE_FILES if tag in lower)
        weapon_tag = next(tag for tag in BODY_FILES if tag in lower)
        fragments = build_weapon(tags, database(BODY_FILES[weapon_tag], BODY_FOLDER), database(BASE_FILES[base][0], BODY_FOLDER),
                                 database(BASE_FILES[base][1], ADB_FOLDER), action_fragments, named, animations, not_exported,
                                 clips_not_exported, counts)
        if fragments is None:
            # A tag set with no attack graph (1H_Melee with the tag of a two-handed weapon: an item of a test of the game).
            weapons_not_exported.append(["+".join(tags), f"no fragment {START}"])
            continue
        counts["weapons"] += 1
        counts["fragments"] += len(fragments)
        weapons.append({"tags": list(tags), "files": [BODY_FILES[weapon_tag], BASE_FILES[base][0], BASE_FILES[base][1]], "start": START,
                        "fragments": fragments})
    sources.check_complete()
    return {"format": 1, "counts": counts, "weapons": weapons,
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
                print(f"    {transition[0]:8s} {transition[1]}..{transition[2]} {transition[3]:8s} -> {transition[4]} ({transition[5]})")
            for strike in fragment["strikes"]:
                print(f"    strike {strike[0]} {strike[1]}..{strike[2]} rows {strike[3]}")
            for payment in fragment["payments"]:
                print(f"    payment {payment}")


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
          f"{counts['strikes']} strikes ({counts['clips']} damage clips), {counts['payments']} payments, "
          f"{counts['lengthsNotKnown']} fragments with no known length, {len(data['clipsNotExported'])} clips not exported "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

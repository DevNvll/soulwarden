#!/usr/bin/env python3
"""Write the stamina rules of a player and the dodge as one JSON file (stage 3.4f-B).

The backend reads this file for the stamina of a character (world/stamina.h): its maximum, its regeneration and the delays of the
regeneration, the cost of a dodge for each load class, the times of the dodge fragment (the payment, the immune window, the mark
CanTransition, the windows of a second dodge), and the exhausted state. The client of the owner predicts the same values itself (live
checks LS1 to LS7 of stage 3.4f-B: the memory of the Stamina component of the client). Without the file the backend starts: a
character keeps its full stamina, and a dodge has no immune window (the log says it at the start).

Input: the game copy, from the same sources as the creature data tool (creature_data.py).

  Source                                                       What the file gets
  slices/player.dynamicslice (the Stamina component)           "component": m_min, m_initMax, m_regenRate, m_blockRegenRate,
                                                               m_maxWindedTime (the client uses these: m_useVitalsStaminaValues false)
  BaseVitals_Player (VitalsBaseData), row Player               "vitals": StaminaRegenDelay, StaminaRegenDelayOnEmpty,
                                                               StaminaFullyDepletedDelay (the delay after a payment; live check LS5: 1 s)
  StaminaCosts_Player (StaminaData)                            the cost of each cost id of the dodge
  EncumbranceLimits (EncumbranceData), row Player              "load": EquipLoadStaminaCostMult* and EquipLoadStaminaRegenMult*
  playerdata/cage/playeractions_dodge.actionlist (Dodge)       the fragment of each load class (ChangeActionFragment with the named
                                                               conditions ActionConditionEquipLoadCategory), the cost id of each one
                                                               (PayStaminaCost), the conditional costs that need a rule of a weapon
                                                               ("costsNotApplied"), and the regeneration delay at the exit
                                                               (SetStaminaRegenDelay OnExit)
  playerdata/cage/playeractions_global.actionlist (Exhausted)  the stamina percent that starts and that ends the exhausted state
  animations/mannequin/adb/player/player.adb                   the dodge fragments: the time of PayStaminaCost, the Invulnerability
                                                               clip, the mark CanTransition, and the CanDodgeCancel transitions

The variant of a dodge fragment: the variant with no tag (the roll of a character that stands). The tool checks that each standing
variant (the tags LeftNav, RightNav, BackNav, and none) has the same times; the prone variants (Prone+...) have a later CanTransition
and are listed in "variantsNotUsed" (the server does not know the posture of the client).

Output: {"format": 1,
"component": {"min": N, "max": N, "regenRate": N, "blockRegenRate": N, "maxWindedTime": N},
"vitals": {"regenDelay": N, "regenDelayOnEmpty": N, "fullyDepletedDelay": N},
"load": {"costMult": {"light": N, "medium": N, "heavy": N, "overburdened": N}, "regenMult": {...}},
"dodge": {"action": "Dodge", "regenDelayOnExit": N,
  "classes": {"light": {"fragment": NAME, "costId": ID, "cost": N}, "medium": {...}, "heavy": {...}},
  "fragments": {NAME: {"pay": S, "immune": [S, S], "canTransition": S, "cancel": [[start, end or null, buffered], ...]}, ...},
  "costsNotApplied": [[condition, costId, multiplier], ...], "variantsNotUsed": [[fragment, tags], ...]},
"exhausted": {"action": "Exhausted", "enter": PART, "exit": PART}}

The tool writes no file (status 1) when a source is not in the game copy, when a value is not a finite number, when a cost id has no
row, or when the standing variants of a dodge fragment do not have the same times.

Usage: stamina.py [OUTPUT.json]
Default output: <SOULWARDEN_GAMEDATA_DIR>/stamina-data.json
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
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import PLAYER_SLICE, TABLES_FOLDER, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import find_field  # noqa: E402

OUTPUT_NAME = "stamina-data.json"
FORMAT = 1
ACTION_FOLDER = "sharedassets/springboardentitites/playerdata/cage/"
DODGE_ACTIONS = ACTION_FOLDER + "playeractions_dodge.actionlist"
GLOBAL_ACTIONS = ACTION_FOLDER + "playeractions_global.actionlist"
PLAYER_ADB = "animations/mannequin/adb/player/player.adb"
WANTED = (TABLES_FOLDER, ACTION_FOLDER, PLAYER_ADB, PLAYER_SLICE)
DODGE_ACTION = "Dodge"
EXHAUSTED_ACTION = "Exhausted"
VITALS_TYPE = "VitalsBaseData"
VITALS_TABLE = "BaseVitals_Player"
STAMINA_TYPE = "StaminaData"
STAMINA_TABLE = "StaminaCosts_Player"
LOAD_TYPE = "EncumbranceData"
PLAYER_ROW = "Player"
# The load classes of the client (ActionConditionEquipLoadCategory) and the names of the backend (Gear: the load class).
LOAD_CLASSES = {"Fast": "light", "Normal": "medium", "Slow": "heavy", "Overburdened": "overburdened"}
# The fields of the Stamina component of the player slice.
COMPONENT_FIELDS = {"min": "m_min", "max": "m_initMax", "regenRate": "m_regenRate", "blockRegenRate": "m_blockRegenRate",
                    "maxWindedTime": "m_maxWindedTime"}
STANDING_TAGS = ("", "LeftNav", "RightNav", "BackNav")
# Two standing variants agree when each time differs by less than half a step of the client (1/60 s): the BackNav variant of
# Dodge_Roll_Fast has CanTransition at 0.500221 s, the others at 0.5 s.
TIME_TOLERANCE = 1.0 / 60.0


def rounded(value: float, what: str) -> float:
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return round(float(value), 6)


def number_text(text: str | None, what: str) -> float:
    """@return The number of an XML attribute. Text that is not a finite number stops the tool (no default of 0)."""
    try:
        value = float(text if text is not None else "")
    except ValueError as error:
        raise Failure(f"{what} is not a number: {text!r}") from error
    return rounded(value, what)


def read_xml(sources: Sources, path: str) -> ElementTree.Element:
    data = sources.read(path)
    if data is None:
        sources.check_complete()
        raise Failure(f"{path} has no source")
    try:
        return ElementTree.fromstring(data.decode("utf-8-sig", "replace"))
    except ElementTree.ParseError as error:
        raise Failure(f"{path} is not correct XML: {error}") from error


def read_component(sources: Sources) -> dict:
    """@return The values of the Stamina component of the player slice."""
    roots = sources.stream(PLAYER_SLICE)
    if roots is None:
        sources.check_complete()
        raise Failure(f"{PLAYER_SLICE} has no source")
    wanted = {member: field_id(label) for member, label in COMPONENT_FIELDS.items()}
    for root in roots:
        for element in walk(root):
            fields = {member: find_field(element, number) for member, number in wanted.items()}
            if any(node is None for node in fields.values()):
                continue
            values = {}
            for member, node in fields.items():
                if node.value is None or len(node.value) != 4:
                    raise Failure(f"{PLAYER_SLICE}: {COMPONENT_FIELDS[member]} of the Stamina component is not a 32-bit float")
                values[member] = rounded(struct.unpack(">f", node.value)[0], f"{PLAYER_SLICE}: {COMPONENT_FIELDS[member]}")
            if values["max"] <= 0.0 or values["regenRate"] < 0.0 or values["maxWindedTime"] < 0.0 or values["min"] != 0.0:
                raise Failure(f"{PLAYER_SLICE}: the Stamina component has values that are not a stamina model: {values}")
            return values
    raise Failure(f"{PLAYER_SLICE} has no Stamina component (m_initMax, m_regenRate, m_maxWindedTime)")


def read_sheets(sources: Sources) -> dict[str, Sheet]:
    """@return The tables of the used types by their name."""
    sheets: dict[str, Sheet] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None or sheet_type(data) not in (VITALS_TYPE, STAMINA_TYPE, LOAD_TYPE):
            continue
        sheet = Sheet(data)
        sheets.setdefault(sheet.name, sheet)
    sources.check_complete()
    return sheets


def player_row(sheets: dict[str, Sheet], name: str, key_column: str) -> tuple[Sheet, int]:
    sheet = sheets.get(name)
    if sheet is None:
        raise Failure(f"the game data has no table {name}")
    for row in range(sheet.row_count):
        if str(sheet.cell(row, key_column)) == PLAYER_ROW:
            return sheet, row
    raise Failure(f"the table {name} has no row {PLAYER_ROW}")


def cell_number(sheet: Sheet, row: int, column: str) -> float:
    if column not in sheet.columns:
        raise Failure(f"the table {sheet.name} has no column {column}")
    value = sheet.cell(row, column)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise Failure(f"the cell {column} of the table {sheet.name} is not a number")
    return rounded(value, f"the cell {column} of the table {sheet.name}")


def stamina_costs(sheets: dict[str, Sheet]) -> dict[str, float]:
    sheet = sheets.get(STAMINA_TABLE)
    if sheet is None:
        raise Failure(f"the game data has no table {STAMINA_TABLE}")
    costs: dict[str, float] = {}
    for row in range(sheet.row_count):
        identifier = str(sheet.cell(row, "CostID")).strip()
        if identifier:
            cost = cell_number(sheet, row, "StaminaCost")
            if cost < 0.0:
                raise Failure(f"the stamina cost {identifier} is negative")
            costs.setdefault(identifier.lower(), cost)
    return costs


def action_of(root: ElementTree.Element, name: str, path: str) -> ElementTree.Element:
    for action in root.iter("Action"):
        if action.get("Name") == name:
            return action
    raise Failure(f"{path} has no action {name}")


def read_dodge_action(sources: Sources, costs: dict[str, float]) -> dict:
    """@return The fragment and the cost of each load class, the costs that need a rule of a weapon, and the delay at the exit."""
    root = read_xml(sources, DODGE_ACTIONS)
    action = action_of(root, DODGE_ACTION, DODGE_ACTIONS)
    # The named conditions of the load (ActionConditionEquipLoadCategory with a Name) of the action list.
    load_conditions: dict[str, str] = {}
    for node in root.iter("ActionConditionEquipLoadCategory"):
        if node.get("Name"):
            value = node.get("Value", "")
            if value not in LOAD_CLASSES:
                raise Failure(f"{DODGE_ACTIONS}: the condition {node.get('Name')} names the load class {value!r}")
            load_conditions[node.get("Name")] = LOAD_CLASSES[value]
    change = next(iter(action.iter("ChangeActionFragment")), None)
    pay = next(iter(action.iter("PayStaminaCost")), None)
    delay = next(iter(action.iter("SetStaminaRegenDelay")), None)
    if change is None or pay is None or delay is None:
        raise Failure(f"{DODGE_ACTIONS}: the action {DODGE_ACTION} has no ChangeActionFragment, PayStaminaCost, or SetStaminaRegenDelay")
    default_fragment = change.find("Default")
    default_cost = pay.find("Default")
    default_delay = delay.find("Default")
    if default_fragment is None or default_cost is None or default_delay is None:
        raise Failure(f"{DODGE_ACTIONS}: an activity of the action {DODGE_ACTION} has no Default")
    if default_delay.get("OnExit") != "Set":
        raise Failure(f"{DODGE_ACTIONS}: SetStaminaRegenDelay of the action {DODGE_ACTION} sets no delay at the exit")
    exit_delay = number_text(default_delay.get("RegenDelayTime"), f"{DODGE_ACTIONS}: RegenDelayTime")
    fragments = {klass: default_fragment.get("FragmentName", "") for klass in ("light", "medium", "heavy")}
    cost_ids = {klass: (default_cost.get("CostID", ""), number_text(default_cost.get("CostMultiplier", "1"), "CostMultiplier"))
                for klass in ("light", "medium", "heavy")}
    for conditional in change.findall("Conditional"):
        klass = load_conditions.get(conditional.get("Name", ""))
        if klass is None or klass == "overburdened":
            raise Failure(f"{DODGE_ACTIONS}: the fragment condition {conditional.get('Name')!r} is not a load class of a dodge")
        fragments[klass] = conditional.get("FragmentName", "")
    not_applied = []
    for conditional in pay.findall("Conditional"):
        condition = conditional.get("Name", "")
        multiplier = number_text(conditional.get("CostMultiplier", "1"), f"{DODGE_ACTIONS}: CostMultiplier of {condition}")
        klass = load_conditions.get(condition)
        if klass is None:
            # A cost with a condition of a weapon rule (a passive of the hatchet or the spear): not in the backend yet.
            not_applied.append([condition, conditional.get("CostID", ""), multiplier])
            continue
        cost_ids[klass] = (conditional.get("CostID", ""), multiplier)
    classes = {}
    for klass in ("light", "medium", "heavy"):
        cost_id, multiplier = cost_ids[klass]
        if cost_id.lower() not in costs:
            raise Failure(f"the dodge cost id {cost_id!r} of the load class {klass} has no row of {STAMINA_TABLE}")
        if not fragments[klass]:
            raise Failure(f"{DODGE_ACTIONS}: the load class {klass} has no dodge fragment")
        classes[klass] = {"fragment": fragments[klass], "costId": cost_id, "cost": rounded(costs[cost_id.lower()] * multiplier, cost_id)}
    for condition, cost_id, _ in not_applied:
        if cost_id.lower() not in costs:
            raise Failure(f"the dodge cost id {cost_id!r} of the condition {condition} has no row of {STAMINA_TABLE}")
    return {"action": DODGE_ACTION, "regenDelayOnExit": exit_delay, "classes": classes, "costsNotApplied": not_applied}


def read_exhausted(sources: Sources) -> dict:
    """@return The stamina parts that start and that end the exhausted state (the action Exhausted of the global action list)."""
    root = read_xml(sources, GLOBAL_ACTIONS)
    action = action_of(root, EXHAUSTED_ACTION, GLOBAL_ACTIONS)
    parts = {}
    for member, tag, comparison in (("enter", "ActivationCondition", "LessThanOrEqual"), ("exit", "DeactivationCondition", "Equal")):
        block = action.find(tag)
        node = next((item for item in block.iter("IfStaminaValue") if item.get("ComparisonType") == comparison), None) if block is not None else None
        if node is None or str(node.get("ComparisonValueIsPercentage", "")).lower() != "true":
            raise Failure(f"{GLOBAL_ACTIONS}: the action {EXHAUSTED_ACTION} has no {tag} with a stamina percent ({comparison})")
        parts[member] = number_text(node.get("ComparisonValue"), f"{GLOBAL_ACTIONS}: the {member} part of {EXHAUSTED_ACTION}")
    if parts["enter"] != 0.0 or parts["exit"] != 1.0:
        raise Failure(f"{GLOBAL_ACTIONS}: the exhausted state has the parts {parts}, not 0 and 1")
    return {"action": EXHAUSTED_ACTION, **parts}


def fragment_times(fragment: ElementTree.Element, name: str) -> dict:
    """@return The times of one variant of a dodge fragment (the sum of the ExitTime values before each clip of a layer)."""
    result = {"pay": None, "immune": None, "canTransition": None, "cancel": []}
    for layer in fragment.findall("ProcLayer"):
        time = 0.0
        clips = []
        for node in layer:
            if node.tag == "Blend":
                time += max(number_text(node.get("ExitTime") or "0", f"{PLAYER_ADB}: an ExitTime of {name}"), 0.0)
            elif node.tag == "Procedural":
                block = node.find("ProceduralParams")
                parameters = {child.tag: child.get("value", "") for child in block} if block is not None else {}
                clips.append((round(time, 6), node.get("type", ""), parameters))
        for index, (start, kind, parameters) in enumerate(clips):
            end = clips[index + 1][0] if index + 1 < len(clips) else None
            if kind == "CAGE-Named" and parameters.get("Name") == "PayStaminaCost":
                result["pay"] = start
            elif kind == "CAGE-Named" and parameters.get("Name") == "CanTransition":
                result["canTransition"] = start
            elif kind == "Invulnerability":
                if end is None:
                    raise Failure(f"{PLAYER_ADB}: the Invulnerability clip of {name} has no end")
                result["immune"] = [start, end]
            elif kind in ("CAGE-TransitionBuffered", "CAGE-TransitionInstant") and parameters.get("Condition") == "CanDodgeCancel":
                buffered = kind == "CAGE-TransitionBuffered"
                if buffered and end is None:
                    raise Failure(f"{PLAYER_ADB}: the buffered CanDodgeCancel window of {name} has no end")
                result["cancel"].append([start, end if buffered else None, buffered])
    for member in ("pay", "immune", "canTransition"):
        if result[member] is None:
            raise Failure(f"{PLAYER_ADB}: the dodge fragment {name} has no {member}")
    result["cancel"].sort()
    return result


def same_times(first: dict, second: dict) -> bool:
    """@return True when the times of two variants differ by less than TIME_TOLERANCE each (a window end of none in both)."""
    def flat(times: dict) -> list:
        values = [times["pay"], times["canTransition"], *times["immune"]]
        values += [value for window in times["cancel"] for value in window[:2]]
        return values
    a, b = flat(first), flat(second)
    if len(a) != len(b) or [window[2] for window in first["cancel"]] != [window[2] for window in second["cancel"]]:
        return False
    return all((x is None and y is None) or (x is not None and y is not None and abs(x - y) < TIME_TOLERANCE) for x, y in zip(a, b))


def read_fragments(sources: Sources, names: set[str]) -> tuple[dict, list]:
    root = read_xml(sources, PLAYER_ADB)
    fragment_list = root.find("FragmentList")
    found: dict[str, dict] = {}
    not_used = []
    for name in sorted(names):
        group = fragment_list.find(name) if fragment_list is not None else None
        if group is None:
            raise Failure(f"{PLAYER_ADB} has no fragment {name}")
        variants = []
        for variant in group.findall("Fragment"):
            tags = variant.get("Tags", "")
            if tags not in STANDING_TAGS:
                not_used.append([name, tags])
                continue
            times = fragment_times(variant, f"{name} ({tags or 'no tag'})")
            variants.append((tags, times))
        # The variant with no tag (a roll forward of a character that stands) gives the times; each other standing variant must agree.
        standing = next((times for tags, times in variants if tags == ""), None)
        for tags, times in variants:
            if standing is not None and not same_times(times, standing):
                raise Failure(f"{PLAYER_ADB}: the standing variants of {name} do not have the same times ({times} and {standing})")
        if standing is None:
            raise Failure(f"{PLAYER_ADB}: the fragment {name} has no standing variant")
        found[name] = standing
    return found, not_used


def build_data(sources: Sources) -> dict:
    component = read_component(sources)
    sheets = read_sheets(sources)
    vitals_sheet, vitals_row = player_row(sheets, VITALS_TABLE, "VitalsID")
    vitals = {member: cell_number(vitals_sheet, vitals_row, column)
              for member, column in (("regenDelay", "StaminaRegenDelay"), ("regenDelayOnEmpty", "StaminaRegenDelayOnEmpty"),
                                     ("fullyDepletedDelay", "StaminaFullyDepletedDelay"))}
    if any(value < 0.0 for value in vitals.values()):
        raise Failure(f"the table {VITALS_TABLE} has a negative stamina delay")
    load_sheet = next((sheet for sheet in sheets.values() if sheet.type == LOAD_TYPE and "EquipLoadStaminaCostMultFast" in sheet.columns), None)
    if load_sheet is None:
        raise Failure(f"the game data has no table of the type {LOAD_TYPE} with stamina columns")
    load_row = next((row for row in range(load_sheet.row_count) if str(load_sheet.cell(row, "ContainerTypeID")) == PLAYER_ROW), None)
    if load_row is None:
        raise Failure(f"the table {load_sheet.name} has no row {PLAYER_ROW}")
    load = {"costMult": {}, "regenMult": {}}
    for suffix, klass in LOAD_CLASSES.items():
        load["costMult"][klass] = cell_number(load_sheet, load_row, f"EquipLoadStaminaCostMult{suffix}")
        load["regenMult"][klass] = cell_number(load_sheet, load_row, f"EquipLoadStaminaRegenMult{suffix}")
    costs = stamina_costs(sheets)
    dodge = read_dodge_action(sources, costs)
    fragments, not_used = read_fragments(sources, {entry["fragment"] for entry in dodge["classes"].values()})
    for name, times in fragments.items():
        if not 0.0 <= times["pay"] <= times["canTransition"] or times["immune"][0] != 0.0 or times["immune"][1] <= 0.0:
            raise Failure(f"{PLAYER_ADB}: the times of the dodge fragment {name} do not fit together: {times}")
    dodge["fragments"] = fragments
    dodge["variantsNotUsed"] = not_used
    return {"format": FORMAT, "component": component, "vitals": vitals, "load": load, "dodge": dodge,
            "exhausted": read_exhausted(sources)}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    target = Path(arguments[0]) if arguments else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        data = build_data(sources)
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(data, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"stamina: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"stamina: {target}: {error}", file=sys.stderr)
        return 1
    dodge = data["dodge"]
    print(f"{target}: stamina {data['component']['max']:g}, regeneration {data['component']['regenRate']:g} in a second after "
          f"{data['vitals']['regenDelay']:g} s, winded {data['component']['maxWindedTime']:g} s; dodge "
          + ", ".join(f"{klass} {entry['fragment']} {entry['cost']:g}" for klass, entry in dodge["classes"].items())
          + f"; {len(dodge['costsNotApplied'])} costs of a weapon rule not applied "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

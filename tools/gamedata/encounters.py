#!/usr/bin/env python3
"""Write the encounter recipes of the quests (stage QG5c2, docs/systems/encounters.md): one JSON file.

The backend reads GAMEDATA_DIR/encounter-data.json at its start. Without the file the world has no encounter (no quest task of an encounter
event progresses), and the rest of the backend works.

The recipes come from encounter_recipes.py (the chain of a recipe is in its docstring): each quest encounter of a placement of the main
world, its manager, its participant tracker and detector, its stages in the order of the manager, their spawns with the places of the
spawn locations, their objectives and actions, and its parent spawner (the respawn of a new instance). Each member slice is described one
time ("members"): a creature member (its vitals row, its category row, its level, its body slice; creature_data.py makes its kind) or a
start object (its variant, its gatherable row, its game event, its tag; interactions.py makes its template and gathering.py its row).

A member slice that is not a creature and not a start object, in a stage whose objective tracks a different slice, is a visual of the
stage (the smoke burst of the lost pylons): the backend does not make it, and the recipe stays (a rule of this project; a limit of the
picture). A recipe has "refused": the causes of the data that the runner of stage QG5c2 does not run (the rules below; the backend adds the causes of
the other game data files: a member kind, a template, or a row that is not there). Empty: the runner takes the recipe.
  - The scope of stage QG5c2 (a rule of this project; the patterns of research quest-encounters.md, Appendix A): the kill waves after an
    interaction, and the Blighted Ranger. LATER names the recipes of the other patterns with their stage: the ambushes, the summoned
    opponents, the interaction sequences, and the defenses (QG5c3), the seasonal events (later).
  - The manager starts the encounter by itself (m_autoActivate), has no reset event, and the detector is a sphere with a radius.
  - Each objective is an InvasionWaveObjective with a time limit and succeedOnTimeout (a timed stage), or a SliceDestroyedObjective (with a
    tracked slice of its own spawns or none: the members of its stage); no failure condition, no background activation.
  - No child stage, no DestroySliceAction (a later stage); EntityEventAction is a part of the client (the handler of the stage status);
    KillSpawnsAction removes the members.
  - Each member is a creature member or a start object of an interaction (not a proximity object: an ambush); the recipe has a creature
    member (else it is an interaction sequence) and a start object only in a stage that tracks it.

Usage: encounters.py [OUTPUT.json]      (default: GAMEDATA_DIR/encounter-data.json; a test: a path outside the common folder)

The document (format 1):
  {"format": 1, "world": TEXT, "counts": {...},
   "recipes": [{"key": "PLACEMENT_UUID/SPAWNER_ENTITY", "slice", "asset": HEX32, "sub", "root": HEX16, "rootName", "layer", "parentSlice",
                "position": [X, Y, Z], "rotation": [X, Y, Z, W], "yaw",
                "slots": {"manager", "place", "objectives"}, "manager": {...}, "tracker": {"component": HEX16, "grace", "addedEvent"},
                "detector": {"radius", "filter", "sphere"}, "successEvent", "questEvents": [...], "parent": {...},
                "stageOrder": [STAGE INDEX, ...],
                "stages": [{"entity": HEX16, "name", "slot", "component": HEX16, "userWave", "children", "childrenOptional",
                            "disableSpawners", "objective": {...}, "startActions": [...], "endActions": [...],
                            "spawns": [{"member": SLICE PATH, "delay": SECONDS, "count", "locations": [{"position", "rotation", "yaw"}]}]}],
                "unreachable": [STAGE NAME, ...], "pattern": TEXT, "refused": [CAUSE, ...]}, ...],
   "members": {SLICE PATH: {"kind": "creature", "vitals", "category", "level", "body"} or
                           {"kind": "object", "variant", "table", "row", "event", "tag", "onlyOnQuest", "proximity"} or
                           {"kind": "unknown", "cause"}}}
"""

from __future__ import annotations

import collections
import json
import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

from appearance_tables import Failure  # noqa: E402
from creature_data import CATALOG_PATH, WANTED, WORLD, Catalog, Sources, slice_assets  # noqa: E402
from encounter_recipes import read_recipes  # noqa: E402

OUTPUT_NAME = "encounter-data.json"
FORMAT = 1
QG5C3 = "stage QG5c3"
# The recipes of the other patterns (research quest-encounters.md, Appendix A and C) and their stage: the scope of stage QG5c2.
LATER = {
    "slices/worldevents/brimstonesands/enc_worldevent_1625_aeambush.dynamicslice": ("an ambush", QG5C3),
    "slices/worldevents/brightwood/enc_worldevent_99b_msq_yonasretreat.dynamicslice": ("an ambush", QG5C3),
    "slices/worldevents/edengrove/enc_worldevent_99e_msq_blightambush.dynamicslice": ("an ambush", QG5C3),
    "slices/worldevents/edengrove/enc_worldevent_99e_msq_corrstaff.dynamicslice": ("an ambush", QG5C3),
    "slices/worldevents/firstlight/interact/summongazsala/enc_worldevent_09a_s13_gazsalathiss.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/firstlight/encounters/enc_worldevent_09a_s16_hercyneboarnamed.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/mainstoryquest/thorpe/enc_worldevent_msq_thorpe_01.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/everfall/enc_worldevent_windsward_cq3_templarinvectus.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/dunwood/grivaros/enc_worldevent_grivaros.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/dunwood/enc_worldevent_carmilla_mindthief.dynamicslice": ("a summoned opponent with phases", "a later stage (BT3)"),
    "slices/worldevents/dunwood/bathory/enc_worldevent_bathory_guardian.dynamicslice": ("a summoned opponent", QG5C3),
    "slices/worldevents/brimstonesands/interact/enc_worldevent_1656_0401_drusillabanner.dynamicslice": ("an interaction sequence", QG5C3),
    "slices/worldevents/brimstonesands/interact/enc_worldevent_1624_diskatumwave.dynamicslice": ("an interaction sequence", QG5C3),
    "slices/pois/ancient/design/brazier/enc_worldevent_master_brazier2.dynamicslice": ("an interaction sequence", QG5C3),
    "slices/worldevents/cutlasskeys/encounters/enc_worldevent_10a_q4_shipwreck.dynamicslice": ("an interaction sequence", QG5C3),
    "slices/worldevents/firstlight/encounters/enc_defense_09a_m5_waveevent.dynamicslice": ("a defense", QG5C3),
    "slices/worldevents/firstlight/encounters/enc_defense_09a_m5_angryearth.dynamicslice": ("a defense", QG5C3),
    "slices/worldevents/firstlight/encounters/enc_worldevent_09a_s17_wagonattack.dynamicslice": ("a defense", QG5C3),
    "slices/worldevents/firstlight/interact/cc/enc_worldevent_09a_s9_hacker.dynamicslice": ("a defense", QG5C3),
    "slices/worldevents/edengrove/wave_events/enc_worldevent_1434_pyre.dynamicslice": ("a defense", QG5C3),
    # The boat reinforcements of Monarch's Bluffs: a world event of the open world; its only quest event is the game event of the cannon
    # gimmick (IO_Gimmick_BreakingWallsCannon_interact), which the placed cannons give (stage QG5b).
    "slices/worldevents/monarchsbluffs/scallywagshore/enc_worldevent_boatreinforcements_bryseddshore.dynamicslice":
        ("a world event of the open world", "a later stage (the world events)"),
    "slices/worldevents/monarchsbluffs/scallywagshore/enc_worldevent_boatreinforcements_scallywagshore.dynamicslice":
        ("a world event of the open world", "a later stage (the world events)"),
}
LATER_FOLDERS = {
    "slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/": ("an interaction sequence", QG5C3),
    "slices/darkness/": ("a seasonal event (the Darkness)", "a later stage (the scheduled and seasonal events)"),
    "slices/worldevents/seasons/": ("a seasonal event", "a later stage (the scheduled and seasonal events)"),
}
TIMED = "InvasionWaveObjective"
DESTROYED = "SliceDestroyedObjective"


def pattern_of(recipe: dict) -> tuple[str, str] | None:
    path = recipe["slice"]
    if path in LATER:
        return LATER[path]
    return next((value for folder, value in LATER_FOLDERS.items() if path.startswith(folder)), None)


def tracks(tracked: str, member_path: str, members: dict[str, dict]) -> bool:
    """@return True when the tracked slice or alias of a SliceDestroyedObjective is a spawn: its member slice, the body slice or alias of a
    creature member, or a slice of that alias (the lost pylons track undead_berserker.dynamicslice, a slice of the alias of the member)."""
    member = members.get(member_path, {})
    return tracked in (member_path, member.get("body")) or tracked in member.get("bodySlices", [])


def refusals(recipe: dict, members: dict[str, dict]) -> list[str]:
    """@return The causes of the data that the runner does not run (the rules of the docstring). Empty: supported."""
    causes = list(recipe["unsupported"])
    later = pattern_of(recipe)
    if later is not None:
        causes.append(f"{later[0]}: {later[1]}")
    manager = recipe["manager"]
    if not manager["autoActivate"]:
        causes.append("the manager does not start the encounter by itself (m_autoActivate 0: a start of an event)")
    if manager["resetEvent"]:
        causes.append(f"a reset event of the manager ({manager['resetEvent']}): a later stage")
    detector = recipe.get("detector")
    if detector is None or not detector["sphere"] or detector["radius"] <= 0.0:
        causes.append("the detector of the participants is not a sphere with a radius")
    creature = False
    for stage in recipe["stages"]:
        objective = stage["objective"]
        kind = objective["type"]
        name = stage["name"]
        if stage["children"]:
            causes.append(f"the stage {name} has child stages: a later stage")
        if kind == TIMED:
            if objective["timeLimit"] <= 0 or not objective["succeedOnTimeout"]:
                causes.append(f"the wave stage {name} has no timed success")
        elif kind == DESTROYED:
            if objective["requiredCount"] <= 0:
                causes.append(f"the stage {name} needs no destroyed slice")
            tracked = objective["trackedSlice"] or objective["trackedAlias"]
            if tracked and not any(tracks(tracked, spawn["member"], members) for spawn in stage["spawns"]):
                causes.append(f"the stage {name} tracks a slice that it does not spawn")
        else:
            causes.append(f"the objective {kind} of the stage {name}: a later stage")
        if objective.get("failureCondition"):
            causes.append(f"a failure condition of the stage {name}")
        if objective.get("background"):
            causes.append(f"a background activation of the stage {name}")
        for action in stage["startActions"] + stage["endActions"]:
            if action["type"] not in ("EntityEventAction", "KillSpawnsAction"):
                causes.append(f"the action {action['type']} of the stage {name}: a later stage")
        for spawn in stage["spawns"]:
            member = members.get(spawn["member"], {"kind": "unknown", "cause": "no slice"})
            tracked = objective.get("trackedSlice") or objective.get("trackedAlias")
            if member["kind"] == "unknown" and kind == DESTROYED and tracked and not tracks(tracked, spawn["member"], members):
                # A visual of the stage (the smoke burst of the lost pylons): the stage does not track it, and the backend does not make it.
                continue
            if member["kind"] == "unknown":
                causes.append(f"the member {spawn['member'].rsplit('/', 1)[-1]}: {member['cause']}")
            elif member["kind"] == "object":
                if member.get("proximity"):
                    causes.append(f"the start object {spawn['member'].rsplit('/', 1)[-1]} is a proximity object (an ambush: {QG5C3})")
                elif member.get("cause"):
                    causes.append(f"the start object {spawn['member'].rsplit('/', 1)[-1]}: {member['cause']}")
                if kind != DESTROYED or (tracked and tracked != spawn["member"]):
                    causes.append(f"the start object {spawn['member'].rsplit('/', 1)[-1]} is in a stage that does not track it")
                if spawn["count"] != 1 or len(spawn["locations"]) > 1:
                    causes.append(f"the start object {spawn['member'].rsplit('/', 1)[-1]} has more than one place")
            else:
                creature = True
            if spawn["count"] <= 0:
                causes.append(f"a spawn of the stage {name} has no count")
    if not creature:
        causes.append(f"no creature member: not a kill wave (an interaction sequence: {QG5C3})")
    return list(dict.fromkeys(causes))


def hex16(value: int) -> str:
    return f"{value:016X}"


def build(sources: Sources) -> dict:
    catalog = Catalog(sources.read(CATALOG_PATH))
    reader, recipes, counts = read_recipes(sources, catalog)
    assets = slice_assets(catalog)
    members: dict[str, dict] = {}
    for path, member in sorted(reader.members.items()):
        members[path] = {key: value for key, value in member.items() if not key.startswith("_") and key != "slice"}
    out = []
    refused = collections.Counter()
    for recipe in recipes:
        asset = assets.get(recipe["slice"])
        if asset is None:
            raise Failure(f"the asset catalog has no slice asset of the encounter {recipe['slice']}")
        causes = refusals(recipe, members)
        later = pattern_of(recipe)
        entry = {
            "key": recipe["key"], "slice": recipe["slice"], "asset": asset[0].hex().upper(), "sub": asset[1],
            "root": hex16(recipe["root"]), "rootName": recipe["rootName"], "layer": recipe["layer"], "parentSlice": recipe["parentSlice"],
            "position": recipe["position"], "rotation": recipe["rotation"], "yaw": recipe["yaw"],
            "slots": {"manager": recipe["managerSlot"], "place": recipe.get("placeSlot", 0), "objectives": recipe.get("objectivesSlot", 0)},
            "manager": recipe["manager"],
            "tracker": ({"component": hex16(recipe["tracker"]["component"]), "grace": recipe["tracker"]["grace"],
                         "addedEvent": recipe["tracker"]["addedEvent"]} if "tracker" in recipe else None),
            "detector": recipe.get("detector"), "successEvent": recipe.get("successEvent", ""), "questEvents": recipe["questEvents"],
            "parent": recipe["parent"], "stageOrder": recipe["stageOrder"],
            "stages": [{**{key: value for key, value in stage.items() if key not in ("entity", "component")},
                        "entity": hex16(stage["entity"]), "component": hex16(stage["component"])} for stage in recipe["stages"]],
            "unreachable": recipe["unreachable"], "pattern": later[0] if later is not None else "a kill wave",
            "refused": causes,
        }
        for cause in causes:
            refused[cause.split(":")[0] if cause.startswith(("the member ", "the start object ")) else cause.split(" (")[0]] += 1
        out.append(entry)
    counts = dict(counts)
    counts.update(recipes=len(out), taken=sum(1 for entry in out if not entry["refused"]),
                  members=dict(collections.Counter(member["kind"] for member in members.values())),
                  refusals=dict(refused.most_common()))
    return {"format": FORMAT, "world": WORLD, "counts": counts, "recipes": out, "members": members}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        document = build(sources)
        text = json.dumps(document, indent=1, sort_keys=True)
        partial = target.with_name(target.name + f".partial-{os.getpid()}")
        partial.write_text(text + "\n", encoding="utf-8")
        partial.replace(target)
    except Failure as error:
        print(f"encounters: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"encounters: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['recipes']} quest encounter recipes of {counts['encounters']} encounters ({counts['notQuest']} not of a quest; "
          f"{counts['parentsRead']} parent slices of {counts['placements']} placements), {counts['taken']} with no refusal of the data; "
          f"members {counts['members']}")
    for cause, number in counts["refusals"].items():
        print(f"  refused: {cause}: {number}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

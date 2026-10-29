#!/usr/bin/env python3
"""Write the encounter recipes of the quests (stage QG5c2, docs/systems/encounters.md): one JSON file.

The backend reads GAMEDATA_DIR/encounter-data.json at its start. Without the file the world has no encounter (no quest task of an encounter
event progresses), and the rest of the backend works.

The recipes come from encounter_recipes.py (the chain of a recipe is in its docstring): each quest encounter of a placement of the main
world, its manager, its participant tracker and detector, its stages in the order of the manager, their spawns with the places of the
spawn locations, their objectives and actions, and its parent spawner (the respawn of a new instance). Each member slice is described one
time ("members"): a creature member (its vitals row, its category row, its level, its body slice; creature_data.py makes its kind) or a
start object (its variant, its gatherable row, its game event, its tag; interactions.py makes its template and gathering.py its row).

A member slice that is not a creature and not a start object, in a stage that does not need it (a timed stage, or a stage whose objective
tracks a different slice), is a visual of the stage (the smoke burst of the lost pylons in their tracked stages, the cannon storm of
Helvan): the backend does not make it, and the recipe stays (a rule of this project; a limit of the picture). Stage QG5c3: a member that
ends itself ("lifetime" of encounter_recipes.py: the smoke burst, the powder keg) counts as destroyed at its end in a stage that needs it;
the backend makes no record of it. A recipe has "refused": the causes of the data that the runner does not run (the rules below; the backend
adds the causes of the other game data files: a member kind, a template, or a row that is not there). Empty: the runner takes the recipe.
  - The scope (a rule of this project; the patterns of research quest-encounters.md, Appendix A): stage QG5c2 the kill waves after an
    interaction and the Blighted Ranger; stage QG5c3 the summoned opponents, the ambushes, and the kill waves with a cause of the data.
    PATTERNS gives each other recipe its pattern (the label of the counts); LATER names the patterns of a later stage: the interaction
    sequences and the defenses (QG5c4), the summoned opponent with phases (BT3), the world events and the seasonal events (later).
  - The manager starts the encounter by itself (m_autoActivate). A reset event of the manager (m_resetEncounterEventId, Reset_Encounter of
    the stone tablet of Helvan) is no cause: no slice or table of the data sends it, and the fail of the runner (a fight with no
    participant) is the reset (stage QG5c3, a rule of this project).
  - The detector of the participants (the DetectionVolumeComponent of the entity of the ParticipantTracker) is a sphere, a cylinder, or an
    axis box with a size (stage QG5c3: the cylinder of Torment, the box of the Bathory hall).
  - The parent spawner is an area spawner that spawns at its enable (m_spawnOnEnable; stage QG5c3: a point or a prefab spawner of the data
    has no spawn at its enable, and each one has a twin spawner of the same encounter: the EncounterCondition spawners of the Bathory hall
    and of the bookcase, the prefab spawner of Gazsala; the 16 recipes of stage QG5c2 have area parents).
  - Each objective is an InvasionWaveObjective with a time limit and succeedOnTimeout (a timed stage), a SliceDestroyedObjective (with a
    tracked slice of its own spawns or of the spawns of its child stages, or none: the members of its stage and of its child stages), or a
    SliceSpawnedObjective (stage QG5c3: the spawn of its tracked slice); no failure condition, no background activation.
  - Child stages run (stage QG5c3: one after the other in their order, docs/systems/encounters.md); no DestroySliceAction (a later
    stage); EntityEventAction is a part of the client (the handler of the stage status); KillSpawnsAction removes the members.
  - Each member is a creature member or a start object (a gather or a proximity object: stage QG5c3, the ambushes), the recipe has a
    creature member (else it is an interaction sequence), and a start object is only in a stage that tracks it.

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
AMBUSH = "an ambush"
SUMMONED = "a summoned opponent"
SEQUENCE = "an interaction sequence"
DEFENSE = "a defense"
WORLD_EVENT = "a world event of the open world"
# The pattern of each recipe that is not a kill wave after an interaction (research quest-encounters.md, Appendix A and C), with no
# refusal: the label of the counts. Stage QG5c3 runs them (each with the causes of its data); stage QG5c4 the interaction sequences, the
# defenses, and the world events of the open world (each with the causes of its data: research quest-encounters.md section 1, "a defense
# title does not prove an NPC health objective": the placed defenses have no defended target with a health and no failure condition).
PATTERNS = {
    "slices/worldevents/brimstonesands/enc_worldevent_1625_aeambush.dynamicslice": AMBUSH,
    "slices/worldevents/brightwood/enc_worldevent_99b_msq_yonasretreat.dynamicslice": AMBUSH,
    "slices/worldevents/edengrove/enc_worldevent_99e_msq_blightambush.dynamicslice": AMBUSH,
    "slices/worldevents/edengrove/enc_worldevent_99e_msq_corrstaff.dynamicslice": AMBUSH,
    "slices/worldevents/firstlight/interact/summongazsala/enc_worldevent_09a_s13_gazsalathiss.dynamicslice": SUMMONED,
    "slices/worldevents/firstlight/encounters/enc_worldevent_09a_s16_hercyneboarnamed.dynamicslice": SUMMONED,
    "slices/worldevents/mainstoryquest/thorpe/enc_worldevent_msq_thorpe_01.dynamicslice": SUMMONED,
    "slices/worldevents/everfall/enc_worldevent_windsward_cq3_templarinvectus.dynamicslice": SUMMONED,
    "slices/worldevents/dunwood/grivaros/enc_worldevent_grivaros.dynamicslice": SUMMONED,
    "slices/worldevents/dunwood/bathory/enc_worldevent_bathory_guardian.dynamicslice": SUMMONED,
    "slices/worldevents/brimstonesands/interact/enc_worldevent_1656_0401_drusillabanner.dynamicslice": SEQUENCE,
    "slices/worldevents/brimstonesands/interact/enc_worldevent_1624_diskatumwave.dynamicslice": SEQUENCE,
    "slices/pois/ancient/design/brazier/enc_worldevent_master_brazier2.dynamicslice": SEQUENCE,
    "slices/worldevents/cutlasskeys/encounters/enc_worldevent_10a_q4_shipwreck.dynamicslice": SEQUENCE,
    "slices/worldevents/firstlight/encounters/enc_defense_09a_m5_waveevent.dynamicslice": DEFENSE,
    "slices/worldevents/firstlight/encounters/enc_worldevent_09a_s17_wagonattack.dynamicslice": DEFENSE,
    "slices/worldevents/firstlight/interact/cc/enc_worldevent_09a_s9_hacker.dynamicslice": DEFENSE,
    "slices/worldevents/edengrove/wave_events/enc_worldevent_1434_pyre.dynamicslice": DEFENSE,
    # The boat reinforcements of Monarch's Bluffs: a world event of the open world; the encounter starts by itself at its placement, and the
    # look through the spyglass of its setup brings the boat. Its only quest event is the game event of the cannon gimmick
    # (IO_Gimmick_BreakingWallsCannon_interact), which the placed cannons give too (stage QG5b).
    "slices/worldevents/monarchsbluffs/scallywagshore/enc_worldevent_boatreinforcements_bryseddshore.dynamicslice": WORLD_EVENT,
    "slices/worldevents/monarchsbluffs/scallywagshore/enc_worldevent_boatreinforcements_scallywagshore.dynamicslice": WORLD_EVENT,
}
# Stage QG5c4: the pattern of the recipes of a folder (the eight tarot towers of Dunwood).
PATTERN_FOLDERS = {
    "slices/worldevents/dunwood/gimmicks/arcanatarot/tarottower_bossfight/": SEQUENCE,
}
# The recipes of the patterns of a later stage and their stage (refused).
LATER = {
    "slices/worldevents/dunwood/enc_worldevent_carmilla_mindthief.dynamicslice": ("a summoned opponent with phases", "a later stage (BT3)"),
}
LATER_FOLDERS = {
    "slices/darkness/": ("a seasonal event (the Darkness)", "a later stage (the scheduled and seasonal events)"),
    "slices/worldevents/seasons/": ("a seasonal event", "a later stage (the scheduled and seasonal events)"),
}
TIMED = "InvasionWaveObjective"
DESTROYED = "SliceDestroyedObjective"
SPAWNED = "SliceSpawnedObjective"
# Stage QG5c4: the objective that waits for a change of the active state of an entity of the slice (its trigger: a detection volume).
STATE = "EntityActiveStateChangedObjective"
# The detector shapes that the runner tests (encounter_recipes.detector_shape) and the size that each one needs.
SHAPES = ("sphere", "cylinder", "aabb")


def pattern_of(recipe: dict) -> tuple[str, str] | None:
    """@return The pattern and the later stage of a recipe of LATER or LATER_FOLDERS, or no value."""
    path = recipe["slice"]
    if path in LATER:
        return LATER[path]
    return next((value for folder, value in LATER_FOLDERS.items() if path.startswith(folder)), None)


def pattern_label(recipe: dict) -> str:
    """@return The label of the pattern of a recipe (the counts; stage QG5c3: the members decide some of them)."""
    later = pattern_of(recipe)
    if later is not None:
        return later[0]
    folder = next((value for folder, value in PATTERN_FOLDERS.items() if recipe["slice"].startswith(folder)), None)
    return PATTERNS.get(recipe["slice"], folder or "a kill wave")


def descendants(recipe: dict, index: int) -> list[int]:
    """@return The stage and its child stages (each level of m_stages, in their order)."""
    out, pending = [], [index]
    while pending:
        current = pending.pop(0)
        if current in out or current >= len(recipe["stages"]):
            continue
        out.append(current)
        pending.extend(recipe["stages"][current]["children"])
    return out


def ancestors(recipe: dict, index: int) -> list[int]:
    """@return The stage and each stage above it (the stage whose m_stages names it, and so on)."""
    parent = {child: at for at, stage in enumerate(recipe["stages"]) for child in stage["children"]}
    out = [index]
    while out[-1] in parent and parent[out[-1]] not in out:
        out.append(parent[out[-1]])
    return out


def good_shape(shape: dict) -> bool:
    """@return True for a detector shape that the runner tests: a sphere with a radius, a cylinder with a radius and a height, an axis box
    with its three extents."""
    if shape.get("shape") == "sphere":
        return shape.get("radius", 0.0) > 0.0
    if shape.get("shape") == "cylinder":
        return shape.get("radius", 0.0) > 0.0 and shape.get("height", 0.0) > 0.0
    if shape.get("shape") == "aabb":
        return len(shape.get("extents", [])) == 3 and all(value > 0.0 for value in shape["extents"])
    return False


def good_detector(detector: dict | None) -> bool:
    """@return True when the detector of the participants has at least one shape and each of its shapes is one that the runner tests."""
    shapes = (detector or {}).get("shapes")
    return bool(shapes) and all(good_shape(shape) for shape in shapes)


def tracks(tracked: str, member_path: str, members: dict[str, dict]) -> bool:
    """@return True when the tracked slice or alias of a SliceDestroyedObjective is a spawn: its member slice, the body slice or alias of a
    creature member, or a slice of that alias (the lost pylons track undead_berserker.dynamicslice, a slice of the alias of the member)."""
    member = members.get(member_path, {})
    return tracked in (member_path, member.get("body")) or tracked in member.get("bodySlices", [])


def makes(member: dict | None) -> bool:
    """@return True for a member that the runner makes or counts: a creature, a start object, or a member that ends itself."""
    return member is not None and (member["kind"] in ("creature", "object") or member.get("lifetime", 0.0) > 0.0)


def comes(stage: dict, spawn: dict) -> bool:
    """@return False for a spawn that its stage never makes: its delay is at or after the time limit of its stage (the end of the stage comes
    first; code review of stage QG5c3, finding QG5C3R-R3)."""
    limit = stage["objective"].get("timeLimit", 0) or 0
    return not (limit > 0 and spawn.get("delay", 0) >= limit)


def objectives_of(stage: dict) -> list[dict]:
    """@return Each objective of a stage: the first one and (stage QG5c4) "more", the others in their order."""
    return [stage["objective"], *stage["objective"].get("more", [])]


def needs(stage: dict, member_path: str, members: dict[str, dict]) -> bool:
    """@return True when an objective of a stage counts a spawn of this member: a destroyed or a spawned objective that tracks it or tracks
    no slice. A timed stage needs no spawn (it ends at its time)."""
    for objective in objectives_of(stage):
        if objective["type"] not in (DESTROYED, SPAWNED):
            continue
        tracked = objective.get("trackedSlice") or objective.get("trackedAlias")
        if not tracked or tracks(tracked, member_path, members):
            return True
    return False


def objective_causes(name: str, objective: dict, kind: str, tracked: str | None, below: list[dict], members: dict[str, dict]) -> list[str]:
    """@return The causes of one objective of the stage `name` (the rules of the docstring); `below` is the stage and its child stages."""
    causes: list[str] = []
    if kind == STATE:
        # Stage QG5c4: the stage ends at the change of the active state of its target entity (m_completeOnDeactivated or
        # m_completeOnActivated) that a detection volume of the slice sends when a player enters it (encounter_recipes.state_senders).
        triggers = objective.get("triggers") or []
        target = objective.get("targetName") or objective.get("target")
        if not objective.get("completeOn"):
            causes.append(f"the stage {name} waits for no change of its entity (m_completeOnActivated 0 and m_completeOnDeactivated 0)")
        elif not triggers and objective.get("changedBy"):
            causes.append(f"the stage {name} waits for a change of the entity {target} that the gather of {objective['changedBy'][0]} of "
                          "the encounter slice sends (an object of the root record): a later stage")
        elif not triggers:
            causes.append(f"the stage {name} waits for a change of the entity {target} that no detection volume of the encounter slice sends")
        elif not all(good_shape(shape) for trigger in triggers for shape in trigger["shapes"]) \
                or not all("player" in trigger.get("filter", "").lower() for trigger in triggers):
            causes.append(f"the trigger of the stage {name} is not a shape that the runner tests, or not a volume of the players")
    elif kind == TIMED:
        if objective["timeLimit"] <= 0 or not objective["succeedOnTimeout"]:
            causes.append(f"the wave stage {name} has no timed success")
    elif kind in (DESTROYED, SPAWNED):
        if objective["requiredCount"] <= 0:
            causes.append(f"the stage {name} needs no {'destroyed' if kind == DESTROYED else 'spawned'} slice")
        if tracked and not any(tracks(tracked, spawn["member"], members) for part in below for spawn in part["spawns"]):
            causes.append(f"the stage {name} tracks a slice that it and its child stages do not spawn")
        # Code review of stage QG5c2, finding QG5C2R-R3: the spawns that can count for the objective (its own and those of its child
        # stages that it tracks, not a visual: a creature, a start object, or a member that ends itself) must reach its count, or a time
        # limit with succeedOnTimeout ends it. A stage with no spawn below it and no tracked slice counts each death of a member of its
        # instance (stage QG5c3: Kill Helvan Boss) and needs that time limit.
        # Code review of stage QG5c3, finding QG5C3R-R3: a spawn due at or after the time limit of its own stage never comes (the end of
        # the stage cancels its due entry), so it gives no count.
        timed_end = objective["timeLimit"] > 0 and objective["succeedOnTimeout"]
        possible = sum(spawn["count"] for part in below for spawn in part["spawns"]
                       if (not tracked or tracks(tracked, spawn["member"], members)) and makes(members.get(spawn["member"]))
                       and comes(part, spawn))
        if not timed_end and objective["requiredCount"] > possible:
            causes.append(f"the stage {name} needs {objective['requiredCount']} {'destroyed' if kind == DESTROYED else 'spawned'} "
                          f"slices, and its spawns can give only {possible}")
    else:
        causes.append(f"the objective {kind} of the stage {name}: a later stage")
    return causes


def refusals(recipe: dict, members: dict[str, dict]) -> list[str]:
    """@return The causes of the data that the runner does not run (the rules of the docstring). Empty: supported."""
    causes = list(recipe["unsupported"])
    later = pattern_of(recipe)
    if later is not None:
        causes.append(f"{later[0]}: {later[1]}")
    manager = recipe["manager"]
    if not manager["autoActivate"]:
        causes.append("the manager does not start the encounter by itself (m_autoActivate 0: a start of an event)")
    if not good_detector(recipe.get("detector")):
        causes.append("the detector of the participants is not a sphere, a cylinder, or an axis box with a size")
    parent = recipe.get("parent") or {}
    if parent.get("kind", "area") != "area" or not parent.get("spawnOnEnable", True):
        # Stage QG5c4: the rules of the spawner entity (encounter_recipes.spawner_rules): a prefab or a point spawner starts with
        # m_spawnOnInitialize; the prefab spawners of the Bathory hall and of the rotating bookcase have it 0 and m_spawnOnTrigger 0 (an
        # event of their parent slice starts them).
        start = parent.get("startField", "m_spawnOnEnable")
        causes.append(f"the parent spawner (a {parent.get('kind', '?')} spawner) does not spawn at its start ({start} 0, m_spawnOnTrigger "
                      f"{int(parent.get('spawnOnTrigger', False))}): an event of its parent slice starts it")
    for index, stage in enumerate(recipe["stages"]):
        name = stage["name"]
        below = [recipe["stages"][at] for at in descendants(recipe, index)]
        nested = [spawn["member"].rsplit("/", 1)[-1] for part in below for spawn in part["spawns"]
                  if members.get(spawn["member"], {}).get("nested")]
        if nested:
            # Stage QG5c4: a spawn that is an encounter itself (the four sub-encounters of the braziers, each with its destructible
            # brazier): the stage tracks a slice of the nested encounter.
            causes.append(f"the stage {name} spawns a nested encounter ({nested[0]}: its own manager and stages): a later stage")
        # The members of the stage first (stage QG5c4: a group member is the first cause of the count that it cannot give).
        for spawn in stage["spawns"]:
            member = members.get(spawn["member"], {"kind": "unknown", "cause": "no slice"})
            short = spawn["member"].rsplit("/", 1)[-1]
            # The stages that count this spawn: the stage itself and each parent of it (stage QG5c3: a parent counts the spawns of its
            # child stages).
            needed = any(needs(recipe["stages"][at], spawn["member"], members) for at in ancestors(recipe, index))
            if member["kind"] == "unknown":
                if not needed:
                    # A visual of the stage (the smoke burst of the lost pylons in their tracked stages, the cannon storm of Helvan in a
                    # timed stage): no stage counts it, and the backend does not make it.
                    continue
                if member.get("lifetime", 0.0) > 0.0:
                    continue  # Stage QG5c3: a member that ends itself counts as destroyed at its end (the smoke burst, the powder keg).
                causes.append(f"the member {short}: {member['cause']}")
            elif member["kind"] == "object":
                if member.get("cause"):
                    causes.append(f"the start object {short}: {member['cause']}")
                if not any(part["type"] == DESTROYED and (not (part.get("trackedSlice") or part.get("trackedAlias"))
                                                          or tracks(part.get("trackedSlice") or part.get("trackedAlias"), spawn["member"],
                                                                    members))
                           for part in objectives_of(stage)):
                    causes.append(f"the start object {short} is in a stage that does not track it")
            elif member.get("parts", 1) > 1 and needed:
                # Stage QG5c4: a member slice with more than one creature spawner (the boat landings: an officer and a navigator on their
                # paths); the runner makes one creature for each member.
                causes.append(f"the creature member {short} has {member['parts']} creature spawners (a group on paths): a later stage")
            if spawn["count"] <= 0:
                causes.append(f"a spawn of the stage {name} has no count")
        # Stage QG5c4: each objective of the stage (the stage ends when each one is met: one objective for each tracked member slice).
        for number, objective in enumerate(objectives_of(stage)):
            kind = objective["type"]
            label = name if number == 0 else f"{name} (its objective {number + 1})"
            tracked = objective.get("trackedSlice") or objective.get("trackedAlias")
            if number > 0 and (kind not in (DESTROYED, SPAWNED) or objective["timeLimit"] > 0):
                causes.append(f"the objective {number + 1} of the stage {name} is not a destroyed or a spawned objective with no time limit")
                continue
            causes.extend(objective_causes(label, objective, kind, tracked, below, members))
            if objective.get("failureCondition"):
                causes.append(f"a failure condition of the stage {label}")
            if objective.get("background"):
                causes.append(f"a background activation of the stage {label}")
        for action in stage["startActions"] + stage["endActions"]:
            if action["type"] not in ("EntityEventAction", "KillSpawnsAction"):
                causes.append(f"the action {action['type']} of the stage {name}: a later stage")
    # Stage QG5c4: a recipe with no creature member (an interaction sequence: the Disk of Atum of 1624_0401) is no cause; its stages count
    # its start objects.
    return list(dict.fromkeys(causes))


def hex16(value: int) -> str:
    return f"{value:016X}"


def build(sources: Sources) -> dict:
    catalog = Catalog(sources.read(CATALOG_PATH))
    reader, recipes, counts = read_recipes(sources, catalog)
    # Code review of stage QG5c2, finding QG5C2R-R2: a source that the reads needed and that the game copy did not give (an Oodle entry
    # with no library) stops the tool before a file is written; a smaller document with no message is not a result.
    sources.check_complete()
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
            "unreachable": recipe["unreachable"], "pattern": pattern_label(recipe),
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

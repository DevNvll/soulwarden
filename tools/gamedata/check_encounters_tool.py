#!/usr/bin/env python3
"""Check of the encounter recipe tool (encounters.py) with made-up recipes and members (no file of the game). Stages QG5c2 and QG5c3.

The tool reads the recipes of the game copy (encounter_recipes.py) and gives each recipe the causes that the runner of the backend does not
run (refusals: the rules of the docstring of encounters.py). This check gives the rule functions made-up recipes and compares each result.
The test program of the backend runs it (case QG5C2I; QG5C2A and QG5C3A have the backend side of the file).

  taken              a kill wave after an interaction (a timed setup, a start object that its stage tracks, a kill stage, a cleanup)
                     has no cause; a stage that tracks no slice (the members of its stage) is also taken
  manager            m_autoActivate 0 is a cause; stage QG5c3: a reset event is no cause (no slice sends it); a detector with no shape,
                     a sphere of the radius 0, or a shape that the runner does not test is a cause; a cylinder and an axis box are taken
  parent             stage QG5c3: a point or a prefab parent, or an area parent with no spawn at its enable, is a cause
  objectives         a wave stage with no timed success, a destroyed stage with no count, a destroyed stage that tracks a slice that it
                     does not spawn, an objective of an other class, a failure condition, a background activation, an action that is not
                     EntityEventAction or KillSpawnsAction: one cause each; stage QG5c3: a SliceSpawnedObjective is taken
  cardinality        finding QG5C2R-R3: a destroyed stage that needs more destroyed spawns than its spawns give is a cause, also with a
                     visual; a time limit with succeedOnTimeout ends it (no cause); a stage with no spawn below it and no tracked slice
                     needs that time limit (Kill Helvan Boss)
  children           stage QG5c3: a stage that tracks a slice that only its child stages spawn is taken; its count adds the spawns of
                     its child stages
  members            stage QG5c3: a proximity start object and a start object with two places are taken; a start object in a stage that
                     does not track it, a member with no slice, a recipe with no creature member (an interaction sequence): a cause each
  visual             a member that is not a creature and not a start object, in a stage that tracks a different slice or in a timed
                     stage, is a visual of the stage: no cause; stage QG5c3: in a stage that counts it, a member that ends itself
                     (lifetime) is taken, and an other one is a cause
  lifetime           stage QG5c3 (encounter_recipes.lifetime_of): the trigger chain of made-up entities (a delayed event of the init, a
                     delayed event of a trigger, a rerouter) to a DestroyGDEComponent gives the sum of the delays; an activation ends
                     nothing; a slice with no DestroyGDEComponent has no lifetime
  shapes             stage QG5c3: good_shape for a sphere, a cylinder, and an axis box; a size of 0 is not good
  alias              a stage that tracks a slice of the alias of a creature member (bodySlices) tracks that member
  later              a slice of LATER and a slice of a folder of LATER_FOLDERS get the cause of their stage; a slice of PATTERNS gets its
                     label and no cause of a later stage; an other slice none
  counts             a cause is in the list one time (two same causes of two spawns)
  complete           finding QG5C2R-R2: build() stops when a read found no source of a file (no smaller document, no file)

Usage: python3 -B check_encounters_tool.py [WORK_FOLDER] [CASE...]    (status 0: each case passed; the folder is not used)
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

import encounter_recipes as reader  # noqa: E402
import encounters as tool  # noqa: E402

START = "slices/test/io_test_encstart.dynamicslice"
MEMBER = "slices/test/worldevent_ai_test.dynamicslice"
BODY = "slices/test/test_body.dynamicslice"
ALIAS_SLICE = "slices/test/test_alias_body.dynamicslice"
VISUAL = "slices/test/smoke_burst.dynamicslice"
TIMED_VISUAL = "slices/test/powder_keg.dynamicslice"
PROXIMITY = "slices/test/io_test_ambush.dynamicslice"


def members() -> dict[str, dict]:
    return {START: {"kind": "object", "variant": "Test_EncStart", "table": "Gatherable_Quest", "row": "Test_EncStart",
                    "event": "Test_EncStart_Event", "tag": "", "onlyOnQuest": False, "proximity": False, "interact": True},
            PROXIMITY: {"kind": "object", "variant": "Test_Ambush", "table": "Gatherable_Quest_Proximity", "row": "Test_Ambush",
                        "event": "Test_Ambush_Event", "tag": "IO_Test_Ambush", "onlyOnQuest": True, "proximity": True, "interact": True,
                        "radius": 5.0},
            MEMBER: {"kind": "creature", "vitals": "Test_Member", "category": "Test_Category", "level": 23, "bodyField": "slice",
                     "body": BODY, "bodySlices": [BODY]},
            VISUAL: {"kind": "unknown", "cause": "no vitals row and no start object (GatherableController and Interact)"},
            TIMED_VISUAL: {"kind": "unknown", "cause": "no vitals row and no start object; it ends itself 13.15 s after its spawn",
                           "lifetime": 13.15}}


def objective(kind: str, **changes) -> dict:
    value = {"type": kind, "timeLimit": 0.0, "succeedOnTimeout": False, "requiredCount": 0, "trackedSlice": "", "trackedAlias": "",
             "failureCondition": "", "background": False}
    if kind == tool.TIMED:
        value.update(timeLimit=3.0, succeedOnTimeout=True)
    value.update(changes)
    return value


def stage(name: str, kind: str, spawns: list[dict], **changes) -> dict:
    value = {"name": name, "children": [], "objective": objective(kind, **changes.pop("objective", {})), "startActions": [],
             "endActions": [], "spawns": spawns}
    value.update(changes)
    return value


def spawn(member: str, count: int = 1, places: int = 1) -> dict:
    return {"member": member, "delay": 0.0, "count": count, "locations": [{"position": [0.0, 0.0, 0.0]}] * places}


def sphere(radius: float = 20.0) -> dict:
    return {"radius": radius, "filter": "", "sphere": True, "shapes": [{"shape": "sphere", "radius": radius, "center": [0.0, 0.0, 0.0]}]}


def recipe(**changes) -> dict:
    """@return A made-up kill wave after an interaction: the setup (3 s), the start object, the kill stage (2 members), the cleanup."""
    value = {"slice": "slices/test/enc_test.dynamicslice", "unsupported": [],
             "manager": {"autoActivate": True, "resetEvent": ""},
             "detector": sphere(),
             "parent": {"kind": "area", "spawnOnEnable": True},
             "stages": [stage("Wave_00 - Setup", tool.TIMED, []),
                        stage("Wave_01: Interact with IO", tool.DESTROYED, [spawn(START)],
                              objective={"requiredCount": 1, "trackedSlice": START}),
                        stage("Wave_02 - Objective: Kill AI", tool.DESTROYED, [spawn(MEMBER, 2)],
                              objective={"requiredCount": 2, "trackedSlice": MEMBER},
                              endActions=[{"type": "KillSpawnsAction"}, {"type": "EntityEventAction"}]),
                        stage("Wave_03 - Cleanup", tool.TIMED, [])]}
    value.update(changes)
    return value


def with_stage(index: int, **changes) -> dict:
    value = recipe()
    target = value["stages"][index]
    target["objective"].update(changes.pop("objective", {}))
    target.update(changes)
    return value


def has(causes: list[str], text: str) -> bool:
    return any(text in cause for cause in causes)


def case_taken() -> bool:
    good = tool.refusals(recipe(), members()) == []
    untracked = with_stage(2, objective={"trackedSlice": ""})
    return good and tool.refusals(untracked, members()) == []


def case_manager() -> bool:
    results = []
    value = recipe()
    value["manager"]["autoActivate"] = False
    results.append(has(tool.refusals(value, members()), "m_autoActivate 0"))
    value = recipe()
    value["manager"]["resetEvent"] = "Reset_Encounter"
    results.append(tool.refusals(value, members()) == [])
    for detector in (None, {"radius": 20.0, "filter": "", "sphere": False, "shapes": []}, sphere(0.0),
                     {"radius": 0.0, "filter": "", "sphere": False, "shapes": [{"shape": "none", "center": [0.0, 0.0, 0.0]}]}):
        results.append(has(tool.refusals(recipe(detector=detector), members()), "not a sphere, a cylinder, or an axis box with a size"))
    cylinder = {"radius": 40.0, "filter": "", "sphere": False,
                "shapes": [{"shape": "cylinder", "radius": 40.0, "height": 10.0, "center": [0.0, 0.0, 5.0]}]}
    box = {"radius": 0.0, "filter": "", "sphere": False,
           "shapes": [{"shape": "aabb", "extents": [20.2, 30.3, 4.25], "center": [0.0, 0.0, 0.0]}, sphere(30.0)["shapes"][0]]}
    results.append(tool.refusals(recipe(detector=cylinder), members()) == [])
    results.append(tool.refusals(recipe(detector=box), members()) == [])
    return all(results)


def case_parent() -> bool:
    point = tool.refusals(recipe(parent={"kind": "point", "spawnOnEnable": False}), members())
    prefab = tool.refusals(recipe(parent={"kind": "prefab", "spawnOnEnable": False}), members())
    idle = tool.refusals(recipe(parent={"kind": "area", "spawnOnEnable": False}), members())
    return all(has(causes, "does not spawn at its enable") for causes in (point, prefab, idle))


def case_objectives() -> bool:
    checks = [
        (with_stage(0, objective={"succeedOnTimeout": False}), "the wave stage Wave_00 - Setup has no timed success"),
        (with_stage(0, objective={"timeLimit": 0.0}), "the wave stage Wave_00 - Setup has no timed success"),
        (with_stage(2, objective={"requiredCount": 0}), "the stage Wave_02 - Objective: Kill AI needs no destroyed slice"),
        (with_stage(2, objective={"trackedSlice": "slices/test/other.dynamicslice"}),
         "the stage Wave_02 - Objective: Kill AI tracks a slice that it and its child stages do not spawn"),
        (with_stage(2, objective={"type": "BossPhaseObjective"}), "the objective BossPhaseObjective of the stage Wave_02"),
        (with_stage(2, objective={"failureCondition": "Test_Fail"}), "a failure condition of the stage Wave_02"),
        (with_stage(2, objective={"background": True}), "a background activation of the stage Wave_02"),
        (with_stage(2, endActions=[{"type": "DestroySliceAction"}]), "the action DestroySliceAction of the stage Wave_02"),
    ]
    spawned = with_stage(2, objective={"type": tool.SPAWNED, "requiredCount": 1, "trackedSlice": MEMBER})
    return all(has(tool.refusals(value, members()), text) for value, text in checks) and tool.refusals(spawned, members()) == []


def case_cardinality() -> bool:
    three = with_stage(2, objective={"requiredCount": 3})
    visual = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(VISUAL)], objective={"requiredCount": 3})
    timed = with_stage(2, objective={"requiredCount": 3, "timeLimit": 180.0, "succeedOnTimeout": True})
    helvan = recipe()
    helvan["stages"].insert(3, stage("Wave_06 - Objective: Kill Helvan Boss", tool.DESTROYED, [],
                                     objective={"requiredCount": 1, "timeLimit": 180.0, "succeedOnTimeout": True}))
    helvan_untimed = recipe()
    helvan_untimed["stages"].insert(3, stage("Wave_06 - Objective: Kill Helvan Boss", tool.DESTROYED, [], objective={"requiredCount": 1}))
    return (has(tool.refusals(three, members()), "needs 3 destroyed slices, and its spawns can give only 2")
            and has(tool.refusals(visual, members()), "can give only 2")
            and tool.refusals(timed, members()) == [] and tool.refusals(helvan, members()) == []
            and has(tool.refusals(helvan_untimed, members()), "needs 1 destroyed slices, and its spawns can give only 0"))


def case_children() -> bool:
    value = recipe()
    # The ambush form: the kill stage tracks the member and has two timed child stages that spawn it (2 and 1).
    value["stages"][2]["spawns"] = []
    value["stages"][2]["objective"].update(requiredCount=3)
    value["stages"][2]["children"] = [4, 5]
    value["stages"] += [stage("Subwave01_Basepop", tool.TIMED, [spawn(MEMBER, 2)], objective={"timeLimit": 25.0}),
                        stage("Subwave02_Basepop", tool.TIMED, [spawn(MEMBER, 1), spawn(VISUAL)], objective={"timeLimit": 30.0})]
    taken = tool.refusals(value, members()) == []
    value["stages"][2]["objective"].update(requiredCount=4)
    short = has(tool.refusals(value, members()), "needs 4 destroyed slices, and its spawns can give only 3")
    return taken and short and tool.descendants(value, 2) == [2, 4, 5] and tool.ancestors(value, 5) == [5, 2]


def case_members() -> bool:
    checks = [
        (with_stage(1, objective={"type": tool.TIMED, "timeLimit": 3.0, "succeedOnTimeout": True}), "is in a stage that does not track it"),
        (with_stage(2, spawns=[spawn("slices/test/missing.dynamicslice"), spawn(MEMBER, 2)], objective={"trackedSlice": ""}),
         "the member missing.dynamicslice: no slice"),
        (with_stage(2, spawns=[], objective={"trackedSlice": "", "requiredCount": 1, "timeLimit": 9.0, "succeedOnTimeout": True}),
         "no creature member: not a kill wave"),
    ]
    ambush = with_stage(1, spawns=[spawn(PROXIMITY)], objective={"trackedSlice": PROXIMITY})
    places = with_stage(1, spawns=[spawn(START, 2, 3)], objective={"requiredCount": 2})
    return (all(has(tool.refusals(value, members()), text) for value, text in checks)
            and tool.refusals(ambush, members()) == [] and tool.refusals(places, members()) == [])


def case_visual() -> bool:
    value = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(VISUAL)])
    visual = tool.refusals(value, members()) == []
    timed_stage = with_stage(0, spawns=[spawn(VISUAL)])
    # The same slice in a stage that tracks nothing (each spawn counts) is a member that the runner cannot make; a member that ends itself
    # (the smoke burst, the powder keg) counts.
    untracked = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(VISUAL)], objective={"trackedSlice": ""})
    ends = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(TIMED_VISUAL)], objective={"trackedSlice": "", "requiredCount": 3})
    keg = with_stage(1, spawns=[spawn(TIMED_VISUAL)], objective={"trackedSlice": TIMED_VISUAL})
    return (visual and tool.refusals(timed_stage, members()) == [] and has(tool.refusals(untracked, members()), "the member smoke_burst.dynamicslice")
            and tool.refusals(ends, members()) == [] and tool.refusals(keg, members()) == [])


class Node:
    """A made-up element of an object stream (the fields that encounter_recipes reads: name, value, children, type_id)."""

    def __init__(self, name: str | None = None, value: bytes | None = None, children: list | None = None, type_id=None) -> None:
        self.name = reader.F[name] if name in reader.F else None
        self.value = value
        self.children = children or []
        self.type_id = type_id


def entity_ref(identifier: int) -> Node:
    return Node("m_entityRef", None, [Node(None, None, [Node(None, identifier.to_bytes(8, "big"))])])


def delayed(seconds: float, target: int, kind: int, on_init: bool, on_trigger: bool) -> Node:
    return Node(None, None, [Node("m_timeDelaySeconds", struct.pack(">f", seconds)),
                             Node("m_eventData", None, [entity_ref(target), Node("m_type", kind.to_bytes(4, "big"))]),
                             Node("m_startDelayOnInit", b"\x01" if on_init else b"\x00"),
                             Node("m_startDelayOnTrigger", b"\x01" if on_trigger else b"\x00")], reader.DELAYED_EVENT_CLASS)


def case_lifetime() -> bool:
    destroy = Node(type_id=reader.DESTROY_CLASS)
    rerouter = Node(None, None, [Node("m_events", None, [Node(None, None, [entity_ref(40), Node("m_type", b"\x00\x00\x00\x00")])])],
                    reader.REROUTER_CLASS)
    # The powder keg: the fuse (8 s, init) triggers 20; 20 (0.15 s, on its trigger) triggers 30; 30 reroutes to 40; 40 (5 s) triggers 50.
    keg = {10: {"components": [delayed(8.0, 20, 0, True, False)]},
           20: {"components": [delayed(0.15, 30, 0, False, True)]},
           30: {"components": [rerouter]},
           40: {"components": [delayed(5.0, 50, 0, False, True)]},
           50: {"components": [destroy]}}
    # An activation of the destroy entity ends nothing; an event of a trigger that no one triggers does not run.
    activated = {10: {"components": [delayed(2.0, 50, 2, True, False), delayed(1.0, 50, 0, False, True)]}, 50: {"components": [destroy]}}
    no_destroy = {10: {"components": [delayed(2.0, 20, 0, True, False)]}, 20: {"components": []}}
    smoke = {10: {"components": [delayed(4.2, 50, 0, True, False), delayed(3.25, 20, 2, True, False)]}, 20: {"components": []},
             50: {"components": [destroy]}}
    return (reader.lifetime_of(keg) == 13.15 and reader.lifetime_of(activated) is None and reader.lifetime_of(no_destroy) is None
            and reader.lifetime_of(smoke) == 4.2)


def case_shapes() -> bool:
    return (tool.good_shape({"shape": "sphere", "radius": 3.0}) and tool.good_shape({"shape": "cylinder", "radius": 40.0, "height": 10.0})
            and tool.good_shape({"shape": "aabb", "extents": [1.0, 2.0, 3.0]})
            and not tool.good_shape({"shape": "cylinder", "radius": 40.0, "height": 0.0})
            and not tool.good_shape({"shape": "aabb", "extents": [1.0, 0.0, 3.0]}) and not tool.good_shape({"shape": "box", "radius": 2.0}))


def case_alias() -> bool:
    table = members()
    table[MEMBER].update(bodyField="alias", body="slices/test/test.aliasasset", bodySlices=[BODY, ALIAS_SLICE])
    value = with_stage(2, objective={"trackedSlice": ALIAS_SLICE})
    return (tool.tracks(ALIAS_SLICE, MEMBER, table) and tool.tracks(MEMBER, MEMBER, table)
            and not tool.tracks("slices/test/other.dynamicslice", MEMBER, table) and tool.refusals(value, table) == [])


def case_later() -> bool:
    named = next(iter(tool.LATER))
    folder = next(iter(tool.LATER_FOLDERS))
    pattern = next(iter(tool.PATTERNS))
    later_named = tool.refusals(recipe(slice=named), members())
    later_folder = tool.refusals(recipe(slice=folder + "test/enc_test.dynamicslice"), members())
    return (has(later_named, f"{tool.LATER[named][0]}: {tool.LATER[named][1]}")
            and has(later_folder, f"{tool.LATER_FOLDERS[folder][0]}: {tool.LATER_FOLDERS[folder][1]}")
            and tool.pattern_of(recipe()) is None and tool.refusals(recipe(slice=pattern), members()) == []
            and tool.pattern_label(recipe(slice=pattern)) == tool.PATTERNS[pattern] and tool.pattern_label(recipe()) == "a kill wave")


def case_counts() -> bool:
    value = with_stage(1, spawns=[spawn(START), spawn(START)], objective={"type": tool.TIMED, "timeLimit": 3.0, "succeedOnTimeout": True})
    causes = tool.refusals(value, members())
    return len(causes) == len(set(causes)) and sum(1 for cause in causes if "does not track it" in cause) == 1


def case_complete() -> bool:
    """Finding QG5C2R-R2: build() stops (Failure) when a read of the recipes found no source of a file (a layer of an Oodle entry with no
    library), before main() writes a file; a complete read gives the document."""

    class FakeSources:
        def __init__(self, missing: bool) -> None:
            self.absent: list[str] = []
            self.missing = missing

        def read(self, path: str) -> bytes:
            return b""

        def check_complete(self) -> None:
            if self.absent:
                raise tool.Failure(f"{len(self.absent)} file(s) of the game copy have no source (first: {self.absent[0]})")

    def read_stub(sources, catalog):
        if sources.missing:
            sources.absent.append("regions/r_+00_+00/capitals/test/test.capitals.json")
        return type("Reader", (), {"members": {}})(), [], {"encounters": 0, "notQuest": 0, "parentsRead": 0, "placements": 0}

    saved = (tool.Catalog, tool.read_recipes, tool.slice_assets)
    try:
        tool.Catalog = lambda data: None
        tool.read_recipes = read_stub
        tool.slice_assets = lambda catalog: {}
        complete = tool.build(FakeSources(False))["counts"]["recipes"] == 0
        try:
            tool.build(FakeSources(True))
        except tool.Failure:
            return complete
        return False
    finally:
        tool.Catalog, tool.read_recipes, tool.slice_assets = saved


CASES = {"taken": case_taken, "manager": case_manager, "parent": case_parent, "objectives": case_objectives,
         "cardinality": case_cardinality, "children": case_children, "members": case_members, "visual": case_visual,
         "lifetime": case_lifetime, "shapes": case_shapes, "alias": case_alias, "later": case_later, "counts": case_counts,
         "complete": case_complete}


def main() -> int:
    names = [name for name in sys.argv[1:] if name in CASES] or list(CASES)
    unknown = [name for name in sys.argv[2:] if name not in CASES]
    if unknown:
        print(f"check_encounters_tool: unknown case(s) {', '.join(unknown)}", file=sys.stderr)
        return 2
    failed = 0
    for name in names:
        try:
            passed = CASES[name]()
        except (KeyError, TypeError, ValueError, IndexError, AttributeError) as error:
            passed = False
            print(f"{name}: {type(error).__name__}: {error}")
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
        failed += 0 if passed else 1
    print(f"check_encounters_tool: {len(names) - failed} of {len(names)} cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

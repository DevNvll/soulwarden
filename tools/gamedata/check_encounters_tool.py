#!/usr/bin/env python3
"""Check of the encounter recipe tool (encounters.py) with made-up recipes and members (no file of the game). Stage QG5c2.

The tool reads the recipes of the game copy (encounter_recipes.py) and gives each recipe the causes that the runner of the backend does not
run (refusals: the rules of the docstring of encounters.py). This check gives the rule functions made-up recipes and compares each result.
The test program of the backend runs it (case QG5C2I; QG5C2A has the backend side of the file).

  taken              a kill wave after an interaction (a timed setup, a start object that its stage tracks, a kill stage, a cleanup)
                     has no cause; a stage that tracks no slice (the members of its stage) is also taken
  manager            m_autoActivate 0, a reset event, a detector with no sphere or with the radius 0: one cause each
  objectives         a wave stage with no timed success, a destroyed stage with no count, a destroyed stage that tracks a slice that it
                     does not spawn, an objective of an other class, a failure condition, a background activation, a child stage, an
                     action that is not EntityEventAction or KillSpawnsAction: one cause each
  members            a proximity start object (an ambush), a start object in a stage that does not track it, a start object with two
                     places, a member with no slice, a recipe with no creature member (an interaction sequence): one cause each
  visual             a member that is not a creature and not a start object, in a stage that tracks a different slice, is a visual of
                     the stage: no cause (the lost pylons)
  alias              a stage that tracks a slice of the alias of a creature member (bodySlices) tracks that member
  later              a slice of LATER and a slice of a folder of LATER_FOLDERS get the cause of their stage; an other slice none
  counts             a cause is in the list one time (two same causes of two spawns)

Usage: python3 -B check_encounters_tool.py [WORK_FOLDER] [CASE...]    (status 0: each case passed; the folder is not used)
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

import encounters as tool  # noqa: E402

START = "slices/test/io_test_encstart.dynamicslice"
MEMBER = "slices/test/worldevent_ai_test.dynamicslice"
BODY = "slices/test/test_body.dynamicslice"
ALIAS_SLICE = "slices/test/test_alias_body.dynamicslice"
VISUAL = "slices/test/smoke_burst.dynamicslice"


def members() -> dict[str, dict]:
    return {START: {"kind": "object", "variant": "Test_EncStart", "table": "Gatherable_Quest", "row": "Test_EncStart",
                    "event": "Test_EncStart_Event", "tag": "", "onlyOnQuest": False, "proximity": False},
            MEMBER: {"kind": "creature", "vitals": "Test_Member", "category": "Test_Category", "level": 23, "bodyField": "slice",
                     "body": BODY, "bodySlices": [BODY]},
            VISUAL: {"kind": "unknown", "cause": "no vitals row and no start object (GatherableController and Interact)"}}


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


def recipe(**changes) -> dict:
    """@return A made-up kill wave after an interaction: the setup (3 s), the start object, the kill stage (2 members), the cleanup."""
    value = {"slice": "slices/test/enc_test.dynamicslice", "unsupported": [],
             "manager": {"autoActivate": True, "resetEvent": ""},
             "detector": {"radius": 20.0, "filter": "", "sphere": True},
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
    value["manager"]["resetEvent"] = "Test_Reset"
    results.append(has(tool.refusals(value, members()), "a reset event of the manager (Test_Reset)"))
    for detector in (None, {"radius": 20.0, "filter": "", "sphere": False}, {"radius": 0.0, "filter": "", "sphere": True}):
        results.append(has(tool.refusals(recipe(detector=detector), members()), "not a sphere with a radius"))
    return all(results)


def case_objectives() -> bool:
    checks = [
        (with_stage(0, objective={"succeedOnTimeout": False}), "the wave stage Wave_00 - Setup has no timed success"),
        (with_stage(0, objective={"timeLimit": 0.0}), "the wave stage Wave_00 - Setup has no timed success"),
        (with_stage(2, objective={"requiredCount": 0}), "the stage Wave_02 - Objective: Kill AI needs no destroyed slice"),
        (with_stage(2, objective={"trackedSlice": "slices/test/other.dynamicslice"}),
         "the stage Wave_02 - Objective: Kill AI tracks a slice that it does not spawn"),
        (with_stage(2, objective={"type": "BossPhaseObjective"}), "the objective BossPhaseObjective of the stage Wave_02"),
        (with_stage(2, objective={"failureCondition": "Test_Fail"}), "a failure condition of the stage Wave_02"),
        (with_stage(2, objective={"background": True}), "a background activation of the stage Wave_02"),
        (with_stage(2, children=["Wave_02b"]), "the stage Wave_02 - Objective: Kill AI has child stages"),
        (with_stage(2, endActions=[{"type": "DestroySliceAction"}]), "the action DestroySliceAction of the stage Wave_02"),
    ]
    return all(has(tool.refusals(value, members()), text) for value, text in checks)


def case_members() -> bool:
    proximity = members()
    proximity[START]["proximity"] = True
    checks = [
        (recipe(), proximity, "is a proximity object (an ambush"),
        (with_stage(1, objective={"type": tool.TIMED, "timeLimit": 3.0, "succeedOnTimeout": True}), members(),
         "is in a stage that does not track it"),
        (with_stage(1, spawns=[spawn(START, 2)]), members(), "has more than one place"),
        (with_stage(1, spawns=[spawn(START, 1, 2)]), members(), "has more than one place"),
        (with_stage(2, spawns=[spawn("slices/test/missing.dynamicslice")], objective={"trackedSlice": ""}), members(),
         "the member missing.dynamicslice: no slice"),
        (with_stage(2, spawns=[], objective={"trackedSlice": ""}), members(), "no creature member: not a kill wave"),
    ]
    return all(has(tool.refusals(value, table), text) for value, table, text in checks)


def case_visual() -> bool:
    value = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(VISUAL)])
    visual = tool.refusals(value, members()) == []
    # The same slice in a stage that tracks nothing (each spawn counts) is a member that the runner cannot make.
    untracked = with_stage(2, spawns=[spawn(MEMBER, 2), spawn(VISUAL)], objective={"trackedSlice": ""})
    return visual and has(tool.refusals(untracked, members()), "the member smoke_burst.dynamicslice")


def case_alias() -> bool:
    table = members()
    table[MEMBER].update(bodyField="alias", body="slices/test/test.aliasasset", bodySlices=[BODY, ALIAS_SLICE])
    value = with_stage(2, objective={"trackedSlice": ALIAS_SLICE})
    return (tool.tracks(ALIAS_SLICE, MEMBER, table) and tool.tracks(MEMBER, MEMBER, table)
            and not tool.tracks("slices/test/other.dynamicslice", MEMBER, table) and tool.refusals(value, table) == [])


def case_later() -> bool:
    named = next(iter(tool.LATER))
    folder = next(iter(tool.LATER_FOLDERS))
    later_named = tool.refusals(recipe(slice=named), members())
    later_folder = tool.refusals(recipe(slice=folder + "test/enc_test.dynamicslice"), members())
    return (has(later_named, f"{tool.LATER[named][0]}: {tool.LATER[named][1]}")
            and has(later_folder, f"{tool.LATER_FOLDERS[folder][0]}: {tool.LATER_FOLDERS[folder][1]}")
            and tool.pattern_of(recipe()) is None)


def case_counts() -> bool:
    proximity = members()
    proximity[START]["proximity"] = True
    value = with_stage(1, spawns=[spawn(START), spawn(START)])
    causes = tool.refusals(value, proximity)
    return len(causes) == len(set(causes)) and sum(1 for cause in causes if "proximity object" in cause) == 1


CASES = {"taken": case_taken, "manager": case_manager, "objectives": case_objectives, "members": case_members, "visual": case_visual,
         "alias": case_alias, "later": case_later, "counts": case_counts}


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
        except (KeyError, TypeError, ValueError, IndexError) as error:
            passed = False
            print(f"{name}: {type(error).__name__}: {error}")
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
        failed += 0 if passed else 1
    print(f"check_encounters_tool: {len(names) - failed} of {len(names)} cases passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

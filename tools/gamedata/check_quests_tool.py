#!/usr/bin/env python3
"""Check of the quest data tool (quests.py) with made-up tables (no file of the game). Stage 1.8a.

The tool stops with status 1, and writes no file, at a source that it cannot use (its docstring has the list). This check gives the
functions of the tool made-up tables and compares each result. The test program of the backend runs it (case QG-TOOL; the cases QG1 to
QG9 have the backend side of the file).

  tree       the index rule of the client (builder 0x4466350): a child before its container, the index goes up after each child, a
             container after its children; a leaf that comes again gets a new index; a container that comes again is a hole
  blank      a blank cell is not in the document, an explicit "0" stays "0" (TargetQty); the type of a task is its enum value
  stops      a SubTask link with no task row, a root task with no row, a cycle, an unknown task type, two task rows of one id, two
             objective ids with one CRC-32, two achievements with one bit, a bit above 65535, a tree with an index above 255: each one stops
             the tool
  lists      the start objective of a territory, the ids of a backstory override (commas, a note after ":"), the diagnostics of an
             achievement of an action with no row and of a start objective with no row
  write      a run that stops leaves the earlier file; a run that completes replaces it

Usage: check_quests_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import quests as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

CASES = ("tree", "blank", "stops", "lists", "write")
FAILED: list[str] = []


def report(ok: bool, text: str) -> None:
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        FAILED.append(text)


def stops(action) -> str | None:
    try:
        action()
    except Failure as error:
        return str(error)
    return None


def task(identifier: str, kind: str, *children: str, **cells) -> dict:
    row = {"TaskID": identifier, "Type": kind}
    for index, child in enumerate(children, start=1):
        row[f"SubTask{index}"] = child
    row.update(cells)
    return row


def base_tasks() -> list[dict]:
    """The tree of the made-up quest TestQuest: the shape of 06_getoffbeach (a consecutive root, a simple and a partial container)."""
    return [
        task("Root", "ConsecutiveTaskContainer", "Init", "UnlockA", "Check"),
        task("Init", "SimpleTaskContainer", "U1", "L1"),
        task("U1", "TaskUnlockAchievement", ItemName="Test_A", TargetQty="1"),
        task("L1", "TaskLockAchievement", ItemName="Test_B", TargetQty="0", StayActive="1"),
        task("UnlockA", "TaskUnlockAchievement", ItemName="Test_C"),
        task("Check", "PartialTaskContainer", "Wait", "Inner"),
        task("Wait", "TaskAchievement", ItemName="Test_A && !Test_B"),
        task("Inner", "ConsecutiveTaskContainer", "Trigger", "U1"),
        task("Trigger", "TaskTriggerArea", TriggerAreaType="Enter", TP_TargetTag="T"),
    ]


def tables(tasks=None, objectives=None, achievements=None, territories=None, backstories=None) -> tool.Tables:
    if tasks is None:
        tasks = base_tasks()
    if objectives is None:
        objectives = [{"ObjectiveID": "TestQuest", "Type": "MainStoryQuest", "Task": "Root", "RequiredLevel": ""}]
    if achievements is None:
        achievements = [{"AchievementID": "Test_A", "AchievementIndex": 10}, {"AchievementID": "Test_B", "AchievementIndex": 11},
                        {"AchievementID": "Test_C", "AchievementIndex": 12}]
    if territories is None:
        territories = [{"TerritoryID": 6, "ObjectiveID": "TestQuest"}, {"TerritoryID": 7, "ObjectiveID": ""}]
    if backstories is None:
        backstories = [{"BackstoryID": "Archetype_Test", "ObjectiveUnlockOverride": "", "AchievementUnlockOverride": "Test_C"},
                       {"BackstoryID": "Level60_Test", "ObjectiveUnlockOverride": "TestQuest : note,Missing_Start ",
                        "AchievementUnlockOverride": "Test_A,Test_B", "LevelOverride": "60"}]
    return tool.Tables({
        tool.OBJECTIVE_TYPE: [("objectives.datasheet", FakeSheet("Objectives", objectives))],
        tool.TASK_TYPE: [("tasks.datasheet", FakeSheet("ObjectiveTasks", tasks))],
        tool.ACHIEVEMENT_TYPE: [("achievements.datasheet", FakeSheet("AchievementDataTable", achievements))],
        tool.NPA_TYPE: [],
        tool.TERRITORY_TYPE: [("territories.datasheet", FakeSheet("TerritoryDefinitions", territories))],
        tool.BACKSTORY_TYPE: [("backstories.datasheet", FakeSheet("BackstoryDefinitions", backstories))],
        tool.EVENT_TYPE: [], tool.WHISPER_TYPE: [], tool.SCHEDULE_TYPE: []})


def case_tree() -> None:
    document = tool.build(tables())
    objective = document["objectives"][0]
    nodes = [[index, key] for index, key, _ in objective["tree"]]
    # Root children: Init (U1 0, L1 1, Init 2), UnlockA 3, Check (Wait 4, Inner (Trigger 5, U1 again 6, Inner 7), Check 8), Root 9.
    expected = [[0, "u1"], [1, "l1"], [2, "init"], [3, "unlocka"], [4, "wait"], [5, "trigger"], [6, "u1"], [7, "inner"], [8, "check"],
                [9, "root"]]
    report(nodes == expected, f"tree: the index rule of the client (got {nodes})")
    children = {index: kids for index, _, kids in objective["tree"]}
    report(children[9] == [2, 3, 8] and children[8] == [4, 7] and children[7] == [5, 6], f"tree: the children of each container ({children})")
    repeated = base_tasks()
    repeated[0] = task("Root", "ConsecutiveTaskContainer", "Init", "UnlockA", "Init", "Check")
    document = tool.build(tables(tasks=repeated))
    objective = document["objectives"][0]
    report(objective["holes"] == [4] and objective["tree"][-1][0] == 10 and objective["tree"][-1][2] == [2, 3, 9],
           f"tree: a container that comes again keeps a hole (holes {objective['holes']}, root {objective['tree'][-1]})")


def case_blank() -> None:
    document = tool.build(tables())
    tasks = document["tasks"]
    report(tasks["u1"]["cells"].get("TargetQty") == "1" and tasks["l1"]["cells"].get("TargetQty") == "0"
           and "TargetQty" not in tasks["unlocka"]["cells"], "blank: a blank TargetQty is absent, an explicit 0 stays 0")
    report(tasks["root"]["code"] == 1 and tasks["init"]["code"] == 0 and tasks["check"]["code"] == 2 and tasks["wait"]["code"] == 18
           and tasks["l1"]["code"] == 19 and tasks["u1"]["code"] == 20 and tasks["trigger"]["code"] == 3,
           "blank: the type of a task is the value of the client enum")
    report(document["objectives"][0]["crc"] == tool.crc("testquest") and tasks["u1"]["crc"] == tool.crc("U1"),
           "blank: the CRC-32 of an id in lower case")
    report("RequiredLevel" not in document["objectives"][0]["cells"], "blank: a blank objective cell is absent")


def case_stops() -> None:
    missing = base_tasks()
    missing[1] = task("Init", "SimpleTaskContainer", "U1", "NoSuchTask")
    report(stops(lambda: tool.build(tables(tasks=missing))) is not None, "stops: a SubTask link with no task row")
    report(stops(lambda: tool.build(tables(objectives=[{"ObjectiveID": "TestQuest", "Type": "Journey", "Task": "Nothing"}]))) is not None,
           "stops: a root task with no row")
    cycle = base_tasks()
    cycle[7] = task("Inner", "ConsecutiveTaskContainer", "Trigger", "Check")
    report(stops(lambda: tool.build(tables(tasks=cycle))) is not None, "stops: a cycle")
    unknown = base_tasks() + [task("Odd", "TaskDoSomething")]
    report(stops(lambda: tool.build(tables(tasks=unknown))) is not None, "stops: an unknown task type")
    twice = base_tasks() + [task("u1", "TaskUnlockAchievement")]
    report(stops(lambda: tool.build(tables(tasks=twice))) is not None, "stops: two task rows of one id (case folded)")
    # "plumless" and "buckeroo" have one CRC-32 (a known pair of the CRC-32 of zlib).
    report(tool.crc("plumless") == tool.crc("buckeroo"), "stops: the made-up CRC pair has one CRC-32")
    objectives = [{"ObjectiveID": "plumless", "Type": "Journey", "Task": "Root"}, {"ObjectiveID": "buckeroo", "Type": "Journey", "Task": "Root"}]
    report(stops(lambda: tool.build(tables(objectives=objectives))) is not None, "stops: two objective ids with one CRC-32")
    pair = base_tasks() + [task("plumless", "TaskTimer"), task("buckeroo", "TaskTimer")]
    report(stops(lambda: tool.build(tables(tasks=pair))) is not None, "stops: two task ids with one CRC-32")
    bits = [{"AchievementID": "Test_A", "AchievementIndex": 10}, {"AchievementID": "Test_B", "AchievementIndex": 10}]
    report(stops(lambda: tool.build(tables(achievements=bits))) is not None, "stops: two achievements with one bit")
    report(stops(lambda: tool.build(tables(achievements=[{"AchievementID": "Test_A", "AchievementIndex": 65536}]))) is not None,
           "stops: a bit above 65535")
    wide = [task("Root", "SimpleTaskContainer", *[f"L{index}" for index in range(19)])]
    for index in range(19):
        wide.append(task(f"L{index}", "SimpleTaskContainer", *[f"T{index}_{leaf}" for leaf in range(19)]))
        wide.extend(task(f"T{index}_{leaf}", "TaskTimer") for leaf in range(19))
    report(stops(lambda: tool.build(tables(tasks=wide))) is not None, "stops: a tree with an index above 255")


def case_lists() -> None:
    document = tool.build(tables())
    report(document["territories"] == [{"territory": 6, "objective": "TestQuest", "table": "territories.datasheet"}],
           "lists: the start objective of a territory (a row with no ObjectiveID is not a start)")
    template = next(row for row in document["backstories"] if row["id"] == "Level60_Test")
    report(template["objectives"] == ["TestQuest", "Missing_Start"] and template["achievements"] == ["Test_A", "Test_B"]
           and template["level"] == "60", f"lists: the ids of a backstory override ({template})")
    archetype = next(row for row in document["backstories"] if row["id"] == "Archetype_Test")
    report(archetype["archetype"] and archetype["achievements"] == ["Test_C"], "lists: an archetype row and its achievement")
    report(document["diagnostics"]["startObjectivesWithNoRow"] == ["Missing_Start"], "lists: a start objective with no row is a diagnostic")
    unknown = base_tasks()
    unknown[2] = task("U1", "TaskUnlockAchievement", ItemName="Test_Unknown")
    document = tool.build(tables(tasks=unknown))
    report(document["diagnostics"]["actionAchievementsWithNoRow"] == ["Test_Unknown"], "lists: an achievement of an action with no row")


def case_write(folder: Path) -> None:
    target = folder / "quest-data.json"
    target.write_text("earlier file\n")
    try:
        tool.write(target, tool.build(tables(objectives=[{"ObjectiveID": "TestQuest", "Type": "Journey", "Task": "Nothing"}])))
    except Failure:
        pass
    report(target.read_text() == "earlier file\n", "write: a run that stops leaves the earlier file")
    tool.write(target, tool.build(tables()))
    document = json.loads(target.read_text())
    report(document["format"] == tool.FORMAT and document["counts"]["objectives"] == 1, "write: a run that completes replaces it")
    report([path.name for path in folder.iterdir()] == ["quest-data.json"], "write: no temporary file stays")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    for stale in folder.iterdir():
        stale.unlink()
    wanted = sys.argv[2:] or list(CASES)
    for case in wanted:
        if case == "tree":
            case_tree()
        elif case == "blank":
            case_blank()
        elif case == "stops":
            case_stops()
        elif case == "lists":
            case_lists()
        elif case == "write":
            case_write(folder)
        else:
            report(False, f"no case {case}")
    print("status:", "FAIL" if FAILED else "ok")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())

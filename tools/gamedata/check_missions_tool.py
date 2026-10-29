#!/usr/bin/env python3
"""Check of the mission data tool (missions.py) with made-up tables (no file of the game). Stage QG6a.

The tool stops with status 1, and writes no file, at a source that it cannot use (its docstring has the list). This check gives the
functions of the tool made-up tables and compares each result. The test program of the backend runs it (case MB-TOOL; the cases MB1 to
MB9 have the backend side of the file).

  rows       the missions of each table in the table and row order, the table names (town, world), the source text of each cell (a blank
             cell is not in the document, an explicit "0" stays "0"), the CRC-32 of the lower-case MissionID and ObjectiveID
  rules      the territory rule: a blank cell no rule, "N,M" a list, "!N" an exclusion; the two spellings of the column
  joins      the projects (type, level), the events of the missions only, the weights and the boards, the diagnostics (an objective, a
             project, and an event with no row; the rows with the column that the client loader does not read)
  stops      two missions of one id (also in two tables, another case), two projects of one id, a territory cell "!1,2", "x", "0", and
             "70000", a table with no MissionID, no MissionData table: each one stops the tool
  write      a run that stops leaves the earlier file; a run that completes replaces it

Usage: check_missions_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import missions as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402
from quests import crc  # noqa: E402

CASES = ("rows", "rules", "joins", "stops", "write")
FAILED: list[str] = []
TOWN = "sharedassets/springboardentitites/datatables/javelindata_territoryprogression_missions.datasheet"
WORLD = "sharedassets/springboardentitites/datatables/javelindata_owg_mission.datasheet"


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


def town_rows() -> list[dict]:
    return [
        {"MissionID": "TestProject_1_Wolf_5", "ObjectiveID": "Test_Kill_Template", "TerritoryProjectId": "TestProject", "MissionTier": "1",
         "TaskKillContributionOverride": "Wolf", "TaskKillContributionQtyOverride": "5", "SuccessGameEventIdOverride": "Test_Success",
         "FailureGameEventIdOverride": "Test_Failure", "AvailableTerritoryID": "!16", "MinLevel": "", "CommunityGoalAmount": "0",
         "MissionGoalType": "Kill", "Enabled": "1"},
        {"MissionID": "TestProject_1_Boar_8", "ObjectiveID": "Test_Kill_Template", "TerritoryProjectId": "TestProject", "MissionTier": "1",
         "TaskKillContributionOverride": "Boar", "TaskKillContributionQtyOverride": "8", "AvailableTerritoryID": "4,6",
         "PauseAndClearLootTagOnTaskCompleteOverride": "1", "MissionGoalType": "Kill", "Enabled": "1"},
        {"MissionID": "TestProject_1_Lost", "ObjectiveID": "Test_No_Row", "TerritoryProjectId": "NoProject", "AvailableTerritoryID": "",
         "SuccessGameEventIdOverride": "Test_No_Event", "MissionGoalType": "Explore", "Enabled": "1"},
    ]


def world_rows() -> list[dict]:
    return [{"MissionID": "FM_Test_0101", "ObjectiveID": "Test_Kill_Template", "AvailableTerritoryId": "6", "RequiredFaction": "Marauder",
             "MinLevel": "20", "MissionGoalType": "Raid"}]


def tables(town=None, world=None, projects=None, extra_missions=None) -> tool.Tables:
    if town is None:
        town = town_rows()
    if world is None:
        world = world_rows()
    if projects is None:
        projects = [{"ProjectId": "TestProject", "ProjectType": "AlwaysAvailable", "ProgressionLevel": "1", "Cost": "0"}]
    missions = [(TOWN, FakeSheet("TerritoryProgressionMissions", town)), (WORLD, FakeSheet("OWGMissions", world))]
    missions += extra_missions or []
    events = [{"EventID": "Test_Success", "UniversalExpAmount": "110", "CurrencyReward": "135", "TerritoryStanding": "350"},
              {"EventID": "Test_Failure", "AzothReward": "0"}, {"EventID": "Not_Wanted", "UniversalExpAmount": "1"}]
    return tool.Tables({
        tool.MISSION_TYPE: missions,
        tool.PROJECT_TYPE: [("progression.datasheet", FakeSheet("TerritoryProgression", projects))],
        tool.EVENT_TYPE: [("events.datasheet", FakeSheet("GameEvents", events))],
        tool.WEIGHT_TYPE: [("weights.datasheet", FakeSheet("Weights", [{"MissionWeightId": "RAID_slot_1", "SlotNumber": "1",
                                                                          "Bucket": "FactionPvE", "MissionGoalType": "Raid",
                                                                          "Weight": "9000"}]))],
        tool.BOARD_TYPE: [("boards.datasheet", FakeSheet("Boards", [{"FlexibleMissionBoardId": "TestBoard", "MaxDisplayableMissionsCount": "6"}]))],
        tool.OBJECTIVE_TYPE: [("objectives.datasheet", FakeSheet("Objectives", [{"ObjectiveID": "Test_Kill_Template", "Type": "CommunityGoal"}]))]})


def case_rows() -> None:
    document = tool.build(tables())
    missions = document["missions"]
    report([mission["id"] for mission in missions] == ["TestProject_1_Wolf_5", "TestProject_1_Boar_8", "TestProject_1_Lost", "FM_Test_0101"],
           f"rows: the table order and the row order ({[mission['id'] for mission in missions]})")
    report([(mission["table"], mission["row"]) for mission in missions] == [("town", 0), ("town", 1), ("town", 2), ("world", 0)],
           "rows: the table names and the row numbers")
    wolf = missions[0]
    report(wolf["crc"] == crc("TestProject_1_Wolf_5") and wolf["objectiveCrc"] == crc("Test_Kill_Template")
           and wolf["objective"] == "Test_Kill_Template", f"rows: the CRC-32 of the lower-case ids ({wolf['crc']:08x}, {wolf['objectiveCrc']:08x})")
    report("MinLevel" not in wolf["cells"] and wolf["cells"].get("CommunityGoalAmount") == "0" and "MissionID" not in wolf["cells"]
           and wolf["cells"].get("TaskKillContributionQtyOverride") == "5",
           "rows: a blank cell is not in the cells, an explicit 0 stays 0, the id is not a cell")


def case_rules() -> None:
    missions = tool.build(tables())["missions"]
    rules = [mission["territories"] for mission in missions]
    report(rules[0] == {"ids": [16], "exclude": True}, f"rules: '!16' is an exclusion ({rules[0]})")
    report(rules[1] == {"ids": [4, 6], "exclude": False}, f"rules: '4,6' is a list ({rules[1]})")
    report(rules[2] == {"ids": [], "exclude": False}, f"rules: a blank cell is no rule ({rules[2]})")
    report(rules[3] == {"ids": [6], "exclude": False}, f"rules: the column AvailableTerritoryId of the world table ({rules[3]})")


def case_joins() -> None:
    document = tool.build(tables())
    project = document["projects"][0]
    report(project["id"] == "TestProject" and project["type"] == "AlwaysAvailable" and project["level"] == 1
           and project["crc"] == crc("TestProject"), f"joins: the project, its type and level ({project})")
    report(sorted(document["events"]) == ["test_failure", "test_success"]
           and document["events"]["test_success"] == {"id": "Test_Success", "UniversalExpAmount": "110", "CurrencyReward": "135",
                                                      "TerritoryStanding": "350"},
           f"joins: only the events of the missions, with their cells ({sorted(document['events'])})")
    report(len(document["weights"]) == 1 and document["weights"][0]["cells"]["Weight"] == "9000" and len(document["flexibleBoards"]) == 1,
           "joins: the weights and the flexible boards")
    diagnostics = document["diagnostics"]
    report(diagnostics["objectivesWithNoRow"] == ["Test_No_Row"] and diagnostics["projectsWithNoRow"] == ["NoProject"]
           and diagnostics["eventsWithNoRow"] == ["test_no_event"] and diagnostics["aliasColumnRows"] == 1,
           f"joins: the diagnostics ({diagnostics})")
    report(document["counts"]["byTable"] == {"town": 3, "world": 1} and document["counts"]["killOverrides"] == 2,
           f"joins: the counts ({document['counts']})")


def case_stops() -> None:
    twice = town_rows() + [dict(town_rows()[0])]
    report(stops(lambda: tool.build(tables(town=twice))) is not None, "stops: two missions of one id")
    other_case = world_rows() + [{"MissionID": "testproject_1_wolf_5", "ObjectiveID": "Test_Kill_Template"}]
    error = stops(lambda: tool.build(tables(world=other_case)))
    report(error is not None and "one id or one CRC-32" in error, f"stops: two missions of one id in two tables, another case ({error})")
    projects = [{"ProjectId": "TestProject", "ProjectType": "AlwaysAvailable"}, {"ProjectId": "testproject", "ProjectType": "Settlement"}]
    report(stops(lambda: tool.build(tables(projects=projects))) is not None, "stops: two projects of one id")
    for cell in ("!1,2", "x", "0", "70000"):
        rows = town_rows()
        rows[0]["AvailableTerritoryID"] = cell
        report(stops(lambda: tool.build(tables(town=rows))) is not None, f"stops: the territory cell {cell!r}")
    no_id = [{"Name": "X", "ObjectiveID": "Test_Kill_Template"}]
    report(stops(lambda: tool.build(tables(extra_missions=[("other.datasheet", FakeSheet("Other", no_id))]))) is not None,
           "stops: a MissionData table with no column MissionID")
    empty = tables()
    empty.tables[tool.MISSION_TYPE] = []
    report(stops(lambda: tool.build(empty)) is not None, "stops: no MissionData table")


def case_write(folder: Path) -> None:
    target = folder / "mission-data.json"
    target.write_text("the file of an earlier run\n", encoding="utf-8")
    try:
        rows = town_rows()
        rows[0]["AvailableTerritoryID"] = "x"
        tool.write(target, tool.build(tables(town=rows)))
    except Failure:
        pass
    report(target.read_text(encoding="utf-8") == "the file of an earlier run\n", "write: a run that stops leaves the earlier file")
    tool.write(target, tool.build(tables()))
    document = json.loads(target.read_text(encoding="utf-8"))
    report(document["format"] == tool.FORMAT and document["counts"]["missions"] == 4 and not list(folder.glob(".*.tmp")),
           "write: a run that completes replaces the file (no temporary file stays)")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    wanted = sys.argv[2:] or list(CASES)
    for name in wanted:
        if name not in CASES:
            print(f"FAIL unknown case {name}")
            return 1
        if name == "write":
            case_write(folder)
        else:
            globals()[f"case_{name}"]()
    print(f"{'FAIL' if FAILED else 'ok'}: {len(FAILED)} failed")
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())

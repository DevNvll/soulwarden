#!/usr/bin/env python3
"""Write the quest data of the backend (stage 1.8a, docs/systems/quests.md): one JSON file, quest-data.json.

The backend reads GAMEDATA_DIR/quest-data.json at its start. Without the file the backend starts and works with no quest: a new
character gets no start quest, and the other systems do not change (docs/systems/quests.md). The file is game data: it stays outside
the repository.

The sources (all data tables in the pak files of the game copy; research quest-engine.md Q1, Q2, Q7 and quest-engine-3.md Q1):

  1. The objectives: each table of the type Objectives (139 tables, 2,147 rows in the supported build). An objective has its root task
     (Task), its type, its destination (NpcDestinationId), its achievement, its condition (RequiredAchievementId), and the other columns.
  2. The tasks: each table of the type ObjectiveTasks (139 tables, 17,556 rows). The tool keeps each cell as its source text: a blank cell
     is not in the document, an explicit "0" is "0" (a blank TargetQty and a TargetQty of 0 are different rules: research Q1, base task
     constructor 0x3ABBF90).
  3. The task trees: the client builds the tree of an objective from its root task (builder 0x4466350): a child gets its index before its
     container, the index goes up by one after each child, a container gets the index after its children, and a container that the tree
     built before is not built again (its child position keeps its index, with no node: a hole). A leaf that comes again gets a new index.
     The type of a task is the value of the client enum (table 0x3AA6A80; the text of the cell with no case).
  4. The achievement table (AchievementData, javelindata_achievements: 17,005 rows): the bit (AchievementIndex) of each achievement id, its
     category, its event, and its deprecated mark. The non-persisted achievements (NPAData: 428 rows) are expressions of other bits: the
     backend evaluates them, it does not store them.
  5. The start objectives of the territories (TerritoryDefinition, column ObjectiveID; research quest-engine-3.md Q1: Windsward 06 gives
     06_getoffbeach).
  6. The backstories (BackstoryDefinition): the templates of the developers with ObjectiveUnlockOverride (the start objectives: a list
     separated by commas; a text after ":" is a note of the developer) and AchievementUnlockOverride (a list of achievement ids), and the
     archetype rows of the character creation (their achievement).
  7. The game events (GameEventData) that an objective or a task names, the whispers (WhisperData), and the schedules (ScheduleData).

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there, when a table has no column
that the tool reads, when two objective rows or two task rows have one id (case folded), when two ids of one catalog (objectives, tasks,
achievements, whispers) have one CRC-32, when the root task of an objective or a SubTask link names no task row, when a tree has a cycle,
when a task type is not a type of the client enum, when an index of a tree is above 255 (the task index is a u8 on the wire), when two
achievement rows have one bit, or when a bit is outside the range of the client loader (16 bits, 0x43B24A0). A reference that does not
resolve and that is not a task link is a counted diagnostic, not a stop: an achievement of an action with no row (research U10), a
TaskObjective child with no objective, a start objective of a territory or of a backstory with no objective row.
tools/gamedata/check_quests_tool.py is the check of these rules.

Output (one line of JSON):
  {"format": 1,
   "objectives": [{"id", "crc", "table", "root", "cells": {COLUMN: TEXT, ...}, "tree": [[INDEX, TASK, [CHILD_INDEX, ...]], ...],
                   "holes": [INDEX, ...]}, ...],
   "tasks": {TASK: {"id", "crc", "type", "code", "cells": {COLUMN: TEXT, ...}}, ...},     (TASK in lower case; only tasks of a tree)
   "achievements": [[ID, INDEX, CATEGORY, EVENT, DEPRECATED], ...],
   "npa": [[ID, CONDITIONAL, OBJECTIVE_FLOW, STATUS_EFFECT], ...],
   "territories": [{"territory", "objective", "table"}, ...],
   "backstories": [{"id", "name", "purpose", "playtest", "level", "faction", "objectives": [ID, ...], "objectiveText",
                    "achievements": [ID, ...], "archetype"}, ...],
   "events": {EVENT: {COLUMN: TEXT, ...}, ...},
   "whispers": [{"id", "crc", "cells": {COLUMN: TEXT, ...}}, ...],
   "schedules": [{"id", "cells": {COLUMN: TEXT, ...}}, ...],
   "diagnostics": {...}, "counts": {...}}
  "crc" is the CRC-32 of the id in lower case (the id on the wire: the objective of an active-objective element, the task of a task
  element; research quest-engine.md Q2). The nodes of a tree are in the order of their indexes.

Usage: quests.py [OUTPUT.json] [--objective ID]...
  --objective: print these objectives with their trees and tasks, and write no file.
Default output: <SOULWARDEN_GAMEDATA_DIR>/quest-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "quest-data.json"
FORMAT = 1
WANTED = (TABLES_FOLDER,)
OBJECTIVE_TYPE = "Objectives"
TASK_TYPE = "ObjectiveTasks"
ACHIEVEMENT_TYPE = "AchievementData"
NPA_TYPE = "NPAData"
TERRITORY_TYPE = "TerritoryDefinition"
BACKSTORY_TYPE = "BackstoryDefinition"
EVENT_TYPE = "GameEventData"
WHISPER_TYPE = "WhisperData"
SCHEDULE_TYPE = "ScheduleData"
KINDS = (OBJECTIVE_TYPE, TASK_TYPE, ACHIEVEMENT_TYPE, NPA_TYPE, TERRITORY_TYPE, BACKSTORY_TYPE, EVENT_TYPE, WHISPER_TYPE, SCHEDULE_TYPE)
# A table type that the game copy must have. The others can be absent (an empty list).
NEEDED = (OBJECTIVE_TYPE, TASK_TYPE, ACHIEVEMENT_TYPE, TERRITORY_TYPE, BACKSTORY_TYPE)
# The client reads SubTask1 to SubTask19 (research quest-engine.md Q1: the container fields of the loader 0x445EDE0).
SUBTASKS = 19
# The task index of the wire is a u8 (task element: instance:u64 index:u8 ...; reader 0x3ADDB50).
INDEX_LIMIT = 255
# The client loader reads the bit index of an achievement as 16 bits (0x43B24A0).
ACHIEVEMENT_INDEX_LIMIT = 0xFFFF
CONTAINER_TYPES = {"simpletaskcontainer", "consecutivetaskcontainer", "partialtaskcontainer"}
# The task types of the client enum (table 0x3AA6A80; research quest-engine.md Q1): the lower-case text of the column Type to its value.
# The supported build has rows of these 64 types; an enum value with no row (TaskKill and 24 more) has no quest of this build.
TASK_TYPES = {
    "simpletaskcontainer": 0, "consecutivetaskcontainer": 1, "partialtaskcontainer": 2, "tasktriggerarea": 3, "taskgototerritory": 5,
    "taskgotopoitag": 6, "taskgotopoitagonterritory": 7, "taskexitpoitag": 9, "taskkillcontribution": 13, "taskgameevent": 14,
    "tasktriggergameevent": 15, "taskconversationstate": 16, "taskconversationtopic": 17, "taskachievement": 18,
    "tasklockachievement": 19, "taskunlockachievement": 20, "tasktemporaryunlockachievement": 22, "taskinteract": 23,
    "taskhaveitems": 25, "taskhaveandreturnitems": 26, "taskhaveitemswithweaponfamily": 27, "taskhaveitemswithitemfamily": 28,
    "taskgatheritems": 29, "taskgatherandreturnitems": 30, "taskrequiregatheritems": 31, "taskgathercyclicstate": 32,
    "taskgiveitem": 33, "taskgiveandtakeitem": 34, "taskuseitems": 35, "taskremoveitem": 36, "taskcraftrecipe": 38,
    "taskcraftweaponfamily": 39, "taskrefinerecipe": 42, "tasksalvageitem": 44, "taskequipitem": 45, "taskhavelevel": 48,
    "taskcutscenecomplete": 49, "taskcinematicstart": 50, "taskcinematiccomplete": 51, "taskobjective": 52, "taskgamemode": 54,
    "taskcompletedungeon": 55, "taskquickcourse": 56, "taskperformsong": 59, "taskperformemote": 60, "taskhousescore": 61,
    "taskopenuiscreen": 62, "tasktimer": 64, "taskdie": 65, "patroltask": 66, "taskcontrolpoint": 68, "taskfishingequip": 69,
    "taskfishingcast": 70, "taskfishingreel": 71, "taskfishinghook": 72, "taskfishinghotspotany": 73, "taskfishingcatchanyfish": 75,
    "taskfishingcatchfish": 76, "taskfishingcatchweight": 77, "taskfishingcatchlength": 78, "taskchoosefaction": 81,
    "taskslayerscript": 82, "taskcraftmultiplerecipes": 86, "taskquickcoursecheckpoint": 88,
}
# The task types whose ItemName is an achievement id (or a conditional of ids for TaskAchievement): the diagnostic of research U10.
ACHIEVEMENT_ACTIONS = {"taskachievement", "tasklockachievement", "taskunlockachievement", "tasktemporaryunlockachievement"}
OBJECTIVE_COLUMNS = ("ObjectiveID", "Type", "Task")
TASK_COLUMNS = ("TaskID", "Type")
ACHIEVEMENT_COLUMNS = ("AchievementID", "AchievementIndex")
BACKSTORY_COLUMNS = ("BackstoryID", "ObjectiveUnlockOverride", "AchievementUnlockOverride")
# The columns of a game event that the quest engine reads now or in a later part (1.8d: the rewards).
EVENT_COLUMNS = ("UniversalExpAmount", "CurrencyReward", "ItemReward", "ItemRewardQty", "GearScoreRange", "CategoricalProgressionId",
                 "CategoricalProgressionReward", "TerritoryStanding", "FactionReputation", "FactionTokens", "AzothReward",
                 "AchievementId", "StatusEffectId", "EntitlementId", "ScheduleId")
WHISPER_COLUMNS = ("TriggerType", "RequiredAchievementId", "Duration", "GameEvent", "AchievementId", "Subtitle")
SCHEDULE_COLUMNS = ("StartDate", "EndDate", "StartWeekday", "EndWeekday", "UTC")


def crc(text: str) -> int:
    """@return The id on the wire of an id text: the CRC-32 of the text in lower case."""
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def need(sheet, path: str, columns: tuple[str, ...]) -> None:
    for column in columns:
        if column not in sheet.columns:
            raise Failure(f"{path}: the table {sheet.name} has no column {column}")


def cells(sheet, row: int, skip: set[str]) -> dict[str, str]:
    """@return The source text of each cell of a row that is not blank (a blank cell is not in the result)."""
    result = {}
    for column in sheet.columns:
        if column in skip:
            continue
        text = sheet.source(row, column).strip()
        if text:
            result[column] = text
    return result


def check_crcs(catalog: str, ids: list[str]) -> None:
    seen: dict[int, str] = {}
    for identifier in ids:
        value = crc(identifier)
        other = seen.get(value)
        if other is not None and other.lower() != identifier.lower():
            raise Failure(f"the {catalog} {other} and {identifier} have one CRC-32 ({value:08X})")
        seen[value] = identifier


def id_list(text: str) -> list[str]:
    """@return The ids of a list cell: separated by commas; a text after ":" is a note of the developer; no blank id."""
    result = []
    for part in text.split(","):
        identifier = part.split(":", 1)[0].strip()
        if identifier and identifier not in result:
            result.append(identifier)
    return result


class Tables:
    """The data tables of the tool: each table of a type that the tool reads, as (path, sheet)."""

    def __init__(self, tables: dict[str, list[tuple[str, object]]]) -> None:
        self.tables = tables

    @staticmethod
    def read(sources: Sources) -> "Tables":
        tables: dict[str, list[tuple[str, object]]] = {kind: [] for kind in KINDS}
        for path in sources.paths(TABLES_FOLDER, ".datasheet"):
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            if kind in tables:
                tables[kind].append((path, Sheet(data)))
        sources.check_complete()
        return Tables(tables)


def compile_tree(objective: str, root: str, tasks: dict[str, dict]) -> tuple[list, list[int]]:
    """@return The nodes of the tree of an objective ([INDEX, TASK, [CHILD, ...]] in index order) and its holes (builder 0x4466350)."""
    nodes: list[list] = []
    holes: list[int] = []
    built: dict[str, int] = {}
    counter = [0]

    def visit(identifier: str, stack: tuple[str, ...]) -> int | None:
        key = identifier.lower()
        row = tasks.get(key)
        if row is None:
            raise Failure(f"the objective {objective} names the task {identifier}, which no task table has")
        if key in stack:
            raise Failure(f"the tree of the objective {objective} has a cycle at the task {identifier}")
        children: list[int] = []
        if row["type"] in CONTAINER_TYPES:
            if key in built:
                holes.append(counter[0])
                return None  # A container that the tree built before: its child position keeps its index (a hole).
            for child in row["children"]:
                index = visit(child, stack + (key,))
                if index is not None:
                    children.append(index)
                counter[0] += 1
            built[key] = counter[0]
        index = counter[0]
        if index > INDEX_LIMIT:
            raise Failure(f"the tree of the objective {objective} has the index {index}: above the u8 of the wire")
        nodes.append([index, key, children])
        return index

    visit(root, ())
    nodes.sort(key=lambda node: node[0])
    return nodes, sorted(holes)


def read_tasks(tables: Tables) -> dict[str, dict]:
    tasks: dict[str, dict] = {}
    subtasks = {f"SubTask{index}" for index in range(1, SUBTASKS + 1)}
    for path, sheet in tables.tables[TASK_TYPE]:
        need(sheet, path, TASK_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "TaskID").strip()
            if not identifier:
                continue
            key = identifier.lower()
            if key in tasks:
                raise Failure(f"{path}: two task rows have the id {identifier}")
            kind = sheet.source(row, "Type").strip().lower()
            if kind not in TASK_TYPES:
                raise Failure(f"{path}: the task {identifier} has the type {kind!r}, which is not a task type of the client")
            children = []
            for index in range(1, SUBTASKS + 1):
                child = sheet.source(row, f"SubTask{index}").strip()
                if child:
                    children.append(child)
            tasks[key] = {"id": identifier, "type": kind, "code": TASK_TYPES[kind], "children": children, "table": path,
                          "cells": cells(sheet, row, {"TaskID", "Type"} | subtasks)}
    check_crcs("tasks", [task["id"] for task in tasks.values()])
    return tasks


def read_objectives(tables: Tables, tasks: dict[str, dict]) -> list[dict]:
    objectives: list[dict] = []
    seen: set[str] = set()
    for path, sheet in tables.tables[OBJECTIVE_TYPE]:
        need(sheet, path, OBJECTIVE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "ObjectiveID").strip()
            if not identifier:
                continue
            if identifier.lower() in seen:
                raise Failure(f"{path}: two objective rows have the id {identifier}")
            seen.add(identifier.lower())
            root = sheet.source(row, "Task").strip()
            if not root:
                raise Failure(f"{path}: the objective {identifier} has no root task")
            tree, holes = compile_tree(identifier, root, tasks)
            objectives.append({"id": identifier, "crc": crc(identifier), "table": path, "root": root.lower(),
                               "cells": cells(sheet, row, {"ObjectiveID", "Task"}), "tree": tree, "holes": holes})
    check_crcs("objectives", [objective["id"] for objective in objectives])
    objectives.sort(key=lambda objective: objective["id"].lower().encode())
    return objectives


def read_achievements(tables: Tables) -> list[list]:
    rows: list[list] = []
    ids: set[str] = set()
    bits: dict[int, str] = {}
    for path, sheet in tables.tables[ACHIEVEMENT_TYPE]:
        need(sheet, path, ACHIEVEMENT_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "AchievementID").strip()
            if not identifier:
                continue
            text = sheet.source(row, "AchievementIndex").strip()
            try:
                index = int(float(text))
            except ValueError as error:
                raise Failure(f"{path}: the achievement {identifier} has the bit {text!r}, which is not a number") from error
            if index != float(text) or index < 0 or index > ACHIEVEMENT_INDEX_LIMIT:
                raise Failure(f"{path}: the achievement {identifier} has the bit {text}: outside the range of the client (0 to 65535)")
            if identifier.lower() in ids:
                raise Failure(f"{path}: two achievement rows have the id {identifier}")
            if index in bits:
                raise Failure(f"{path}: the achievements {bits[index]} and {identifier} have one bit ({index})")
            ids.add(identifier.lower())
            bits[index] = identifier
            deprecated = sheet.source(row, "Is Deprecated").strip().lower() in ("1", "true")
            rows.append([identifier, index, sheet.source(row, "Category").strip(), sheet.source(row, "GameEventId").strip(), deprecated])
    check_crcs("achievements", [row[0] for row in rows])
    rows.sort(key=lambda row: row[1])
    return rows


def read_npa(tables: Tables) -> list[list]:
    rows = []
    for path, sheet in tables.tables[NPA_TYPE]:
        need(sheet, path, ("NPAId", "ReqAchievementConditional"))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "NPAId").strip()
            if identifier:
                rows.append([identifier, sheet.source(row, "ReqAchievementConditional").strip(),
                             sheet.source(row, "ObjectiveFlow").strip(), sheet.source(row, "ReqStatusEffectId").strip()])
    rows.sort(key=lambda row: row[0].lower().encode())
    return rows


def read_territories(tables: Tables) -> list[dict]:
    rows = []
    for path, sheet in tables.tables[TERRITORY_TYPE]:
        if "ObjectiveID" not in sheet.columns:
            continue
        need(sheet, path, ("TerritoryID",))
        for row in range(sheet.row_count):
            objective = sheet.source(row, "ObjectiveID").strip()
            if not objective:
                continue
            text = sheet.source(row, "TerritoryID").strip()
            try:
                territory = int(float(text))
            except ValueError as error:
                raise Failure(f"{path}: a territory row has the id {text!r}, which is not a number") from error
            rows.append({"territory": territory, "objective": objective, "table": path})
    rows.sort(key=lambda row: (row["territory"], row["objective"].lower()))
    return rows


def read_backstories(tables: Tables) -> list[dict]:
    rows = []
    for path, sheet in tables.tables[BACKSTORY_TYPE]:
        need(sheet, path, BACKSTORY_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "BackstoryID").strip()
            objective_text = sheet.source(row, "ObjectiveUnlockOverride").strip()
            achievement_text = sheet.source(row, "AchievementUnlockOverride").strip()
            if not identifier or not (objective_text or achievement_text):
                continue
            rows.append({"id": identifier, "name": sheet.source(row, "BackstoryName").strip(),
                         "purpose": sheet.source(row, "Purpose").strip(), "playtest": sheet.source(row, "PlaytestType").strip(),
                         "level": sheet.source(row, "LevelOverride").strip(), "faction": sheet.source(row, "FactionOverride").strip(),
                         "objectives": id_list(objective_text), "objectiveText": objective_text,
                         "achievements": id_list(achievement_text), "archetype": identifier.startswith("Archetype_"),
                         "table": path})
    ids = [row["id"].lower() for row in rows]
    if len(set(ids)) != len(ids):
        raise Failure("two backstory rows have one id")
    rows.sort(key=lambda row: row["id"].lower().encode())
    return rows


def read_events(tables: Tables, wanted: set[str]) -> dict[str, dict]:
    events: dict[str, dict] = {}
    for path, sheet in tables.tables[EVENT_TYPE]:
        need(sheet, path, ("EventID",))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "EventID").strip()
            if identifier.lower() not in wanted or identifier.lower() in events:
                continue
            entry = {"id": identifier}
            for column in EVENT_COLUMNS:
                text = sheet.source(row, column).strip()
                if text:
                    entry[column] = text
            events[identifier.lower()] = entry
    return dict(sorted(events.items()))


def read_named(tables: Tables, kind: str, key: str, columns: tuple[str, ...], with_crc: bool) -> list[dict]:
    rows = []
    for path, sheet in tables.tables[kind]:
        need(sheet, path, (key,))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, key).strip()
            if not identifier:
                continue
            entry: dict = {"id": identifier}
            if with_crc:
                entry["crc"] = crc(identifier)
            entry["cells"] = {column: sheet.source(row, column).strip() for column in columns if sheet.source(row, column).strip()}
            rows.append(entry)
    if with_crc:
        check_crcs(kind, [row["id"] for row in rows])
    rows.sort(key=lambda row: row["id"].lower().encode())
    return rows


def build(tables: Tables) -> dict:
    for kind in NEEDED:
        if not tables.tables[kind]:
            raise Failure(f"the game copy has no table of the type {kind}")
    catalog = read_tasks(tables)
    objectives = read_objectives(tables, catalog)
    achievements = read_achievements(tables)
    achievement_ids = {row[0].lower() for row in achievements}
    npa = read_npa(tables)
    npa_ids = {row[0].lower() for row in npa}
    objective_ids = {objective["id"].lower() for objective in objectives}
    # Only the tasks of a tree go into the document (1,993 catalog rows are in no tree: research K2).
    used: dict[str, dict] = {}
    for objective in objectives:
        for _, key, _ in objective["tree"]:
            task = catalog[key]
            used[key] = {"id": task["id"], "crc": crc(task["id"]), "type": task["type"], "code": task["code"], "cells": task["cells"]}
    missing_achievements: set[str] = set()
    missing_children: set[str] = set()
    for task in used.values():
        item = task["cells"].get("ItemName", "")
        if task["type"] in ("tasklockachievement", "taskunlockachievement", "tasktemporaryunlockachievement"):
            if item and item.lower() not in achievement_ids:
                missing_achievements.add(item)
        elif task["type"] == "taskachievement":
            for word in item.replace("(", " ").replace(")", " ").replace("!", " ").replace("&&", " ").replace("||", " ").split():
                if word.lower() not in achievement_ids and word.lower() not in npa_ids:
                    missing_achievements.add(word)
        elif task["type"] == "taskobjective" and item and item.lower() not in objective_ids:
            missing_children.add(item)
    territories = read_territories(tables)
    backstories = read_backstories(tables)
    missing_starts = sorted({row["objective"] for row in territories if row["objective"].lower() not in objective_ids}
                            | {identifier for row in backstories for identifier in row["objectives"]
                               if identifier.lower() not in objective_ids})
    wanted_events: set[str] = set()
    for objective in objectives:
        for column in ("SuccessGameEventId", "FailureGameEventId"):
            if objective["cells"].get(column):
                wanted_events.add(objective["cells"][column].lower())
    for task in used.values():
        if task["type"] == "tasktriggergameevent" and task["cells"].get("KillEnemyType"):
            wanted_events.add(task["cells"]["KillEnemyType"].lower())
    whispers = read_named(tables, WHISPER_TYPE, "WhisperId", WHISPER_COLUMNS, True)
    for whisper in whispers:
        if whisper["cells"].get("GameEvent"):
            wanted_events.add(whisper["cells"]["GameEvent"].lower())
    events = read_events(tables, wanted_events)
    schedules = read_named(tables, SCHEDULE_TYPE, "ScheduleId", SCHEDULE_COLUMNS, False)
    diagnostics = {"actionAchievementsWithNoRow": sorted(missing_achievements, key=str.lower),
                   "taskObjectivesWithNoRow": sorted(missing_children, key=str.lower),
                   "startObjectivesWithNoRow": missing_starts,
                   "eventsWithNoRow": sorted(wanted_events - set(events))}
    counts = {"objectives": len(objectives), "taskRows": len(catalog), "treeTasks": len(used),
              "nodes": sum(len(objective["tree"]) for objective in objectives),
              "holes": sum(len(objective["holes"]) for objective in objectives),
              "largestIndex": max((objective["tree"][-1][0] for objective in objectives if objective["tree"]), default=0),
              "achievements": len(achievements), "npa": len(npa), "territories": len(territories), "backstories": len(backstories),
              "events": len(events), "whispers": len(whispers), "schedules": len(schedules)}
    return {"format": FORMAT, "objectives": objectives, "tasks": dict(sorted(used.items())), "achievements": achievements, "npa": npa,
            "territories": territories, "backstories": backstories, "events": events, "whispers": whispers, "schedules": schedules,
            "diagnostics": diagnostics, "counts": counts}


def write(target: Path, document: dict) -> None:
    """Writes the file through a temporary file in the same folder: a stop leaves the earlier file."""
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the quest data of the backend (quest-data.json).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--objective", action="append", help="print an objective with its tree and write no file; more than one is possible")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Tables.read(Sources(game / "assets", WANTED)))
        if arguments.objective:
            wanted = {name.lower() for name in arguments.objective}
            for objective in document["objectives"]:
                if objective["id"].lower() in wanted:
                    print(json.dumps(objective, indent=1))
                    for index, key, children in objective["tree"]:
                        task = document["tasks"][key]
                        print(f"  {index:3d} {task['id']} ({task['type']}, {task['code']}) children {children} {task['cells']}")
            return 0
        write(target, document)
    except Failure as error:
        print(f"quests: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"quests: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    diagnostics = document["diagnostics"]
    print(f"{target}: {counts['objectives']} objectives, {counts['nodes']} task nodes ({counts['treeTasks']} task rows of a tree, "
          f"{counts['holes']} hole(s), largest index {counts['largestIndex']}), {counts['achievements']} achievements, {counts['npa']} "
          f"non-persisted achievements, {counts['territories']} territory start objective(s), {counts['backstories']} backstories, "
          f"{counts['events']} events, {counts['whispers']} whispers, {counts['schedules']} schedules")
    print(f"  references with no row: {len(diagnostics['actionAchievementsWithNoRow'])} achievement(s) of a task, "
          f"{len(diagnostics['taskObjectivesWithNoRow'])} child objective(s), {len(diagnostics['startObjectivesWithNoRow'])} start "
          f"objective(s), {len(diagnostics['eventsWithNoRow'])} event(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

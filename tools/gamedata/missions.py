#!/usr/bin/env python3
"""Write the mission data of the backend (stage QG6a, docs/systems/mission-boards.md): one JSON file, mission-data.json.

The backend reads GAMEDATA_DIR/mission-data.json at its start. Without the file the backend starts and works with no mission: the open
of a board is refused, and each accept is refused (docs/systems/mission-boards.md). The file is game data: it stays outside the repository.

The sources (the data tables in the pak files of the game copy; research mission-boards.md sections 1.2, 3.1, 4, 5):

  1. The missions: each table of the type MissionData. The client loader 0x39C4870 reads all of them, and the lookup 0x3A342E0 finds a row
     by the CRC-32 of its lower-case MissionID (the missionId of an ObjectiveCreationParams element). The supported build has three:
     javelindata_territoryprogression_missions (13,248 rows: the town project missions, "town"), javelindata_owg_mission (3,046 rows: the
     open world missions of the faction boards, "world"), and javelindata_affinitymissions (no row, "affinity"). The tool keeps each cell as
     its source text (a blank cell is not in the document, an explicit "0" is "0"), and the row number of the table (the order of the
     candidates of the client: research mission-boards.md 3.2).
  2. The territory rule of a mission (AvailableTerritoryID in the town table, AvailableTerritoryId in the others; the client test 0x39D3EC0
     with the list at row +0xD8 and the exclusion flag at row +0x146): a blank cell is no rule; "N,M,..." a list; "!N" the territories that
     are not N. The member "territories" has the ids and "exclude".
  3. The projects (TerritoryProgressionData, javelindata_territoryprogression: 50 rows): the project of a town mission (TerritoryProjectId),
     its type (ProjectType: Settlement, Fortress, Lifestyle, AlwaysAvailable) and its ProgressionLevel.
  4. The game events (GameEventData) that a mission names (SuccessGameEventIdOverride, FailureGameEventIdOverride): the rewards of a hand-in.
     The quest data has only the events of the objectives and of the tasks (tools/gamedata/quests.py), and a mission event is often only in
     a mission row (research mission-boards.md 2.3). The form of quest-data.json: the source text of each column of quests.EVENT_COLUMNS.
  5. The mission weights (MissionWeightsData) and the flexible boards (FlexibleMissionBoardData): the slots of the faction boards and of the
     affinity boards (stage QG6b). Only their cells. Stage QG6b: the reward modifiers of the missions (RewardModifierData rows whose
     Modifiers id has "Mission": FactionMissionBonus, the daily bonus of the faction boards; member "rewardModifiers").
  6. The goal buckets of the town missions in the order of the client (member "townBuckets"; static read
     evidence/quests/qg6a/implement/static/offers/OFFERS.md section 6). The client builds a map of the rows of the town table: the key is
     the missionId, the hash of 0x14A0FA0 (the 128-bit product with 0xDE5FB9D2630458E9, the sum of its two halves), 16,384 buckets for
     13,248 rows (one bucket at the start, a power of two at each growth, max load 1); its copy 0x39E3B90 walks the buckets from low to
     high and puts each group at the front, so the stored order is a stable sort of the rows by descending bucket (row order in a bucket).
     The builder 0x445B390 appends each mission of a row with an objective to the vector of its MissionGoalType (Gather 1, Kill 4,
     Explore 5). The vectors are in a flat hash table (the same hash; H1 = hash >> 7; capacity 1, then 3), and the provider copy 0x3A184C0
     inserts them again by physical slot: the selection 0x3A3654E walks the occupied slots from low to high.

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there, when a table has no column that
the tool reads, when two mission rows (in all tables) or two project rows have one id (case folded) or one CRC-32, or when a territory cell
is not a list of numbers 1 to 65535 with an optional "!" before one number. A reference that does not resolve is a counted diagnostic, not a
stop: an ObjectiveID with no objective row, a TerritoryProjectId with no project row, an event with no row. The column of the town table
PauseAndClearLootTagOnTaskCompleteOverride is not the column that the client loader asks for (PauseAndClearLootOnTaskCompleteOverride,
0x39C5933): the tool keeps the source name, and counts the rows that have it (research mission-boards.md 4.2).
tools/gamedata/check_missions_tool.py is the check of these rules.

Output (one line of JSON):
  {"format": 1,
   "missions": [{"id", "crc", "table": "town"|"world"|"affinity", "row", "objective", "objectiveCrc",
                 "territories": {"ids": [N, ...], "exclude": BOOL}, "cells": {COLUMN: TEXT, ...}}, ...],
   "projects": [{"id", "crc", "type", "level", "cells": {COLUMN: TEXT, ...}}, ...],
   "events": {EVENT: {"id", COLUMN: TEXT, ...}, ...},
   "weights": [{"table", "cells"}, ...], "flexibleBoards": [{"cells"}, ...], "rewardModifiers": [{"id", "cells"}, ...],
   "townBuckets": [{"key", "goal", "missions": [CRC, ...]}, ...],
   "townExcluded": [CRC, ...]   (code review of stage QG6b, finding QG6BR-R1: the enabled town rows whose objective has no row),
   "diagnostics": {...}, "counts": {...}}
  The missions are in the order of the tables (the path order of the game copy) and of their rows. "crc" is the CRC-32 of the lower-case
  id (the missionId of the wire); "objectiveCrc" the CRC-32 of the lower-case ObjectiveID (the objectiveId of an active element).

Usage: missions.py [OUTPUT.json] [--mission ID]...
  --mission: print these missions with their project and events, and write no file.
Default output: <SOULWARDEN_GAMEDATA_DIR>/mission-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from quests import EVENT_COLUMNS, OBJECTIVE_TYPE, cells, crc, need  # noqa: E402

OUTPUT_NAME = "mission-data.json"
FORMAT = 1
WANTED = (TABLES_FOLDER,)
MISSION_TYPE = "MissionData"
PROJECT_TYPE = "TerritoryProgressionData"
EVENT_TYPE = "GameEventData"
WEIGHT_TYPE = "MissionWeightsData"
BOARD_TYPE = "FlexibleMissionBoardData"
MODIFIER_TYPE = "RewardModifierData"
KINDS = (MISSION_TYPE, PROJECT_TYPE, EVENT_TYPE, WEIGHT_TYPE, BOARD_TYPE, OBJECTIVE_TYPE, MODIFIER_TYPE)
NEEDED = (MISSION_TYPE, PROJECT_TYPE)
# The table of a mission by the end of its path (the supported build; another MissionData table is "other").
TABLE_NAMES = {"javelindata_territoryprogression_missions.datasheet": "town", "javelindata_owg_mission.datasheet": "world",
               "javelindata_affinitymissions.datasheet": "affinity"}
MISSION_COLUMNS = ("MissionID", "ObjectiveID")
PROJECT_COLUMNS = ("ProjectId", "ProjectType")
TERRITORY_COLUMNS = ("AvailableTerritoryID", "AvailableTerritoryId")
EVENT_REFERENCES = ("SuccessGameEventIdOverride", "FailureGameEventIdOverride")
# The goal keys of the town buckets (the enum of MissionGoalType: the getters 0x2A1A70, 0x2F2B90, 0x2F2BA0 return 1, 4, 5).
GOAL_KEYS = {"Gather": 1, "Kill": 4, "Explore": 5}
MASK64 = (1 << 64) - 1
# The column of the town table whose name is not the name of the client loader (0x39C5933 asks for PauseAndClearLootOnTaskCompleteOverride).
ALIAS_COLUMN = "PauseAndClearLootTagOnTaskCompleteOverride"


def territories(text: str, what: str) -> dict:
    """@return The territory rule of a cell: a blank cell no rule; "N,M" a list; "!N" each territory but N (client 0x39D3EC0)."""
    value = text.strip()
    if not value:
        return {"ids": [], "exclude": False}
    exclude = value.startswith("!")
    parts = [part.strip() for part in (value[1:] if exclude else value).split(",")]
    if exclude and len(parts) != 1:
        raise Failure(f"{what}: the territory cell {text!r} has '!' and more than one id")
    ids = []
    for part in parts:
        if not part.isdigit() or not 1 <= int(part) <= 0xFFFF:
            raise Failure(f"{what}: the territory cell {text!r} is not a list of numbers 1 to 65535")
        if int(part) not in ids:
            ids.append(int(part))
    return {"ids": ids, "exclude": exclude}


def number(text: str, default: int = 0) -> int:
    value = text.strip()
    return int(float(value)) if value else default


class Tables:
    """The data tables of the tool: each table of a type that the tool reads, as (path, sheet), in the path order of the game copy."""

    def __init__(self, tables: dict[str, list[tuple[str, object]]]) -> None:
        self.tables = tables

    @staticmethod
    def read(sources: Sources) -> "Tables":
        tables: dict[str, list[tuple[str, object]]] = {kind: [] for kind in KINDS}
        for path in sorted(sources.paths(TABLES_FOLDER, ".datasheet")):
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            if kind in tables:
                tables[kind].append((path, Sheet(data)))
        sources.check_complete()
        return Tables(tables)


def read_missions(tables: Tables) -> tuple[list[dict], int]:
    missions: list[dict] = []
    seen: dict[int, str] = {}
    aliases = 0
    for path, sheet in tables.tables[MISSION_TYPE]:
        need(sheet, path, MISSION_COLUMNS)
        table = TABLE_NAMES.get(path.rsplit("/", 1)[-1], "other")
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "MissionID").strip()
            if not identifier:
                continue
            value = crc(identifier)
            if value in seen:
                raise Failure(f"the missions {seen[value]} and {identifier} have one id or one CRC-32 ({value:08X})")
            seen[value] = identifier
            row_cells = cells(sheet, row, {"MissionID"})
            column = next((name for name in TERRITORY_COLUMNS if name in sheet.columns), None)
            rule = territories(sheet.source(row, column) if column else "", f"{path} row {row} ({identifier})")
            aliases += 1 if row_cells.get(ALIAS_COLUMN) else 0
            objective = row_cells.get("ObjectiveID", "")
            missions.append({"id": identifier, "crc": value, "table": table, "row": row, "objective": objective,
                             "objectiveCrc": crc(objective) if objective else 0, "territories": rule, "cells": row_cells})
    return missions, aliases


def read_projects(tables: Tables) -> list[dict]:
    projects: list[dict] = []
    seen: dict[int, str] = {}
    for path, sheet in tables.tables[PROJECT_TYPE]:
        need(sheet, path, PROJECT_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "ProjectId").strip()
            if not identifier:
                continue
            value = crc(identifier)
            if value in seen:
                raise Failure(f"the projects {seen[value]} and {identifier} have one id or one CRC-32 ({value:08X})")
            seen[value] = identifier
            row_cells = cells(sheet, row, {"ProjectId"})
            projects.append({"id": identifier, "crc": value, "type": row_cells.get("ProjectType", ""),
                             "level": number(row_cells.get("ProgressionLevel", "")), "cells": row_cells})
    return projects


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
                text = sheet.source(row, column).strip() if column in sheet.columns else ""
                if text:
                    entry[column] = text
            events[identifier.lower()] = entry
    return dict(sorted(events.items()))


def hash_key(value: int) -> int:
    """@return The integer hash of the client (0x14A0FA0): the sum of the two 64-bit halves of value * 0xDE5FB9D2630458E9."""
    product = (value & MASK64) * 0xDE5FB9D2630458E9
    return ((product & MASK64) + (product >> 64)) & MASK64


def flat_order(keys: list[int]) -> list[int]:
    """@return The keys in the physical slot order of the flat hash table of the client (0x445B9E4, insert 0x39E6F80, growth 0x39E8200):
    capacity 1, then 3 (2 * capacity + 1); the start slot H1 = hash >> 7 masked by the capacity; the probe visits the capacity + 1 control
    bytes (the last is the sentinel) and takes the first free real slot; a growth inserts the old slots again in their slot order."""
    slots: list[int | None] = []

    def insert(key: int, table: list[int | None]) -> None:
        mask = len(table)
        start = (hash_key(key) >> 7) & mask
        for offset in range(mask + 1):
            slot = (start + offset) & mask
            if slot < len(table) and table[slot] is None:
                table[slot] = key
                return
        raise Failure("the goal table of the town buckets has no free slot")

    for key in keys:
        if key in slots:
            continue
        if sum(value is not None for value in slots) == len(slots):
            capacity = 2 * len(slots) + 1
            if capacity > 3:
                raise Failure("the town table has more than three goal keys: the order of the client for more keys is not read")
            grown: list[int | None] = [None] * capacity
            for old in slots:
                if old is not None:
                    insert(old, grown)
            slots = grown
        insert(key, slots)
    return [key for key in slots if key is not None]


def town_excluded(missions: list[dict], objective_ids: set[str]) -> list[int]:
    """@return The CRCs of the enabled town missions whose objective has no row of the quest data: town_buckets leaves them out (code review of
    stage QG6b, finding QG6BR-R1: the loader refuses a file whose buckets miss another enabled town row)."""
    return sorted(mission["crc"] for mission in missions if mission["table"] == "town"
                  and mission["cells"].get("Enabled", "1") in ("1", "1.0", "true", "True") and mission["objective"].lower() not in objective_ids)


def town_buckets(missions: list[dict], objective_ids: set[str]) -> list[dict]:
    """@return The goal buckets of the town missions in the order of the client (the docstring, item 6)."""
    rows = [mission for mission in missions if mission["table"] == "town"]
    if not rows:
        return []
    count = 1 << (len(rows) - 1).bit_length()
    ordered = sorted(rows, key=lambda mission: -(hash_key(mission["crc"]) & (count - 1)))
    grouped: dict[int, list[int]] = {}
    for mission in ordered:
        if mission["cells"].get("Enabled", "1") not in ("1", "1.0", "true", "True") or mission["objective"].lower() not in objective_ids:
            continue
        goal = mission["cells"].get("MissionGoalType", "")
        if goal not in GOAL_KEYS:
            raise Failure(f"the town mission {mission['id']} has the MissionGoalType {goal!r}: no key of the client is known")
        grouped.setdefault(GOAL_KEYS[goal], []).append(mission["crc"])
    names = {key: goal for goal, key in GOAL_KEYS.items()}
    # The global table (insertion in the stored order), then the copy of the provider (insertion by physical slot).
    order = flat_order(flat_order(list(grouped)))
    return [{"key": key, "goal": names[key], "missions": grouped[key]} for key in order]


def build(tables: Tables) -> dict:
    for kind in NEEDED:
        if not tables.tables[kind]:
            raise Failure(f"the game copy has no table of the type {kind}")
    missions, aliases = read_missions(tables)
    projects = read_projects(tables)
    project_ids = {project["id"].lower() for project in projects}
    objective_ids: set[str] = set()
    for path, sheet in tables.tables[OBJECTIVE_TYPE]:
        need(sheet, path, ("ObjectiveID",))
        objective_ids.update(sheet.source(row, "ObjectiveID").strip().lower() for row in range(sheet.row_count))
    buckets = town_buckets(missions, objective_ids)
    wanted = {mission["cells"][column].lower() for mission in missions for column in EVENT_REFERENCES if mission["cells"].get(column)}
    events = read_events(tables, wanted)
    weights = [{"table": path.rsplit("/", 1)[-1], "cells": cells(sheet, row, set())}
               for path, sheet in tables.tables[WEIGHT_TYPE] for row in range(sheet.row_count)]
    boards = [{"cells": cells(sheet, row, set())} for _, sheet in tables.tables[BOARD_TYPE] for row in range(sheet.row_count)]
    # Stage QG6b: the reward modifiers of the missions (RewardModifierData rows whose id has "Mission": the daily bonus FactionMissionBonus
    # of the faction boards, AffinityMissionBonus of the flexible boards, and the rows Mission, Mission-, Mission+ of the quest flags).
    modifiers = [{"id": sheet.source(row, "Modifiers").strip(), "cells": cells(sheet, row, {"Modifiers"})}
                 for _, sheet in tables.tables[MODIFIER_TYPE] if "Modifiers" in sheet.columns for row in range(sheet.row_count)
                 if "mission" in sheet.source(row, "Modifiers").strip().lower()]
    no_objective = sorted({mission["objective"] or f"(blank: {mission['id']})" for mission in missions
                           if not mission["objective"] or mission["objective"].lower() not in objective_ids}, key=str.lower)
    no_project = sorted({mission["cells"]["TerritoryProjectId"] for mission in missions
                         if mission["cells"].get("TerritoryProjectId") and mission["cells"]["TerritoryProjectId"].lower() not in project_ids},
                        key=str.lower)
    diagnostics = {"objectivesWithNoRow": no_objective, "projectsWithNoRow": no_project,
                   "eventsWithNoRow": sorted(wanted - set(events)), "aliasColumnRows": aliases}
    by_table: dict[str, int] = {}
    for mission in missions:
        by_table[mission["table"]] = by_table.get(mission["table"], 0) + 1
    counts = {"missions": len(missions), "byTable": dict(sorted(by_table.items())), "projects": len(projects), "events": len(events),
              "weights": len(weights), "flexibleBoards": len(boards), "rewardModifiers": len(modifiers),
              "killOverrides": sum(1 for mission in missions if mission["cells"].get("TaskKillContributionOverride")),
              "townBuckets": [[bucket["goal"], len(bucket["missions"])] for bucket in buckets]}
    return {"format": FORMAT, "missions": missions, "projects": projects, "events": events, "weights": weights, "flexibleBoards": boards,
            "rewardModifiers": modifiers, "townBuckets": buckets, "townExcluded": town_excluded(missions, objective_ids),
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
    parser = argparse.ArgumentParser(description="Write the mission data of the backend (mission-data.json).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--mission", action="append", help="print a mission and write no file; more than one is possible")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Tables.read(Sources(game / "assets", WANTED)))
        if arguments.mission:
            wanted = {name.lower() for name in arguments.mission}
            projects = {project["id"].lower(): project for project in document["projects"]}
            for mission in document["missions"]:
                if mission["id"].lower() in wanted:
                    print(json.dumps(mission, indent=1))
                    project = projects.get(mission["cells"].get("TerritoryProjectId", "").lower())
                    if project is not None:
                        print("  project:", json.dumps(project))
                    for column in EVENT_REFERENCES:
                        event = document["events"].get(mission["cells"].get(column, "").lower())
                        if event is not None:
                            print(f"  {column}:", json.dumps(event))
            return 0
        write(target, document)
    except Failure as error:
        print(f"missions: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"missions: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    diagnostics = document["diagnostics"]
    print(f"{target}: {counts['missions']} missions ({', '.join(f'{name} {count}' for name, count in counts['byTable'].items())}; "
          f"{counts['killOverrides']} with a kill override), {counts['projects']} projects, {counts['events']} events, {counts['weights']} "
          f"weight rows, {counts['flexibleBoards']} flexible boards")
    print("  town buckets (the order of the client): " + ", ".join(f"{goal} {count}" for goal, count in counts["townBuckets"]))
    print(f"  references with no row: {len(diagnostics['objectivesWithNoRow'])} objective(s), {len(diagnostics['projectsWithNoRow'])} "
          f"project(s), {len(diagnostics['eventsWithNoRow'])} event(s); {diagnostics['aliasColumnRows']} row(s) with the column "
          f"{ALIAS_COLUMN} (not read by the client loader)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

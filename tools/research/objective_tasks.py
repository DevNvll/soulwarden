#!/usr/bin/env python3
"""Print the task tree of an objective with the task indexes that the client uses.

The client builds the task objects of an objective from the `Objectives` and `ObjectiveTasks`
data tables (factory at image RVA 0x4464D20, tree builder 0x4466350). Each task gets an index
from one counter: the counter starts at 0, a container builds its sub-tasks first (in the
order SubTask1, SubTask2, ...), the counter increases by one after each sub-task, and the
container takes the counter value after its last sub-task. Thus the indexes are in
post-order and the root task has the highest index. A container that is in the tree a second
time is not built again but it uses one index.

A task state on the wire is `objectiveInstanceId:u64 taskIndex:u8 taskCrc:u32 progress:u32
state:u8`. Ids are the CRC-32 of the lower-case text.

Input: the extracted data tables (pakx.py '\\.datasheet$' --max 5000).

Usage:
  objective_tasks.py OBJECTIVE_ID              task tree with index, CRC, and type
  objective_tasks.py OBJECTIVE_ID --fragment INSTANCE_ID_HEX [ACTIVE_INDEX...]
        also print an example state fragment for the objectives component: one active
        objective, tracked, with task states (sub-task indexes in ACTIVE_INDEX are Active;
        see the --complete option)
  objective_tasks.py OBJECTIVE_ID --fragment INSTANCE_ID_HEX --complete 0,1,2 --active 9,14
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import datasheet  # noqa: E402

TABLES = Path.home() / "Games/soulwarden/work/assets/extract/sharedassets/springboardentitites/datatables"
CONTAINERS = {"SimpleTaskContainer", "ConsecutiveTaskContainer", "PartialTaskContainer"}
STATE_NEW, STATE_ACTIVE, STATE_COMPLETE = 0, 1, 2


def crc(text: str) -> int:
    return zlib.crc32(text.lower().encode()) & 0xFFFFFFFF


def load():
    objectives, tasks = {}, {}
    for path in sorted(TABLES.rglob("*.datasheet")):
        try:
            sheet = datasheet.Sheet(path)
        except (struct.error, ValueError):
            continue
        if sheet.type == "Objectives":
            for record in sheet.records():
                objectives.setdefault(record["ObjectiveID"].lower(), (record, path))
        elif sheet.type == "ObjectiveTasks":
            for record in sheet.records():
                tasks.setdefault(record["TaskID"].lower(), record)
    return objectives, tasks


def sub_tasks(record) -> list[str]:
    names = sorted((key for key in record if key.startswith("SubTask")), key=lambda key: int(key[7:]))
    return [str(record[key]) for key in names if record[key] not in ("", 0, False)]


def build(tasks, root: str):
    """@return [(index or None, depth, task id, record or None)] in the order of the table."""
    rows = []
    counter = 0
    built: set[str] = set()

    def visit(task_id: str, depth: int):
        nonlocal counter
        record = tasks.get(task_id.lower())
        row = [None, depth, task_id, record]
        rows.append(row)
        if record is None:
            return  # No data: the client makes no task. The parent still counts one index.
        if record["Type"] in CONTAINERS:
            if task_id.lower() in built:
                return
            for child in sub_tasks(record):
                visit(child, depth + 1)
                counter += 1
            built.add(task_id.lower())
        row[0] = counter

    visit(root, 0)
    return rows


def size_code(value: int) -> bytes:
    """The `size` prefix code of the wire format (client writer at image RVA 0x877970)."""
    if value < 0x80:
        return bytes([value])
    if value < 0x4000:
        return bytes([0x80 | (value & 0x3F), value >> 6])
    raise ValueError("value is too large for this example")


def fragment(objective_id: str, instance: int, rows, active: set[int], complete: set[int]) -> bytes:
    stamp = b"\x01\x01"  # Sequence number is present, value 1.
    tracked = b"\x00" + stamp + size_code(1) + struct.pack(">Q", instance)
    record = struct.pack(">IQI", crc(objective_id), instance, 0)  # objective, instance, mission 0
    record += bytes(16)  # destinationOverride
    record += struct.pack(">QH", 0, 0)  # acceptTime, originTerritoryId
    record += bytes([1, 0, 0, 0])  # available, isCommunityGoal, isPoiObjective, isDynamicPoiObjective
    record += struct.pack(">Q", 0) + b"\x00" + size_code(0)  # parentObjectiveId, isResetObjective, ingredients
    objectives = b"\x00" + stamp + size_code(1) + record
    states = b""
    count = 0
    for index, _, task_id, data in rows:
        if index is None or data is None:
            continue
        state = STATE_COMPLETE if index in complete else STATE_ACTIVE if index in active else STATE_NEW
        target = data.get("TargetQty") or 0
        progress = int(target) if state == STATE_COMPLETE and isinstance(target, (int, float)) else 0
        states += struct.pack(">QBIIB", instance, index, crc(task_id), progress, state)
        count += 1
    task_states = b"\x00" + stamp + size_code(count) + states
    # Group mask: group 1. Field mask: 1 trackedObjectives, 3 activeObjectives, 4 taskStates.
    return bytes([0x02, 0x1A]) + tracked + objectives + task_states


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("objective")
    parser.add_argument("--fragment", metavar="INSTANCE_ID_HEX")
    parser.add_argument("--active", default="", help="comma-separated task indexes in state Active")
    parser.add_argument("--complete", default="", help="comma-separated task indexes in state Complete")
    arguments = parser.parse_args()
    objectives, tasks = load()
    found = objectives.get(arguments.objective.lower())
    if found is None:
        sys.exit(f"objective {arguments.objective} is not in the data tables")
    record, path = found
    print(f"objective {record['ObjectiveID']}  crc 0x{crc(record['ObjectiveID']):08X}  type {record['Type']}")
    print(f"  table {path.relative_to(TABLES)}")
    for key in ("Title", "Description", "RequiredAchievementId", "AchievementId", "NpcDestinationId",
                "ObjectiveReward", "SuccessGameEventId", "PushObjectiveOnReqMet", "ExclusiveTerritory"):
        if record.get(key) not in (None, "", 0, False):
            print(f"  {key} = {record[key]}")
    rows = build(tasks, str(record["Task"]))
    print("  index  crc         type                       task")
    for index, depth, task_id, data in rows:
        shown = "  --" if index is None else f"{index:4d}"
        kind = data["Type"] if data else "(no data or built before)"
        detail = ""
        if data:
            parts = []
            for key in ("TargetQty", "ItemName", "UIScreenTag", "TP_TargetTag", "KillEnemyType", "OverrideLocation",
                        "TP_DescriptionTag", "IsHidden", "StayActive"):
                if data.get(key) not in (None, "", 0, False):
                    parts.append(f"{key}={data[key]}")
            detail = "  " + " ".join(parts)
        print(f"  {shown}   0x{crc(task_id):08X}  {kind:26s} {'  ' * depth}{task_id}{detail}")
    if arguments.fragment:
        instance = int(arguments.fragment, 16)
        active = {int(value) for value in arguments.active.split(",") if value}
        complete = {int(value) for value in arguments.complete.split(",") if value}
        data = fragment(str(record["ObjectiveID"]), instance, rows, active, complete)
        print(f"state fragment for slot 22, type 3857 ({len(data)} bytes):")
        for offset in range(0, len(data), 32):
            print("  " + data[offset:offset + 32].hex())


if __name__ == "__main__":
    main()

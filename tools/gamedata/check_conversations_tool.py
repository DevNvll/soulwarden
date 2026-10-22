#!/usr/bin/env python3
"""Check of the conversation data tool (conversations.py, stage 2.5c) with made-up tables (no file of the game).

The check makes data tables of the three types in memory (NPCData, ConversationStateData, ConversationTopicData), gives them to the
build function of the tool, and compares each result. The test program of the backend runs it (case NC2; the case has the backend side
of the file).

  order      the states of an NPC row and the topics and services of a state are in the order of the numbers of their columns
             (ConversationStateId10 after ConversationStateId2), with no blank cell
  cells      a requirement keeps its source text; a blank cell is not in the document; IsInitialState "True" and "1" are true
  first      the first row of an id wins (any case); a later row is a counted diagnostic
  missing    a state that an NPC row names with no row and a topic that a state names with no row are counted diagnostics
  crc        the CRC-32 of the lower-case id; two topic ids with one CRC-32 stop the tool
  tables     an empty table with no id column is not read; a table with rows and no id column stops the tool; a type with no table stops
             the tool

Usage: check_conversations_tool.py
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import conversations as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_combat_tool import sheet  # noqa: E402
from item_catalog import Sheet  # noqa: E402

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


def table(name: str, kind: str, rows: list[dict]) -> tuple[str, Sheet]:
    return (f"{tool.TABLES_FOLDER}{name}.datasheet", Sheet(sheet(name, kind, rows)))


def npc_row(identifier: str, *states: str, **cells) -> dict:
    row = {"NPCId": identifier, "GenericName": f"@{identifier}", "Title": "", "ShopId": "", "MissionBoardId": ""}
    for index in range(1, 11):
        row[f"ConversationStateId{index}"] = states[index - 1] if index <= len(states) else ""
    row.update(cells)
    return row


def state_row(identifier: str, topics: tuple[str, ...] = (), services: tuple[str, ...] = (), **cells) -> dict:
    row = {"ConversationStateId": identifier, "ConversationType": "Linear_Objective", "DefaultDialogue": f"@{identifier}",
           "IsInitialState": "", "RequiredAchievement": "", "RequiredLevel": "", "RequiredActiveObjectiveId": "", "RequiredActiveTaskId": ""}
    for index in range(1, 11):
        row[f"TopicId{index}"] = topics[index - 1] if index <= len(topics) else ""
        row[f"ConversationService{index}"] = services[index - 1] if index <= len(services) else ""
    row.update(cells)
    return row


def topic_row(identifier: str, **cells) -> dict:
    row = {"ConversationTopicId": identifier, "PlayerPrompt": f"@{identifier}_Prompt", "NPCResponse": f"@{identifier}_Response",
           "RequiredLevel": "", "RequiredAchievement": "", "AchievementUnlock": ""}
    row.update(cells)
    return row


def base_tables() -> dict:
    # Ten states: the tenth comes after the second (the order of the numbers of the columns, not of their texts).
    states = [f"Test_State_{index:02d}" for index in range(1, 11)]
    npcs = [npc_row("Test_Npc", *states), npc_row("TEST_NPC", "Test_State_01"), npc_row("Test_Other", "Test_State_01", "Missing_State")]
    state_rows = [state_row(states[0], topics=("Test_Topic_B", "", "Test_Topic_A"), services=("Inn", "", "Open_Shop"), IsInitialState="True",
                            RequiredLevel="12", RequiredActiveObjectiveId="Test_Objective", RequiredActiveTaskId="Test_Task")]
    state_rows += [state_row(identifier, IsInitialState="1" if identifier.endswith("02") else "false") for identifier in states[1:]]
    state_rows.append(state_row("Test_State_Missing_Topic", topics=("No_Such_Topic",)))
    return {tool.NPC_TYPE: [table("javelindata_test_npcs", tool.NPC_TYPE, npcs)],
            tool.STATE_TYPE: [table("javelindata_test_states", tool.STATE_TYPE, state_rows)],
            tool.TOPIC_TYPE: [table("javelindata_test_topics", tool.TOPIC_TYPE,
                                    [topic_row("Test_Topic_A", RequiredAchievement="Test_Flag && !Test_Block"), topic_row("Test_Topic_B")])]}


def main() -> int:
    document = tool.build(base_tables())
    npcs = {entry["id"]: entry for entry in document["npcs"]}
    states = {entry["id"]: entry for entry in document["states"]}
    topics = {entry["id"]: entry for entry in document["topics"]}
    first = states.get("Test_State_01", {})
    report(npcs.get("Test_Npc", {}).get("states") == [f"Test_State_{index:02d}" for index in range(1, 11)]
           and first.get("topics") == ["Test_Topic_B", "Test_Topic_A"] and first.get("services") == ["Inn", "Open_Shop"],
           "order: the states of an NPC row (ConversationStateId10 after ConversationStateId2), the topics and the services of a state, "
           "with no blank cell")
    report(first.get("requires") == {"RequiredLevel": "12", "RequiredActiveObjectiveId": "Test_Objective", "RequiredActiveTaskId": "Test_Task"}
           and first.get("initial") is True and states.get("Test_State_02", {}).get("initial") is True
           and states.get("Test_State_03", {}).get("initial") is False and states.get("Test_State_03", {}).get("requires") == {}
           and topics.get("Test_Topic_A", {}).get("requires") == {"RequiredAchievement": "Test_Flag && !Test_Block"},
           "cells: a requirement keeps its source text; a blank cell is not in the document; IsInitialState True and 1 are true")
    report(len(document["npcs"]) == 2 and document["diagnostics"]["duplicateNpcs"] == ["TEST_NPC (javelindata_test_npcs.datasheet)"],
           "first: the first row of an NPC id wins (any case); the second row is a diagnostic")
    report(document["diagnostics"]["statesWithNoRow"] == ["Test_Other: Missing_State"]
           and document["diagnostics"]["topicsWithNoRow"] == ["Test_State_Missing_Topic: No_Such_Topic"],
           "missing: a state with no row and a topic with no row are counted diagnostics")
    crc_ok = topics.get("Test_Topic_A", {}).get("crc") == zlib.crc32(b"test_topic_a") and npcs["Test_Npc"]["crc"] == zlib.crc32(b"test_npc")
    original = tool.crc_of
    tool.crc_of = lambda identifier: 7
    collision = stops(lambda: tool.build(base_tables()))
    tool.crc_of = original
    report(crc_ok and collision is not None and "one CRC-32" in collision,
           "crc: the CRC-32 of the lower-case id; two ids with one CRC-32 stop the tool")
    empty = base_tables()
    # The build has a table of this form (questdata/conversationstatedata_npc_04667): no column, no row.
    empty_sheet = Sheet(sheet("Empty", tool.STATE_TYPE, [{"Other": "x"}]))
    empty_sheet.row_count = 0
    empty[tool.STATE_TYPE].append((f"{tool.TABLES_FOLDER}questdata/empty.datasheet", empty_sheet))
    broken = base_tables()
    broken[tool.TOPIC_TYPE].append(table("javelindata_broken_topics", tool.TOPIC_TYPE, [{"Prompt": "@x"}]))
    no_type = base_tables()
    no_type[tool.TOPIC_TYPE] = []
    report(stops(lambda: tool.build(empty)) is None and stops(lambda: tool.build(broken)) is not None
           and stops(lambda: tool.read_tables(type("Sources", (), {"paths": lambda self, a, b: [], "read": lambda self, p: None})())) is not None,
           "tables: an empty table with no id column is not read; a table with rows and no id column stops the tool; a type with no table "
           "stops the tool")
    return 0 if not FAILED else 1


if __name__ == "__main__":
    sys.exit(main())

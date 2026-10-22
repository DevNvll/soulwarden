#!/usr/bin/env python3
"""Write the conversation data of the persons (stage 2.5c, docs/systems/persons.md): one JSON file, conversation-data.json.

The backend reads GAMEDATA_DIR/conversation-data.json at its start. Without the file the backend starts and works: a person of the
interaction data keeps its conversation (the reply 3743 and the close as in stage 2.5a), the backend selects no conversation state,
accepts no topic (3158, 5397), and gives no service grant (docs/systems/persons.md). The file is game data: it stays outside the
repository.

The client builds the conversation of a person from its own tables (research npc-conversations.md Q2): it scans the conversation states
of the NPC row in reverse and takes the first state whose requirements pass (client 0x37AC710, 0x37AB440), then it shows the dialogue,
the topics whose requirements pass (0x37AB7A0), the services (0x37ACF10), and Leave. The backend makes the same selection for each
character, so that a topic request and a service of a later stage have a source. The sources (data tables of the pak files):

  1. NPCData (1,483 rows in the supported build): NPCId, GenericName, Title, ConversationStateId1..N (in order), ShopId, MissionBoardId.
  2. ConversationStateData (3,429 rows): ConversationStateId, ConversationType, DefaultDialogue, IsInitialState, the requirements
     (RequiredAchievement, RequiredLevel, RequiredFaction, RequiredFactionCooldown, RequiredProgression, RequiredActiveObjectiveId,
     RequiredActiveTaskId, RequiredCompletedObjective), TopicId1..N, ConversationService1..N, LinearObjectiveId1..N.
  3. ConversationTopicData (556 rows): ConversationTopicId, PlayerPrompt, NPCResponse, the requirements (RequiredLevel, RequiredFaction,
     RequiredFactionCooldown, RequiredAchievement), AchievementUnlock, ProvidedTopics.

Each cell is its source text; a blank cell is not in the document. The first row of an id wins (case folded); the other rows are counted
in "diagnostics" (research npc-conversations.md Unknowns: the precedence of duplicate rows is not known). A state that an NPC names with
no row, and a topic that a state names with no row, are counted diagnostics: the backend treats them as not there. The tool stops with
status 1 and one message, and writes no file, when a source table type is not in the game copy, when a table has no id column, or when
two NPC ids or two topic ids of the catalog have one CRC-32 (the id on the wire: client hash 0x12F4730 of the lower-case id; live check
L1 of stage 2.5c: the NPC CRC of the request of Tekla Petrowski is CFD93AC5). tools/gamedata/check_conversations_tool.py is the check of
these rules.

Output (one line of JSON):
  {"format": 1,
   "npcs": [{"id", "crc", "name", "title", "states": [STATE, ...], "shop", "missionBoard", "table"}, ...],
   "states": [{"id", "type", "dialogue", "initial", "requires": {COLUMN: TEXT, ...}, "topics": [TOPIC, ...], "services": [NAME, ...],
               "objectives": [OBJECTIVE, ...], "table"}, ...],
   "topics": [{"id", "crc", "prompt", "response", "requires": {COLUMN: TEXT, ...}, "achievementUnlock", "providedTopics", "table"}, ...],
   "counts": {...}, "diagnostics": {...}}

Usage: conversations.py [OUTPUT.json] [--npc ID]...
  --npc: print these NPC rows with their states and topics, and write no file.
Default output: <SOULWARDEN_GAMEDATA_DIR>/conversation-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "conversation-data.json"
FORMAT = 1
WANTED = (TABLES_FOLDER,)
NPC_TYPE = "NPCData"
STATE_TYPE = "ConversationStateData"
TOPIC_TYPE = "ConversationTopicData"
STATE_REQUIREMENTS = ("RequiredAchievement", "RequiredLevel", "RequiredFaction", "RequiredFactionCooldown", "RequiredProgression",
                      "RequiredActiveObjectiveId", "RequiredActiveTaskId", "RequiredCompletedObjective")
TOPIC_REQUIREMENTS = ("RequiredLevel", "RequiredFaction", "RequiredFactionCooldown", "RequiredAchievement")


def crc_of(identifier: str) -> int:
    """@return The id on the wire: the CRC-32 (IEEE 802.3) of the lower-case text (client hash 0x12F4730)."""
    return zlib.crc32(identifier.lower().encode("utf-8")) & 0xFFFFFFFF


def numbered(sheet: Sheet, row: int, prefix: str) -> list[str]:
    """@return The values of the columns PREFIX1, PREFIX2, ... of a row in the order of their numbers, with no blank cell."""
    columns = sorted((int(match[1]), name) for name in sheet.columns if (match := re.fullmatch(re.escape(prefix) + r"(\d+)", name)))
    return [value for _, name in columns if (value := sheet.source(row, name).strip())]


def read_tables(sources: Sources) -> dict[str, list[tuple[str, Sheet]]]:
    """@return The tables of the three types, each with its path."""
    tables: dict[str, list[tuple[str, Sheet]]] = {NPC_TYPE: [], STATE_TYPE: [], TOPIC_TYPE: []}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind in tables:
            try:
                tables[kind].append((path, Sheet(data)))
            except (struct.error, ValueError, IndexError) as error:
                raise Failure(f"{path}: the table did not parse: {error}") from error
    for kind, found in tables.items():
        if not found:
            raise Failure(f"the game copy has no table of the type {kind}")
    return tables


def build(tables: dict[str, list[tuple[str, Sheet]]]) -> dict:
    diagnostics: dict[str, list[str]] = {"duplicateNpcs": [], "duplicateStates": [], "duplicateTopics": [], "statesWithNoRow": [],
                                         "topicsWithNoRow": [], "npcsWithNoState": []}
    npcs: dict[str, dict] = {}
    states: dict[str, dict] = {}
    topics: dict[str, dict] = {}

    def rows(kind: str, id_column: str):
        for path, sheet in sorted(tables[kind], key=lambda entry: entry[0]):
            if id_column not in sheet.columns and sheet.row_count == 0:
                continue  # An empty table of the build (questdata/conversationstatedata_npc_04667: no column, no row).
            if id_column not in sheet.columns:
                raise Failure(f"{path}: the table of the type {kind} has no column {id_column}")
            table = path.rsplit("/", 1)[-1]
            for row in range(sheet.row_count):
                identifier = sheet.source(row, id_column).strip()
                if identifier:
                    yield table, sheet, row, identifier

    def requires(sheet: Sheet, row: int, columns: tuple[str, ...]) -> dict[str, str]:
        return {column: value for column in columns if (value := sheet.source(row, column).strip())}

    for table, sheet, row, identifier in rows(NPC_TYPE, "NPCId"):
        key = identifier.lower()
        if key in npcs:
            diagnostics["duplicateNpcs"].append(f"{identifier} ({table})")
            continue
        entry = {"id": identifier, "crc": crc_of(identifier), "name": sheet.source(row, "GenericName").strip(),
                 "title": sheet.source(row, "Title").strip(), "states": numbered(sheet, row, "ConversationStateId"),
                 "shop": sheet.source(row, "ShopId").strip(), "missionBoard": sheet.source(row, "MissionBoardId").strip(), "table": table}
        npcs[key] = entry
    for table, sheet, row, identifier in rows(STATE_TYPE, "ConversationStateId"):
        key = identifier.lower()
        if key in states:
            diagnostics["duplicateStates"].append(f"{identifier} ({table})")
            continue
        initial = sheet.source(row, "IsInitialState").strip().lower()
        states[key] = {"id": identifier, "type": sheet.source(row, "ConversationType").strip(),
                       "dialogue": sheet.source(row, "DefaultDialogue").strip(), "initial": initial in ("true", "1"),
                       "requires": requires(sheet, row, STATE_REQUIREMENTS), "topics": numbered(sheet, row, "TopicId"),
                       "services": numbered(sheet, row, "ConversationService"), "objectives": numbered(sheet, row, "LinearObjectiveId"),
                       "table": table}
    for table, sheet, row, identifier in rows(TOPIC_TYPE, "ConversationTopicId"):
        key = identifier.lower()
        if key in topics:
            diagnostics["duplicateTopics"].append(f"{identifier} ({table})")
            continue
        topics[key] = {"id": identifier, "crc": crc_of(identifier), "prompt": sheet.source(row, "PlayerPrompt").strip(),
                       "response": sheet.source(row, "NPCResponse").strip(), "requires": requires(sheet, row, TOPIC_REQUIREMENTS),
                       "achievementUnlock": sheet.source(row, "AchievementUnlock").strip(),
                       "providedTopics": sheet.source(row, "ProvidedTopics").strip(), "table": table}
    for catalog, label in ((npcs, "NPC"), (topics, "topic")):
        seen: dict[int, str] = {}
        for entry in catalog.values():
            if entry["crc"] in seen:
                raise Failure(f"the {label} ids {seen[entry['crc']]} and {entry['id']} have one CRC-32 ({entry['crc']:08x})")
            seen[entry["crc"]] = entry["id"]
    for entry in npcs.values():
        missing = [state for state in entry["states"] if state.lower() not in states]
        diagnostics["statesWithNoRow"] += [f"{entry['id']}: {state}" for state in missing]
        if not entry["states"]:
            diagnostics["npcsWithNoState"].append(entry["id"])
    for entry in states.values():
        diagnostics["topicsWithNoRow"] += [f"{entry['id']}: {topic}" for topic in entry["topics"] if topic.lower() not in topics]
    services = sorted({service for entry in states.values() for service in entry["services"]})
    counts = {"npcs": len(npcs), "states": len(states), "topics": len(topics), "services": services,
              **{name: len(values) for name, values in diagnostics.items()}}
    return {"format": FORMAT, "npcs": sorted(npcs.values(), key=lambda entry: entry["id"].lower()),
            "states": sorted(states.values(), key=lambda entry: entry["id"].lower()),
            "topics": sorted(topics.values(), key=lambda entry: entry["id"].lower()), "counts": counts,
            "diagnostics": {name: sorted(values) for name, values in diagnostics.items()}}


def write(target: Path, document: dict) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    # The backend must not read a file that is not complete: write, then rename.
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
    temporary.replace(target)


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the conversation data of the persons (conversation-data.json).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--npc", action="append", help="print an NPC row with its states and topics and write no file; more than one")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(read_tables(Sources(game / "assets", WANTED)))
        if arguments.npc:
            states = {entry["id"].lower(): entry for entry in document["states"]}
            topics = {entry["id"].lower(): entry for entry in document["topics"]}
            for wanted in arguments.npc:
                for npc in document["npcs"]:
                    if npc["id"].lower() != wanted.lower():
                        continue
                    print(json.dumps(npc))
                    for state in npc["states"]:
                        print("  state", json.dumps(states.get(state.lower(), {"id": state, "missing": True})))
                        for topic in states.get(state.lower(), {}).get("topics", []):
                            print("    topic", json.dumps(topics.get(topic.lower(), {"id": topic, "missing": True})))
            return 0
        write(target, document)
    except Failure as error:
        print(f"conversations: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"conversations: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['npcs']} NPC rows, {counts['states']} conversation states, {counts['topics']} topics; services: "
          f"{', '.join(counts['services'])}")
    print(f"  diagnostics: {counts['duplicateNpcs']} duplicate NPC rows, {counts['duplicateStates']} duplicate states, "
          f"{counts['duplicateTopics']} duplicate topics, {counts['statesWithNoRow']} states with no row, {counts['topicsWithNoRow']} "
          f"topics with no row, {counts['npcsWithNoState']} NPC rows with no state")
    return 0


if __name__ == "__main__":
    sys.exit(main())

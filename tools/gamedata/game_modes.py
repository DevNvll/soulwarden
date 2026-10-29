#!/usr/bin/env python3
"""Write the game modes of the backend (stage DG1, docs/systems/instances.md): one JSON file, game-modes.json.

The backend reads GAMEDATA_DIR/game-modes.json at its start. Without the file the backend starts and works: the development command
`instance enter` is refused, and nothing else changes (docs/systems/instances.md). The file is game data: it stays outside the repository.

The sources (the data tables in the pak files of the game copy; research dungeons.md section 3, expeditions.md "Instance and mode catalog"):

  1. The modes (GameModeData, javelindata_gamemodes: 53 rows): the id, the display name key and its English text, the levels
     (RecommendedLevel, RequiredLevel), the group sizes (TeamCapacity, MinGroupSize), the kind flags (IsDungeon, IsSoloTrial, IsRaidTrial),
     and the timeout of an empty instance (TimerThresholdDefault).
  2. The maps (GameModeMapData, javelindata_gamemodemaps: 62 rows): the map id, its mode, the terrain project (CoatlicueName), and the start
     places of its teams (TeamTeleportData: "x,y,z,heading", a "+" between two teams). A mode can have more than one map (Outpost Rush, the
     arenas): a command names the map id.

The level text and the terrain text of the level information (message 1635, protocol notes 4.1): the terrain project of the map in lower case
with "/" ("coatlicue/nw_dungeon_windsward_00"), and the last part of it ("nw_dungeon_windsward_00"). The live probe of research dungeons.md
section 2 sent these lower-case texts, and the client loaded the level [live]. The main world map rows have the terrain project
coatlicue/newworld_vitaeeterna: the level of the main world.

A map row with no terrain project or with no start place is not a place of an instance (the rows Dungeon, Mutation, Duel, TestMode1,
TestMode2): the file does not have it, and the counts name it.

The tool stops with status 1 and one message, and writes no file, when a source is not there, when a table has no column that the tool reads,
when two modes or two maps have one id (case folded), when a map names a mode that the mode table does not have, or when a start place of a
map with a terrain project is not four finite numbers with a height from -99 to 999. tools/gamedata/check_game_modes_tool.py is the check
of these rules.

Output (one line of JSON):
  {"format": 1, "counts": {"modes": N, "maps": N, "mapsWithNoPlace": N},
   "maps": [{"id", "mode", "row", "modeRow", "level", "terrain", "starts": [[x, y, z, heading], ...], "nameKey", "name",
             "recommendedLevel", "requiredLevel", "teamCapacity", "minGroupSize", "dungeon", "soloTrial", "raidTrial",
             "timeoutSeconds"}, ...]}
  The maps are in the order of the map table.

Usage: game_modes.py [OUTPUT.json]
       game_modes.py --list          print the maps, and write no file
Default output: <SOULWARDEN_GAMEDATA_DIR>/game-modes.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet  # noqa: E402
from travel_places import TEXTS_FOLDER, read_texts  # noqa: E402

OUTPUT_NAME = "game-modes.json"
FORMAT = 1
MODES_TABLE = TABLES_FOLDER + "javelindata_gamemodes.datasheet"
MAPS_TABLE = TABLES_FOLDER + "javelindata_gamemodemaps.datasheet"
MODE_COLUMNS = ("GameModeId", "DisplayName", "RecommendedLevel", "RequiredLevel", "TeamCapacity", "MinGroupSize", "IsDungeon",
                "IsSoloTrial", "IsRaidTrial", "TimerThresholdDefault")
MAP_COLUMNS = ("GameModeMapId", "GameModeId", "CoatlicueName", "TeamTeleportData")
# Limits of a place: the height range of a world position on the wire, with the margin of the backend (World::travel).
LOWEST_HEIGHT, HIGHEST_HEIGHT = -99.0, 999.0


def read_sheet(sources: Sources, path: str, columns: tuple[str, ...]) -> Sheet:
    data = sources.read(path)
    if data is None:
        raise Failure(f"the game copy has no {path}")
    try:
        sheet = Sheet(data)
    except (struct.error, ValueError, IndexError) as error:
        raise Failure(f"{path}: the table did not parse: {error}") from error
    missing = [column for column in columns if column not in sheet.columns]
    if missing:
        raise Failure(f"{path} has no column {', '.join(missing)}")
    return sheet


def whole(value) -> int:
    """@return A cell number as a whole number (a blank number cell is 0)."""
    return int(value) if isinstance(value, (int, float)) and math.isfinite(value) else 0


def read_starts(text: str, what: str) -> list[list[float]]:
    """@return The start places of the teams of a map: "x,y,z,heading", a "+" between two teams."""
    starts = []
    for team in text.split("+"):
        parts = [part.strip() for part in team.split(",")]
        try:
            values = [float(part) for part in parts]
        except ValueError as error:
            raise Failure(f"{what}: the start place {team!r} is not four numbers") from error
        if len(values) != 4 or not all(math.isfinite(value) for value in values):
            raise Failure(f"{what}: the start place {team!r} is not four finite numbers")
        if not LOWEST_HEIGHT <= values[2] <= HIGHEST_HEIGHT:
            raise Failure(f"{what}: the start place {team!r} has a height outside -99 to 999")
        starts.append([round(value, 3) for value in values])
    return starts


def build_document(sources: Sources) -> dict:
    modes_sheet = read_sheet(sources, MODES_TABLE, MODE_COLUMNS)
    maps_sheet = read_sheet(sources, MAPS_TABLE, MAP_COLUMNS)
    sources.check_complete()
    modes: dict[str, dict] = {}
    for row in range(modes_sheet.row_count):
        identifier = str(modes_sheet.cell(row, "GameModeId"))
        if not identifier:
            raise Failure(f"{MODES_TABLE}: row {row} has no GameModeId")
        if identifier.lower() in modes:
            raise Failure(f"{MODES_TABLE}: two modes have the id {identifier}")
        modes[identifier.lower()] = {"id": identifier, "row": row, "nameKey": str(modes_sheet.cell(row, "DisplayName")),
                                     "recommendedLevel": whole(modes_sheet.cell(row, "RecommendedLevel")),
                                     "requiredLevel": whole(modes_sheet.cell(row, "RequiredLevel")),
                                     "teamCapacity": whole(modes_sheet.cell(row, "TeamCapacity")),
                                     "minGroupSize": whole(modes_sheet.cell(row, "MinGroupSize")),
                                     "dungeon": bool(modes_sheet.cell(row, "IsDungeon")),
                                     "soloTrial": bool(modes_sheet.cell(row, "IsSoloTrial")),
                                     "raidTrial": bool(modes_sheet.cell(row, "IsRaidTrial")),
                                     "timeoutSeconds": whole(modes_sheet.cell(row, "TimerThresholdDefault"))}
    texts = read_texts(sources, {mode["nameKey"].lstrip("@").lower() for mode in modes.values() if mode["nameKey"]})
    sources.check_complete()
    maps = []
    seen: set[str] = set()
    no_place = 0
    for row in range(maps_sheet.row_count):
        identifier = str(maps_sheet.cell(row, "GameModeMapId"))
        what = f"{MAPS_TABLE}: row {row} ({identifier})"
        if not identifier:
            raise Failure(f"{MAPS_TABLE}: row {row} has no GameModeMapId")
        if identifier.lower() in seen:
            raise Failure(f"{MAPS_TABLE}: two maps have the id {identifier}")
        seen.add(identifier.lower())
        mode = modes.get(str(maps_sheet.cell(row, "GameModeId")).lower())
        if mode is None:
            raise Failure(f"{what} names the mode {maps_sheet.cell(row, 'GameModeId')!r}, which the mode table does not have")
        terrain = str(maps_sheet.cell(row, "CoatlicueName")).replace("\\", "/").strip().lower()
        start_text = str(maps_sheet.cell(row, "TeamTeleportData")).strip()
        if not terrain or not start_text:
            no_place += 1
            continue
        level = terrain.rsplit("/", 1)[-1]
        key = mode["nameKey"].lstrip("@").lower()
        maps.append({"id": identifier, "mode": mode["id"], "row": row, "modeRow": mode["row"], "level": level, "terrain": terrain,
                     "starts": read_starts(start_text, what), "nameKey": mode["nameKey"], "name": texts.get(key, ""),
                     **{name: mode[name] for name in ("recommendedLevel", "requiredLevel", "teamCapacity", "minGroupSize", "dungeon",
                                                      "soloTrial", "raidTrial", "timeoutSeconds")}})
    if not maps:
        raise Failure("the map table has no map with a terrain project and a start place")
    return {"format": FORMAT, "counts": {"modes": len(modes), "maps": len(maps), "mapsWithNoPlace": no_place}, "maps": maps}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the game modes of the backend as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--list", action="store_true", help="print the maps, and write no file")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        sources = Sources(game / "assets", (TABLES_FOLDER, TEXTS_FOLDER))
        document = build_document(sources)
        if not arguments.list:
            target.parent.mkdir(parents=True, exist_ok=True)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target.with_name(target.name + ".tmp")
            temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"game modes: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"game modes: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.list:
        for entry in document["maps"]:
            x, y, z, heading = entry["starts"][0]
            kind = "dungeon" if entry["dungeon"] else "solo" if entry["soloTrial"] else ""
            print(f"{entry['id']:<30} {entry['level']:<34} {x:9.2f} {y:9.2f} {z:7.2f} {heading:6.1f}  {kind:<8} {entry['name']}")
    counts = document["counts"]
    print(f"{'the game copy' if arguments.list else target}: {counts['maps']} maps of {counts['modes']} modes "
          f"({counts['mapsWithNoPlace']} map rows with no terrain project or no start place are not in the file)")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

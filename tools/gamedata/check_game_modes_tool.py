#!/usr/bin/env python3
"""Check of the game modes tool with made-up tables (no file of the game).

The game modes tool (game_modes.py) must write no file when its tables have a fault: two modes or two maps with one id, a map that names a
mode that the mode table does not have, a start place that is not four finite numbers, or a start place with a height outside -99 to 999.
A map row with no terrain project or with no start place is not a fault: the file does not have it, and its count names it. A correct file
has the level text and the terrain text in lower case with "/", the start place of each team, and the cells of the mode.

This check makes the two tables and a text file in memory, gives them to the entry point of the tool with a source provider that reads no
game copy, and compares the status and the output file of each case. The test program of the backend runs it (case DG1-TOOL).

Usage: check_game_modes_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_modes as tool  # noqa: E402
from check_combat_tool import sheet  # noqa: E402


def mode_row(identifier: str, key: str, dungeon: bool, solo: bool, capacity: int, minimum: int) -> dict:
    return {"GameModeId": identifier, "DisplayName": key, "RecommendedLevel": 25, "RequiredLevel": 0, "TeamCapacity": capacity,
            "MinGroupSize": minimum, "IsDungeon": dungeon, "IsSoloTrial": solo, "IsRaidTrial": False, "TimerThresholdDefault": 600,
            "IsMutable": dungeon and capacity > 5, "EntryAchievementConditional": "", "RequiredConsumableItemClasses": ""}


def map_row(identifier: str, mode: str, terrain: str, start: str) -> dict:
    return {"GameModeMapId": identifier, "GameModeId": mode, "CoatlicueName": terrain, "TeamTeleportData": start}


class MadeUpSources:
    """The source provider of the tool, with made-up files. `mode` selects the fault of the tables."""

    mode = "valid"

    def __init__(self, assets, wanted=None) -> None:
        self.used = {"made-up files": 3}
        modes = [mode_row("MadeUpDungeon", "@made_up_dungeon", True, False, 5, 3), mode_row("MadeUpArena", "@made_up_arena", False, False, 3, 0),
                 mode_row("Dungeon", "", False, False, 5, 0)]
        maps = [map_row("MadeUpDungeon", "MadeUpDungeon", "coatlicue\\NW_Made_Up_00", "645.0,663.0,153.0,0"),
                map_row("MadeUpArena_01", "MadeUpArena", "coatlicue\\NW_Made_Up_Arena", "939,935,226.5,180+939,873,227,0"),
                map_row("MadeUpArena_02", "MadeUpArena", "coatlicue\\NewWorld_VitaeEterna", "1241,10746,96,0"),
                map_row("Dungeon", "Dungeon", "", "")]
        if self.mode == "two-modes":
            modes.append(mode_row("madeupdungeon", "", False, False, 0, 0))
        if self.mode == "two-maps":
            maps.append(map_row("MADEUPARENA_01", "MadeUpArena", "coatlicue\\NW_Made_Up_Arena", "1,2,3,0"))
        if self.mode == "unknown-mode":
            maps.append(map_row("NoMode", "NotAMode", "coatlicue\\NW_Made_Up_00", "1,2,3,0"))
        if self.mode == "three-numbers":
            maps[0]["TeamTeleportData"] = "645.0,663.0,153.0"
        if self.mode == "not-a-number":
            maps[1]["TeamTeleportData"] = "939,935,226.5,180+939,x,227,0"
        if self.mode == "not-finite":
            maps[0]["TeamTeleportData"] = "645.0,nan,153.0,0"
        if self.mode == "too-high":
            maps[2]["TeamTeleportData"] = "1241,10746,1200,0"
        self.files = {
            tool.MODES_TABLE: sheet("GameModes", "GameModeData", modes),
            tool.MAPS_TABLE: sheet("GameModeMap", "GameModeMapData", maps),
            tool.TEXTS_FOLDER + "made-up.loc.xml": (b'<strings><string key="made_up_dungeon">Made-up Dungeon</string>'
                                                     b'<string key="made_up_arena">Made-up Arena</string></strings>'),
        }

    def paths(self, prefix, suffix):
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def read(self, path):
        return self.files.get(path)

    def check_complete(self) -> None:
        pass


# Each case: the fault of the tables, and True when the tool must write its file.
CASES = [
    ("valid", True, "complete tables (a dungeon, an arena with two maps and two teams, a map row with no terrain and no start)"),
    ("two-modes", False, "two modes with one id (case folded)"),
    ("two-maps", False, "two maps with one id (case folded)"),
    ("unknown-mode", False, "a map that names a mode that the mode table does not have"),
    ("three-numbers", False, "a start place with three numbers"),
    ("not-a-number", False, "a start place of the second team with a text that is not a number"),
    ("not-finite", False, "a start place with a number that is not finite"),
    ("too-high", False, "a start place with the height 1200"),
]


def valid_document(document: dict) -> bool:
    maps = {entry["id"]: entry for entry in document["maps"]}
    dungeon = maps.get("MadeUpDungeon", {})
    arena = maps.get("MadeUpArena_01", {})
    main = maps.get("MadeUpArena_02", {})
    return (document["counts"] == {"modes": 3, "maps": 3, "mapsWithNoPlace": 1} and "Dungeon" not in maps
            and dungeon.get("level") == "nw_made_up_00" and dungeon.get("terrain") == "coatlicue/nw_made_up_00"
            and dungeon.get("starts") == [[645.0, 663.0, 153.0, 0.0]] and dungeon.get("name") == "Made-up Dungeon"
            and dungeon.get("dungeon") is True and dungeon.get("teamCapacity") == 5 and dungeon.get("minGroupSize") == 3
            and dungeon.get("timeoutSeconds") == 600 and dungeon.get("modeRow") == 0 and dungeon.get("row") == 0
            and arena.get("starts") == [[939.0, 935.0, 226.5, 180.0], [939.0, 873.0, 227.0, 0.0]] and arena.get("mode") == "MadeUpArena"
            and main.get("level") == "newworld_vitaeeterna" and main.get("terrain") == "coatlicue/newworld_vitaeeterna")


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-game-modes-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    failures = 0
    for mode, writes, what in CASES:
        MadeUpSources.mode = mode
        target = work / f"game-modes-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["game_modes.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        exists = target.exists()
        document = json.loads(target.read_text(encoding="utf-8")) if exists else {}
        correct = (status == 0 and exists) if writes else (status == 1 and not exists)
        if correct and mode == "valid":
            correct = valid_document(document)
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        message = message.replace(tool.TABLES_FOLDER, "")
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, "
              + (f"a file with {len(document['maps'])} maps" if exists else "no file") + (f" ({message})" if not writes and message else ""))
        failures += not correct
        target.unlink(missing_ok=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

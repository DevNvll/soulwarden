#!/usr/bin/env python3
"""Write the level data set of an own level (stage DG2, docs/systems/instances.md, "The level data").

An own level is a level of a map row that is not the level of the main world (newworld_vitaeeterna): Amrine (nw_dungeon_windsward_00),
the other expeditions, the solo story levels, the arenas. The level and its terrain folder come from the map row of the game copy
(javelindata_gamemodemaps: CoatlicueName; game_modes.level_terrain), not from a constant of a tool. Each level has one region (r_+00_+00)
with no region.chunks: its geometry is in its capitals.

The set is the folder OUTPUT/levels/LEVEL:
  level.json               the manifest: {"format": 1, "tool": N, "level", "terrain", "maps": [map ids], "parts": {...}, "spawnEntries":
                           {...}, "seconds"}; the backend refuses a set with no manifest or with a different format
  creatures.json           the spawn points of the layers of the level with their kinds and slices (creature_data.py --level); not there
                           for a level whose layers have no usual spawn point (its creatures are members of the encounters of its mode
                           root: "spawnEntries" counts the entries of each flag)
  navigation/, heights/    the navigation data and the terrain heights of its region (navigation_data.py --level: the collision of the
                           capitals, the terrain in the scale of its terrain.json)
  interaction-data.json    the objects of its capitals (interactions.py --level)
  chest-data.json          the chests of its layers (chests.py --level); not there for a level with no chest
The tool makes the set in OUTPUT/levels/LEVEL.part and then puts it in the place of the old set: the backend never reads a part of a set.
A tool that stops (status 1) stops this tool: the old set stays. The work files of the navigation are in
SOULWARDEN_NAVIGATION_WORK/levels/LEVEL.

Usage: level_data.py LEVEL [OUTPUT_DIR] [--jobs N]     LEVEL: a level or a map id of the map table ("DungeonAmrine")
       level_data.py --list [OUTPUT_DIR]               each own level of the map table, its maps, and its set in OUTPUT_DIR
Default output: <SOULWARDEN_GAMEDATA_DIR> (the set goes to OUTPUT_DIR/levels/LEVEL)
Environment: as creature_data.py and navigation_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB,
             SOULWARDEN_NAVIGATION_WORK, CC, CXX).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import creature_data  # noqa: E402
import game_modes  # noqa: E402
from appearance_tables import Failure  # noqa: E402

FORMAT = 1
TOOL_VERSION = 1
MANIFEST = "level.json"
LEVELS_FOLDER = creature_data.LEVELS_FOLDER
# The text of the stop of the creature tool for a level with no usual spawn point (creature_data.build_document).
NO_SPAWN_POINT = "has no spawn point of a creature"

ToolRunner = Callable[[str, list[str]], tuple[int, str]]


def run_tool(script: str, arguments: list[str]) -> tuple[int, str]:
    """Runs one tool of this folder. @return Its status and its error text."""
    result = subprocess.run([sys.executable, "-I", "-B", str(HERE / script), *arguments], capture_output=True, text=True, check=False)
    return result.returncode, (result.stderr.strip() or result.stdout.strip())


def bytes_of(path: Path) -> int:
    if path.is_file():
        return path.stat().st_size
    return sum(item.stat().st_size for item in path.rglob("*") if item.is_file()) if path.is_dir() else 0


def spawn_entries(sources, terrain: str) -> dict:
    """@return The spawn entries of the layers of the level by their flags (the usual entries are the spawn points of creatures.json)."""
    folder = f"sharedassets/{terrain}/regions/"
    counts = {"layers": 0, "entries": 0, "usual": 0, "encounter": 0, "override": 0, "notSpawned": 0}
    for path in sources.paths(folder, ".metadata"):
        roots = sources.stream(path)
        if roots is None:
            continue
        counts["layers"] += 1
        for entry in creature_data.read_spawn_entries(roots):
            counts["entries"] += 1
            if entry["encounter"]:
                counts["encounter"] += 1
            elif entry["override"]:
                counts["override"] += 1
            elif not entry["coatlicue"]:
                counts["notSpawned"] += 1
            else:
                counts["usual"] += 1
    return counts


def read_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise Failure(f"{path}: {error}") from error


def make_set(sources, name: str, output: Path, jobs: int, runner: ToolRunner = run_tool) -> dict:
    """Makes the set of one level in OUTPUT/levels/LEVEL. @return Its manifest."""
    started = time.perf_counter()
    level, terrain, rows = game_modes.level_terrain(sources, name)
    levels = output / LEVELS_FOLDER
    final = levels / level
    part = levels / (level + ".part")
    if part.exists():
        shutil.rmtree(part)
    part.mkdir(parents=True)
    parts: dict[str, dict] = {}
    try:
        status, text = runner("creature_data.py", [str(part / "creatures.json"), "--level", level])
        if status == 0:
            document = read_json(part / "creatures.json")
            parts["creatures"] = {"file": "creatures.json", "bytes": bytes_of(part / "creatures.json"),
                                  "spawnPoints": len(document.get("spawnPoints", [])), "kinds": len(document.get("kinds", [])),
                                  "slices": len(document.get("slices", []))}
        elif NO_SPAWN_POINT in text:
            parts["creatures"] = {"file": None, "spawnPoints": 0,
                                  "cause": "the layers of the level have no usual spawn point (its creatures are members of the encounters "
                                           "of its mode root)"}
        else:
            raise Failure(f"the creature tool stopped: {text}")
        status, text = runner("navigation_data.py", [str(part / "navigation"), "--level", level, "--jobs", str(jobs)])
        if status != 0:
            raise Failure(f"the navigation tool stopped: {text}")
        navigation = read_json(part / "navigation" / "navigation.json")
        parts["navigation"] = {"folder": "navigation", "bytes": bytes_of(part / "navigation"),
                               "regions": navigation.get("totals", {}).get("regions", 0),
                               "freeFloors": navigation.get("totals", {}).get("freeFloors", 0),
                               "placements": navigation.get("totals", {}).get("placements", 0),
                               "leftOut": navigation.get("totals", {}).get("leftOut", {})}
        parts["heights"] = {"folder": "heights", "bytes": bytes_of(part / "heights")}
        status, text = runner("interactions.py", [str(part / "interaction-data.json"), "--level", level])
        if status != 0:
            raise Failure(f"the interaction tool stopped: {text}")
        interactions = read_json(part / "interaction-data.json")
        parts["interactions"] = {"file": "interaction-data.json", "bytes": bytes_of(part / "interaction-data.json"),
                                 "objects": interactions.get("counts", {}).get("objects", 0),
                                 "byKind": interactions.get("counts", {}).get("byKind", {})}
        status, text = runner("chests.py", [str(part / "chest-data.json"), "--level", level])
        if status != 0:
            raise Failure(f"the chest tool stopped: {text}")
        chests = read_json(part / "chest-data.json").get("counts", {}).get("chests", 0)
        if chests:
            parts["chests"] = {"file": "chest-data.json", "bytes": bytes_of(part / "chest-data.json"), "chests": chests}
        else:
            (part / "chest-data.json").unlink()
            parts["chests"] = {"file": None, "chests": 0}
        manifest = {"format": FORMAT, "tool": TOOL_VERSION, "level": level, "terrain": terrain, "maps": [row["id"] for row in rows],
                    "modes": sorted({row["mode"] for row in rows}), "parts": parts, "spawnEntries": spawn_entries(sources, terrain),
                    "bytes": bytes_of(part), "seconds": round(time.perf_counter() - started, 1)}
        manifest["bytes"] += len(json.dumps(manifest, indent=1)) + 1
        (part / MANIFEST).write_text(json.dumps(manifest, indent=1) + "\n")
    except BaseException:
        shutil.rmtree(part, ignore_errors=True)
        raise
    # The new set takes the place of the old one: the backend reads a set only at the first instance of its level.
    old = levels / (level + ".old")
    if old.exists():
        shutil.rmtree(old)
    if final.exists():
        final.rename(old)
    part.rename(final)
    if old.exists():
        shutil.rmtree(old)
    return manifest


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the level data set of an own level.")
    parser.add_argument("level", nargs="?", help="a level or a map id of the map table (DungeonAmrine)")
    parser.add_argument("output", nargs="?", default=str(gamedata), help="the game data folder of the set (default: SOULWARDEN_GAMEDATA_DIR)")
    parser.add_argument("--jobs", type=int, default=4, metavar="N", help="processes of the navigation tool (default 4)")
    parser.add_argument("--list", action="store_true", help="print each own level of the map table and its set in the output folder")
    arguments = parser.parse_args()
    if arguments.list and arguments.level and arguments.output == str(gamedata):
        arguments.output = arguments.level
        arguments.level = None
    if not arguments.list and not arguments.level:
        print("level: give a level or a map id (tools/gamedata/level.sh --list names them). No file was written.", file=sys.stderr)
        return 1
    output = Path(arguments.output)
    try:
        sources = creature_data.Sources(game / "assets", (creature_data.TABLES_FOLDER, "localization/", "sharedassets/coatlicue/"))
        if arguments.list:
            for level, terrain, maps in game_modes.own_levels(sources):
                manifest_path = output / LEVELS_FOLDER / level / MANIFEST
                state = "no set"
                if manifest_path.is_file():
                    manifest = read_json(manifest_path)
                    creatures = manifest.get("parts", {}).get("creatures", {})
                    state = (f"set of format {manifest.get('format')}: {creatures.get('spawnPoints', 0)} spawn points, "
                             f"{manifest.get('bytes', 0)} bytes")
                print(f"{level:34} {terrain:46} {', '.join(maps):40} {state}")
            return 0
        manifest = make_set(sources, arguments.level, output, max(1, min(arguments.jobs, 8)))
    except Failure as error:
        print(f"level: {error}. The set was not written (an old set stays).", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"level: {output}: {error}", file=sys.stderr)
        return 1
    parts = manifest["parts"]
    entries = manifest["spawnEntries"]
    print(f"{output / LEVELS_FOLDER / manifest['level']}: the level {manifest['level']} (maps {', '.join(manifest['maps'])}), "
          f"{manifest['bytes']} bytes, {manifest['seconds']} s")
    print(f"  creatures: {parts['creatures'].get('spawnPoints', 0)} spawn points of {parts['creatures'].get('kinds', 0)} kinds"
          f"{' (' + parts['creatures']['cause'] + ')' if 'cause' in parts['creatures'] else ''}; spawn entries of the layers: "
          f"{entries['entries']} ({entries['usual']} usual, {entries['encounter']} of an encounter, {entries['override']} override, "
          f"{entries['notSpawned']} not spawned)")
    print(f"  navigation: {parts['navigation']['regions']} region(s), {parts['navigation']['bytes']} bytes, "
          f"{parts['navigation']['placements']} placements; heights {parts['heights']['bytes']} bytes")
    print(f"  interactions: {parts['interactions']['objects']} objects {parts['interactions']['byKind']}; chests: {parts['chests']['chests']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check of the level tool (level_data.py, stage DG2) and of the terrain scale of a level, with made-up tables (no file of the game).

The level tool takes the level and its terrain folder from the map row (game_modes.level_terrain): a level text or a map id, never the
level of the main world, never a name of no map row. It makes the set in levels/LEVEL.part and puts it in the place of the old set only
when each tool of the set ended with status 0: a tool that stops keeps the old set. A level whose layers have no usual spawn point has no
creatures.json (the part names the cause). Stage QG9e: the part "encounters" (the instance recipes of the mode roots of the level,
encounters.py --level): a file only for a level with a recipe; the part "creatures" counts the member kinds. The terrain of a level has its own mountainHeight (256 m in most levels): its samples go to the
scale of the main world (2048 m), the scale of each tool file and of the backend.

The check uses the made-up map tables of check_game_modes_tool.py and a made-up runner of the tools (it writes the files of each part).
The test program of the backend runs it (case LV1).

Usage: check_level_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
from array import array
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_modes  # noqa: E402
import level_data as tool  # noqa: E402
import nav_terrain  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_game_modes_tool import MadeUpSources  # noqa: E402


class Runner:
    """A made-up runner of the tools: each tool writes a small file of its part. `fail` names a tool that stops; `empty` makes the
    creature tool stop for a level with no usual spawn point; `recipes` is the count of the instance recipes of the encounter tool (stage
    QG9e)."""

    def __init__(self, fail: str = "", empty: bool = False, chests: int = 0, recipes: int = 0) -> None:
        self.fail, self.empty, self.chests, self.recipes = fail, empty, chests, recipes
        self.calls: list[tuple[str, list[str]]] = []

    def __call__(self, script: str, arguments: list[str]) -> tuple[int, str]:
        self.calls.append((script, arguments))
        target = Path(arguments[0])
        if script == self.fail:
            return 1, f"{script}: a made-up stop"
        if script == "creature_data.py":
            if self.empty:
                return 1, f"creatures: the world {tool.NO_SPAWN_POINT} in the regions r_+00_+00. No file was written."
            members = [["slices/made_up_boss.dynamicslice", 1, ""], ["slices/made_up_hidden.dynamicslice", -1, "no body"]]
            target.write_text(json.dumps({"spawnPoints": [[1], [2]], "kinds": [{}, {}], "slices": [{}], "members": members}))
        elif script == "navigation_data.py":
            target.mkdir(parents=True, exist_ok=True)
            (target / "r_+00_+00.swnav").write_bytes(b"N" * 100)
            (target / "navigation.json").write_text(json.dumps({"totals": {"regions": 1, "freeFloors": 7, "placements": 3, "leftOut": {}}}))
            (target.parent / "heights").mkdir(exist_ok=True)
            (target.parent / "heights" / "r_+00_+00.swh").write_bytes(b"H" * 50)
        elif script == "interactions.py":
            target.write_text(json.dumps({"counts": {"objects": 4, "byKind": {"lorePage": 3, "dungeonExit": 1}}}))
        elif script == "chests.py":
            target.write_text(json.dumps({"counts": {"chests": self.chests}}))
        elif script == "encounters.py":
            target.write_text(json.dumps({"counts": {"recipes": self.recipes, "taken": max(0, self.recipes - 1), "instanceObjects": 2}}))
        return 0, ""


def run_set(sources, name: str, output: Path, runner: Runner) -> tuple[dict | None, str]:
    try:
        return tool.make_set(sources, name, output, 2, runner), ""
    except Failure as error:
        return None, str(error)


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-level-tool-"))
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    MadeUpSources.mode = "valid"
    sources = MadeUpSources(None)
    # No layer of a made-up level: the spawn entries are 0.
    sources.stream = lambda path: None
    failures = 0

    def report(correct: bool, what: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if correct else 'FAIL'} {what}")
        failures += not correct

    level, terrain, rows = game_modes.level_terrain(sources, "nw_made_up_00")
    by_map = game_modes.level_terrain(sources, "MADEUPDUNGEON")
    report((level, terrain, [row["id"] for row in rows]) == ("nw_made_up_00", "coatlicue/nw_made_up_00", ["MadeUpDungeon"])
           and by_map[:2] == (level, terrain), "the level and its terrain folder come from the map row (a level, or a map id in any case)")
    for name, cause in (("nw_vitae", "no map row"), ("MadeUpArena_02", "the level of the main world")):
        try:
            game_modes.level_terrain(sources, name)
            report(False, f"{cause} stops the tool")
        except Failure as error:
            report("map" in str(error) or "main world" in str(error), f"{cause} stops the tool ({error})")
    levels = [entry[0] for entry in game_modes.own_levels(sources)]
    report(levels == ["nw_made_up_00", "nw_made_up_arena"], f"the own levels of the map table: {levels}")

    output = work / "gamedata"
    manifest, problem = run_set(sources, "MadeUpDungeon", output, Runner())
    folder = output / "levels" / "nw_made_up_00"
    written = json.loads((folder / tool.MANIFEST).read_text()) if (folder / tool.MANIFEST).is_file() else {}
    report(manifest is not None and written.get("format") == 1 and written.get("level") == "nw_made_up_00"
           and written.get("terrain") == "coatlicue/nw_made_up_00" and written["parts"]["creatures"]["spawnPoints"] == 2
           and written["parts"]["navigation"]["regions"] == 1 and written["parts"]["interactions"]["objects"] == 4
           and (folder / "creatures.json").is_file() and (folder / "heights" / "r_+00_+00.swh").is_file()
           and not (folder / "chest-data.json").exists() and written["parts"]["chests"] == {"file": None, "chests": 0}
           and not (output / "levels" / "nw_made_up_00.part").exists() and written["parts"]["creatures"]["memberKinds"] == 1
           and written["parts"]["encounters"] == {"file": None, "recipes": 0} and not (folder / "encounter-data.json").exists(),
           f"a complete set: the manifest, the parts, no chest file for a level with no chest, no encounter file for a level with no "
           f"recipe, the member kinds, no part folder ({problem})")
    manifest, problem = run_set(sources, "nw_made_up_00", output, Runner(recipes=2))
    report(manifest is not None and (folder / "encounter-data.json").is_file()
           and manifest["parts"]["encounters"].get("recipes") == 2 and manifest["parts"]["encounters"].get("taken") == 1
           and manifest["parts"]["encounters"].get("instanceObjects") == 2 and manifest["tool"] == tool.TOOL_VERSION,
           f"stage QG9e: a level with instance recipes keeps encounter-data.json and its counts ({problem})")
    _, problem = run_set(sources, "nw_made_up_00", output, Runner(fail="encounters.py"))
    report(problem.startswith("the encounter tool stopped") and (folder / "encounter-data.json").is_file(),
           "stage QG9e: the encounter tool that stops stops the level tool: the old set stays")
    first_bytes = json.loads((folder / tool.MANIFEST).read_text()).get("bytes", 0) if (folder / tool.MANIFEST).is_file() else 0
    _, problem = run_set(sources, "nw_made_up_00", output, Runner(fail="navigation_data.py"))
    kept = json.loads((folder / tool.MANIFEST).read_text()) if (folder / tool.MANIFEST).is_file() else {}
    report(problem.startswith("the navigation tool stopped") and kept.get("bytes") == first_bytes and (folder / "creatures.json").is_file()
           and not (output / "levels" / "nw_made_up_00.part").exists(),
           "a tool that stops stops the level tool: the old set stays, and no part folder")
    manifest, problem = run_set(sources, "nw_made_up_00", output, Runner(empty=True, chests=2))
    report(manifest is not None and not (folder / "creatures.json").exists() and manifest["parts"]["creatures"]["spawnPoints"] == 0
           and "mode root" in manifest["parts"]["creatures"]["cause"] and (folder / "chest-data.json").is_file()
           and manifest["parts"]["chests"]["chests"] == 2,
           "a level with no usual spawn point: no creatures.json, its cause; a level with chests keeps chest-data.json")
    _, problem = run_set(sources, "MadeUpArena_02", output, Runner())
    report("main world" in problem and not (output / "levels" / "newworld_vitaeeterna").exists(), "no set of the main world level")

    # The terrain scale of a level (nav_terrain.to_main_scale, mountain_height).
    samples = array("H", [0, 8191, 65535])
    scaled = nav_terrain.to_main_scale(samples, 256.0)
    report(list(scaled) == [0, 1024, 8192] and nav_terrain.to_main_scale(samples, 2048.0) is samples
           and abs(scaled[1] * 2048.0 / 65535.0 - 8191 * 256.0 / 65535.0) < 0.02,
           f"a terrain of 256 m: the samples in the scale of 2048 m ({list(scaled)}; 32.0 m stays 32.0 m)")
    try:
        nav_terrain.mountain_height(json.dumps({"mountainHeight": 4096.0}).encode(), "made-up terrain.json")
        report(False, "a mountain height above 2048 m stops the tool")
    except nav_terrain.Failure as error:
        report("2048" in str(error), "a mountain height above 2048 m stops the tool")
    report(nav_terrain.mountain_height(json.dumps({"oceanLevel": 40}).encode(), "made-up") == 2048.0,
           "a terrain.json with no mountain height has the 2048 m of the main world")

    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

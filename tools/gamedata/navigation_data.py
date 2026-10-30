#!/usr/bin/env python3
"""Write the navigation data of the world for the backend (protocol notes 4.19).

The game copy has no navigation mesh: the original one was only on the server. But the client
stops its own character at each wall, thus the game copy has the collision shapes of the
world. This tool makes the navigation data from them, region by region:

  1. The program navcore (navcore.cpp, with the noise file of vendor/open-simplex-noise) is
     compiled with the C and C++ compilers of the computer, and it makes the noise texture of
     the terrain rule.
  2. collision_data.py writes the collision data of each region: the terrain (the height map
     after the cookies of the placements), the water, each solid shape, and the navigation
     marks of the level designers. A region needs the collision data of the regions around it.
  3. `navcore build` makes the grid of each region from its collision data, and nav_file.py
     writes it as the file of the game data folder (one zlib stream for each tile of 64 m).
  4. The terrain heights of the game data folder (height_maps.py, the folder `heights`) are
     made again from the terrain after the cookies: the backend uses them where it has no
     tile of the navigation data.

Output: OUTPUT_DIR/r_+XX_+YY.swnav for each region, and OUTPUT_DIR/navigation.json (the list
of the regions with their sizes, the agent values, each thing that the tools left out with its
count, the time, and the memory). A region that is complete is not made again: the tool reads
its entry of navigation.json. The work files (the collision data, 1.6 GB; the index of the pak
files) are in SOULWARDEN_NAVIGATION_WORK, outside the repository.

The tool stops with status 1 and one message when a source is not there or does not decode:
it then writes no file for that region, and navigation.json does not name the region. A thing
that the tools leave out (a spawner that is off at the start of the game, a shape with no
physics data) is counted in navigation.json with its cause.

Usage: navigation_data.py [OUTPUT_DIR] [--region NAME ...] [--jobs N] [--force] [--no-heights] [--keep-raw] [--level LEVEL]
       navigation_data.py [OUTPUT_DIR] --spawn-points CREATURES.json [REPORT_DIR]
                          (which spawn points of the creature data are on a free floor of the data)
Default output: <SOULWARDEN_GAMEDATA_DIR>/navigation (stage DG2, with --level: <SOULWARDEN_GAMEDATA_DIR>/levels/LEVEL/navigation; the
                collision data and the grids of a level are in the work folder levels/LEVEL)
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_OODLE_LIB (an Oodle library of
             the computer; SOULWARDEN_OODLE_DLL with the helper is the second source),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_NAVIGATION_WORK (default ~/Games/soulwarden/work/navigation-build),
             CC and CXX (the compilers; default clang and clang++, then gcc and g++),
             and the agent values of navcore.cpp (NAV_RADIUS, NAV_HEIGHT, NAV_STEP, NAV_SLOPE, NAV_WADE)
"""

from __future__ import annotations

import argparse
import json
import os
import resource
import shutil
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import collision_data  # noqa: E402
import nav_file  # noqa: E402
from nav_assets import Failure  # noqa: E402

OUTPUT_FOLDER = "navigation"
MANIFEST = "navigation.json"
# A change of a rule of the tools: each region is made again. 2: a physics file that does not
# decode stops a region (finding F10 of the code review of stage 3.4d), and a cell with more than
# 16 floors stops it too (finding F8 of the code review of the corrections after user test 1).
TOOL_VERSION = 2
AGENT_VARIABLES = ("NAV_CELL", "NAV_FINE", "NAV_RADIUS", "NAV_HEIGHT", "NAV_STEP", "NAV_SLOPE", "NAV_WADE")
_WORLD: collision_data.World | None = None


def level_world(name: str) -> tuple[str, str]:
    """Stage DG2: @return An own level and the folder of its terrain project, from its map row (game_modes.level_terrain)."""
    from creature_data import TABLES_FOLDER, Sources  # noqa: PLC0415
    import game_modes  # noqa: PLC0415
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or Path.home() / "Games/soulwarden/game")
    import appearance_tables  # noqa: PLC0415
    try:
        level, terrain, _rows = game_modes.level_terrain(Sources(game / "assets", (TABLES_FOLDER, "localization/")), name)
    except appearance_tables.Failure as error:
        raise Failure(str(error)) from error
    if not terrain.startswith("coatlicue/") or terrain.count("/") != 1:
        raise Failure(f"the terrain project {terrain!r} of the map row is not a folder coatlicue/NAME")
    return level, terrain.split("/", 1)[1]


def compiler(variable: str, names: tuple[str, ...]) -> str:
    chosen = os.environ.get(variable)
    if chosen:
        return chosen
    for name in names:
        if shutil.which(name):
            return name
    raise Failure(f"no compiler: install one of {', '.join(names)}, or set {variable}")


def build_program(work: Path) -> Path:
    """Compiles navcore when its sources are newer than the program. @return The program."""
    program = work / "bin" / "navcore"
    noise_source = HERE / "vendor/open-simplex-noise/open-simplex-noise.c"
    sources = [HERE / "navcore.cpp", noise_source, HERE / "vendor/open-simplex-noise/open-simplex-noise.h"]
    for source in sources:
        if not source.is_file():
            raise Failure(f"{source} is not there")
    if program.is_file() and all(source.stat().st_mtime <= program.stat().st_mtime for source in sources):
        return program
    program.parent.mkdir(parents=True, exist_ok=True)
    noise_object = program.parent / "open-simplex-noise.o"
    # No joined float operations: the terrain rule needs the numbers of the client.
    steps = ([compiler("CC", ("clang", "gcc", "cc")), "-O2", "-ffp-contract=off", "-c", str(noise_source), "-o", str(noise_object)],
             [compiler("CXX", ("clang++", "g++", "c++")), "-std=c++17", "-O2", "-ffp-contract=off", "-pthread", f"-I{HERE}",
              "-o", str(program) + ".part", str(HERE / "navcore.cpp"), str(noise_object)])
    for step in steps:
        result = subprocess.run(step, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise Failure(f"the compiler failed: {' '.join(step[:1])}: {result.stderr.strip()[:2000]}")
    os.replace(str(program) + ".part", program)
    return program


def run_program(arguments: list[str], environment: dict | None = None) -> dict:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False, env=environment)
    if result.returncode != 0:
        raise Failure(f"{Path(arguments[0]).name} {arguments[1]} failed: {result.stderr.strip() or result.stdout.strip()}")
    try:
        return json.loads(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError) as error:
        raise Failure(f"{Path(arguments[0]).name} {arguments[1]} gave no result line") from error


def _start_worker(world_name: str, cache: str) -> None:
    global _WORLD
    os.nice(10)
    _WORLD = collision_data.World(world_name, Path(cache))


def _collision(job: tuple[str, str, bool]) -> tuple[str, dict | None, str]:
    name, out, force = job
    try:
        return name, collision_data.make_region(_WORLD, name, Path(out), force, TOOL_VERSION), ""
    except Failure as error:
        return name, None, str(error)


def neighbors(name: str) -> list[str]:
    x, y = collision_data.region_origin(name)
    return [f"r_{x + dx:+03d}_{y + dy:+03d}" for dy in (-1, 0, 1) for dx in (-1, 0, 1)]


def write_manifest(path: Path, manifest: dict) -> None:
    temporary = path.with_name(path.name + ".part")
    temporary.write_text(json.dumps(manifest, indent=1) + "\n")
    os.replace(temporary, path)


def agent_environment() -> dict:
    return {name: os.environ[name] for name in AGENT_VARIABLES if os.environ.get(name)}


def make(arguments: argparse.Namespace, output: Path, work: Path, level: str | None = None,
         world_name: str = collision_data.DEFAULT_WORLD) -> int:
    """@param level, world_name Stage DG2: an own level and the folder of its terrain project (from its map row); the collision data, the
           grids, and the terrain after the cookies of a level are in the work folder levels/LEVEL (its regions have the names of the
           regions of the main world)."""
    started = time.perf_counter()
    program = build_program(work)
    noise = work / "noise.f32"
    if not noise.is_file() or noise.stat().st_mtime < program.stat().st_mtime:
        run_program([str(program), "noise", str(noise)])
    own = work / "levels" / level if level else work
    world = collision_data.World(world_name, work / "cache", program, noise)
    known = world.regions()
    if not known:
        raise Failure("the game copy has no region with a height map")
    wanted = arguments.region or known
    for name in wanted:
        if name not in known:
            raise Failure(f"the world has no region {name}")
    jobs = max(1, min(arguments.jobs, max(1, (os.cpu_count() or 2) // 2)))
    collision = own / "collision"
    collision.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / MANIFEST
    manifest: dict = {}
    if manifest_path.is_file() and not arguments.force:
        try:
            manifest = json.loads(manifest_path.read_text())
        except ValueError:
            manifest = {}
    agent = agent_environment()
    if manifest.get("tool") != TOOL_VERSION or manifest.get("format") != nav_file.VERSION or manifest.get("agentVariables") != agent:
        manifest = {}
    manifest.update({"format": nav_file.VERSION, "tool": TOOL_VERSION, "agentVariables": agent})
    if level:
        manifest.update({"level": level, "world": world_name})
    regions: dict = manifest.setdefault("regions", {})
    # A region whose file is not the file of its entry is not complete.
    for name in list(regions):
        file = output / f"{name}.swnav"
        if not file.is_file() or file.stat().st_size != regions[name].get("bytes"):
            del regions[name]
    todo = [name for name in wanted if arguments.force or name not in regions]
    print(f"navigation: {len(known)} regions in the world, {len(wanted)} wanted, {len(wanted) - len(todo)} complete already, "
          f"{jobs} processes", flush=True)

    # Step 2: the collision data of each region that a grid needs (a region and the regions around it).
    needed = sorted({other for name in todo for other in neighbors(name) if other in known})
    collision_seconds = time.perf_counter()
    if needed:
        work_list = [(name, str(collision), arguments.force and name in todo) for name in needed]
        with ProcessPoolExecutor(max_workers=min(jobs, len(work_list)), initializer=_start_worker,
                                 initargs=(world_name, str(work / "cache"))) as pool:
            for name, report, problem in pool.map(_collision, work_list):
                if problem:
                    raise Failure(problem)
                if report is not None:
                    print(f"  collision {name}: {report['placements']} placements, {sum(report['leftOut'].values())} left out, "
                          f"{report['terrain']['cookiesUsed']} cookies, {report['seconds']} s", flush=True)
    collision_seconds = time.perf_counter() - collision_seconds

    # Step 3: the grid of each region.
    raw = own / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    for name in todo:
        region_started = time.perf_counter()
        raw_file = raw / f"{name}.raw"
        counts = run_program(["nice", "-n", "10", str(program), "build", str(collision), name, str(raw_file), str(jobs)], environment)
        packed = nav_file.pack(raw_file, output / f"{name}.swnav")
        if not arguments.keep_raw:
            raw_file.unlink()
        report = json.loads((collision / f"{name}.json").read_text())
        regions[name] = {
            "bytes": packed["bytes"], "plainBytes": packed["plainBytes"],
            "cells": counts["cells"], "cellsWithFloor": counts["cellsWithFloor"], "cellsWith2Floors": counts["cellsWith2Floors"],
            "freeFloors": counts["freeFloors"], "floorsOnCollider": counts["floorsOnCollider"],
            "floorsShallowWater": counts["floorsShallowWater"], "floorsDeepWater": counts["floorsDeepWater"],
            "blockedFloors": counts["blockedFloors"], "tilesWithNoCollider": counts["tilesWithNoCollider"],
            "placements": report["placements"], "cookies": report["terrain"]["cookiesUsed"],
            "navigationMarks": {key.split(".")[1]: value for key, value in report["counts"].items()
                                if key.startswith("navigationMarks.")},
            "leftOut": report["leftOut"],
            "seconds": round(time.perf_counter() - region_started, 1),
        }
        manifest["agent"] = {key: counts[key] for key in ("cell", "radius", "height", "step", "slope", "wade")}
        write_manifest(manifest_path, manifest)
        print(f"  {name}: {packed['bytes']} bytes, {counts['freeFloors']} free floors, {regions[name]['seconds']} s", flush=True)

    # Step 4: the terrain heights of the game data folder, from the terrain after the cookies.
    heights_note = "not made (--no-heights)"
    if not arguments.no_heights:
        result = subprocess.run([sys.executable, "-B", str(HERE / "height_maps.py"), str(output.parent / "heights"),
                                 "--jobs", str(jobs), "--region", *wanted, *(["--level", level] if level else [])],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise Failure(f"the terrain height tool failed: {result.stderr.strip()}")
        heights_note = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "made"
        print(f"  {heights_note}", flush=True)

    left_out: dict[str, int] = {}
    for entry in regions.values():
        for cause, count in entry.get("leftOut", {}).items():
            left_out[cause] = left_out.get(cause, 0) + count
    files = sorted(output.glob("r_*.swnav"))
    manifest["totals"] = {
        "regions": len(regions), "regionsOfTheWorld": len(known), "bytes": sum(file.stat().st_size for file in files),
        "freeFloors": sum(entry["freeFloors"] for entry in regions.values()),
        "placements": sum(entry["placements"] for entry in regions.values()),
        "cookies": sum(entry["cookies"] for entry in regions.values()),
        "leftOut": dict(sorted(left_out.items(), key=lambda item: -item[1])),
    }
    if todo:
        manifest["lastRun"] = {
            "regionsMade": len(todo), "seconds": round(time.perf_counter() - started, 1),
            "collisionSeconds": round(collision_seconds, 1), "processes": jobs,
            "largestProcessMegabytes": round(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1024.0, 1),
            "terrainHeights": heights_note,
        }
    write_manifest(manifest_path, manifest)
    totals = manifest["totals"]
    print(f"{output}: the navigation data of {totals['regions']} region(s) of {totals['regionsOfTheWorld']}, {totals['bytes']} bytes, "
          f"{sum(left_out.values())} things left out (see {MANIFEST}), {time.perf_counter() - started:.0f} s")
    return 0


def spawn_points(output: Path, creatures: Path, report: Path | None) -> int:
    """Says which spawn points of the creature data are on a free floor of the navigation data."""
    try:
        document = json.loads(creatures.read_text())
    except (OSError, ValueError) as error:
        raise Failure(f"{creatures}: {error}") from error
    navigation = nav_file.Navigation(output)
    kinds = document.get("kinds", [])
    slices = document.get("slices", [])
    causes: dict[str, int] = {}
    fighting_causes: dict[str, int] = {}
    rows = []
    # A spawn point of the creature data: [placement id, kind, level, x, y, z, yaw, respawnMin, respawnMax, ground].
    points = document.get("spawnPoints", [])
    try:
        ordered = sorted(points, key=lambda item: (item[3] // 64, item[4] // 64))
        for point in ordered:
            x, y, z = (float(value) for value in point[3:6])
            kind = kinds[point[1]]
            # The rule of the backend: a kind fights when one of its slices has a sense, a run speed, and an attack.
            fights = any(slice_fights(slices[body[0]]) for body in kind["bodies"])
            cause = footing(navigation, x, y, z)
            causes[cause] = causes.get(cause, 0) + 1
            if fights:
                fighting_causes[cause] = fighting_causes.get(cause, 0) + 1
            if cause != "walks":
                rows.append(f"{x:.3f}\t{y:.3f}\t{z:.3f}\t{kind.get('id', '')}\t{'fights' if fights else 'no attack'}\t{cause}\n")
    except (IndexError, KeyError, TypeError, ValueError) as error:
        raise Failure(f"{creatures}: the file does not have the form of the creature data tool ({error!r})") from error
    total = len(points)
    summary = {"spawnPoints": total, "walk": causes.get("walks", 0),
               "causes": dict(sorted(causes.items(), key=lambda item: -item[1])),
               "spawnPointsOfKindsThatFight": sum(fighting_causes.values()),
               "walkOfKindsThatFight": fighting_causes.get("walks", 0),
               "causesOfKindsThatFight": dict(sorted(fighting_causes.items(), key=lambda item: -item[1]))}
    print(json.dumps(summary, indent=1))
    if report is not None:
        report.mkdir(parents=True, exist_ok=True)
        (report / "spawn-points-summary.json").write_text(json.dumps(summary, indent=1) + "\n")
        (report / "spawn-points-that-do-not-walk.tsv").write_text("x\ty\tz\tkind\tattack\tcause\n" + "".join(rows))
    return 0


def slice_fights(body: dict) -> bool:
    """@return True for a slice of the creature data with a vision distance, a run speed, and an attack."""
    behavior = body.get("behavior") or {}
    return bool(body.get("attacks")) and behavior.get("sight", [0, 0])[1] > 0 and behavior.get("speed", [0, 0])[1] > 0


# The rule of the backend (world/navigation.h, navigation_rules): a creature walks when a free floor is this near.
FOOTING_REACH, FOOTING_HEIGHT = 0.5, 0.6


def footing(navigation: nav_file.Navigation, x: float, y: float, z: float) -> str:
    """@return "walks", or the cause for a creature at this place that does not walk."""
    cell = navigation.cell()
    steps = int(FOOTING_REACH / cell + 0.999)
    nearest = None
    blocked = None
    any_data = False
    for dy in range(-steps, steps + 1):
        for dx in range(-steps, steps + 1):
            floors = navigation.floors(x + dx * cell, y + dy * cell)
            if floors is None:
                continue
            any_data = True
            for height, flags, _low, _high in floors:
                rise = height - z
                if flags & nav_file.BLOCKED:
                    if dx == 0 and dy == 0 and (blocked is None or abs(rise) < abs(blocked)):
                        blocked = rise
                    continue
                if flags & nav_file.DEEP:
                    if dx == 0 and dy == 0 and abs(rise) <= 2.5 and nearest is None:
                        blocked = rise
                        nearest = ("deep", rise)
                    continue
                if abs(rise) <= FOOTING_HEIGHT:
                    return "walks"
                if nearest is None or nearest[0] == "deep" or abs(rise) < abs(nearest[1]):
                    nearest = ("floor", rise)
    if not any_data:
        return "no navigation data for the region"
    if nearest is not None and nearest[0] == "deep":
        return "in deep water"
    if blocked is not None and abs(blocked) <= FOOTING_HEIGHT:
        return "on a surface that is no place for a creature (too steep, too near to a solid, or a mark of the level designers)"
    if nearest is None:
        return "no free floor at the place"
    if nearest[1] < 0:
        return "more than 0.6 m above the nearest free floor (a creature on a mesh with no collision, or a place in the air)"
    return "more than 0.6 m below the nearest free floor (below the ground, or in a solid of the data)"


def main() -> int:
    home = Path.home()
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the navigation data of the world for the backend.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_FOLDER))
    parser.add_argument("report", nargs="?", help="with --spawn-points: the folder of the report files")
    parser.add_argument("--region", nargs="+", metavar="NAME", help="only these regions (r_+04_+02)")
    parser.add_argument("--jobs", type=int, default=4, metavar="N", help="processes and threads (default 4; half of the cores at most)")
    parser.add_argument("--force", action="store_true", help="make a region again that is complete")
    parser.add_argument("--no-heights", action="store_true", help="do not make the terrain heights of the game data folder again")
    parser.add_argument("--keep-raw", action="store_true", help="keep the files of navcore in the work folder")
    parser.add_argument("--spawn-points", metavar="CREATURES.json",
                        help="say which spawn points of a creature data file are on a free floor, and write nothing else")
    parser.add_argument("--level", metavar="LEVEL",
                        help="stage DG2: the regions of an own level (a level or a map id of the map table; default output "
                             "GAMEDATA/levels/LEVEL/navigation, and its heights beside it)")
    arguments = parser.parse_args()
    output = Path(arguments.output)
    try:
        if arguments.spawn_points:
            return spawn_points(output, Path(arguments.spawn_points), Path(arguments.report) if arguments.report else None)
        if arguments.level:
            level, world_name = level_world(arguments.level)
            if arguments.output == parser.get_default("output"):
                output = gamedata / "levels" / level / OUTPUT_FOLDER
            return make(arguments, output, collision_data.work_folder(), level, world_name)
        return make(arguments, output, collision_data.work_folder())
    except (Failure, nav_file.Failure) as error:
        print(f"navigation: {error}. The data of the folder is not complete: {MANIFEST} names the regions that are.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"navigation: {output}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

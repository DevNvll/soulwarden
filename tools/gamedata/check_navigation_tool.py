#!/usr/bin/env python3
"""Check of the navigation tool with made-up sources (no file of the game).

Two findings of the code review of stage 3.4d:

- F10: a placed collider whose physics file does not decode. The region writer of
  collision_data.py kept the region with no collider there and marked it complete, and the
  navigation driver wrote a complete navigation file with status 0: a wall of the game was not
  in the data, with no sign. The tool must stop: no region file, no complete report, status 1.
- F11: the terrain reader of navcore.cpp at the east edge of a region with no region to its
  east. It took the mean of the east and the north samples for the missing diagonal sample;
  the reader of height_maps.py (the rule of the files of the backend) takes the north sample.
  The two readers must give one height.

Two findings of the code review of the corrections after user test 1:

- F6: a complete collision report of the tool before the correction of F10, with a physics
  file that did not decode in its list `physicsFilesNotDecoded`, was used again by the
  corrected tool. Such a report, and a report of an earlier tool version (TOOL_VERSION of
  navigation_data.py), must not be used again.
- F8: the tile writer of navcore.cpp wrote a cell with 17 floors, and the backend refuses
  such a tile. The writer must stop the region: no complete region with a tile that the
  backend cannot use.

Usage: check_navigation_tool.py WORK_FOLDER
Status 0: each case has the expected result. The test program of the backend runs it (case RN10).
The check compiles a small program with navcore.cpp (a C and a C++ compiler, as the tool needs).
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import subprocess
import sys
from array import array
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import collision_data as collision  # noqa: E402
import navigation_data as navigation  # noqa: E402

REGION = "r_+00_+00"
BROKEN = "madeup/broken.rnr"
WORLD = collision.World


class Sources:
    """The pak files of a made-up world: a height map, and a physics file of three bytes."""

    files_read = 0
    bytes_read = 0

    def has(self, path: str) -> bool:
        return path.endswith("/region.heightmap") or path == BROKEN

    def paths(self, prefix: str, ending: str) -> list:
        return []

    def read(self, path: str) -> bytes:
        data = b"bad" if path == BROKEN else b"made-up height samples"
        self.files_read += 1
        self.bytes_read += len(data)
        return data


def made_up_world(*_arguments) -> collision.World:
    world = WORLD.__new__(WORLD)
    world.regions = lambda: [REGION]
    world.folder = "madeup/regions/"
    world.paks = Sources()
    world.ocean_level = 0.0
    world.navcore = Path("unused")
    world.noise = Path("unused")
    world.shapes = {}
    world.shape_statistics = collision.Statistics()
    world.shape_failures = {}
    world.mesh_physics = {}
    world.slices = {}
    world.filters = type("Filters", (), {"flags": lambda self, name: 3})()
    return world


def placements(broken: bool):
    def read_chunks(region) -> None:
        key = ("rnr", BROKEN) if broken else ("box", 1.0, 1.0, 1.0)
        region.add(key, collision.IDENTITY, "", collision.SOURCE_CHUNK, "made-up placement")
    return read_chunks


def region_writer(folder: Path, broken: bool) -> tuple[int, dict | None]:
    """Runs collision_data.py for the made-up region. @return Its status and its report (no value: no report)."""
    world = made_up_world()
    with patch.object(collision, "World", lambda *arguments: world), \
         patch.object(collision, "read_heightmap", lambda data: array("H", [1600]) * (2048 * 2048)), \
         patch.object(collision.Region, "read_chunks", placements(broken)), \
         patch.object(collision, "work_folder", lambda: folder.parent), \
         patch.object(sys, "argv", ["collision_data.py", REGION, "--out", str(folder), "--force"]), \
         contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        status = collision.main()
    report_path = folder / f"{REGION}.json"
    return status, json.loads(report_path.read_text()) if report_path.is_file() else None


def navigation_driver(root: Path) -> tuple[int, bool, bool, bool]:
    """Runs the driver of navigation_data.py with the broken source.

    @return Status, region in the manifest, region file, and True when the driver started the
            grid of the region (the step after a complete collision report).
    """
    work = root / "navigation-work"
    work.mkdir(parents=True, exist_ok=True)
    program = work / "bin" / "navcore"
    program.parent.mkdir(parents=True, exist_ok=True)
    program.write_text("not a program: the driver must stop before the grid\n")
    output = root / "navigation-output"
    calls: list[str] = []

    def run_program(arguments: list[str], environment: dict | None = None) -> dict:
        if "noise" in arguments:
            Path(arguments[-1]).write_bytes(b"made-up noise")
            return {}
        calls.append("build" if "build" in arguments else " ".join(arguments[1:3]))
        raise navigation.Failure("the grid of a region with a broken source was started")

    class OneWorker:
        def __init__(self, max_workers, initializer, initargs) -> None:
            initializer(*initargs)

        def __enter__(self):
            return self

        def __exit__(self, *arguments) -> None:
            pass

        def map(self, function, jobs):
            return map(function, jobs)

    arguments = argparse.Namespace(region=[REGION], jobs=1, force=True, keep_raw=False, no_heights=True)
    with patch.object(collision, "World", made_up_world), \
         patch.object(collision, "read_heightmap", lambda data: array("H", [1600]) * (2048 * 2048)), \
         patch.object(collision.Region, "read_chunks", placements(True)), \
         patch.object(navigation, "build_program", lambda folder: program), \
         patch.object(navigation, "run_program", run_program), \
         patch.object(navigation, "ProcessPoolExecutor", OneWorker), \
         contextlib.redirect_stdout(io.StringIO()):
        try:
            status = navigation.make(arguments, output, work)
        except (navigation.Failure, collision.Failure) as error:
            status = 1  # the status of navigation_data.py main for a Failure
            calls.append(f"failure: {error}")
    manifest_path = output / "navigation.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {"regions": {}}
    for call in calls:
        if call.startswith("failure: "):
            print("        the driver: " + call[9:][:200])
    return status, REGION in manifest.get("regions", {}), (output / f"{REGION}.swnav").is_file(), "build" in calls


EDGE_PROGRAM = r'''
#define main navcore_main
#include "navcore.cpp"
#undef main

// The terrain reader of navcore with one made-up region and no neighbor: each sample is 1000,
// and the row 101 has 2000. Prints the heights of three places.
int main() {
    Scene scene;
    auto region = std::make_unique<Collision>();
    region->heights.assign(static_cast<size_t>(kRegionSize) * kRegionSize, 1000);
    for (int x = 0; x < kRegionSize; ++x) region->heights[101 * kRegionSize + x] = 2000;
    for (int y = 0; y < kRegionSize; ++y) region->heights[static_cast<size_t>(y) * kRegionSize + 600] = 3000;
    scene.regions.emplace(std::make_pair(0, 0), std::move(region));
    const double places[][2] = {{100.5, 100.5}, {2047.5, 100.5}, {600.5, 2047.5}};
    for (const auto& place : places) {
        float height = 0.0f;
        const bool found = scene.terrain(place[0], place[1], height);
        std::printf("%.1f %.1f %d %.6f\n", place[0], place[1], found ? 1 : 0, height);
    }
    return 0;
}
'''


def navcore_heights(root: Path) -> list[tuple[float, float, float]]:
    """Compiles the terrain reader of navcore.cpp with a made-up region. @return (x, y, height) for each place."""
    result = subprocess.run([str(compile_program(root, "navcore-edge", EDGE_PROGRAM))], capture_output=True, text=True,
                            check=True)
    heights = []
    for line in result.stdout.split("\n"):
        if line.strip():
            x, y, found, height = line.split()
            heights.append((float(x), float(y), float(height) if found == "1" else float("nan")))
    return heights


FLOOR_PROGRAM = r'''
#define main navcore_main
#include "navcore.cpp"
#undef main

// The tile writer of navcore with one made-up grid: the first column has N floors, each other
// column one floor of the terrain. Prints "N refused" or "N written" for 16 and 17 floors.
int main() {
    Scene scene;
    auto region = std::make_unique<Collision>();
    region->heights.assign(static_cast<size_t>(kRegionSize) * kRegionSize, 1600);
    scene.regions.emplace(std::make_pair(0, 0), std::move(region));
    for (unsigned count : {16U, 17U}) {
        Grid grid;
        grid.width = grid.height = 256;
        for (unsigned column = 0; column < 256U * 256U; ++column) {
            grid.first.push_back(static_cast<uint32_t>(grid.spans.size()));
            grid.firstFloor.push_back(static_cast<uint32_t>(grid.floors.size()));
            const unsigned floors = column == 0 ? count : 1;
            for (unsigned index = 0; index < floors; ++index) {
                const float height = 50.0f + index * 3.0f;
                grid.spans.push_back({height - 0.25f, height, 1, static_cast<uint8_t>(column != 0), 0});
                grid.floors.push_back({height, index + 1 < floors ? height + 2.75f : kFar, kFree,
                                       static_cast<uint8_t>(column != 0 ? kFloorTerrain : 0)});
            }
        }
        grid.first.push_back(static_cast<uint32_t>(grid.spans.size()));
        grid.firstFloor.push_back(static_cast<uint32_t>(grid.floors.size()));
        bool refused = false;
        try {
            (void)encode_tile(scene, grid, 0, 256, 1, 0, 0);
        } catch (const Failure&) {
            refused = true;
        }
        std::printf("%u %s\n", count, refused ? "refused" : "written");
    }
    return 0;
}
'''


def compile_program(root: Path, name: str, text: str) -> Path:
    """Compiles a small program with navcore.cpp. @return The program."""
    folder = root / name
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / f"{name}.cpp"
    source.write_text(text)
    program = folder / name
    noise_object = folder / "open-simplex-noise.o"
    steps = ([navigation.compiler("CC", ("clang", "gcc", "cc")), "-O2", "-ffp-contract=off", "-c",
              str(HERE / "vendor/open-simplex-noise/open-simplex-noise.c"), "-o", str(noise_object)],
             [navigation.compiler("CXX", ("clang++", "g++", "c++")), "-std=c++17", "-O1", "-ffp-contract=off", "-pthread",
              f"-I{HERE}", "-o", str(program), str(source), str(noise_object)])
    for step in steps:
        result = subprocess.run(step, capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"the compiler failed: {result.stderr.strip()[:500]}")
    return program


def floor_limit(root: Path) -> dict[int, str]:
    """@return For 16 and 17 floors in one cell: "refused" or "written" (the tile writer of navcore.cpp)."""
    result = subprocess.run([str(compile_program(root, "navcore-floors", FLOOR_PROGRAM))], capture_output=True, text=True,
                            check=True)
    answers = {}
    for line in result.stdout.split("\n"):
        if line.strip():
            count, answer = line.split()
            answers[int(count)] = answer
    return answers


def old_cache(folder: Path, stamp: int, failures: list[str]) -> str:
    """Puts a complete collision report of an earlier run into a folder, and runs the region writer with no --force.

    @return "used" (the writer took the report), "made again", or "refused" (the writer read the sources again and
            stopped at the physics file that does not decode)."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{REGION}.swcol").write_bytes(b"a region file of an earlier run")
    (folder / f"{REGION}.json").write_text(json.dumps({"complete": True, "format": collision.FORMAT_VERSION, "tool": stamp,
                                                       "placements": 0, "physicsFilesNotDecoded": failures}))
    with patch.object(collision, "read_heightmap", lambda data: array("H", [1600]) * (2048 * 2048)), \
         patch.object(collision.Region, "read_chunks", placements(True)), \
         contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        try:
            made = collision.make_region(made_up_world(), REGION, folder, False, navigation.TOOL_VERSION)
        except collision.Failure:
            return "refused"
    return "used" if made is None else "made again"


class OneRegion:
    """The same made-up region for the reader of height_maps.py."""

    def __init__(self, samples: array) -> None:
        self.names = {REGION}
        self.regions = {REGION: samples}
        self.with_cookies: set = set()


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.split("Usage:")[1], file=sys.stderr)
        return 1
    root = Path(sys.argv[1]) / "navigation-tool-check"
    root.mkdir(parents=True, exist_ok=True)
    results: list[bool] = []

    def report(correct: bool, text: str) -> None:
        results.append(correct)
        print(("ok    " if correct else "FAIL  ") + text)

    status, good = region_writer(root / "valid" / "collision", False)
    report(status == 0 and good is not None and good.get("complete") and good.get("placements") == 1,
           "control: the region writer keeps a valid made-up collider and marks the region complete")
    status, broken = region_writer(root / "broken" / "collision", True)
    complete = broken is not None and bool(broken.get("complete"))
    written = (root / "broken" / "collision" / f"{REGION}.swcol").is_file()
    report(status == 1 and not complete and not written,
           f"a physics file that does not decode stops the region writer with status 1, and it writes no complete report "
           f"and no region file (status {status}, complete report {complete}, region file {written}) (F10)")
    status, in_manifest, written, grid = navigation_driver(root)
    report(status == 1 and not in_manifest and not written and not grid,
           f"the navigation driver stops with status 1 before the grid of the region, and writes no region file and no "
           f"manifest entry (status {status}, grid started {grid}, in the manifest {in_manifest}, file {written}) (F10)")

    answer = old_cache(root / "cache-current" / "collision", navigation.TOOL_VERSION, [])
    report(answer == "used", f"control: a complete report of the current tool with no failure is used again ({answer})")
    answer = old_cache(root / "cache-failure" / "collision", navigation.TOOL_VERSION, [BROKEN])
    report(answer != "used", f"a complete report with a physics file that did not decode is not used again ({answer}) (F6)")
    answer = old_cache(root / "cache-old-tool" / "collision", 1, [])
    report(navigation.TOOL_VERSION > 1 and answer != "used",
           f"a complete report of the tool version 1 (before the corrections) is not used again ({answer}, tool version "
           f"{navigation.TOOL_VERSION}) (F6)")
    try:
        answers = floor_limit(root)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        report(False, f"the tile writer of navcore.cpp compiles and runs: {error}")
        answers = {}
    report(answers.get(16) == "written", f"control: the tile writer writes a cell with 16 floors ({answers.get(16)})")
    report(answers.get(17) == "refused", f"the tile writer refuses a cell with 17 floors, which the backend refuses "
           f"({answers.get(17)}) (F8)")

    import height_maps
    samples = array("H", [1000]) * (2048 * 2048)
    samples[101 * 2048:102 * 2048] = array("H", [2000]) * 2048
    for row in range(2048):
        samples[row * 2048 + 600] = 3000
    terrain = height_maps.Terrain.__new__(height_maps.Terrain)
    terrain.__dict__.update(OneRegion(samples).__dict__)
    terrain.load = lambda names: None
    try:
        heights = navcore_heights(root)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        report(False, f"the terrain reader of navcore.cpp compiles and runs: {error}")
        heights = []
    names = {(100.5, 100.5): "control: a place in the middle of the region",
             (2047.5, 100.5): "the last half meter at the east edge, with no region to the east (F11)",
             (600.5, 2047.5): "the last half meter at the north edge, with no region to the north"}
    for x, y, height in heights:
        expected = height_maps.Terrain.height(terrain, x, y)
        report(expected is not None and abs(height - expected) < 0.001,
               f"{names[(x, y)]}: navcore {height:.4f} m, height_maps.py {expected:.4f} m")
    if len(heights) != 3:
        report(False, "the terrain reader of navcore.cpp gives three heights")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

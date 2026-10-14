#!/usr/bin/env python3
"""Check of the terrain height tool with made-up samples (no file of the game).

The terrain height tool (height_maps.py) has two readers of the same samples: `Terrain.height`
(the creature data tool uses it for the terrain height at each spawn point) and the files that
`region_file` writes (the backend reads them). The two must give one height for each place.
At the last half meter of a region with no region to its east they did not: `Terrain.height`
took a wrong sample for the diagonal one, and a spawn point there got a terrain height that was
some meters from the height of the file (code review of stage 3.4b, finding F9).

The check also makes sure that the tool takes the terrain after the cookies of the navigation
tool (a file REGION.u16 of its work folder) when that file is there.

Usage: check_heights_tool.py WORK_FOLDER
Status 0: each case has the expected result. The test program of the backend runs it (case RF10).
"""

from __future__ import annotations

import sys
from array import array
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import height_maps as tool  # noqa: E402

SIZE = tool.REGION_SIZE


class MadeUpTerrain(tool.Terrain):
    """One region with no neighbor: each row has one sample value, and the value changes at some rows and columns."""

    def __init__(self) -> None:
        self.names = {"r_+00_+00"}
        samples = array("H", [1000]) * (SIZE * SIZE)
        samples[101 * SIZE:102 * SIZE] = array("H", [2000]) * SIZE          # one row higher: a step from y 100 to y 101
        for row in range(300, 400):
            samples[row * SIZE + SIZE - 1] = 3000                           # the last column higher: a step at the east edge
        samples[(SIZE - 1) * SIZE + 500:(SIZE - 1) * SIZE + 600] = array("H", [4000]) * 100   # the last row higher
        self.regions = {"r_+00_+00": samples}
        self.with_cookies = set()

    def load(self, names) -> None:
        pass


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.split("Usage:")[1], file=sys.stderr)
        return 1
    folder = Path(sys.argv[1]) / "heights-tool-check"
    folder.mkdir(parents=True, exist_ok=True)
    terrain = MadeUpTerrain()
    (folder / "r_+00_+00.swh").write_bytes(tool.region_file(terrain, "r_+00_+00"))
    failures = 0
    places = (("a place in the middle of the region", 2046.5, 100.5),
              ("the last half meter at the east edge, with no region to the east", 2047.5, 100.5),
              ("the east edge at a step of the last column", 2047.75, 299.5),
              ("the last half meter at the north edge, with no region to the north", 550.5, 2047.5),
              ("the north-east corner of the region", 2047.5, 2047.5))
    for label, x, y in places:
        direct = terrain.height(x, y)
        from_file = tool.height_from_files(folder, x, y)
        correct = direct is not None and from_file is not None and abs(direct - from_file) < 0.001
        print(f"{'ok  ' if correct else 'FAIL'} {label}: the height for a spawn point {direct:.6f}, the height of the file "
              f"{from_file:.6f}")
        failures += 0 if correct else 1
    # The terrain after the cookies: the tool takes the samples of the file of the navigation tool.
    changed = folder / "changed"
    changed.mkdir(exist_ok=True)
    (changed / "r_+00_+00.u16").write_bytes((array("H", [5000]) * (SIZE * SIZE)).tobytes())

    class NoSources:
        entries: dict = {}

        @staticmethod
        def paths(prefix: str, suffix: str) -> list[str]:
            return [tool.REGIONS_FOLDER + "r_+00_+00/" + tool.HEIGHTMAP_NAME]

    with_cookies = tool.Terrain(NoSources(), 1, changed)
    height = with_cookies.height(100.5, 100.5)
    correct = height is not None and abs(height - 5000 * tool.HEIGHT_RANGE / 65535.0) < 0.001 and "r_+00_+00" in with_cookies.with_cookies
    print(f"{'ok  ' if correct else 'FAIL'} a region with a file of the terrain after the cookies has the samples of that file")
    failures += 0 if correct else 1
    for file in (folder / "r_+00_+00.swh", changed / "r_+00_+00.u16"):
        file.unlink()
    changed.rmdir()
    folder.rmdir()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

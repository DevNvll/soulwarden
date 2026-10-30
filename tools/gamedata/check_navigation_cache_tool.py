#!/usr/bin/env python3
"""Check of the region cache of the navigation tool (navigation_data.py; code review of stage DG2, finding DG2R-R4), with made-up files.

An output folder of the navigation tool keeps its complete regions (navigation.json: the regions with their bytes) for a later run. The own
levels and the main world have regions with one name (r_+00_+00): a complete region of an output of level A is not a region of level B or
of the main world. The tool must make each region again when the output is of a different level or world, and it keeps the regions of an
output of the same level (control).

The check runs the function make of the tool with a made-up world of one region; the work of step 2 (the collision data of the regions to
make) stops at once: the check reads only the number of regions that the tool takes as complete ("N complete already").
The test program of the backend runs it (case DG2R4).

Usage: check_navigation_cache_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import re
import shutil
import sys
import tempfile
from argparse import Namespace
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import navigation_data as navigation  # noqa: E402

REGION = "r_+00_+00"


class Stop(Exception):
    """The made-up stop of step 2 (the tool wants to make a region)."""


class MadeUpWorld:
    def __init__(self, *arguments) -> None:
        pass

    def regions(self) -> list[str]:
        return [REGION]


class NoPool:
    def __init__(self, *arguments, **keywords) -> None:
        raise Stop()


def complete_already(work: Path, cached: str | None, run: str | None) -> int:
    """@return The regions that a run for `run` (a level; None: the main world) takes as complete in an output of `cached`."""
    output = work / "output"
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    region = output / f"{REGION}.swnav"
    region.write_bytes(b"N" * 128)
    manifest = {"format": navigation.nav_file.VERSION, "tool": navigation.TOOL_VERSION, "agentVariables": navigation.agent_environment(),
                "regions": {REGION: {"bytes": 128}}}
    if cached is not None:
        manifest.update({"level": cached, "world": cached})
    (output / navigation.MANIFEST).write_text(json.dumps(manifest))
    program = work / "navcore"
    program.write_bytes(b"program")
    (work / "noise.f32").write_bytes(b"noise")
    arguments = Namespace(region=None, jobs=1, force=False, keep_raw=False, no_heights=True)
    text = io.StringIO()
    with patch.object(navigation, "build_program", return_value=program), patch.object(navigation.collision_data, "World", MadeUpWorld), \
            patch.object(navigation, "ProcessPoolExecutor", NoPool), contextlib.redirect_stdout(text):
        try:
            navigation.make(arguments, output, work, run, run or navigation.collision_data.DEFAULT_WORLD)
        except Stop:
            pass
        except Exception as error:  # noqa: BLE001 - a later step of a made-up run: the count is printed before it
            print(f"navigation: stopped ({type(error).__name__})")
    found = re.search(r"(\d+) complete already", text.getvalue())
    return int(found.group(1)) if found else -1


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="navigation-cache-check-"))
    work.mkdir(parents=True, exist_ok=True)
    failures = 0

    def report(correct: bool, what: str) -> None:
        nonlocal failures
        print(("ok   " if correct else "FAIL ") + what)
        failures += 0 if correct else 1

    same = complete_already(work, "nw_check_a_00", "nw_check_a_00")
    report(same == 1, f"control: a run of level A keeps the complete region of an output of level A ({same} complete)")
    other = complete_already(work, "nw_check_a_00", "nw_check_b_00")
    report(other == 0, f"a run of level B makes the region of an output of level A again ({other} complete) (DG2R-R4)")
    main_world = complete_already(work, "nw_check_a_00", None)
    report(main_world == 0, f"a run of the main world makes the region of an output of level A again ({main_world} complete) (DG2R-R4)")
    level = complete_already(work, None, "nw_check_b_00")
    report(level == 0, f"a run of level B makes the region of an output of the main world again ({level} complete) (DG2R-R4)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

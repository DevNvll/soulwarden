#!/usr/bin/env python3
"""Check of the travel places tool with made-up region files (no file of the game).

The travel places tool (travel_places.py) must write no file when a spirit shrine or an inn of
the region files has a position with a wrong form: no position field, a field that does not
have 12 bytes, or a number that is not finite. The tool left such a place out, wrote the other
places, and gave status 0: the file looked complete, and the list of the backend had a place
less with no sign of the fault (code review of stage 3.4c, finding C6). A landmark of a type
that is not a place for a character is not a fault: the tool leaves it out.

This check makes the territory table, the text file, and the landmarks in memory, gives them to
the entry point of the tool with a source provider that reads no game copy, and compares the
status and the output file of each case. The test program of the backend runs it (case TV9).

Usage: check_places_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import travel_places as tool  # noqa: E402
from check_combat_tool import sheet  # noqa: E402

SHRINE, INN, STORAGE = 27, 20, 5
# Ids of the made-up territory table: a territory, and two points of interest in it.
LAND, SHRINE_POINT, INN_POINT = 1, 2, 3


def node(name: int, value: bytes | None = None, children: list | None = None) -> SimpleNamespace:
    """@return One element of an object stream, as the tool reads it."""
    return SimpleNamespace(name=name, value=value, children=children or [])


def landmark(kind: int, point: int, position: bytes | None) -> SimpleNamespace:
    """@return One territory landmark. With no position: the landmark has no position field."""
    fields = [node(tool.TYPE_FIELD, kind.to_bytes(4, "big")),
              node(tool.IDS_FIELD, children=[node(0, LAND.to_bytes(4, "big")), node(0, point.to_bytes(4, "big"))])]
    if position is not None:
        fields.append(node(tool.POSITION_FIELD, position))
    return node(0, children=fields)


class MadeUpSources:
    """The source provider of the tool, with made-up files. `mode` selects the fault of one landmark."""

    mode = "valid"

    def __init__(self, assets, wanted=None) -> None:
        self.used = {"made-up files": 4}
        self.files = {
            tool.TABLES_FOLDER + "territory.datasheet": sheet("Territories", tool.DEFINITION_TYPE, [
                {"TerritoryID": LAND, "IsArea": 0, "IsPOI": 0, "NameLocalizationKey": "@land"},
                {"TerritoryID": SHRINE_POINT, "IsArea": 0, "IsPOI": 1, "NameLocalizationKey": "@shrine"},
                {"TerritoryID": INN_POINT, "IsArea": 0, "IsPOI": 1, "NameLocalizationKey": "@town"},
            ]),
            tool.TEXTS_FOLDER + "made-up.loc.xml": (b'<strings><string key="land">Made-up Land</string>'
                                                     b'<string key="shrine">Made-up Shrine</string>'
                                                     b'<string key="town">Nowhere</string></strings>'),
            tool.REGIONS_FOLDER + "r_+00_+00/region.metadata": b"made-up",
            tool.REGIONS_FOLDER + "r_+01_+00/region.metadata": b"made-up",
        }

    def paths(self, prefix, suffix):
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def read(self, path):
        return self.files[path]

    def stream(self, path):
        """@return The landmarks of one region. The second region has the landmark with the fault."""
        if "r_+00_" in path:
            # A correct shrine, and a landmark of a type that is not a place: it has no position.
            return [node(tool.LANDMARKS_FIELD, children=[landmark(SHRINE, SHRINE_POINT, struct.pack(">fff", 1000.0, 1000.0, 50.0)),
                                                        landmark(STORAGE, SHRINE_POINT, None)])]
        mode = self.mode
        shrine = struct.pack(">fff", 3000.0, 1000.0, 80.0)
        inn = struct.pack(">fff", 3010.0, 1020.0, 81.5)
        if mode == "shrine-short-position":
            shrine = shrine[:-1]
        if mode == "shrine-no-position":
            shrine = None
        if mode == "shrine-not-finite":
            shrine = struct.pack(">fff", 3000.0, float("nan"), 80.0)
        if mode == "inn-long-position":
            inn = inn + b"\0"
        if mode == "inn-no-position":
            inn = None
        return [node(tool.LANDMARKS_FIELD, children=[landmark(SHRINE, SHRINE_POINT, shrine), landmark(INN, INN_POINT, inn)])]

    def check_complete(self) -> None:
        pass


# Each case: the fault of the landmarks, and True when the tool must write its file.
CASES = [
    ("valid", True, "complete landmarks (two shrines, one inn, and a landmark of a different type with no position)"),
    ("shrine-short-position", False, "a shrine whose position has 11 bytes"),
    ("shrine-no-position", False, "a shrine with no position"),
    ("shrine-not-finite", False, "a shrine whose position has a number that is not finite"),
    ("inn-long-position", False, "an inn whose position has 13 bytes"),
    ("inn-no-position", False, "an inn with no position"),
]


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-places-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    failures = 0
    for mode, writes, what in CASES:
        MadeUpSources.mode = mode
        target = work / f"places-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["travel_places.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        exists = target.exists()
        count = len(json.loads(target.read_text(encoding="utf-8"))["places"]) if exists else 0
        correct = (status == 0 and exists) if writes else (status == 1 and not exists)
        if correct and mode == "valid":
            places = json.loads(target.read_text(encoding="utf-8"))["places"]
            correct = (sorted(place["id"] for place in places) == [f"inn_{INN_POINT}", f"shrine_{SHRINE_POINT}", f"shrine_{SHRINE_POINT}_2"]
                       and {place["territory"] for place in places} == {"Made-up Land"}
                       and [place["position"] for place in places if place["kind"] == "inn"] == [[3010.0, 1020.0, 81.5]])
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        message = message.replace(tool.REGIONS_FOLDER, "")
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, "
              + (f"a file with {count} places" if exists else "no file") + (f" ({message})" if not writes and message else ""))
        failures += not correct
        target.unlink(missing_ok=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

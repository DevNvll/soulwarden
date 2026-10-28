#!/usr/bin/env python3
"""Check of the point of interest tool (stage 1.8c) with made-up sources (no file of the game).

The tool (pois.py) gives each shape of a point of interest the POI placement of its region for which the entries of the region metadata
agree (an entry that names the id is in the shape, an entry that does not name it is not; an entry near the border is not wrong). It leaves
out a shape with no such placement, and it writes no file for a shape file with a wrong form or when no shape gets an origin.

Stage QG1 (format 2): the tool also writes the bounds of each territory (objects/worldprisms/bounds/territory, the property TerritoryId)
and counts the entries of the region metadata outside the bounds of their territory.

This check makes the territory table, the shape files, the bounds file of the territory 7, the POI placements, and the entries in memory, gives them to the entry point of the
tool with a source provider that reads no game copy, and compares the status and the output file of each case. The test program of the
backend runs it (case QE-TOOL).

Usage: check_pois_tool.py [WORK_FOLDER]
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
import pois as tool  # noqa: E402
from check_combat_tool import sheet  # noqa: E402

LAND, AREA, FIRST, SECOND, LOST = 7, 9000, 40901, 40902, 40999
REGION = "r_+00_+00"


def node(name, value=None, children=None) -> SimpleNamespace:
    return SimpleNamespace(name=name, value=value, children=children or [])


def entry(x: float, y: float, ids: list[int]) -> SimpleNamespace:
    return node(0, children=[node(tool.IDS_FIELD, children=[node(0, value.to_bytes(4, "big")) for value in ids]),
                             node(tool.POSITION_FIELD, struct.pack(">fff", x, y, 10.0))])


def shape(points: list[tuple[float, float]], count: int | None = None) -> bytes:
    data = struct.pack("<II", 0, len(points) if count is None else count)
    for x, y in points:
        data += struct.pack("<fff", x, y, 0.0)
    return data + bytes(20)


SQUARE = [(-20.0, -20.0), (20.0, -20.0), (20.0, 20.0), (-20.0, 20.0)]
# Stage QG1: the bounds of the territory LAND (world coordinates) hold the entries of the check.
LAND_BOUNDS = [(0.0, 0.0), (1000.0, 0.0), (1000.0, 1000.0), (0.0, 1000.0)]


def bounds(points: list[tuple[float, float]], territory: str, tail: int = 16) -> bytes:
    """@return A bounds file of a territory: the points, the property list with TerritoryId, and the tail (16 bytes)."""
    data = struct.pack("<II", 0, len(points))
    for x, y in points:
        data += struct.pack("<fff", x, y, 0.0)
    name, value = b"TerritoryId", territory.encode()
    data += struct.pack("<I", 1) + struct.pack("<I", len(name)) + name + struct.pack("<I", len(value)) + value
    return data + bytes(tail)


class MadeUpSources:
    """The source provider of the tool with made-up files. `mode` selects the fault."""

    mode = "valid"

    def __init__(self, assets, wanted=None) -> None:
        self.used = {"made-up files": 5}
        plots = {"Capitals": [{"sliceName": "POIs/POI_CapitalPlot", "worldPosition": {"x": x, "y": y, "z": 10.0}}
                              for x, y in ((100.0, 100.0), (300.0, 100.0), (700.0, 700.0))]
                 + [{"sliceName": "Something/Else", "worldPosition": {"x": 500.0, "y": 100.0, "z": 10.0}}]}
        self.files = {
            tool.TABLES_FOLDER + "territory.datasheet": sheet("Territories", tool.DEFINITION_TYPE, [
                {"TerritoryID": LAND, "POITag": "", "IsPOI": 0, "IsArea": 0, "DiscoveryRadius": None, "NameLocalizationKey": "@land"},
                {"TerritoryID": FIRST, "POITag": "test_a", "IsPOI": 1, "IsArea": 0, "DiscoveryRadius": 150, "NameLocalizationKey": "@first"},
                {"TerritoryID": SECOND, "POITag": "test_b, extra", "IsPOI": 1, "IsArea": 0, "DiscoveryRadius": 100,
                 "NameLocalizationKey": "@second"},
                {"TerritoryID": LOST, "POITag": "lost", "IsPOI": 1, "IsArea": 0, "DiscoveryRadius": 100, "NameLocalizationKey": "@lost"},
            ]),
            f"{tool.SHAPES_FOLDER}{REGION}/poi_{FIRST}_first.vshapec": shape(SQUARE),
            f"{tool.SHAPES_FOLDER}{REGION}/poi_{SECOND}_second.vshapec": shape(SQUARE if self.mode != "short-shape" else SQUARE[:3],
                                                                                 None if self.mode != "short-shape" else 4),
            f"{tool.SHAPES_FOLDER}{REGION}/poi_{LOST}_lost.vshapec": shape(SQUARE),
            f"{tool.REGIONS_FOLDER}{REGION}/capitals/pois/pois.capitals.json": json.dumps(plots).encode(),
            f"{tool.REGIONS_FOLDER}{REGION}/region.metadata": b"made-up",
            f"{tool.BOUNDS_FOLDER}land.vshapec": bounds(LAND_BOUNDS if self.mode != "small-bounds" else SQUARE, f"{LAND}:@land",
                                                       16 if self.mode != "bounds-tail" else 12),
        }
        if self.mode == "no-bounds":
            del self.files[f"{tool.BOUNDS_FOLDER}land.vshapec"]

    def paths(self, prefix, suffix):
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def read(self, path):
        return self.files.get(path)

    def stream(self, path):
        entries = [entry(100.0 + dx, 100.0 + dy, [LAND, FIRST, AREA]) for dx in (-10.0, 0.0, 10.0) for dy in (-10.0, 0.0, 10.0)]
        entries += [entry(300.0 + dx, 100.0 + dy, [LAND, SECOND, AREA]) for dx in (-15.0, 5.0) for dy in (-5.0, 15.0)]
        entries += [entry(150.0, 100.0, [LAND, AREA]), entry(121.0, 100.0, [LAND, AREA]), entry(321.0, 101.0, [LAND, SECOND])]
        if self.mode == "no-origin":
            entries = [entry(900.0, 900.0, [LAND, FIRST]), entry(910.0, 900.0, [LAND, SECOND])]
        # The entry at 121, 100 is outside the first shape and does not name it; the entry at 321, 101 names the second shape 1 m outside
        # its border: within the tolerance of the border, not wrong.
        return [node(0, children=entries)]

    def check_complete(self) -> None:
        pass


CASES = [
    ("valid", True, "two shapes get their placement; a shape with no entry is left out; a near-border entry is not wrong"),
    ("short-shape", False, "a shape file whose size does not agree with its count of points"),
    ("no-origin", False, "no shape gets an origin"),
    ("small-bounds", True, "stage QG1: the bounds of the territory do not hold the entries: the count boundsWrong has them"),
    ("bounds-tail", False, "stage QG1: a bounds file whose tail after the properties is not 16 bytes"),
    ("no-bounds", False, "stage QG1: the game copy has no bounds file of a territory"),
]


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-pois-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    failures = 0
    for mode, writes, what in CASES:
        MadeUpSources.mode = mode
        target = work / f"pois-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["pois.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        exists = target.exists()
        correct = (status == 0 and exists) if writes else (status == 1 and not exists)
        detail = ""
        if correct and mode == "valid":
            document = json.loads(target.read_text(encoding="utf-8"))
            found = {poi["id"]: poi for poi in document["pois"]}
            correct = (sorted(found) == [FIRST, SECOND] and found[FIRST]["center"][:2] == [100.0, 100.0]
                       and found[SECOND]["center"][:2] == [300.0, 100.0] and found[SECOND]["tags"] == ["test_b", "extra"]
                       and found[FIRST]["territory"] == LAND and found[FIRST]["radius"] == 150.0 and found[SECOND]["wrong"] == 0
                       and found[FIRST]["shape"][0] == [80.0, 80.0] and [entry["id"] for entry in document["left"]] == [LOST]
                       and document["format"] == 2 and document["territories"] == [{"id": LAND, "name": "@land",
                                                                                     "shape": [list(point) for point in LAND_BOUNDS]}]
                       and document["counts"]["boundsEntries"] == 16 and document["counts"]["boundsWrong"] == 0)
            detail = (f"{len(found)} points of interest, left out {[entry['id'] for entry in document['left']]}, territories "
                      f"{[entry['id'] for entry in document['territories']]}, bounds check {document['counts'].get('boundsEntries')} entries "
                      f"{document['counts'].get('boundsWrong')} wrong")
        if correct and mode == "small-bounds":
            document = json.loads(target.read_text(encoding="utf-8"))
            correct = document["counts"]["boundsEntries"] == 16 and document["counts"]["boundsWrong"] == 16
            detail = f"bounds check {document['counts'].get('boundsEntries')} entries {document['counts'].get('boundsWrong')} wrong"
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, " + (f"a file ({detail})" if exists else "no file")
              + (f" ({message})" if not writes and message else ""))
        failures += not correct
        target.unlink(missing_ok=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

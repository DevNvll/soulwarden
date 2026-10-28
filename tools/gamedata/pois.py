#!/usr/bin/env python3
"""Write the points of interest of the world (stage 1.8c, docs/systems/quests.md) as one JSON file.

The backend reads GAMEDATA_DIR/poi-data.json at its start: the shape of each point of interest, for the quest tasks of a place
(TaskGoToPOITag, TaskGoToPOITagOnTerritory, TaskExitPOITag) and the location rule of a task (EnforceLocationRequirement). Without the file
the backend starts, and no quest task of a place progresses.

The sources (all in the pak files of the game copy):

  1. The rows of the territory tables (the table type TerritoryDefinition): TerritoryID, POITag (a list), IsPOI, IsArea, DiscoveryRadius,
     the name key.
  2. The shape of a point of interest: objects/worldprisms/pois/REGION/poi_ID_NAME.vshapec (version:u32 count:u32, then x y z f32 for each
     point, then a tail of 20 bytes). The points are relative to an origin, and the client does not turn them (research map.md 2.9).
  3. The origins: the placements of the layer "pois" of each region (regions/REGION/capitals/pois/pois.capitals.json, the slices
     POI_CapitalPlot). The data has no link from a shape to its placement.
  4. The check of each origin: the region metadata (regions/REGION/region.metadata, an object stream) has thousands of entries with a world
     position and the ids of the territory, the area, and the points of interest that hold it (TerritoryIds: a product of the build tools of
     the game). The origin of a shape is the placement of its region for which the shape holds the most entries that name its id and the
     fewest entries that do not (research map.md 2.9: 0 of 994 entries wrong for 40220, no rotation). A shape with no such placement is left
     out (the counts name it).

  5. Stage QG1 (format 2): the bounds of each territory: objects/worldprisms/bounds/territory/NAME.vshapec (the points in world meters, then
     a property list with TerritoryId "6:@Windsward", then a tail of 16 bytes). The backend finds the territory of a character with them
     (the territory at its position: the claim territory of the client, research map.md 2.6). The check: each entry of the region metadata
     whose ids have a territory (an id below 100) is in the bounds of that territory, or within BORDER_TOLERANCE of their border
     (counts boundsEntries and boundsWrong).

The rule of the shape has a live check (POI1 of stage 1.8c): the client sent 2297 for 40142 with its "charted" achievement 0.3 m from the
shape of this rule (the placement of 40142 as the origin), and 2297 with its "discovered" achievement at 150 m (DiscoveryRadius) from the
same placement.

Output: {"format": 1, "world": TEXT, "pois": [{"id", "name", "tags": [TAG, ...], "territory", "center": [X, Y, Z], "radius",
                                              "shape": [[X, Y], ...], "inside", "wrong"}, ...], "left": [{"id", "cause"}, ...],
         "territories": [{"id", "name", "shape": [[X, Y], ...]}, ...], "counts": {...}}
`territory` is the territory that holds the point of interest (the first id of the TerritoryIds of its entries: 6 for Windsward), `inside`
the entries of the check in the shape, `wrong` the entries on the wrong side of the border.

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when a
shape file has a wrong form (fewer than 3 points, a size that does not agree with its count, a number that is not finite), or when no shape
gets an origin. tools/gamedata/check_pois_tool.py is the check of these rules, with made-up sources.

Usage: pois.py [OUTPUT.json] [--region NAME]...
Default output: <SOULWARDEN_GAMEDATA_DIR>/poi-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import REGIONS_FOLDER, TABLES_FOLDER, WORLD, Sources, number, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "poi-data.json"
FORMAT = 2
SHAPES_FOLDER = "objects/worldprisms/pois/"
# Stage QG1: the bounds of the territories (one shape for each territory, world coordinates, the property TerritoryId).
BOUNDS_FOLDER = "objects/worldprisms/bounds/territory/"
BOUNDS_TAIL = 16
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, SHAPES_FOLDER, BOUNDS_FOLDER)
DEFINITION_TYPE = "TerritoryDefinition"
IDS_FIELD = field_id("TerritoryIds")
POSITION_FIELD = field_id("WorldPosition")
SHAPE_NAME = re.compile(r"poi_(\d+)(?:_[^/]*)?\.vshapec")
# A placement of the layer "pois" is an origin when its slice name has this text.
PLOT_SLICE = "poi_capitalplot"
# An origin needs this part of the entries of its id in its shape, and this largest part of wrong entries. An entry on the wrong side of
# the border and nearer to it than BORDER_TOLERANCE is not wrong: the membership of the build tools has a margin (the run of stage 1.8c:
# the wrong entries of the watchtower 40148 were 0.0 to 5.4 m from the border of the right origin; the live 2297 of 40142 came 1.5 m
# inside the border).
LEAST_INSIDE = 0.9
MOST_WRONG = 0.05
BORDER_TOLERANCE = 6.0
GRID = 64.0
# The tail of a shape file after its points (each of the 761 shape files of the supported build has 20 bytes: 16 zero bytes and the height
# of the prism, 180.0).
SHAPE_TAIL = 20


def read_shape(data: bytes, path: str) -> list[tuple[float, float]]:
    """@return The points (x, y) of a shape file. A wrong form stops the tool."""
    if len(data) < 8:
        raise Failure(f"{path}: the shape file is shorter than its header")
    _, count = struct.unpack_from("<II", data, 0)
    if count < 3 or len(data) != 8 + 12 * count + SHAPE_TAIL:
        raise Failure(f"{path}: the shape file has {count} points and {len(data)} bytes (3 points or more, 12 bytes each, and a tail of "
                      f"{SHAPE_TAIL} bytes)")
    points = []
    for index in range(count):
        x, y, _ = struct.unpack_from("<fff", data, 8 + 12 * index)
        if not (math.isfinite(x) and math.isfinite(y)):
            raise Failure(f"{path}: point {index} of the shape is not finite")
        points.append((x, y))
    return points


def read_bounds(data: bytes, path: str) -> tuple[int, str, list[tuple[float, float]]]:
    """@return The territory id, its name key, and the points (x, y) of a bounds file of a territory (stage QG1). A wrong form stops the tool."""
    if len(data) < 8:
        raise Failure(f"{path}: the bounds file is shorter than its header")
    _, count = struct.unpack_from("<II", data, 0)
    offset = 8 + 12 * count
    if count < 3 or len(data) < offset + 4:
        raise Failure(f"{path}: the bounds file has {count} points and {len(data)} bytes")
    points = []
    for index in range(count):
        x, y, _ = struct.unpack_from("<fff", data, 8 + 12 * index)
        if not (math.isfinite(x) and math.isfinite(y)):
            raise Failure(f"{path}: point {index} of the bounds is not finite")
        points.append((x, y))
    properties = {}
    (entries,) = struct.unpack_from("<I", data, offset)
    offset += 4
    for _ in range(entries):
        values = []
        for _part in range(2):
            if len(data) < offset + 4:
                raise Failure(f"{path}: the property list of the bounds file is not complete")
            (size,) = struct.unpack_from("<I", data, offset)
            offset += 4
            if len(data) < offset + size:
                raise Failure(f"{path}: the property list of the bounds file is not complete")
            values.append(data[offset:offset + size].decode("utf-8", "replace"))
            offset += size
        properties[values[0]] = values[1]
    if len(data) != offset + BOUNDS_TAIL:
        raise Failure(f"{path}: the bounds file has {len(data) - offset} bytes after its properties, not {BOUNDS_TAIL}")
    match = re.fullmatch(r"(\d+):(.*)", properties.get("TerritoryId", "").strip())
    if match is None or not 0 < int(match[1]) <= 65535:
        raise Failure(f"{path}: the bounds file has no property TerritoryId with a territory id")
    return int(match[1]), match[2], points


def inside(x: float, y: float, points: list[tuple[float, float]]) -> bool:
    """The even-odd rule (the same rule as gamedata::Poi::contains of the backend)."""
    result = False
    previous = points[-1]
    for point in points:
        x1, y1 = point
        x2, y2 = previous
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            result = not result
        previous = point
    return result


def border_distance(x: float, y: float, points: list[tuple[float, float]]) -> float:
    """@return The distance of a point to the border of a shape."""
    best = math.inf
    previous = points[-1]
    for point in points:
        x1, y1 = previous
        x2, y2 = point
        dx, dy = x2 - x1, y2 - y1
        length = dx * dx + dy * dy
        t = 0.0 if length == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / length))
        best = min(best, math.hypot(x - (x1 + t * dx), y - (y1 + t * dy)))
        previous = point
    return best


def read_definitions(sources: Sources) -> dict[int, dict]:
    rows: dict[int, dict] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None or sheet_type(data) != DEFINITION_TYPE:
            continue
        try:
            sheet = Sheet(data)
            for row in range(sheet.row_count):
                text = sheet.source(row, "TerritoryID").strip()
                if not text.isdigit():
                    continue
                identifier = int(text)
                tags = [tag.strip() for tag in sheet.source(row, "POITag").split(",") if tag.strip()]
                radius = sheet.source(row, "DiscoveryRadius").strip()
                rows.setdefault(identifier, {"tags": tags, "name": sheet.source(row, "NameLocalizationKey").strip(),
                                             "radius": float(radius) if re.fullmatch(r"[0-9.]+", radius) else 0.0,
                                             "poi": sheet.source(row, "IsPOI").strip() in ("1", "true", "TRUE", "True"),
                                             "area": sheet.source(row, "IsArea").strip() in ("1", "true", "TRUE", "True")})
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    if not rows:
        raise Failure("the game copy has no territory table")
    return rows


def read_entries(sources: Sources, region: str) -> list[tuple[float, float, frozenset[int]]]:
    """@return Each entry of the region metadata with a world position and territory ids (the check of the origins)."""
    path = f"{REGIONS_FOLDER}{region}/region.metadata"
    roots = sources.stream(path)
    if roots is None:
        return []
    entries = []
    for root in roots:
        for element in walk(root):
            fields = {child.name: child for child in element.children}
            ids = fields.get(IDS_FIELD)
            position = fields.get(POSITION_FIELD)
            if ids is None or position is None or position.value is None or len(position.value) != 12:
                continue
            x, y, _ = struct.unpack(">fff", position.value)
            if math.isfinite(x) and math.isfinite(y):
                entries.append((x, y, frozenset(number(child) for child in ids.children)))
    return entries


def read_plots(sources: Sources, region: str) -> list[tuple[float, float, float]]:
    plots = []
    for path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/pois/", ".capitals.json"):
        data = sources.read(path)
        if data is None:
            continue
        try:
            document = json.loads(data)
        except ValueError as error:
            raise Failure(f"{path} is not JSON: {error}") from error
        for placement in document.get("Capitals") or []:
            if PLOT_SLICE not in str(placement.get("sliceName", "")).lower():
                continue
            position = placement.get("worldPosition") or {}
            try:
                plots.append((float(position["x"]), float(position["y"]), float(position["z"])))
            except (KeyError, TypeError, ValueError) as error:
                raise Failure(f"{path}: a POI placement has no world position") from error
    return plots


def origin_of(identifier: int, shape: list[tuple[float, float]], plots: list[tuple[float, float, float]],
              entries: list[tuple[float, float, frozenset[int]]], grid: dict) -> tuple[tuple[float, float, float], int, int] | None:
    """@return The origin of a shape (a placement), the entries in it, and the wrong entries; no value when no placement agrees."""
    positives = [(x, y) for x, y, ids in entries if identifier in ids]
    if not positives:
        return None
    low_x, high_x = min(x for x, _ in shape), max(x for x, _ in shape)
    low_y, high_y = min(y for _, y in shape), max(y for _, y in shape)
    best = None
    for plot in plots:
        # A cheap first test: the bounds of the placed shape hold most of the entries of the id.
        held = sum(1 for x, y in positives if low_x - BORDER_TOLERANCE <= x - plot[0] <= high_x + BORDER_TOLERANCE
                   and low_y - BORDER_TOLERANCE <= y - plot[1] <= high_y + BORDER_TOLERANCE)
        if held < LEAST_INSIDE * len(positives):
            continue
        placed = [(plot[0] + x, plot[1] + y) for x, y in shape]
        inner = 0
        wrong = 0
        for cell_x in range(int(math.floor((plot[0] + low_x) / GRID)), int(math.floor((plot[0] + high_x) / GRID)) + 1):
            for cell_y in range(int(math.floor((plot[1] + low_y) / GRID)), int(math.floor((plot[1] + high_y) / GRID)) + 1):
                for x, y, ids in grid.get((cell_x, cell_y), ()):
                    if inside(x, y, placed):
                        if identifier in ids:
                            inner += 1
                        elif border_distance(x, y, placed) > BORDER_TOLERANCE:
                            wrong += 1
        for x, y in positives:
            if not inside(x, y, placed):
                if border_distance(x, y, placed) > BORDER_TOLERANCE:
                    wrong += 1
                else:
                    inner += 1
        if inner == 0 or inner < LEAST_INSIDE * len(positives) or wrong > MOST_WRONG * (inner + wrong):
            continue
        if best is None or (inner - wrong) > (best[1] - best[2]):
            best = (plot, inner, wrong)
    return best


def build(sources: Sources, regions: list[str] | None) -> dict:
    definitions = read_definitions(sources)
    shapes: dict[str, list[tuple[int, str]]] = {}
    for path in sources.paths(SHAPES_FOLDER, ".vshapec"):
        parts = path[len(SHAPES_FOLDER):].split("/")
        if len(parts) != 2:
            continue
        match = SHAPE_NAME.fullmatch(parts[1].lower())
        if match is None:
            continue
        if regions and parts[0] not in regions:
            continue
        shapes.setdefault(parts[0], []).append((int(match[1]), path))
    pois = []
    left = []
    counts = Counter()
    territories = []
    for path in sources.paths(BOUNDS_FOLDER, ".vshapec"):
        data = sources.read(path)
        if data is None:
            continue
        identifier, name, points = read_bounds(data, path)
        if any(entry["id"] == identifier for entry in territories):
            raise Failure(f"{path}: a second bounds file of the territory {identifier}")
        territories.append({"id": identifier, "name": name, "shape": [[round(x, 4), round(y, 4)] for x, y in points]})
    counts["boundsEntries"] += 0
    counts["boundsWrong"] += 0
    for region in sorted(shapes):
        plots = read_plots(sources, region)
        entries = read_entries(sources, region)
        grid: dict = {}
        for entry in entries:
            grid.setdefault((int(math.floor(entry[0] / GRID)), int(math.floor(entry[1] / GRID))), []).append(entry)
        counts["regions"] += 1
        counts["entries"] += len(entries)
        # The check of the bounds (stage QG1): the territory of an entry (its id below 100) holds its position.
        for x, y, ids in entries:
            owned = [identifier for identifier in ids if identifier < 100]
            bounds = [entry for entry in territories if entry["id"] in owned]
            if len(owned) != 1 or not bounds:
                continue
            counts["boundsEntries"] += 1
            points = [(px, py) for px, py in bounds[0]["shape"]]
            if not inside(x, y, points) and border_distance(x, y, points) > BORDER_TOLERANCE:
                counts["boundsWrong"] += 1
        for identifier, path in sorted(shapes[region]):
            data = sources.read(path)
            if data is None:
                continue
            shape = read_shape(data, path)
            counts["shapes"] += 1
            row = definitions.get(identifier)
            if row is None:
                left.append({"id": identifier, "cause": "no row of the territory tables"})
                continue
            found = origin_of(identifier, shape, plots, entries, grid)
            if found is None:
                left.append({"id": identifier, "cause": "no POI placement of the region agrees with the entries of the region metadata"})
                continue
            plot, inner, wrong = found
            territory = Counter(min(ids) for x, y, ids in entries if identifier in ids and ids and min(ids) < 100)
            pois.append({"id": identifier, "name": row["name"], "tags": row["tags"],
                         "territory": territory.most_common(1)[0][0] if territory else 0,
                         "center": [round(plot[0], 4), round(plot[1], 4), round(plot[2], 4)], "radius": row["radius"],
                         "shape": [[round(plot[0] + x, 4), round(plot[1] + y, 4)] for x, y in shape], "inside": inner,
                         "wrong": wrong})
    sources.check_complete()
    if not pois:
        raise Failure("no shape of a point of interest got an origin")
    if not territories:
        raise Failure(f"the game copy has no bounds file of a territory ({BOUNDS_FOLDER})")
    counts["pois"] = len(pois)
    counts["left"] = len(left)
    counts["territories"] = len(territories)
    return {"format": FORMAT, "world": WORLD, "pois": sorted(pois, key=lambda poi: poi["id"]),
            "left": sorted(left, key=lambda entry: entry["id"]), "territories": sorted(territories, key=lambda entry: entry["id"]),
            "counts": dict(counts)}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the points of interest of the world (one JSON file).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="only these regions (a file for a test)")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Sources(game / "assets", WANTED), arguments.region)
        text = json.dumps(document, separators=(",", ":"), allow_nan=False)
        partial = target.with_name(target.name + ".partial")
        partial.write_text(text, encoding="utf-8")
        partial.replace(target)
    except Failure as error:
        print(f"pois: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"pois: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['pois']} points of interest with a shape of {counts['shapes']} shape files in {counts['regions']} regions "
          f"({counts['entries']} entries of the region metadata as the check); {counts['left']} shapes left out; the bounds of "
          f"{counts['territories']} territories ({counts.get('boundsEntries', 0)} entries of the region metadata as the check, "
          f"{counts.get('boundsWrong', 0)} outside the bounds of their territory)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

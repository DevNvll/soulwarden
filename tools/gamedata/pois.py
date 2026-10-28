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

  6. Stage QG3 (the format stays 2: the new members are additions that the loaders of 1.8c and QG1 do not read), the places that the
     tool of 1.8c did not find:
     - The origin of a POI shape can be a POI or an area plot (the slices POI_CapitalPlot and Area_CapitalPlot) of each layer and each
       region of the world, not only of the layer "pois" of the folder of the shape (research quest-gaps.md section 1: 40300
       Everfall_Settlement_01 has its shape in r_+04_+03 and its entries in r_+04_+02). The check uses the entries of each region. The
       fit rule does not change (LEAST_INSIDE, MOST_WRONG, BORDER_TOLERANCE).
     - The area shapes: objects/worldprisms/areas/FOLDER/NAME.vshapec (the form of a POI shape). Their points are world meters: the origin
       is zero (Great Cleave area 5 spans x 8625 to 9416). The data has no id in the file; the name of the file only resembles the name
       key of the row (@ui_area_northwindsward_7 and northwindsward/area7, but monarchsbluffs/area1 and @ui_area_monarchsbluff_1). The
       id of an area shape is each area row (IsArea) whose entries agree with the shape at the origin zero, by the same fit rule. The
       counts give the agreement with the name key (areasByName, areasNotByName).
     - Each point of interest keeps its source (the shape file), its origin (the placement: the capitals file and the row, or the world
       origin of an area), and its world.
     - `unshaped` lists each tag of the territory tables that no point of interest of the file has, with the cause for each id of the tag.
       `tasks` (with the quest data) compares the POITag of the quest tasks of a place and of the location rule with the tags of the file.

The rule of the shape has a live check (POI1 of stage 1.8c): the client sent 2297 for 40142 with its "charted" achievement 0.3 m from the
shape of this rule (the placement of 40142 as the origin), and 2297 with its "discovered" achievement at 150 m (DiscoveryRadius) from the
same placement.

Output: {"format": 2, "world": TEXT,
         "pois": [{"id", "name", "tags": [TAG, ...], "territory", "center": [X, Y, Z], "radius", "shape": [[X, Y], ...], "inside",
                   "wrong", "kind": "poi" or "area", "source": PATH, "origin": {"capitals": PATH, "row": N} or {"world": true},
                   "candidates", "world": TEXT}, ...],
         "left": [{"id", "source", "cause"}, ...], "unshaped": [{"tag", "ids": [ID, ...], "cause"}, ...],
         "territories": [{"id", "name", "shape": [[X, Y], ...]}, ...], "tasks": {"tags", "found", "missing": [{"tag", "cause"}, ...]},
         "counts": {...}}
`territory` is the territory that holds the point of interest (the first id of the TerritoryIds of its entries: 6 for Windsward), `inside`
the entries of the check in the shape, `wrong` the entries on the wrong side of the border, `candidates` the count of the origins (or of
the area files) that pass the fit rule (the best one is the origin).

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when a
shape file has a wrong form (fewer than 3 points, a size that does not agree with its count, a number that is not finite), or when no shape
gets an origin. tools/gamedata/check_pois_tool.py is the check of these rules, with made-up sources.

Usage: pois.py [OUTPUT.json] [--region NAME]... [--quests QUEST_DATA.json]
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
# Stage QG3: the area shapes (world coordinates; research quest-gaps.md section 1, map.md 2.9).
AREAS_FOLDER = "objects/worldprisms/areas/"
# Stage QG1: the bounds of the territories (one shape for each territory, world coordinates, the property TerritoryId).
BOUNDS_FOLDER = "objects/worldprisms/bounds/territory/"
BOUNDS_TAIL = 16
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, SHAPES_FOLDER, AREAS_FOLDER, BOUNDS_FOLDER)
DEFINITION_TYPE = "TerritoryDefinition"
IDS_FIELD = field_id("TerritoryIds")
POSITION_FIELD = field_id("WorldPosition")
# poi_ID_NAME.vshapec; stage QG3: also poi_NAME_ID.vshapec (poi_ancient_observatory_20423.vshapec), the id at the end.
SHAPE_NAME = re.compile(r"poi_(?:(\d+)(?:_[^/]*)?|[^/]*_(\d+))\.vshapec")
AREA_KEY = re.compile(r"@ui_area_([a-z]+)_(\d+)")
# A placement of a capitals file is an origin when its slice name has one of these texts (stage QG3: each layer of each region).
PLOT_SLICES = ("poi_capitalplot", "area_capitalplot")
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


def read_plots(sources: Sources) -> list[tuple[float, float, float, str, int]]:
    """@return Each POI and area plot of each layer of each region of the world: x, y, z, the capitals file (below the regions folder),
    and the row (stage QG3; the tool of 1.8c read only the layer "pois" of the folder of a shape)."""
    plots = []
    for path in sources.paths(REGIONS_FOLDER, ".capitals.json"):
        data = sources.read(path)
        if data is None:
            continue
        try:
            document = json.loads(data)
        except ValueError as error:
            raise Failure(f"{path} is not JSON: {error}") from error
        for row, placement in enumerate(document.get("Capitals") or []):
            name = str(placement.get("sliceName", "")).lower()
            if not any(plot in name for plot in PLOT_SLICES):
                continue
            position = placement.get("worldPosition") or {}
            try:
                plots.append((float(position["x"]), float(position["y"]), float(position["z"]), path[len(REGIONS_FOLDER):], row))
            except (KeyError, TypeError, ValueError) as error:
                raise Failure(f"{path}: a POI placement has no world position") from error
    return plots


def origin_of(identifier: int, shape: list[tuple[float, float]], plots: list[tuple], positives: list[tuple[float, float]],
              grid: dict, home: str = "") -> tuple[tuple, int, int, int] | None:
    """@return The origin of a shape (a placement), the entries in it, the wrong entries, and the count of the placements that agree; no
    value when no placement agrees. `positives`: the entries of the id (each region). Of two placements with the same count (inner - wrong;
    two plots a few meters apart), the plot of the layer "pois" of `home` (the region of the shape folder: the rule of stage 1.8c) comes
    first, then a plot of that region, then the first in the sequence of the files."""
    if not positives:
        return None
    low_x, high_x = min(x for x, _ in shape), max(x for x, _ in shape)
    low_y, high_y = min(y for _, y in shape), max(y for _, y in shape)
    first_x, last_x = min(x for x, _ in positives), max(x for x, _ in positives)
    first_y, last_y = min(y for _, y in positives), max(y for _, y in positives)
    best = None
    agree = 0
    for plot in plots:
        # Stage QG3: a placement whose placed bounds do not touch the bounds of the entries holds none of them (the same result as the
        # test below, with no count).
        if (plot[0] + low_x - BORDER_TOLERANCE > last_x or plot[0] + high_x + BORDER_TOLERANCE < first_x
                or plot[1] + low_y - BORDER_TOLERANCE > last_y or plot[1] + high_y + BORDER_TOLERANCE < first_y):
            continue
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
        agree += 1
        rank = (inner - wrong, plot[3].startswith(f"{home}/capitals/pois/") if home else False,
                plot[3].startswith(f"{home}/") if home else False)
        if best is None or rank > best[3]:
            best = (plot, inner, wrong, rank)
    return (*best[:3], agree) if best is not None else None


def entry_of(identifier: int, row: dict, territory: int, plot: tuple, inner: int, wrong: int, agree: int, shape: list[tuple[float, float]],
             kind: str, source: str) -> dict:
    """@return One point of interest of the output (stage QG3: with its kind, its source, its origin, and its world)."""
    origin = {"world": True} if kind == "area" else {"capitals": plot[3], "row": plot[4]}
    return {"id": identifier, "name": row["name"], "tags": row["tags"], "territory": territory,
            "center": [round(plot[0], 4), round(plot[1], 4), round(plot[2], 4)], "radius": row["radius"],
            "shape": [[round(plot[0] + x, 4), round(plot[1] + y, 4)] for x, y in shape], "inside": inner, "wrong": wrong,
            "kind": kind, "source": source, "origin": origin, "candidates": agree, "world": WORLD}


def area_files(sources: Sources) -> list[str]:
    return [path for path in sources.paths(AREAS_FOLDER, ".vshapec") if len(path[len(AREAS_FOLDER):].split("/")) == 2]


def unshaped_tags(definitions: dict[int, dict], pois: list[dict], left: list[dict]) -> list[dict]:
    """@return Each tag of the territory tables that no point of interest of the output has, with the cause for each id (stage QG3)."""
    shaped = {tag.lower() for poi in pois for tag in poi["tags"]}
    causes = {}
    for entry in left:
        causes.setdefault(entry["id"], []).append(entry["cause"])
    found = {poi["id"] for poi in pois}
    result: dict[str, dict] = {}
    for identifier, row in sorted(definitions.items()):
        for tag in row["tags"]:
            key = tag.lower()
            if key in shaped:
                continue
            if identifier in found:
                continue
            if identifier in causes:
                cause = "; ".join(sorted(set(causes[identifier])))
            elif row["area"]:
                cause = "no area shape file agrees with the entries of the id at the origin zero" if row["positives"] else \
                    "an area row with no entry of the region metadata (no area shape can be checked)"
            elif row["positives"]:
                cause = "no shape file of the id (objects/worldprisms/pois/REGION/poi_ID_*.vshapec)"
            else:
                cause = "no shape file of the id and no entry of the region metadata"
            item = result.setdefault(key, {"tag": key, "ids": [], "causes": []})
            item["ids"].append(identifier)
            item["causes"].append(f"{identifier}: {cause}")
    return [{"tag": item["tag"], "ids": item["ids"], "cause": "; ".join(item["causes"])} for _, item in sorted(result.items())]


def build(sources: Sources, regions: list[str] | None, task_tags: set[str] | None = None) -> dict:
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
        shapes.setdefault(parts[0], []).append((int(match[1] or match[2]), path))
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
    # Stage QG3: the plots of each layer of each region, and the entries of each region (a shape can have its origin and its entries in
    # another region than its folder: 40300). `--region` limits the shape folders, not these sources.
    plots = read_plots(sources)
    entries: list[tuple[float, float, frozenset[int]]] = []
    for path in sources.paths(REGIONS_FOLDER, "/region.metadata"):
        region = path[len(REGIONS_FOLDER):].split("/")[0]
        found_entries = read_entries(sources, region)
        if found_entries:
            counts["regions"] += 1
        entries.extend(found_entries)
    counts["entries"] = len(entries)
    counts["plots"] = len(plots)
    grid: dict = {}
    positives: dict[int, list[tuple[float, float]]] = {}
    owners: dict[int, Counter] = {}
    for entry in entries:
        grid.setdefault((int(math.floor(entry[0] / GRID)), int(math.floor(entry[1] / GRID))), []).append(entry)
        territory = min(entry[2]) if entry[2] else 0
        for identifier in entry[2]:
            positives.setdefault(identifier, []).append((entry[0], entry[1]))
            if 0 < territory < 100:
                owners.setdefault(identifier, Counter())[territory] += 1
    for identifier, row in definitions.items():
        row["positives"] = len(positives.get(identifier, ()))
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

    def owner(identifier: int) -> int:
        found = owners.get(identifier)
        return found.most_common(1)[0][0] if found else 0

    for region in sorted(shapes):
        counts["shapeRegions"] += 1
        for identifier, path in sorted(shapes[region]):
            data = sources.read(path)
            if data is None:
                continue
            shape = read_shape(data, path)
            counts["shapes"] += 1
            row = definitions.get(identifier)
            if row is None:
                left.append({"id": identifier, "source": path, "cause": "no row of the territory tables"})
                continue
            if not positives.get(identifier):
                left.append({"id": identifier, "source": path, "cause": "no entry of the region metadata names the id"})
                continue
            found = origin_of(identifier, shape, plots, positives[identifier], grid, region)
            if found is None:
                left.append({"id": identifier, "source": path,
                             "cause": "no POI or area plot of the world agrees with the entries of the region metadata"})
                continue
            plot, inner, wrong, agree = found
            if not plot[3].startswith(f"{region}/capitals/pois/"):
                counts["poisWithAnOtherPlot"] += 1  # stage QG3: the origin is in another layer or region than the folder of the shape
            pois.append(entry_of(identifier, row, owner(identifier), plot, inner, wrong, agree, shape, "poi", path))
    # Stage QG3: the area shapes (world coordinates). The id of a file is each area row whose entries agree at the origin zero.
    zero = (0.0, 0.0, 0.0, "", -1)
    areas: dict[int, list[tuple[int, int, int, int, str, list]]] = {}
    for path in area_files(sources) if not regions else []:
        data = sources.read(path)
        if data is None:
            continue
        shape = read_shape(data, path)
        counts["areaShapes"] += 1
        low_x, high_x = min(x for x, _ in shape), max(x for x, _ in shape)
        low_y, high_y = min(y for _, y in shape), max(y for _, y in shape)
        candidates = set()
        for cell_x in range(int(math.floor(low_x / GRID)), int(math.floor(high_x / GRID)) + 1):
            for cell_y in range(int(math.floor(low_y / GRID)), int(math.floor(high_y / GRID)) + 1):
                for _x, _y, ids in grid.get((cell_x, cell_y), ()):
                    candidates.update(identifier for identifier in ids if definitions.get(identifier, {}).get("area"))
        fitted = []
        for identifier in sorted(candidates):
            found = origin_of(identifier, shape, [zero], positives[identifier], grid)
            if found is not None:
                fitted.append((identifier, found[1], found[2]))
        if not fitted:
            left.append({"id": 0, "source": path, "cause": "no area row agrees with the entries of the region metadata at the origin zero"})
            continue
        for identifier, inner, wrong in fitted:
            areas.setdefault(identifier, []).append((inner - wrong, inner, wrong, len(fitted), path, shape))
    for identifier, files in sorted(areas.items()):
        files.sort(key=lambda item: (-item[0], item[4]))
        _, inner, wrong, agree, path, shape = files[0]
        for other in files[1:]:
            left.append({"id": identifier, "source": other[4], "cause": f"a second area shape of the id ({path} agrees better)"})
        row = definitions[identifier]
        match = AREA_KEY.fullmatch(row["name"].lower())
        named = match is not None and path == f"{AREAS_FOLDER}{match[1]}/area{int(match[2])}.vshapec"
        counts["areasByName" if named else "areasNotByName"] += 1
        pois.append(entry_of(identifier, row, owner(identifier), zero, inner, wrong, agree, shape, "area", path))
    sources.check_complete()
    if not pois:
        raise Failure("no shape of a point of interest got an origin")
    if not territories:
        raise Failure(f"the game copy has no bounds file of a territory ({BOUNDS_FOLDER})")
    ids = Counter(poi["id"] for poi in pois)
    for identifier, number_of in ids.items():
        if number_of > 1:
            # Stage QG3: two POI shape files of one id (or a POI and an area file): the best fit stays, the others are left out.
            same = sorted((poi for poi in pois if poi["id"] == identifier), key=lambda poi: (poi["wrong"] - poi["inside"], poi["source"]))
            for poi in same[1:]:
                left.append({"id": identifier, "source": poi["source"], "cause": f"a second shape of the id ({same[0]['source']} agrees better)"})
                pois.remove(poi)
    unshaped = unshaped_tags(definitions, pois, left)
    counts["pois"] = len(pois)
    counts["areas"] = sum(1 for poi in pois if poi["kind"] == "area")
    counts["left"] = len(left)
    counts["territories"] = len(territories)
    counts["unshapedTags"] = len(unshaped)
    document = {"format": FORMAT, "world": WORLD, "pois": sorted(pois, key=lambda poi: poi["id"]),
                "left": sorted(left, key=lambda entry: (entry["id"], entry["source"])), "unshaped": unshaped,
                "territories": sorted(territories, key=lambda entry: entry["id"]), "counts": dict(counts)}
    if task_tags is not None:
        shaped = {tag.lower() for poi in pois for tag in poi["tags"]}
        known = {item["tag"]: item["cause"] for item in unshaped}
        missing = [{"tag": tag, "cause": known.get(tag, "no row of the territory tables has the tag")} for tag in sorted(task_tags - shaped)]
        document["tasks"] = {"tags": len(task_tags), "found": len(task_tags & shaped), "missing": missing}
    return document


def quest_tags(path: Path) -> set[str] | None:
    """@return The lower-case POITag of each quest task of a place and of each task with the location rule (stage QG3), or None when the
    file is not there."""
    if not path.is_file():
        return None
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise Failure(f"{path} is not a quest data file: {error}") from error
    tags = set()
    places = ("taskgotopoitag", "taskgotopoitagonterritory", "taskexitpoitag")
    for task in document.get("tasks", {}).values():
        cells = task.get("cells", {})
        if task.get("type") in places or str(cells.get("EnforceLocationRequirement", "")).strip().lower() in ("1", "true"):
            tags.update(tag.strip().lower() for tag in str(cells.get("POITag", "")).split(",") if tag.strip())
    return tags


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the points of interest of the world (one JSON file).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="only the POI shapes of these regions, and no area shape (a file for a test)")
    parser.add_argument("--quests", default=str(gamedata / "quest-data.json"), help="the quest data for the comparison of the tags")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Sources(game / "assets", WANTED), arguments.region, quest_tags(Path(arguments.quests)))
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
    tasks = document.get("tasks")
    print(f"{target}: {counts['pois']} points of interest with a shape ({counts.get('areas', 0)} areas) of {counts.get('shapes', 0)} POI "
          f"shape files and {counts.get('areaShapes', 0)} area shape files ({counts['entries']} entries of {counts.get('regions', 0)} regions "
          f"as the check, {counts.get('plots', 0)} plots); {counts['left']} shapes left out; {counts.get('unshapedTags', 0)} tags with no "
          f"shape; the bounds of {counts['territories']} territories ({counts.get('boundsEntries', 0)} entries of the region metadata as "
          f"the check, {counts.get('boundsWrong', 0)} outside the bounds of their territory)"
          + (f"; the quest data names {tasks['tags']} tags, {tasks['found']} with a shape" if tasks else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

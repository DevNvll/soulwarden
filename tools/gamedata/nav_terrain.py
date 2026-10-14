#!/usr/bin/env python3
"""Read the terrain height and the water of one region of the game.

Height map: `region.heightmap` is a TIFF file with 2048 x 2048 samples of 16 bits, LZW data
(compression 5) in strips, and the horizontal predictor (predictor 2). The sample of local
(x, y) is in row `2047 - y` and column `x`. The height in meters is sample * 2048 / 65535
(`mountainHeight` of terrain.json). `read_heightmap` gives the samples with row 0 = local y 0.

Water: `region.waterqt` is an object stream with `regionSize` and `quadtreeNodes`. A node has
`Height` (the water surface), `FloorHeight`, and `Flags`. The nodes are a quadtree of the
region in level order (the root first, then each level). A node with the height 65535 has
four child nodes. A node with the height 0 has no water. Each other node is a square of
water. `Flags` has four bytes with the least significant byte first: bit 0 gatherable, bit 1
fishable, bit 2 fresh water, bit 3 salt water (the names are from the client code). The
sequence of the four children is (x, y) = (0, 0), (1, 0), (0, 1), (1, 1) of the parent square
(protocol notes 4.19 have the proof).

Cookies: the height map is not the terrain of the client. A placement of a capitals file of the
region (capitals/LAYER/LAYER.capitals.json) can have a member `cookie`:
    {"innerRadius": 15.0, "outerRadius": 30.0, "maxHeightBlend": 0.4, "edgeNoise": 0.1,
     "edgeNoiseFeatureSize": 4.0, "groundNoiseScale": 0.35}
The client changes the samples of the height map with each cookie of the region, one time,
after it read the height map: the terrain near the placement goes to the height of the
placement, with noise at its edge and on its ground (client code 0x6A26A70 and 0x6A23AC0;
protocol notes 4.19 have the rule). `read_cookies` reads the cookies, and `apply_cookies`
gives the changed samples: the program navcore computes them (`navcore cookies`), because the
rule is a sequence of float operations with a noise texture.

The module needs only the standard library of Python.
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
from array import array
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

from nav_assets import Failure, field_id, number, parse, real  # noqa: E402

REGION_SIZE = 2048
HEIGHT_SCALE = 2048.0 / 65535.0
NO_WATER = 0xFFFF
# The water raster has one sample for each square of this size (meters).
WATER_CELL = 1

HEIGHT_FIELD = field_id("Height")
FLOOR_FIELD = field_id("FloorHeight")
FLAGS_FIELD = field_id("Flags")
SIZE_FIELD = field_id("regionSize")
NODES_FIELD = field_id("quadtreeNodes")
SUBDIVIDED = 65535.0
# The values of a cookie key that is not in the document (client code 0x69B42D0).
COOKIE_KEYS = (("innerRadius", 0.0), ("outerRadius", 0.0), ("maxHeightBlend", 0.0), ("edgeNoise", 0.5),
               ("edgeNoiseFeatureSize", 8.0), ("groundNoiseScale", 0.5))


def lzw_decode(data: bytes, expected: int) -> bytearray:
    """Decodes TIFF LZW data (codes with the most significant bit first, early change)."""
    out = bytearray()
    table: list[bytes] = [bytes((i,)) for i in range(256)] + [b"", b""]
    bits = 0
    available = 0
    width = 9
    previous: bytes | None = None
    position = 0
    size = len(data)
    limit = 511
    while True:
        while available < width:
            if position >= size:
                return out
            bits = (bits << 8) | data[position]
            position += 1
            available += 8
        available -= width
        code = (bits >> available) & ((1 << width) - 1)
        bits &= (1 << available) - 1
        if code == 256:
            del table[258:]
            width = 9
            limit = 511
            previous = None
            continue
        if code == 257:
            return out
        if previous is None:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            table.append(previous + entry[:1])
        elif code == len(table):
            entry = previous + previous[:1]
            table.append(entry)
        else:
            raise Failure("the LZW data of the height map has a code that is not in the table")
        out += entry
        previous = entry
        if len(table) >= limit and width < 12:
            width += 1
            limit = (1 << width) - 1
        if len(out) >= expected:
            return out


def read_heightmap(data: bytes) -> array:
    """@return The 2048 x 2048 samples (16 bits) of a height map, row 0 = local y 0."""
    if data[:4] != b"II*\0":
        raise Failure("the height map is not a little-endian TIFF file")
    (offset,) = struct.unpack_from("<I", data, 4)
    (count,) = struct.unpack_from("<H", data, offset)
    tags: dict[int, tuple] = {}
    for index in range(count):
        tag, kind, values, value = struct.unpack_from("<HHI4s", data, offset + 2 + 12 * index)
        unit = {1: 1, 2: 1, 3: 2, 4: 4}.get(kind)
        if unit is None:
            continue
        size = unit * values
        raw = value[:size] if size <= 4 else data[struct.unpack("<I", value)[0]:struct.unpack("<I", value)[0] + size]
        tags[tag] = struct.unpack("<" + str(values) + {1: "B", 2: "B", 3: "H", 4: "I"}[kind], raw)
    width, height = tags.get(256, (0,))[0], tags.get(257, (0,))[0]
    if (width, height) != (REGION_SIZE, REGION_SIZE) or tags.get(258) != (16,) or tags.get(277, (1,)) != (1,):
        raise Failure(f"the height map has {width} x {height} samples of {tags.get(258)} bits, not 2048 x 2048 of 16")
    if tags.get(259) != (5,) or tags.get(317, (1,))[0] not in (1, 2):
        raise Failure(f"the height map has compression {tags.get(259)} and predictor {tags.get(317)}")
    predictor = tags.get(317, (1,))[0] == 2
    rows_per_strip = tags.get(278, (height,))[0]
    offsets, sizes = tags.get(273, ()), tags.get(279, ())
    if len(offsets) != len(sizes) or len(offsets) * rows_per_strip < height:
        raise Failure("the strip tables of the height map do not fit its size")
    image = array("H", bytes(2 * width * height))
    row = 0
    for strip_offset, strip_size in zip(offsets, sizes):
        rows = min(rows_per_strip, height - row)
        plain = lzw_decode(data[strip_offset:strip_offset + strip_size], rows * width * 2)
        if len(plain) < rows * width * 2:
            raise Failure(f"strip {row // rows_per_strip} of the height map has {len(plain)} bytes")
        samples = array("H", bytes(plain[:rows * width * 2]))
        if sys.byteorder != "little":
            samples.byteswap()
        for strip_row in range(rows):
            start = strip_row * width
            line = samples[start:start + width]
            if predictor:
                total = 0
                for column in range(width):
                    total = (total + line[column]) & 0xFFFF
                    line[column] = total
            target = (height - 1 - row) * width
            image[target:target + width] = line
            row += 1
    return image


def read_water(data: bytes) -> tuple[array, array, dict]:
    """@return (surface, floor, counts): two rasters of 2048 x 2048 with the height in 1/32 m.

    A sample with no water has the value NO_WATER.
    """
    roots = parse(data)
    root = roots[0]
    size = number(root.child(SIZE_FIELD))
    nodes_field = root.child(NODES_FIELD)
    if size != REGION_SIZE or nodes_field is None:
        raise Failure(f"the water file has the region size {size}")
    def flags_of(node) -> int:
        field = node.child(FLAGS_FIELD)
        return int.from_bytes(field.value, "little") if field is not None and field.value is not None else 0

    nodes = [(real(node.child(HEIGHT_FIELD)), real(node.child(FLOOR_FIELD)), flags_of(node))
             for node in nodes_field.children]
    cells = REGION_SIZE // WATER_CELL
    surface = array("H", [NO_WATER]) * (cells * cells)
    floor = array("H", [NO_WATER]) * (cells * cells)
    counts = {"nodes": len(nodes), "waterNodes": 0, "waterArea": 0, "flags": {}, "smallestNode": REGION_SIZE}
    if not nodes:
        return surface, floor, counts
    # Level order: `level` has the squares of one level, and the children of its nodes follow.
    level = [(0, 0, REGION_SIZE)]
    position = 0
    while level:
        following = []
        for x, y, edge in level:
            if position >= len(nodes):
                raise Failure("the water quadtree ends before its last node")
            height, floor_height, flags = nodes[position]
            position += 1
            if height == SUBDIVIDED:
                half = edge // 2
                if half < 1:
                    raise Failure("the water quadtree has a node smaller than 1 m with children")
                following.extend(((x, y, half), (x + half, y, half), (x, y + half, half), (x + half, y + half, half)))
                continue
            if height == 0.0 and floor_height == 0.0:
                continue
            counts["waterNodes"] += 1
            counts["waterArea"] += edge * edge
            counts["flags"][flags] = counts["flags"].get(flags, 0) + 1
            counts["smallestNode"] = min(counts["smallestNode"], edge)
            top = min(max(int(round(height * 32.0)), 0), NO_WATER - 1)
            bottom = min(max(int(round(floor_height * 32.0)), 0), NO_WATER - 1)
            line_top = array("H", [top]) * (edge // WATER_CELL)
            line_bottom = array("H", [bottom]) * (edge // WATER_CELL)
            for row in range(y // WATER_CELL, (y + edge) // WATER_CELL):
                start = row * cells + x // WATER_CELL
                surface[start:start + edge // WATER_CELL] = line_top
                floor[start:start + edge // WATER_CELL] = line_bottom
        level = following
    if position != len(nodes):
        raise Failure(f"the water quadtree has {len(nodes) - position} nodes after its last level")
    return surface, floor, counts


def read_cookies(paks, folder: str) -> list[tuple[float, ...]]:
    """@return The cookies of the placements of one region, in the sequence of the file paths.

    One cookie: (x, y, z, innerRadius, outerRadius, maxHeightBlend, edgeNoise, edgeNoiseFeatureSize,
    groundNoiseScale). The client uses each placement with a cookie, also a placement with the
    slice name "<PLOT>".
    @param folder The folder of the region in the pak files, with "/" at its end.
    """
    cookies: list[tuple[float, ...]] = []
    for path in paks.paths(folder + "capitals/", ".capitals.json"):
        try:
            document = json.loads(paks.read(path).decode("utf-8-sig"))
        except ValueError as error:
            raise Failure(f"{path}: not a JSON document: {error}") from error
        for placement in document.get("Capitals", []):
            cookie = placement.get("cookie")
            if not isinstance(cookie, dict):
                continue
            try:
                position = placement["worldPosition"]
                values = (float(position["x"]), float(position["y"]), float(position["z"]),
                          *(float(cookie.get(key, default)) for key, default in COOKIE_KEYS))
            except (KeyError, TypeError, ValueError) as error:
                raise Failure(f"{path}: a placement with a cookie has a value that is not a number: {error}") from error
            cookies.append(values)
    return cookies


def apply_cookies(heights: array, origin: tuple[int, int], cookies: list[tuple[float, ...]], navcore: Path,
                  noise: Path) -> tuple[array, dict]:
    """@return (the samples of a height map after the cookies of its region, the counts of the program).

    @param heights The 2048 x 2048 samples of `read_heightmap`.
    @param origin The world position of the sample (0, 0), in meters.
    @param navcore The program of navcore.cpp. @param noise The noise texture (`navcore noise`).
    """
    if not cookies:
        return heights, {"cookies": 0, "cookiesUsed": 0, "sampleChanges": 0}
    with tempfile.TemporaryDirectory(prefix="soulwarden-terrain-") as folder:
        source, table, target = (Path(folder) / name for name in ("in.u16", "cookies.tsv", "out.u16"))
        samples = array("H", heights)
        if sys.byteorder != "little":
            samples.byteswap()
        source.write_bytes(samples.tobytes())
        table.write_text("".join(" ".join(repr(value) for value in cookie) + "\n" for cookie in cookies))
        result = subprocess.run([str(navcore), "cookies", str(noise), str(origin[0]), str(origin[1]), str(source), str(table),
                                 str(target)], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise Failure(f"the terrain rule failed: {result.stderr.strip() or result.stdout.strip()}")
        changed = array("H")
        changed.frombytes(target.read_bytes())
        if sys.byteorder != "little":
            changed.byteswap()
    if len(changed) != len(heights):
        raise Failure("the terrain rule gave a height map of a different size")
    try:
        counts = json.loads(result.stdout)
    except ValueError:
        counts = {}
    return changed, counts

#!/usr/bin/env python3
"""The navigation data file of one region: write it from the output of navcore, and read it.

The backend reads these files (apps/backend/src/gamedata/navigation_data.h, protocol notes
4.19). One file has one region of the world (2048 m by 2048 m) as 32 by 32 tiles of 64 m. A
tile is one zlib stream, so that the backend reads only the tiles near the players.

file      := header directory tile*                       (all numbers little-endian)
header    := "SWNV" version:u32 (3) regionX:i32 regionY:i32
             cell:f32 radius:f32 height:f32 step:f32 slope:f32 wade:f32     (the agent, see navcore.cpp)
             tileCells:u32 (256) tilesPerSide:u32 (32) ocean:f32
             heightScale:f32 (the terrain height of the sample 65535, in meters: 2048) reserved:u32 * 2
directory := (offset:u64 size:u32 plainSize:u32) * tilesPerSide * tilesPerSide
             the tiles from the south to the north, and in each row from the west to the east
             (index = tileY * tilesPerSide + tileX); the offset is from the start of the file
tile      := one zlib stream of: terrain runCount:u32 run* extraCount:u32 extra*
terrain   := u16 * 65 * 65: the terrain samples at the whole meters of the tile, the rows from
             the south to the north. The last row and the last column are the first ones of the
             next tiles. A sample is stored as its difference to the sample to its west, and the
             first sample of a row as its difference to the first sample of the row to its south
             (16 bits, with wrap). Height = sample * heightScale / 65535.
run       := code:u8 length:u16: the same code for `length` cells, row by row (x first)
             0 no floor; 1 one free floor, the terrain surface, the sky above it; 2 the same
             below shallow water; 3 the same below deep water; 4 the terrain surface, no place
             for a creature; 4 + n: n floors follow in the extra list for each cell of the run
extra     := height:u16 (1/32 m) flags:u8 sightLow:u8 (1/8 m) sightHigh:u8 (1/4 m)
             flags: 1 the terrain surface, 2 shallow water, 4 deep water, 8 blocked (no place
             for a creature). The sight is free from height + sightLow to height + sightHigh
             (sightLow 255: no free interval; sightHigh 255: the sky).

The module needs only the standard library of Python.

Usage: nav_file.py pack INPUT.raw OUTPUT.swnav        (prints one JSON line with the sizes)
       nav_file.py info FILE.swnav
       nav_file.py at FOLDER X Y                      (the floors of the cell of a world position)
"""

from __future__ import annotations

import json
import os
import struct
import sys
import zlib
from array import array
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

MAGIC = b"SWNV"
RAW_MAGIC = b"SWNR"
VERSION = 3
REGION_SIZE = 2048
TILE_METERS = 64
TILE_SAMPLES = TILE_METERS + 1
HEIGHT_RANGE = 2048.0
RAW_HEADER = struct.Struct("<4sI2i6f2If")
HEADER = struct.Struct("<4sI2i6f2If" + "fII")
ENTRY = struct.Struct("<QII")
# Flags of a floor.
TERRAIN, SHALLOW, DEEP, BLOCKED = 1, 2, 4, 8
NO_SIGHT = OPEN_SKY = 255
CODE_FLOORS = 4
EXTRA_SIZE = 5


class Failure(Exception):
    """A file is not there, or it does not have the form of this module."""


def region_name(x: int, y: int) -> str:
    return f"r_{x:+03d}_{y:+03d}"


def delta_rows(samples: array, forward: bool) -> array:
    """Changes the terrain samples of a tile to differences (forward), or back."""
    out = array("H", samples)
    for row in range(TILE_SAMPLES):
        start = row * TILE_SAMPLES
        if forward:
            for column in range(TILE_SAMPLES - 1, 0, -1):
                out[start + column] = (samples[start + column] - samples[start + column - 1]) & 0xFFFF
            out[start] = (samples[start] - (samples[start - TILE_SAMPLES] if row else 0)) & 0xFFFF
        else:
            out[start] = (samples[start] + (out[start - TILE_SAMPLES] if row else 0)) & 0xFFFF
            for column in range(1, TILE_SAMPLES):
                out[start + column] = (samples[start + column] + out[start + column - 1]) & 0xFFFF
    return out


def pack(source: Path, target: Path) -> dict:
    """Writes the file of the game data folder from the file of `navcore build`."""
    data = source.read_bytes()
    if len(data) < RAW_HEADER.size or data[:4] != RAW_MAGIC:
        raise Failure(f"{source} is not an output file of navcore")
    magic, version, region_x, region_y, *numbers = RAW_HEADER.unpack_from(data, 0)
    cell, radius, height, step, slope, wade, tile_cells, tiles, ocean = numbers
    if version != VERSION:
        raise Failure(f"{source} has the version {version}, and this tool writes the version {VERSION}")
    count = tiles * tiles
    terrain_bytes = TILE_SAMPLES * TILE_SAMPLES * 2
    out_tiles: list[bytes] = []
    plain_sizes: list[int] = []
    for index in range(count):
        offset, size, _floors = ENTRY.unpack_from(data, RAW_HEADER.size + ENTRY.size * index)
        if offset + size > len(data) or size < terrain_bytes + 8:
            raise Failure(f"{source}: tile {index} is not in the file")
        samples = array("H")
        samples.frombytes(data[offset:offset + terrain_bytes])
        if sys.byteorder != "little":
            samples.byteswap()
        deltas = delta_rows(samples, True)
        if sys.byteorder != "little":
            deltas.byteswap()
        plain = deltas.tobytes() + data[offset + terrain_bytes:offset + size]
        out_tiles.append(zlib.compress(plain, 6))
        plain_sizes.append(len(plain))
    temporary = target.with_name(target.name + ".part")
    with open(temporary, "wb") as handle:
        handle.write(HEADER.pack(MAGIC, VERSION, region_x, region_y, cell, radius, height, step, slope, wade,
                                 tile_cells, tiles, ocean, HEIGHT_RANGE, 0, 0))
        position = HEADER.size + ENTRY.size * count
        for packed, plain_size in zip(out_tiles, plain_sizes):
            handle.write(ENTRY.pack(position, len(packed), plain_size))
            position += len(packed)
        for packed in out_tiles:
            handle.write(packed)
    os.replace(temporary, target)
    return {"bytes": position, "plainBytes": sum(plain_sizes), "tiles": count}


class Tile:
    """One tile of a region file, with the floors of each cell."""

    __slots__ = ("cells", "terrain", "first", "floors")

    def __init__(self, plain: bytes, cells: int) -> None:
        terrain_bytes = TILE_SAMPLES * TILE_SAMPLES * 2
        if len(plain) < terrain_bytes + 8:
            raise Failure("a tile is shorter than its terrain samples")
        deltas = array("H")
        deltas.frombytes(plain[:terrain_bytes])
        if sys.byteorder != "little":
            deltas.byteswap()
        self.cells = cells
        self.terrain = delta_rows(deltas, False)
        (run_count,) = struct.unpack_from("<I", plain, terrain_bytes)
        runs = terrain_bytes + 4
        (extra_count,) = struct.unpack_from("<I", plain, runs + 3 * run_count)
        extra = runs + 3 * run_count + 4
        if extra + EXTRA_SIZE * extra_count != len(plain):
            raise Failure("a tile does not have the size of its lists")
        # first[cell] is the code of a plain cell (as a negative number), or the index of its first floor.
        self.first: list[tuple[int, int]] = []
        self.floors = plain[extra:]
        used = 0
        for index in range(run_count):
            code, length = struct.unpack_from("<BH", plain, runs + 3 * index)
            if code <= CODE_FLOORS:
                self.first.extend([(code, 0)] * length)
            else:
                number = code - CODE_FLOORS
                for _ in range(length):
                    self.first.append((code, used))
                    used += number
        if len(self.first) != cells * cells or used != extra_count:
            raise Failure("a tile does not have each cell")

    def terrain_height(self, x: float, y: float) -> float:
        """@return The terrain height at a place of the tile (meters from its south-west corner)."""
        column, row = min(int(x), TILE_METERS - 1), min(int(y), TILE_METERS - 1)
        fx, fy = x - column, y - row
        base = row * TILE_SAMPLES + column
        t = self.terrain
        low = t[base] * (1 - fx) + t[base + 1] * fx
        high = t[base + TILE_SAMPLES] * (1 - fx) + t[base + TILE_SAMPLES + 1] * fx
        return (low * (1 - fy) + high * fy) * HEIGHT_RANGE / 65535.0

    def floors_of(self, cell_x: int, cell_y: int, cell: float) -> list[tuple[float, int, float, float]]:
        """@return The floors of a cell: (height, flags, sight from, sight to), the heights in meters."""
        code, first = self.first[cell_y * self.cells + cell_x]
        if code == 0:
            return []
        if code <= CODE_FLOORS:
            height = self.terrain_height((cell_x + 0.5) * cell, (cell_y + 0.5) * cell)
            flags = TERRAIN | {1: 0, 2: SHALLOW, 3: DEEP, 4: BLOCKED}[code]
            return [(height, flags, height, float("inf"))]
        out = []
        for index in range(code - CODE_FLOORS):
            value, flags, low, high = struct.unpack_from("<HBBB", self.floors, EXTRA_SIZE * (first + index))
            height = value / 32.0
            if flags & TERRAIN:
                height = self.terrain_height((cell_x + 0.5) * cell, (cell_y + 0.5) * cell)
            if low == NO_SIGHT:
                out.append((height, flags, float("inf"), float("-inf")))
            else:
                out.append((height, flags, value / 32.0 + low / 8.0,
                            float("inf") if high == OPEN_SKY else value / 32.0 + high / 4.0))
        return out


class Region:
    """One region file. The tiles are read when they are asked."""

    def __init__(self, path: Path) -> None:
        self.path = path
        try:
            self.handle = open(path, "rb")
        except OSError as error:
            raise Failure(f"{path}: {error}") from error
        head = self.handle.read(HEADER.size)
        if len(head) != HEADER.size or head[:4] != MAGIC:
            raise Failure(f"{path} is not a navigation data file")
        (_magic, self.version, self.x, self.y, self.cell, self.radius, self.height, self.step, self.slope, self.wade,
         self.tile_cells, self.tiles, self.ocean, self.scale, _a, _b) = HEADER.unpack(head)
        if self.version != VERSION or self.tiles * TILE_METERS != REGION_SIZE or self.tile_cells * self.cell != TILE_METERS:
            raise Failure(f"{path} has a version or a layout that this module does not read")
        self.directory = self.handle.read(ENTRY.size * self.tiles * self.tiles)
        self.loaded: dict[int, Tile] = {}

    def entry(self, index: int) -> tuple[int, int, int]:
        return ENTRY.unpack_from(self.directory, ENTRY.size * index)

    def tile(self, tile_x: int, tile_y: int) -> Tile:
        index = tile_y * self.tiles + tile_x
        tile = self.loaded.get(index)
        if tile is None:
            offset, size, plain_size = self.entry(index)
            self.handle.seek(offset)
            try:
                plain = zlib.decompress(self.handle.read(size))
            except zlib.error as error:
                raise Failure(f"{self.path}: tile {index} does not decode: {error}") from error
            if len(plain) != plain_size:
                raise Failure(f"{self.path}: tile {index} has {len(plain)} bytes, not {plain_size}")
            tile = Tile(plain, self.tile_cells)
            if len(self.loaded) > 256:
                self.loaded.clear()
            self.loaded[index] = tile
        return tile


class Navigation:
    """The navigation data of a folder: the floors of a world position."""

    def __init__(self, folder: Path) -> None:
        self.folder = folder
        self.regions: dict[tuple[int, int], Region | None] = {}

    def region(self, x: int, y: int) -> Region | None:
        key = (x, y)
        if key not in self.regions:
            path = self.folder / f"{region_name(x, y)}.swnav"
            self.regions[key] = Region(path) if path.is_file() else None
        return self.regions[key]

    def floors(self, x: float, y: float) -> list[tuple[float, int, float, float]] | None:
        """@return The floors of the cell of a world position, or no value when the world has no data there."""
        region = self.region(int(x // REGION_SIZE), int(y // REGION_SIZE))
        if region is None:
            return None
        local_x, local_y = x - region.x * REGION_SIZE, y - region.y * REGION_SIZE
        tile_x, tile_y = int(local_x // TILE_METERS), int(local_y // TILE_METERS)
        tile = region.tile(tile_x, tile_y)
        cell_x = min(int((local_x - tile_x * TILE_METERS) / region.cell), region.tile_cells - 1)
        cell_y = min(int((local_y - tile_y * TILE_METERS) / region.cell), region.tile_cells - 1)
        return tile.floors_of(cell_x, cell_y, region.cell)

    def cell(self) -> float:
        for region in self.regions.values():
            if region is not None:
                return region.cell
        return 0.25


def main() -> int:
    arguments = sys.argv[1:]
    try:
        if len(arguments) == 3 and arguments[0] == "pack":
            print(json.dumps(pack(Path(arguments[1]), Path(arguments[2]))))
            return 0
        if len(arguments) == 2 and arguments[0] == "info":
            region = Region(Path(arguments[1]))
            sizes = [region.entry(index) for index in range(region.tiles * region.tiles)]
            print(json.dumps({"region": region_name(region.x, region.y), "version": region.version, "cell": region.cell,
                              "radius": region.radius, "height": region.height, "step": region.step,
                              "slope": region.slope, "wade": region.wade, "tiles": len(sizes),
                              "bytes": Path(arguments[1]).stat().st_size, "tileBytes": sum(size for _, size, _ in sizes),
                              "plainBytes": sum(plain for _, _, plain in sizes),
                              "largestTile": max(plain for _, _, plain in sizes)}))
            return 0
        if len(arguments) == 4 and arguments[0] == "at":
            floors = Navigation(Path(arguments[1])).floors(float(arguments[2]), float(arguments[3]))
            if floors is None:
                print("no data")
                return 0
            for height, flags, low, high in floors:
                marks = [name for bit, name in ((TERRAIN, "terrain"), (SHALLOW, "shallow water"), (DEEP, "deep water"),
                                                (BLOCKED, "blocked")) if flags & bit]
                print(f"{height:.3f}  {'free' if not flags & BLOCKED else 'blocked'}  {' '.join(marks)}  "
                      f"sight {low:.2f} to {high:.2f}")
            return 0
    except Failure as error:
        print(f"nav_file: {error}", file=sys.stderr)
        return 1
    print(__doc__.split("Usage:")[1], file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

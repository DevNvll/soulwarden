#!/usr/bin/env python3
"""Write the terrain heights of the world as one file for each region.

The backend has no terrain of its own. A creature that walks needs the height of the ground at
each step: the client shows a creature at the exact position of its states and looks for no
floor (protocol notes 4.17). The backend reads the files of this tool for the regions that it
needs, one tile of 64 m at a time.

Source: `region.heightmap` of each region of the world (the pak files of the game copy). It is
a TIFF file with 2048 x 2048 samples of 16 bits, LZW data in strips, and the horizontal
predictor. The sample of the local position (x, y) of a region is in row `2047 - y` and column
`x`, and the samples are at whole meters. The height in meters is sample * 2048 / 65535. The
region r_+XX_+YY starts at x = XX * 2048 and y = YY * 2048.

The terrain height is the height of the terrain only: a floor of a building, a bridge, and a
cave are not in it (the navigation data has them, tools/gamedata/navigation.sh).

The height map is not the terrain of the client near some placements: a placement with a
member `cookie` changes the terrain around it (protocol notes 4.19; the differences are up to
some meters). The navigation tool computes the terrain after the cookies and writes it to its
work folder (SOULWARDEN_NAVIGATION_WORK/terrain/REGION.u16). This tool takes the samples of a
region from that file when it is there, and from the height map when it is not: its last line
says how many regions have the cookies. tools/gamedata/navigation.sh runs this tool at its end.

Output, one file for each region: OUTPUT_DIR/r_+XX_+YY.swh (all numbers little-endian)

  header  "SWHT" version:u32 (1) regionX:i32 regionY:i32 size:u32 (2048) tile:u32 (64)
          scale:f32 (the height of the sample 65535, in meters: 2048) reserved:u32
  index   (size / tile)^2 entries of offset:u32 length:u32, for the tiles from the south to the
          north and in each row from the west to the east (tile index = ty * 32 + tx). The offset
          is from the start of the file.
  tiles   Each tile is one zlib stream of 65 x 65 samples of 16 bits: the rows from the south to
          the north, each row from the west to the east. The last row and the last column are the
          first samples of the next tile (of the next region at the edge of a region; at the edge
          of the world they repeat the last sample). A sample is stored as its difference to the
          sample to its west, and the first sample of a row as its difference to the first sample
          of the row to its south (16 bits, with wrap).

The tool needs only the standard library of Python and the modules appearance_tables.py and
creature_data.py of this folder. The pak entries of the height maps are stored data: the tool
needs no Oodle library.

Usage: height_maps.py [OUTPUT_DIR] [--region NAME ...] [--jobs N] [--level LEVEL]
       height_maps.py [OUTPUT_DIR] --height X Y     print the height of a position from the files (no write)
Default output: <SOULWARDEN_GAMEDATA_DIR>/heights (with --level: <SOULWARDEN_GAMEDATA_DIR>/levels/LEVEL/heights; stage DG2)
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_NAVIGATION_WORK (default ~/Games/soulwarden/work/navigation-build: the terrain after the cookies)
"""

from __future__ import annotations

import argparse
import math
import os
import re
import struct
import sys
import zlib
from array import array
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, plain_stream  # noqa: E402
from nav_assets import Failure as NavigationFailure  # noqa: E402
from nav_terrain import MAIN_MOUNTAIN_HEIGHT, mountain_height, to_main_scale  # noqa: E402

OUTPUT_FOLDER = "heights"
WORLD = "newworld_vitaeeterna"
REGIONS_FOLDER = f"sharedassets/coatlicue/{WORLD}/regions/"
# Stage DG2: the level of the regions (None: the main world). The terrain after the cookies of a level is in its own work folder.
LEVEL: str | None = None


def use_level(level: str, terrain: str) -> None:
    """Stage DG2: the regions of an own level (its terrain folder from its map row, game_modes.level_terrain)."""
    global WORLD, REGIONS_FOLDER, LEVEL
    if not terrain.startswith("coatlicue/") or terrain.count("/") != 1:
        raise Failure(f"the terrain project {terrain!r} of the map row is not a folder coatlicue/NAME")
    LEVEL = level
    WORLD = terrain.split("/", 1)[1]
    REGIONS_FOLDER = f"sharedassets/coatlicue/{WORLD}/regions/"
HEIGHTMAP_NAME = "region.heightmap"
REGION_SIZE = 2048
TILE_SIZE = 64
TILES = REGION_SIZE // TILE_SIZE
# The height of the sample 65535 in meters (`mountainHeight` of the terrain file of the world).
HEIGHT_RANGE = 2048.0
MAGIC = b"SWHT"
VERSION = 1
HEADER = struct.Struct("<4sIiiIIfI")
REGION_NAME = re.compile(r"r_\+(\d\d)_\+(\d\d)")
# TIFF tags and values that the reader uses.
TAG_WIDTH, TAG_HEIGHT, TAG_BITS, TAG_COMPRESSION, TAG_STRIP_OFFSETS = 256, 257, 258, 259, 273
TAG_SAMPLES, TAG_ROWS_PER_STRIP, TAG_STRIP_SIZES, TAG_PREDICTOR = 277, 278, 279, 317
COMPRESSION_LZW = 5
PREDICTOR_HORIZONTAL = 2
LZW_CLEAR, LZW_END = 256, 257


def lzw_strip(data: bytes, size: int) -> bytes:
    """@return The plain bytes of one LZW strip of a TIFF file (codes with the high bit first, 9 to 12 bits; the
    code size grows one code before the table is full)."""
    out = bytearray()
    first_codes = [bytes((value,)) for value in range(256)] + [b"", b""]
    table = list(first_codes)
    width = 9
    buffer = 0
    bits = 0
    previous = b""
    for byte in data:
        buffer = (buffer << 8) | byte
        bits += 8
        if bits < width:
            continue
        bits -= width
        code = (buffer >> bits) & ((1 << width) - 1)
        buffer &= (1 << bits) - 1
        if code == LZW_CLEAR:
            table = list(first_codes)
            width = 9
            previous = b""
            continue
        if code == LZW_END:
            break
        if not previous:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            table.append(previous + entry[:1])
        elif code == len(table):
            entry = previous + previous[:1]
            table.append(entry)
        else:
            raise Failure("the LZW data of a height map has a code that its table does not have")
        out += entry
        previous = entry
        if len(table) >= (1 << width) - 1 and width < 12:
            width += 1
        if len(out) >= size:
            break
    return bytes(out)


def read_heightmap(data: bytes) -> array:
    """@return The samples of one height map: 2048 rows of 2048 values, the first row at the local y 0 (the south)."""
    if data[:4] != b"II*\0":
        raise Failure("a height map is not a little-endian TIFF file")
    directory = struct.unpack_from("<I", data, 4)[0]
    tags: dict[int, tuple[int, ...]] = {}
    for index in range(struct.unpack_from("<H", data, directory)[0]):
        tag, kind, count, value = struct.unpack_from("<HHI4s", data, directory + 2 + 12 * index)
        letter = {1: "B", 3: "H", 4: "I"}.get(kind)
        if letter is None:
            continue
        size = struct.calcsize(letter) * count
        raw = value[:size] if size <= 4 else data[struct.unpack("<I", value)[0]:struct.unpack("<I", value)[0] + size]
        tags[tag] = struct.unpack(f"<{count}{letter}", raw)
    width = tags.get(TAG_WIDTH, (0,))[0]
    height = tags.get(TAG_HEIGHT, (0,))[0]
    if (width, height) != (REGION_SIZE, REGION_SIZE) or tags.get(TAG_BITS) != (16,) or tags.get(TAG_SAMPLES, (1,)) != (1,):
        raise Failure(f"a height map has {width} x {height} samples of {tags.get(TAG_BITS)} bits, not 2048 x 2048 of 16 bits")
    predictor = tags.get(TAG_PREDICTOR, (1,))[0]
    if tags.get(TAG_COMPRESSION) != (COMPRESSION_LZW,) or predictor not in (1, PREDICTOR_HORIZONTAL):
        raise Failure(f"a height map has the compression {tags.get(TAG_COMPRESSION)} and the predictor {predictor}")
    rows_per_strip = tags.get(TAG_ROWS_PER_STRIP, (height,))[0]
    offsets, sizes = tags.get(TAG_STRIP_OFFSETS, ()), tags.get(TAG_STRIP_SIZES, ())
    if not rows_per_strip or len(offsets) != len(sizes) or len(offsets) * rows_per_strip < height:
        raise Failure("the strip tables of a height map do not fit its size")
    image = array("H", bytes(2 * width * height))
    row = 0
    for offset, size in zip(offsets, sizes):
        rows = min(rows_per_strip, height - row)
        plain = lzw_strip(data[offset:offset + size], rows * width * 2)
        if len(plain) < rows * width * 2:
            raise Failure(f"a strip of a height map has {len(plain)} bytes, not {rows * width * 2}")
        samples = array("H", plain[:rows * width * 2])
        if sys.byteorder != "little":
            samples.byteswap()
        for strip_row in range(rows):
            line = samples[strip_row * width:(strip_row + 1) * width]
            if predictor == PREDICTOR_HORIZONTAL:
                total = 0
                for column in range(width):
                    total = (total + line[column]) & 0xFFFF
                    line[column] = total
            # The first row of the file is the north edge of the region.
            target = (height - 1 - row) * width
            image[target:target + width] = line
            row += 1
    return image


def changed_terrain_folder() -> Path:
    """@return The folder of the navigation tool with the terrain samples after the cookies (stage DG2: of a level, in its own work folder
    levels/LEVEL: the regions of a level have the names of the regions of the main world)."""
    work = Path(os.environ.get("SOULWARDEN_NAVIGATION_WORK") or Path.home() / "Games/soulwarden/work/navigation-build")
    return (work / "levels" / LEVEL if LEVEL else work) / "terrain"


def region_names(sources) -> list[str]:
    """@return The name of each region of the world that has a height map."""
    names = []
    for path in sources.paths(REGIONS_FOLDER, "/" + HEIGHTMAP_NAME):
        name = path[len(REGIONS_FOLDER):].split("/")[0]
        if REGION_NAME.fullmatch(name):
            names.append(name)
    return sorted(names)


def region_place(name: str) -> tuple[int, int]:
    match = REGION_NAME.fullmatch(name)
    if match is None:
        raise Failure(f"{name} is not the name of a region (r_+XX_+YY)")
    return int(match[1]), int(match[2])


def _decode(job: tuple[str, str, int, int, int]) -> tuple[str, bytes]:
    """One job of the process pool: the samples of one region as bytes, from a pak entry that is stored or deflate data."""
    name, pak, start, size, method = job
    with open(pak, "rb") as handle:
        handle.seek(start)
        raw = handle.read(size)
    data = plain_stream(raw if method == 0 else zlib.decompress(raw, -15))
    return name, read_heightmap(data).tobytes()


class Terrain:
    """The terrain heights of the regions of the world, decoded when a region is necessary."""

    def __init__(self, sources, jobs: int = 0, changed: Path | None = None) -> None:
        """@param sources The pak files of the game copy (`Sources` of creature_data.py, with the region folder).
        @param changed The folder with the terrain after the cookies (REGION.u16), from the navigation tool."""
        self.sources = sources
        self.names = set(region_names(sources))
        self.jobs = jobs or min(os.cpu_count() or 1, 8)
        self.regions: dict[str, array] = {}
        self.changed = changed if changed is not None else changed_terrain_folder()
        # The regions whose samples are the terrain after the cookies.
        self.with_cookies: set[str] = set()
        # Stage DG2: the height of the sample 65535 of the terrain project (terrain.json: 2048 m in the main world, 256 m in most levels).
        self.mountain = MAIN_MOUNTAIN_HEIGHT
        terrain_file = f"sharedassets/coatlicue/{WORLD}/terrain.json"
        if hasattr(sources, "has") and sources.has(terrain_file):
            try:
                self.mountain = mountain_height(sources.read(terrain_file), terrain_file)
            except NavigationFailure as error:
                raise Failure(str(error)) from error

    def load(self, names: list[str]) -> None:
        """Decodes the regions of the list that the object does not have, in more than one process."""
        wanted = [name for name in names if name in self.names and name not in self.regions]
        work = []
        results = []
        for name in wanted:
            file = self.changed / f"{name}.u16"
            if file.is_file() and file.stat().st_size == 2 * REGION_SIZE * REGION_SIZE:
                # The terrain after the cookies of the placements (the navigation tool made it).
                results.append((name, file.read_bytes()))
                self.with_cookies.add(name)
                continue
            path = REGIONS_FOLDER + name + "/" + HEIGHTMAP_NAME
            pak, info = self.sources.entries[path]
            if info.compress_type in (0, 8):
                with open(pak, "rb") as handle:
                    handle.seek(info.header_offset)
                    name_size, extra_size = struct.unpack_from("<HH", handle.read(30), 26)
                work.append((name, str(pak), info.header_offset + 30 + name_size + extra_size, info.compress_size,
                             info.compress_type))
                continue
            # A different compression: the reader of the sources (one process).
            data = self.sources.read(path)
            if data is None:
                raise Failure(f"the height map of the region {name} has no source")
            results.append((name, read_heightmap(data).tobytes()))
        if self.jobs > 1 and len(work) > 1:
            with ProcessPoolExecutor(max_workers=min(self.jobs, len(work))) as pool:
                results.extend(pool.map(_decode, work))
        else:
            results.extend(_decode(job) for job in work)
        for name, raw in results:
            samples = array("H", raw)
            if sys.byteorder != "little":
                samples.byteswap()
            # Stage DG2: a height map of a terrain whose sample 65535 is not 2048 m goes to the scale of the main world (the terrain
            # after the cookies of the navigation tool has that scale already).
            self.regions[name] = samples if name in self.with_cookies else to_main_scale(samples, self.mountain)

    def forget(self, name: str) -> None:
        self.regions.pop(name, None)

    def sample(self, x: int, y: int) -> int | None:
        """@return The sample at a whole position of the world, or no value where the world has no height map."""
        name = f"r_+{x // REGION_SIZE:02d}_+{y // REGION_SIZE:02d}" if x >= 0 and y >= 0 else ""
        if name not in self.names:
            return None
        if name not in self.regions:
            self.load([name])
        return self.regions[name][(y % REGION_SIZE) * REGION_SIZE + (x % REGION_SIZE)]

    def height(self, x: float, y: float) -> float | None:
        """@return The terrain height at a position, between the four samples around it. No value outside the height maps."""
        low_x, low_y = math.floor(x), math.floor(y)
        corner = self.sample(low_x, low_y)
        if corner is None:
            return None
        # A neighbor that the world does not have repeats the sample of the edge, as the files of
        # `region_file` do: with no region to the east, the sample to the east is the sample itself,
        # and the diagonal sample is the sample to the north.
        east = self.sample(low_x + 1, low_y)
        north = self.sample(low_x, low_y + 1)
        diagonal = self.sample(low_x + 1, low_y + 1)
        no_east = east is None
        east = corner if east is None else east
        north = corner if north is None else north
        if diagonal is None:
            diagonal = north if no_east else east
        part_x, part_y = x - low_x, y - low_y
        south_edge = corner + (east - corner) * part_x
        north_edge = north + (diagonal - north) * part_x
        return (south_edge + (north_edge - south_edge) * part_y) * HEIGHT_RANGE / 65535.0


def region_file(terrain: Terrain, name: str) -> bytes:
    """@return The file of one region (see the head of this file)."""
    region_x, region_y = region_place(name)
    base_x, base_y = region_x * REGION_SIZE, region_y * REGION_SIZE
    # The samples of the region with one more row and one more column: the first samples of the neighbors.
    terrain.load([name, f"r_+{region_x + 1:02d}_+{region_y:02d}", f"r_+{region_x:02d}_+{region_y + 1:02d}",
                  f"r_+{region_x + 1:02d}_+{region_y + 1:02d}"])
    own = terrain.regions[name]
    edge = REGION_SIZE + 1
    grid = array("H", bytes(2 * edge * edge))
    for row in range(REGION_SIZE):
        grid[row * edge:row * edge + REGION_SIZE] = own[row * REGION_SIZE:(row + 1) * REGION_SIZE]
        east = terrain.sample(base_x + REGION_SIZE, base_y + row)
        grid[row * edge + REGION_SIZE] = own[row * REGION_SIZE + REGION_SIZE - 1] if east is None else east
    for column in range(edge):
        north = terrain.sample(base_x + column, base_y + REGION_SIZE)
        grid[REGION_SIZE * edge + column] = grid[(REGION_SIZE - 1) * edge + column] if north is None else north
    tiles = []
    side = TILE_SIZE + 1
    for tile_y in range(TILES):
        for tile_x in range(TILES):
            deltas = array("H", bytes(2 * side * side))
            below = 0
            for row in range(side):
                start = (tile_y * TILE_SIZE + row) * edge + tile_x * TILE_SIZE
                line = grid[start:start + side]
                deltas[row * side] = (line[0] - below) & 0xFFFF
                below = line[0]
                for column in range(1, side):
                    deltas[row * side + column] = (line[column] - line[column - 1]) & 0xFFFF
            if sys.byteorder != "little":
                deltas.byteswap()
            tiles.append(zlib.compress(deltas.tobytes(), 9))
    index = bytearray()
    offset = HEADER.size + 8 * len(tiles)
    for tile in tiles:
        index += struct.pack("<II", offset, len(tile))
        offset += len(tile)
    return HEADER.pack(MAGIC, VERSION, region_x, region_y, REGION_SIZE, TILE_SIZE, HEIGHT_RANGE, 0) + bytes(index) + b"".join(tiles)


def read_tile(file: Path, tile_x: int, tile_y: int) -> array:
    """@return The 65 x 65 samples of one tile of a file of this tool (the reader of the backend does the same)."""
    with open(file, "rb") as handle:
        magic, version, _, _, size, tile, _, _ = HEADER.unpack(handle.read(HEADER.size))
        if magic != MAGIC or version != VERSION or size != REGION_SIZE or tile != TILE_SIZE:
            raise Failure(f"{file} is not a height file of this tool")
        handle.seek(HEADER.size + 8 * (tile_y * TILES + tile_x))
        offset, length = struct.unpack("<II", handle.read(8))
        handle.seek(offset)
        deltas = array("H", zlib.decompress(handle.read(length)))
    if sys.byteorder != "little":
        deltas.byteswap()
    side = TILE_SIZE + 1
    samples = array("H", bytes(2 * side * side))
    below = 0
    for row in range(side):
        value = (below + deltas[row * side]) & 0xFFFF
        below = value
        samples[row * side] = value
        for column in range(1, side):
            value = (value + deltas[row * side + column]) & 0xFFFF
            samples[row * side + column] = value
    return samples


def height_from_files(folder: Path, x: float, y: float) -> float | None:
    """@return The terrain height at a position from the files of a folder, as the backend computes it."""
    region_x, region_y = int(x // REGION_SIZE), int(y // REGION_SIZE)
    file = folder / f"r_+{region_x:02d}_+{region_y:02d}.swh"
    if x < 0 or y < 0 or not file.is_file():
        return None
    local_x, local_y = x - region_x * REGION_SIZE, y - region_y * REGION_SIZE
    tile_x, tile_y = min(int(local_x // TILE_SIZE), TILES - 1), min(int(local_y // TILE_SIZE), TILES - 1)
    samples = read_tile(file, tile_x, tile_y)
    in_x, in_y = local_x - tile_x * TILE_SIZE, local_y - tile_y * TILE_SIZE
    low_x, low_y = min(int(in_x), TILE_SIZE - 1), min(int(in_y), TILE_SIZE - 1)
    side = TILE_SIZE + 1
    corner, east = samples[low_y * side + low_x], samples[low_y * side + low_x + 1]
    north, diagonal = samples[(low_y + 1) * side + low_x], samples[(low_y + 1) * side + low_x + 1]
    part_x, part_y = in_x - low_x, in_y - low_y
    south_edge = corner + (east - corner) * part_x
    north_edge = north + (diagonal - north) * part_x
    return (south_edge + (north_edge - south_edge) * part_y) * HEIGHT_RANGE / 65535.0


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the terrain heights of the world as one file for each region.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_FOLDER))
    parser.add_argument("--region", nargs="+", metavar="NAME", help="only these regions (r_+04_+02)")
    parser.add_argument("--jobs", type=int, default=0, metavar="N", help="number of processes (default: 8 at most)")
    parser.add_argument("--height", nargs=2, type=float, metavar=("X", "Y"),
                        help="print the height of a position from the files of the folder, and write no file")
    parser.add_argument("--level", metavar="LEVEL",
                        help="stage DG2: the regions of an own level (a level or a map id of the map table; default output "
                             "GAMEDATA/levels/LEVEL/heights; needs an Oodle source for the map table)")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    if arguments.height is not None:
        height = height_from_files(target, arguments.height[0], arguments.height[1])
        if height is None:
            print(f"heights: {target} has no file for the position {arguments.height[0]:g} {arguments.height[1]:g}",
                  file=sys.stderr)
            return 1
        print(f"{height:.3f}")
        return 0
    try:
        from creature_data import TABLES_FOLDER, Sources  # noqa: PLC0415
        if arguments.level:
            import game_modes  # noqa: PLC0415
            level, terrain, _rows = game_modes.level_terrain(Sources(game / "assets", wanted=(TABLES_FOLDER, "localization/")),
                                                             arguments.level)
            use_level(level, terrain)
            if arguments.output == parser.get_default("output"):
                target = gamedata / "levels" / level / OUTPUT_FOLDER
        sources = Sources(game / "assets", wanted=(REGIONS_FOLDER, f"sharedassets/coatlicue/{WORLD}/terrain.json"))
        names = region_names(sources)
        if not names:
            raise Failure(f"the game copy has no height map below {REGIONS_FOLDER}")
        selected = arguments.region or names
        for name in selected:
            if name not in names:
                raise Failure(f"the world has no region {name} with a height map")
        terrain = Terrain(sources, arguments.jobs)
        target.mkdir(parents=True, exist_ok=True)
        total = 0
        # The regions from the west to the east: a region needs the first samples of three neighbors.
        ordered = sorted(selected, key=region_place)
        terrain.load(ordered)
        for name in ordered:
            content = region_file(terrain, name)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target / (name + ".swh.tmp")
            temporary.write_bytes(content)
            temporary.replace(target / (name + ".swh"))
            total += len(content)
    except Failure as error:
        print(f"heights: {error}. The files of the folder are not complete.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"heights: {target}: {error}", file=sys.stderr)
        return 1
    cookies = sum(1 for name in ordered if name in terrain.with_cookies)
    note = "each one with the cookies of its placements" if cookies == len(ordered) else \
        f"{cookies} of them with the cookies of the placements (run tools/gamedata/navigation.sh for the others)"
    print(f"{target}: the terrain heights of {len(ordered)} region(s) of {len(names)}, {total} bytes, {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

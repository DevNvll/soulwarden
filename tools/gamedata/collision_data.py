#!/usr/bin/env python3
"""Write the collision data of the world of the game: one file for each region.

A region is a square of 2048 m (r_+XX_+YY starts at XX * 2048, YY * 2048). The file of a region
has the terrain height, the water, and each solid shape that the game puts in the region, in
region coordinates. The navigation tool (navcore) reads these files.

The sources of one region (all in the pak files of the game copy, below
sharedassets/coatlicue/WORLD/regions/REGION/):

  1. region.heightmap      the terrain height (nav_terrain.py), changed by the cookies of the
                           placements of the capitals files as the client changes it (the
                           terrain near a placement goes to the height of the placement).
  2. region.waterqt        the water surface and the floor below it (nav_terrain.py).
  3. region.chunks         the list of the chunk slices of the region. A chunk slice
                           (coatgen/HASH/LAYER_SIZExSIZE_KIND_chunk_X_Y_LEVEL.dynamicslice) has
                           the static objects of one square of a layer. The transform of an
                           entity of a chunk is relative to the `WorldPosition` of the chunk.
                           The collision chunks (chunkType 2) have the colliders. The tool reads
                           each kind of chunk: a static chunk can have a collider and a spawner.
  4. capitals/L/L.capitals.json   the placements of a layer. A placement with the slice name
                           "<PLOT>" is in the chunks already. Each other placement names a slice
                           that the game makes at run time: the tool reads the slice, and each
                           slice that it spawns at its start.
  5. region.distribution   the placements of the plants and stones of the region (88,820 in the
                           start region): a slice name, a variant name, a position, a rotation,
                           and a scale for each one. The height is the terrain height.
  6. region.slicedata      the metadata of each slice and variant of the region. The tool uses
                           it for a slice with a variant: the variant changes the mesh of the
                           slice, and the metadata has the meshes of the variant.
  region.vegetation is not a source: it has only the grass of the region (material names of
  grass and fern, no mesh with a physics file).

The collision of a slice is in nav_slices.py, and the physics file of a shape in nav_shapes.py.

Output: OUTPUT_DIR/REGION.swcol (the data, see `write_region`) and OUTPUT_DIR/REGION.json (the
report: the counts, each placement that the tool left out with its cause, the time). A region
whose report says "complete" is not made again (--force makes it again).

The tool stops with status 1 and one message when a source is not there or does not decode. It
writes no data file for that region. A placement whose shape has no source is counted in the
report with its cause: status 0 does not mean that the report has no such count.

The tool also writes the terrain samples of the region after the cookies to
OUTPUT_DIR/../terrain/REGION.u16 (2048 x 2048 samples of 16 bits): the terrain height tool
(height_maps.py) uses them.

tools/gamedata/navigation.sh runs this tool for each region. Alone:
Usage: collision_data.py [REGION...] [--all] [--out DIR] [--force] [--world NAME]
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_OODLE_LIB,
             SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see nav_assets.py),
             SOULWARDEN_NAVIGATION_WORK (the folder of the work files: the collision data and the
             index of the pak files; default ~/Games/soulwarden/work/navigation-build)
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
import time
import uuid
from array import array
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nav_assets import (CATALOG_PATH, Catalog, Element, Failure, Paks, asset_id, field_id, number,  # noqa: E402
                        parse, text)
from nav_shapes import (IDENTITY, Statistics, box_part, cylinder_part, multiply, outward,  # noqa: E402
                        read_physics, sphere_part)
from nav_slices import (MARK_CUT, MARK_NONE, MARK_NOT_WALKABLE, MARK_WALKABLE, Collider, SliceContent,  # noqa: E402
                        matrix_of, physics_path, read_slice)
from nav_terrain import (HEIGHT_SCALE, NO_WATER, REGION_SIZE, apply_cookies, read_cookies, read_heightmap,  # noqa: E402
                         read_water)

DEFAULT_WORLD = "newworld_vitaeeterna"
FILTERS_PATH = "sharedassets/physics/default.collisionfilters"
# Flags of a collision filter in the data file.
BLOCKS_CREATURE, BLOCKS_PLAYER = 1, 2
# The filters of a creature and of a player in the filter file of the game.
CREATURE_FILTER, PLAYER_FILTER = "AI", "Player"
FORMAT_VERSION = 2
MAX_DEPTH = 12
# The filter text of a navigation mark with a shape of its own: it is no solid.
MARK_FILTER = "(navigation mark)"
MARK_NAMES = {MARK_NOT_WALKABLE: "notWalkable", MARK_CUT: "cut", MARK_WALKABLE: "walkable"}
# Sources of a placement.
SOURCE_CHUNK, SOURCE_CAPITAL, SOURCE_DISTRIBUTION = 0, 1, 2
SOURCE_NAMES = ("chunk", "capital", "distribution")
PLOT = "<PLOT>"

CHUNKS_FIELD = field_id("Chunks")
ASSET_FIELD = field_id("AssetId")
POSITION_FIELD = field_id("WorldPosition")
CHUNK_TYPE_FIELD = field_id("chunkType")
MAP_FIELD = field_id("sliceMetaDataMap")
SLICE_NAME_FIELD = field_id("sliceName")
VARIANT_NAME_FIELD = field_id("variantName")
HAS_COLLISION_FIELD = field_id("hasCollision")
MESHES_FIELD = field_id("meshes")
MESH_ID_FIELD = field_id("meshAssetId")
ROOT_TRANSFORM_FIELD = field_id("rootRelativeTransform")
INSTANCE_TRANSFORMS_FIELD = field_id("rootRelativeInstanceTransforms")
# A collider of a slice with this filter is the state of a gatherable after its use (a stump).
DEPLETED_FILTERS = ("GatherableDepleted",)


def slice_key(name: str) -> str:
    """@return The key of a slice name: lower case, with "/", no "slices/" at its start, no ending."""
    name = name.lower().replace("\\", "/")
    while "//" in name:
        name = name.replace("//", "/")
    for ending in (".dynamicslice", ".slice"):
        if name.endswith(ending):
            name = name[:-len(ending)]
    return name[7:] if name.startswith("slices/") else name


def quaternion_matrix(q: dict, position: dict, scale: float) -> tuple[float, ...]:
    x, y, z, w = (float(q.get("x", 0.0)), float(q.get("y", 0.0)), float(q.get("z", 0.0)), float(q.get("w", 1.0)))
    size = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / size, y / size, z / size, w / size
    s = scale
    return ((1 - 2 * (y * y + z * z)) * s, 2 * (x * y - z * w) * s, 2 * (x * z + y * w) * s, float(position["x"]),
            2 * (x * y + z * w) * s, (1 - 2 * (x * x + z * z)) * s, 2 * (y * z - x * w) * s, float(position["y"]),
            2 * (x * z - y * w) * s, 2 * (y * z + x * w) * s, (1 - 2 * (x * x + y * y)) * s, float(position["z"]))


class Counts:
    """The counts of one region for its report."""

    def __init__(self) -> None:
        self.values: dict[str, int] = {}
        self.left_out: dict[str, int] = {}
        self.examples: dict[str, list[str]] = {}

    def add(self, name: str, amount: int = 1) -> None:
        self.values[name] = self.values.get(name, 0) + amount

    def leave_out(self, cause: str, example: str = "") -> None:
        self.left_out[cause] = self.left_out.get(cause, 0) + 1
        if example:
            examples = self.examples.setdefault(cause, [])
            if len(examples) < 12 and example not in examples:
                examples.append(example)


class World:
    """The readers and the caches that all regions share."""

    def __init__(self, name: str, cache: Path, navcore: Path | None = None, noise: Path | None = None) -> None:
        self.name = name
        self.folder = f"sharedassets/coatlicue/{name}/regions/"
        # The program and the noise texture of the terrain rule (the cookies).
        work = work_folder()
        self.navcore = navcore or work / "bin" / "navcore"
        self.noise = noise or work / "noise.f32"
        self.paks = Paks(cache / "pak-index.pickle")
        if not self.paks.has(CATALOG_PATH):
            raise Failure(f"the game copy has no {CATALOG_PATH}")
        self.catalog = Catalog(self.paks.read(CATALOG_PATH), (".dynamicslice", ".rnr", ".cgf", ".aliasasset"))
        self.slices: dict[str, SliceContent | None] = {}
        self.shapes: dict[tuple, list | None] = {}
        self.shape_statistics = Statistics()
        self.shape_failures: dict[str, str] = {}
        if not self.paks.has(FILTERS_PATH):
            raise Failure(f"the game copy has no {FILTERS_PATH}")
        self.filters = Filters(self.paks.read(FILTERS_PATH))
        terrain = f"sharedassets/coatlicue/{name}/terrain.json"
        try:
            self.ocean_level = float(json.loads(self.paks.read(terrain))["oceanLevel"])
        except (Failure, ValueError, KeyError, TypeError) as error:
            raise Failure(f"{terrain}: no ocean level: {error}") from error
        self.mesh_physics: dict[str, bool] = {}

    def mesh_has_physics(self, path: str) -> bool:
        """@return True when a mesh file with no physics file has a physics chunk inside it."""
        if path not in self.mesh_physics:
            self.mesh_physics[path] = self.paks.has(path) and cgf_has_physics(self.paks.read(path))
        return self.mesh_physics[path]

    def regions(self) -> list[str]:
        names = set()
        for path in self.paks.entries:
            if path.startswith(self.folder) and path.endswith("/region.heightmap"):
                names.add(path[len(self.folder):].split("/")[0])
        return sorted(names)

    def slice(self, path: str) -> SliceContent | None:
        """@return The content of a slice of the game (not of a chunk), or no value when it is not there."""
        if path not in self.slices:
            self.slices[path] = read_slice(self.paks.read(path)) if self.paks.has(path) else None
        return self.slices[path]

    def shape(self, key: tuple) -> list | None:
        """@return The parts of a shape: ("rnr", path) or ("box" | "sphere" | "cylinder", numbers...)."""
        if key in self.shapes:
            return self.shapes[key]
        parts = None
        if key[0] == "rnr":
            try:
                parts = read_physics(self.paks.read(key[1]), self.shape_statistics)
            except Failure as error:
                self.shape_failures[key[1]] = str(error)
        elif key[0] == "box":
            parts = [outward(box_part(key[1:4]))]
        elif key[0] == "sphere":
            parts = [outward(sphere_part(key[1]))]
        elif key[0] == "cylinder":
            parts = [outward(cylinder_part((0.0, 0.0, -key[1] / 2), (0.0, 0.0, key[1] / 2), key[2]))]
        self.shapes[key] = parts
        return parts


class Filters:
    """The collision filters of the game (sharedassets/physics/default.collisionfilters).

    The file is an object stream with `Categories` (texts) and `Filters`. A filter has `Name`,
    `InheritsFilters`, `IsCategories`, and `CollideWithCategories`. A filter has the categories
    of each filter that it inherits. A collider whose filter text is not the name of a filter
    has that text as its one category.

    Two bodies collide when a category of one of them is in the "collide with" list of the
    other one (a rule of this project, from the data: `Structure` has an empty list, and
    `PlayerOnly` has no category and the list `Player`).
    """

    def __init__(self, data: bytes) -> None:
        root = parse(data)[0]
        filters = root.child(field_id("Filters"))
        if filters is None:
            raise Failure(f"{FILTERS_PATH}: no filter list")
        self.raw: dict[str, tuple[list[str], list[str], list[str]]] = {}
        for node in filters.children:
            def texts(label: str) -> list[str]:
                field = node.child(field_id(label))
                return [text(child) for child in field.children] if field is not None else []
            self.raw[text(node.child(field_id("Name")))] = (texts("InheritsFilters"), texts("IsCategories"),
                                                           texts("CollideWithCategories"))
        for name in (CREATURE_FILTER, PLAYER_FILTER):
            if name not in self.raw:
                raise Failure(f"{FILTERS_PATH}: no filter {name}")
        self.resolved: dict[str, tuple[frozenset, frozenset]] = {}

    def categories(self, name: str, stack: tuple[str, ...] = ()) -> tuple[frozenset, frozenset]:
        """@return (the categories of a filter, the categories that it collides with)."""
        known = self.resolved.get(name)
        if known is not None:
            return known
        raw = self.raw.get(name)
        if raw is None:
            return frozenset((name,)) if name else frozenset(), frozenset()
        own, collide = set(raw[1]), set(raw[2])
        for parent in raw[0]:
            if parent not in stack:
                parent_own, parent_collide = self.categories(parent, stack + (name,))
                own |= parent_own
                collide |= parent_collide
        result = (frozenset(own), frozenset(collide))
        if not stack:
            self.resolved[name] = result
        return result

    def flags(self, name: str) -> int:
        own, collide = self.categories(name)
        value = 0
        for flag, mover in ((BLOCKS_CREATURE, CREATURE_FILTER), (BLOCKS_PLAYER, PLAYER_FILTER)):
            mover_own, mover_collide = self.categories(mover)
            if own & mover_collide or collide & mover_own:
                value |= flag
        return value


def cgf_has_physics(data: bytes) -> bool:
    """@return True when a mesh file has a physics chunk (type 0x1018).

    file  := "CrCh" version:u32 chunkCount:u32 tableOffset:u32
    entry := type:u16 version:u16 id:u32 size:u32 offset:u32      (16 bytes, at tableOffset)
    """
    if len(data) < 16 or data[:4] != b"CrCh":
        raise Failure("the mesh file has no CrCh header")
    _version, count, table = struct.unpack_from("<3I", data, 4)
    if table + 16 * count > len(data):
        raise Failure("the chunk table of the mesh file is longer than the file")
    return any(struct.unpack_from("<H", data, table + 16 * index)[0] == 0x1018 for index in range(count))


def work_folder() -> Path:
    """@return The folder of the work files of the navigation tools (outside the repository)."""
    return Path(os.environ.get("SOULWARDEN_NAVIGATION_WORK") or Path.home() / "Games/soulwarden/work/navigation-build")


def region_origin(name: str) -> tuple[int, int]:
    match = re.fullmatch(r"r_([+-]\d+)_([+-]\d+)", name)
    if not match:
        raise Failure(f"{name} is not a region name (r_+XX_+YY)")
    return int(match[1]), int(match[2])


class Region:
    def __init__(self, world: World, name: str) -> None:
        self.world = world
        self.name = name
        self.index = region_origin(name)
        self.origin = (self.index[0] * REGION_SIZE, self.index[1] * REGION_SIZE)
        self.folder = world.folder + name + "/"
        self.counts = Counts()
        self.metadata: dict[tuple[str, str], Element] = {}
        self.variant_colliders: dict[tuple[str, str], list | None] = {}
        self.placements: dict[tuple, tuple] = {}
        self.shape_index: dict[tuple, int] = {}
        self.shape_keys: list[tuple] = []
        self.filters: dict[str, int] = {}
        self.heights: array | None = None

    # ---- the sources ------------------------------------------------------------------------

    def read(self, path: str) -> bytes:
        return self.world.paks.read(self.folder + path)

    def read_metadata(self) -> None:
        if not self.world.paks.has(self.folder + "region.slicedata"):
            return
        root = parse(self.read("region.slicedata"))[0]
        table = root.child(MAP_FIELD)
        if table is None:
            raise Failure(f"{self.name}: region.slicedata has no sliceMetaDataMap")
        for entry in table.children:
            if len(entry.children) != 2:
                continue
            key, value = entry.children
            # The letter case of a variant name is not the same in each file.
            self.metadata[(slice_key(text(key.child(SLICE_NAME_FIELD))),
                           text(key.child(VARIANT_NAME_FIELD)).lower())] = value
        self.counts.add("sliceMetadataEntries", len(self.metadata))

    def terrain_height(self, x: float, y: float) -> float:
        """@return The terrain height at a region position (bilinear between the four samples)."""
        heights = self.heights
        x = min(max(x, 0.0), REGION_SIZE - 1.0)
        y = min(max(y, 0.0), REGION_SIZE - 1.0)
        column, row = min(int(x), REGION_SIZE - 2), min(int(y), REGION_SIZE - 2)
        fx, fy = x - column, y - row
        base = row * REGION_SIZE + column
        low = heights[base] * (1 - fx) + heights[base + 1] * fx
        high = heights[base + REGION_SIZE] * (1 - fx) + heights[base + REGION_SIZE + 1] * fx
        return (low * (1 - fy) + high * fy) * HEIGHT_SCALE

    def water_height(self, x: float, y: float, terrain: float) -> float | None:
        """@return The water surface at a region position, or no value when the place has no water."""
        column, row = min(max(int(x), 0), REGION_SIZE - 1), min(max(int(y), 0), REGION_SIZE - 1)
        value = self.water_surface[row * REGION_SIZE + column]
        surface = None if value == NO_WATER else value / 32.0
        if terrain < self.world.ocean_level:
            surface = self.world.ocean_level if surface is None else max(surface, self.world.ocean_level)
        return surface

    # ---- placements -------------------------------------------------------------------------

    def add(self, key: tuple, matrix: tuple[float, ...], collision_filter: str, source: int, example: str,
            mark: int = MARK_NONE) -> None:
        parts = self.world.shape(key)
        if parts is None:
            self.counts.leave_out("the physics file does not decode" if key[0] == "rnr" else "a shape with no values",
                                  str(key[1]) if key[0] == "rnr" else example)
            return
        if not any(triangles for _, _, triangles in parts):
            self.counts.leave_out("the shape has no triangle", example)
            return
        if not all(math.isfinite(value) for value in matrix):
            self.counts.leave_out("the transform is not finite", example)
            return
        index = self.shape_index.get(key)
        if index is None:
            index = len(self.shape_keys)
            self.shape_index[key] = index
            self.shape_keys.append(key)
        local = (matrix[0], matrix[1], matrix[2], matrix[3] - self.origin[0],
                 matrix[4], matrix[5], matrix[6], matrix[7] - self.origin[1],
                 matrix[8], matrix[9], matrix[10], matrix[11])
        filter_index = self.filters.setdefault(collision_filter, len(self.filters))
        identity = (index, filter_index, mark) + tuple(round(value, 3) for value in local)
        if identity in self.placements:
            self.counts.add("placementsTwice")
            return
        self.placements[identity] = (index, filter_index, source, local, mark)
        self.counts.add("placements." + SOURCE_NAMES[source])
        if mark != MARK_NONE:
            self.counts.add("navigationMarks." + MARK_NAMES[mark])

    def count_marks(self, content: SliceContent, path: str) -> None:
        """Counts the navigation marks of one placed slice, and the marks that the tool does not use."""
        for area, count in content.navigation_areas.items():
            self.counts.add(f"navigationComponents.area{area}", count)
        for cause, count in content.navigation_not_read.items():
            for _ in range(count):
                self.counts.leave_out("navigation mark: " + cause, path)

    def add_collider(self, collider: Collider, matrix: tuple[float, ...], source: int, where: str) -> None:
        place = multiply(matrix, collider.matrix)
        example = f"{where}: {collider.name}"
        if collider.dynamic:
            self.counts.leave_out("a collider of a body that moves (m_isDynamic)", example)
            return
        if collider.kind == "rnr":
            path = self.world.catalog.path(collider.reference)
            if path is None or not self.world.paks.has(path):
                self.counts.leave_out("the physics file of the collider is not in the game copy", example)
                return
            self.add(("rnr", path), place, collider.filter, source, example, collider.mark)
        elif collider.kind == "mesh":
            path = physics_path(self.world.catalog, collider.reference)
            if path is None or not self.world.paks.has(path):
                mesh = self.world.catalog.path(collider.reference)
                if mesh is None or not self.world.paks.has(mesh):
                    self.counts.leave_out("the mesh of the collider is not in the game copy", mesh or example)
                elif self.world.mesh_has_physics(mesh):
                    self.counts.leave_out("NOT READ: the mesh of the collider has a physics chunk and no physics file",
                                          mesh)
                else:
                    self.counts.leave_out("the mesh of the collider has no physics data (no physics file, no "
                                          "physics chunk)", mesh)
                return
            self.add(("rnr", path), place, collider.filter, source, example, collider.mark)
        elif collider.kind == "shape":
            values, offset, kind, scale_shapes = collider.reference
            if values is None:
                self.counts.leave_out("a simple shape of a class that the tool does not read",
                                      f"{uuid.UUID(bytes=kind)} in {example}")
                return
            shifted = multiply(place, (1.0, 0.0, 0.0, offset[0], 0.0, 1.0, 0.0, offset[1], 0.0, 0.0, 1.0, offset[2]))
            if values[0] == "box":
                key = ("box", round(values[1], 4), round(values[2], 4), round(values[3], 4))
            elif values[0] == "sphere":
                key = ("sphere", round(values[1], 4))
            else:
                key = ("cylinder", round(values[1], 4), round(values[2], 4))
            self.add(key, shifted, MARK_FILTER if collider.mark == MARK_CUT else collider.filter, source, example,
                     collider.mark)
        else:
            self.counts.leave_out("a collider with no shape in the slice (no physics file, no mesh, no simple shape)",
                                  example)

    def variant(self, key: str, variant: str, content: SliceContent) -> list[Collider] | None:
        """@return The colliders of a slice with a variant: the meshes of the variant, not of the slice."""
        cache_key = (key, variant.lower())
        if cache_key in self.variant_colliders:
            return self.variant_colliders[cache_key]
        result: list[Collider] | None = None
        metadata = self.metadata.get(cache_key)
        if metadata is not None:
            mesh_colliders = [collider for collider in content.colliders
                              if collider.kind in ("mesh", "none") and collider.filter not in DEPLETED_FILTERS]
            mesh_filters = [collider.filter for collider in mesh_colliders]
            mesh_mark = mesh_colliders[0].mark if mesh_colliders else MARK_NONE
            result = [collider for collider in content.colliders if collider.kind in ("rnr", "shape")]
            meshes = metadata.child(MESHES_FIELD)
            if mesh_filters and number(metadata.child(HAS_COLLISION_FIELD)) and meshes is not None:
                for mesh in meshes.children:
                    asset = asset_id(mesh.child(MESH_ID_FIELD))
                    root = matrix_of(getattr(mesh.child(ROOT_TRANSFORM_FIELD), "value", None)) or IDENTITY
                    instances = mesh.child(INSTANCE_TRANSFORMS_FIELD)
                    matrices = [m for m in (matrix_of(node.value) for node in instances.children)
                                if m is not None] if instances is not None else []
                    if asset is None:
                        continue
                    for instance in (matrices or [IDENTITY]):
                        result.append(Collider(multiply(root, instance) if matrices else root, "mesh", asset,
                                               mesh_filters[0], f"variant {variant}", False, mesh_mark))
        self.variant_colliders[cache_key] = result
        return result

    def place_slice(self, path: str, variant: str, matrix: tuple[float, ...], source: int, depth: int,
                    stack: tuple[str, ...]) -> None:
        """Adds the colliders of a slice of the game at a place, and of each slice that it spawns at its start."""
        if depth > MAX_DEPTH or path in stack:
            self.counts.leave_out("a slice that spawns itself, or more than 12 levels of spawners", path)
            return
        content = self.world.slice(path)
        if content is None:
            self.counts.leave_out("the slice is not in the game copy", path)
            return
        self.counts.add("slicesPlaced")
        self.count_marks(content, path)
        if content.triggers:
            self.counts.add("engineCollidersWithNoStaticPhysics", content.triggers)
        for _ in range(content.disabled):
            self.counts.leave_out("a collider of the engine whose static physics is not on at its start "
                                  "(EnabledInitially is 0)", path)
        colliders = content.colliders
        if variant:
            changed = self.variant(slice_key(path), variant, content)
            if changed is None:
                if any(collider.kind in ("mesh", "none") for collider in colliders):
                    self.counts.leave_out("a variant with no metadata in region.slicedata", f"{path} @ {variant}")
                colliders = [collider for collider in colliders if collider.kind in ("rnr", "shape")]
            else:
                colliders = changed
        for collider in colliders:
            if collider.filter in DEPLETED_FILTERS:
                self.counts.leave_out("the collider of a used gatherable (a stump)", path)
                continue
            self.add_collider(collider, matrix, source, path)
        for spawner in content.spawners:
            if spawner.slice is None:
                if spawner.alias is not None:
                    self.counts.leave_out("a spawner of an alias (a choice of slices at run time: a creature)",
                                          self.world.catalog.path(spawner.alias) or path)
                continue
            if not spawner.on_start:
                self.counts.leave_out("a spawner that does not spawn at its start (m_spawnOnInitialize is 0)",
                                      self.world.catalog.path(spawner.slice) or path)
                continue
            child = self.world.catalog.path(spawner.slice)
            if child is None:
                self.counts.leave_out("the slice of a spawner is not in the asset catalog", path)
                continue
            self.place_slice(child, spawner.variant, multiply(matrix, spawner.matrix), source, depth + 1,
                             stack + (path,))

    def read_chunks(self) -> None:
        if not self.world.paks.has(self.folder + "region.chunks"):
            return
        root = parse(self.read("region.chunks"))[0]
        chunks = root.child(CHUNKS_FIELD)
        if chunks is None:
            raise Failure(f"{self.name}: region.chunks has no chunk list")
        for chunk in chunks.children:
            asset = asset_id(chunk.child(ASSET_FIELD))
            position = chunk.child(POSITION_FIELD)
            kind = number(chunk.child(CHUNK_TYPE_FIELD))
            path = self.world.catalog.path(asset)
            self.counts.add(f"chunks.type{kind}")
            if path is None or position is None or position.value is None or len(position.value) != 12:
                raise Failure(f"{self.name}: a chunk of region.chunks has no slice in the asset catalog")
            x, y, z = struct.unpack(">3f", position.value)
            base = (1.0, 0.0, 0.0, x, 0.0, 1.0, 0.0, y, 0.0, 0.0, 1.0, z)
            content = read_slice(self.world.paks.read(path))
            self.count_marks(content, path)
            if content.triggers:
                self.counts.add("engineCollidersWithNoStaticPhysics", content.triggers)
            for _ in range(content.disabled):
                self.counts.leave_out("a collider of the engine whose static physics is not on at its start "
                                      "(EnabledInitially is 0)", path)
            for collider in content.colliders:
                self.add_collider(collider, base, SOURCE_CHUNK, path)
            for spawner in content.spawners:
                if spawner.slice is None:
                    continue
                child = self.world.catalog.path(spawner.slice)
                if child is None or not spawner.on_start:
                    self.counts.leave_out("a spawner of a chunk that does not spawn at its start, or with no slice",
                                          path)
                    continue
                self.place_slice(child, spawner.variant, multiply(base, spawner.matrix), SOURCE_CHUNK, 1, (path,))

    def slice_path(self, name: str, asset: str) -> str | None:
        """@return The pak path of the slice of a placement: from its asset id, or from its name."""
        match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", asset or "")
        if match:
            path = self.world.catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16)))
            if path is not None and self.world.paks.has(path):
                return path
        key = slice_key(name)
        for path in (key + ".dynamicslice", "slices/" + key + ".dynamicslice"):
            if self.world.paks.has(path):
                return path
        return None

    def read_capitals(self) -> None:
        for path in self.world.paks.paths(self.folder + "capitals/", ".capitals.json"):
            try:
                document = json.loads(self.world.paks.read(path))
            except ValueError as error:
                raise Failure(f"{path}: not a JSON document: {error}") from error
            for placement in document.get("Capitals", []):
                name = str(placement.get("sliceName", ""))
                if name == PLOT:
                    self.counts.add("capitals.plot")
                    continue
                self.counts.add("capitals.slice")
                slice_path = self.slice_path(name, str(placement.get("sliceAssetId", "")))
                if slice_path is None:
                    self.counts.leave_out("the slice of a capitals placement is not in the game copy", name)
                    continue
                try:
                    matrix = quaternion_matrix(placement.get("rotation", {}), placement["worldPosition"],
                                               float(placement.get("scale", 1.0)))
                except (KeyError, TypeError, ValueError) as error:
                    raise Failure(f"{path}: a placement has no position: {error}") from error
                self.place_slice(slice_path, str(placement.get("variantName", "")), matrix, SOURCE_CAPITAL, 0, ())

    def read_distribution(self) -> None:
        """Reads region.distribution.

        file  := [mark:u16 = 0xB9D6 version:i16] count:u16 sliceName*count variantName*count
                 rows:u32 index:u16*rows position:u32*rows rotation:u32*rows scale:u8*rows
                 [u8*rows, only with the mark and a version of 1 or more]
                 2 * (n:u32, u32*n, u8*n)
        (the client reads the file with the function at image address 0x69DD030)
        name  := size:u8 text
        The client decodes a row at image address 0x6A01D50:
        position: x = (value >> 16) * 2048 / 65535, y = (value & 0xFFFF) * 2048 / 65535 (meters in the region)
        rotation: a quaternion. x = ((value >> 21) & 0x3FF) / 255 - 1, y = ((value >> 11) & 0x3FF) / 255 - 1,
                  z = (value & 0x7FF) / 511 - 1, w = sqrt(1 - min(1, x*x + y*y + z*z)), negative when bit 31
                  is 1. Then the four numbers are made a unit quaternion.
        scale:    value * 0.01.
        height:   the second byte array selects it: 0 the terrain height, 2 the water surface, each other
                  value the higher one of the two. An old file has no such array: each row has 0. A place
                  with no water has the terrain height.
        The two tails are not rows (packed positions with one byte each, purpose not known): not used.
        """
        if not self.world.paks.has(self.folder + "region.distribution"):
            return
        data = self.read("region.distribution")
        try:
            version = -1
            position = 0
            if struct.unpack_from("<H", data, 0)[0] == 0xB9D6:
                (version,) = struct.unpack_from("<h", data, 2)
                position = 4
            (count,) = struct.unpack_from("<H", data, position)
            position += 2
            names: list[str] = []
            for _ in range(count * 2):
                size = data[position]
                names.append(data[position + 1:position + 1 + size].decode("utf-8", "replace"))
                position += 1 + size
            (rows,) = struct.unpack_from("<I", data, position)
            position += 4
            indexes = struct.unpack_from(f"<{rows}H", data, position)
            position += rows * 2
            positions = struct.unpack_from(f"<{rows}I", data, position)
            position += rows * 4
            rotations = struct.unpack_from(f"<{rows}I", data, position)
            position += rows * 4
            scales = data[position:position + rows]
            position += rows
            sources = bytes(rows)
            if version >= 1:
                sources = data[position:position + rows]
                position += rows
            for _ in range(2):
                (extra,) = struct.unpack_from("<I", data, position)
                position += 4 + 5 * extra
            if position != len(data) or len(scales) != rows or len(sources) != rows:
                raise Failure(f"the file has {len(data)} bytes, and its tables end at {position}")
        except (struct.error, IndexError) as error:
            raise Failure(f"{self.folder}region.distribution: {error}") from error
        paths: dict[int, str | None] = {}
        for row in range(rows):
            index = indexes[row]
            if index >= count:
                raise Failure(f"{self.folder}region.distribution: row {row} names the slice {index} of {count}")
            self.counts.add("distribution.rows")
            if index not in paths:
                paths[index] = self.slice_path(names[index], "")
            path = paths[index]
            if path is None:
                self.counts.leave_out("the slice of a distribution row is not in the game copy", names[index])
                continue
            x = (positions[row] >> 16) * REGION_SIZE / 65535.0
            y = (positions[row] & 0xFFFF) * REGION_SIZE / 65535.0
            packed = rotations[row]
            qx = ((packed >> 21) & 0x3FF) / 255.0 - 1.0
            qy = ((packed >> 11) & 0x3FF) / 255.0 - 1.0
            qz = (packed & 0x7FF) / 511.0 - 1.0
            qw = math.sqrt(1.0 - min(1.0, qx * qx + qy * qy + qz * qz))
            if packed >> 31:
                qw = -qw
            height = self.terrain_height(x, y)
            if sources[row] != 0:
                self.counts.add(f"distribution.heightSource{sources[row]}")
                water = self.water_height(x, y, height)
                if water is not None:
                    height = water if sources[row] == 2 else max(height, water)
            matrix = quaternion_matrix({"x": qx, "y": qy, "z": qz, "w": qw},
                                       {"x": x + self.origin[0], "y": y + self.origin[1], "z": height},
                                       scales[row] / 100.0)
            self.place_slice(path, names[count + index], matrix, SOURCE_DISTRIBUTION, 0, ())

    # ---- the file ---------------------------------------------------------------------------

    def build(self) -> dict:
        start = time.perf_counter()
        files_before, bytes_before = self.world.paks.files_read, self.world.paks.bytes_read
        if not self.world.paks.has(self.folder + "region.heightmap"):
            raise Failure(f"{self.name}: the game copy has no region.heightmap for this region")
        self.heights = read_heightmap(self.read("region.heightmap"))
        plain_low, plain_high = min(self.heights), max(self.heights)
        cookies = read_cookies(self.world.paks, self.folder)
        if cookies and not (self.world.navcore.is_file() and self.world.noise.is_file()):
            raise Failure(f"{self.name}: the terrain rule needs the program {self.world.navcore} and the noise texture "
                          f"{self.world.noise}: make them with tools/gamedata/navigation.sh")
        self.heights, cookie_counts = apply_cookies(self.heights, self.origin, cookies, self.world.navcore, self.world.noise)
        if self.world.paks.has(self.folder + "region.waterqt"):
            self.water_surface, self.water_floor, water_counts = read_water(self.read("region.waterqt"))
        else:
            self.water_surface = array("H", [NO_WATER]) * (REGION_SIZE * REGION_SIZE)
            self.water_floor = array("H", [NO_WATER]) * (REGION_SIZE * REGION_SIZE)
            water_counts = {"nodes": 0, "waterNodes": 0, "waterArea": 0, "flags": {}, "smallestNode": 0}
        self.read_metadata()
        self.read_chunks()
        self.read_capitals()
        self.read_distribution()
        report = {
            "format": FORMAT_VERSION, "region": self.name, "origin": list(self.origin),
            "oceanLevel": self.world.ocean_level,
            "terrain": {"lowest": round(min(self.heights) * HEIGHT_SCALE, 3),
                        "highest": round(max(self.heights) * HEIGHT_SCALE, 3),
                        "lowestOfTheHeightMap": round(plain_low * HEIGHT_SCALE, 3),
                        "highestOfTheHeightMap": round(plain_high * HEIGHT_SCALE, 3),
                        "cookies": len(cookies), "cookiesUsed": cookie_counts.get("cookiesUsed", 0),
                        "samplesChangedByCookies": cookie_counts.get("sampleChanges", 0)},
            "water": {**water_counts, "flags": {f"0x{key:08X}": value for key, value in water_counts["flags"].items()}},
            "counts": dict(sorted(self.counts.values.items())),
            "placements": len(self.placements),
            "leftOut": dict(sorted(self.counts.left_out.items())),
            "leftOutExamples": self.counts.examples,
            "filters": {},
            "filesRead": self.world.paks.files_read - files_before,
            "packedBytesRead": self.world.paks.bytes_read - bytes_before,
        }
        by_filter = {index: 0 for index in self.filters.values()}
        for _, filter_index, _, _, _ in self.placements.values():
            by_filter[filter_index] += 1
        names = {index: name for name, index in self.filters.items()}
        report["filters"] = {}
        for index, count in sorted(by_filter.items()):
            flags = self.world.filters.flags(names[index])
            report["filters"][names[index] or "(no filter)"] = {
                "placements": count, "blocksCreature": bool(flags & BLOCKS_CREATURE),
                "blocksPlayer": bool(flags & BLOCKS_PLAYER)}
        report["seconds"] = round(time.perf_counter() - start, 2)
        return report

    def write(self, path: Path, report: dict) -> None:
        """Writes the data file of the region (all numbers little-endian).

        file      := "SWCL" version:u32 regionX:i32 regionY:i32 regionSize:u32 section*
        section   := tag:4 bytes size:u64 data
        "OCEN"    f32: the ocean level of the world (terrain.json): water is at each place whose
                  terrain is lower
        "HGHT"    2048 * 2048 u16: the terrain sample of (x, y) at index y * 2048 + x;
                  height = sample * 2048 / 65535 m
        "WSRF"    2048 * 2048 u16: the water surface in 1/32 m (0xFFFF: no water)
        "WFLR"    2048 * 2048 u16: the floor below the water in 1/32 m (0xFFFF: no water)
        "FILT"    count:u32 (flags:u8 size:u8 text)*: the collision filters. Flags bit 0: a collider
                  with this filter stops a creature; bit 1: it stops a player (see `Filters`)
        "SNAM"    count:u32 (size:u16 text)*: the name of each shape (its physics file, or its values)
        "SHAP"    count:u32 (firstPart:u32 partCount:u32)*
        "PART"    count:u32 (kind:u32 firstVertex:u32 vertexCount:u32 firstIndex:u32 indexCount:u32)*
                  kind 1: a closed convex solid; 2: a closed mesh; 0: an open mesh. The
                  triangles of kind 1 and 2 have their front to the outside
        "VERT"    f32 * 3 for each vertex
        "INDX"    u32 for each index: three for each triangle, relative to firstVertex of the part
        "PLAC"    count:u32 (shape:u32 filter:u16 source:u8 mark:u8 matrix:f32 * 12)*
                  matrix: three rows of a 3 x 4 matrix from the shape to region coordinates
                  (meters; add regionX * 2048 and regionY * 2048 for world coordinates)
                  source: 0 chunk, 1 capitals placement, 2 distribution row
                  mark: the navigation mark of the level designers (nav_slices.py). 0 none;
                  1 a solid whose top is no floor; 2 no solid: a volume in which no floor is
                  free; 3 a solid that the designers marked as a floor (no effect)
        """
        parts_table = bytearray()
        shapes_table = bytearray()
        names_table = bytearray()
        vertices = array("f")
        indexes = array("I")
        part_count = 0
        for key in self.shape_keys:
            parts = self.world.shape(key)
            shapes_table += struct.pack("<2I", part_count, len(parts))
            name = (key[1] if key[0] == "rnr" else " ".join(str(value) for value in key)).encode("utf-8")[:65535]
            names_table += struct.pack("<H", len(name)) + name
            for convex, part_vertices, part_triangles in parts:
                parts_table += struct.pack("<5I", convex, len(vertices) // 3, len(part_vertices) // 3,
                                           len(indexes), len(part_triangles))
                vertices.extend(part_vertices)
                indexes.extend(part_triangles)
                part_count += 1
        filters_table = bytearray()
        for name, _ in sorted(self.filters.items(), key=lambda item: item[1]):
            encoded = name.encode("utf-8")[:255]
            filters_table += struct.pack("<2B", self.world.filters.flags(name), len(encoded)) + encoded
        placements = bytearray()
        for shape, filter_index, source, matrix, mark in self.placements.values():
            placements += struct.pack("<IH2B12f", shape, filter_index, source, mark, *matrix)
        if sys.byteorder != "little":
            for values in (vertices, indexes, self.heights, self.water_surface, self.water_floor):
                values.byteswap()
        sections = [
            (b"OCEN", struct.pack("<f", self.world.ocean_level)),
            (b"HGHT", self.heights.tobytes()), (b"WSRF", self.water_surface.tobytes()),
            (b"WFLR", self.water_floor.tobytes()),
            (b"FILT", struct.pack("<I", len(self.filters)) + bytes(filters_table)),
            (b"SNAM", struct.pack("<I", len(self.shape_keys)) + bytes(names_table)),
            (b"SHAP", struct.pack("<I", len(self.shape_keys)) + bytes(shapes_table)),
            (b"PART", struct.pack("<I", part_count) + bytes(parts_table)),
            (b"VERT", vertices.tobytes()), (b"INDX", indexes.tobytes()),
            (b"PLAC", struct.pack("<I", len(self.placements)) + bytes(placements)),
        ]
        temporary = path.with_suffix(".swcol.part")
        with open(temporary, "wb") as handle:
            handle.write(b"SWCL" + struct.pack("<I2iI", FORMAT_VERSION, self.index[0], self.index[1], REGION_SIZE))
            for tag, data in sections:
                handle.write(tag + struct.pack("<Q", len(data)))
                handle.write(data)
        os.replace(temporary, path)
        triangles = 0
        shape_triangles = [sum(len(t) // 3 for _, _, t in self.world.shape(key)) for key in self.shape_keys]
        for shape, _, _, _, _ in self.placements.values():
            triangles += shape_triangles[shape]
        report["shapes"] = len(self.shape_keys)
        report["shapeTriangles"] = sum(shape_triangles)
        report["placedTriangles"] = triangles
        report["bytes"] = path.stat().st_size


def make_region(world: World, name: str, out: Path, force: bool = False, stamp: int = 0) -> dict | None:
    """Makes the collision data file of one region. @return Its report, or no value when the region was complete."""
    report_path = out / f"{name}.json"
    if not force and report_path.is_file() and (out / f"{name}.swcol").is_file():
        try:
            known = json.loads(report_path.read_text())
            if known.get("complete") and known.get("format") == FORMAT_VERSION and known.get("tool", 0) == stamp:
                return None
        except ValueError:
            pass
    region = Region(world, name)
    report = region.build()
    region.write(out / f"{name}.swcol", report)
    # The terrain after the cookies, for the terrain height tool (height_maps.py).
    terrain = out.parent / "terrain"
    terrain.mkdir(parents=True, exist_ok=True)
    samples = array("H", region.heights)
    if sys.byteorder != "little":
        samples.byteswap()
    temporary = terrain / f"{name}.u16.part"
    temporary.write_bytes(samples.tobytes())
    os.replace(temporary, terrain / f"{name}.u16")
    report["physicsFilesNotDecoded"] = {path: cause for path, cause in world.shape_failures.items()}
    report["tool"] = stamp
    report["complete"] = True
    report_path.write_text(json.dumps(report, indent=1) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Write the collision data of the world of the game, region by region.")
    parser.add_argument("regions", nargs="*", help="region names (r_+04_+02)")
    parser.add_argument("--all", action="store_true", help="each region of the world")
    parser.add_argument("--out", help="output folder (default <SOULWARDEN_NAVIGATION_WORK>/collision)")
    parser.add_argument("--force", action="store_true", help="make a region again that is complete")
    parser.add_argument("--world", default=DEFAULT_WORLD)
    arguments = parser.parse_args()
    base = work_folder()
    out = Path(arguments.out) if arguments.out else base / "collision"
    try:
        world = World(arguments.world, base / "cache")
        names = world.regions() if arguments.all else arguments.regions
        if not names:
            parser.error("give a region name, or --all")
        out.mkdir(parents=True, exist_ok=True)
        for name in names:
            report = make_region(world, name, out, arguments.force)
            if report is None:
                print(f"{name}: complete already", flush=True)
                continue
            left = sum(report["leftOut"].values())
            print(f"{name}: {report['placements']} placements, {report['shapes']} shapes, "
                  f"{report['placedTriangles']} triangles, {left} left out, {report['bytes']} bytes, "
                  f"{report['seconds']} s", flush=True)
    except Failure as error:
        print(f"collision_data: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

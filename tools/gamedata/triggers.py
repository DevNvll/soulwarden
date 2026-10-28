#!/usr/bin/env python3
"""Write the trigger volumes of the quests (stage 1.8b, docs/systems/quests.md) as one JSON file, trigger-data.json.

The backend reads GAMEDATA_DIR/trigger-data.json at its start: the volume of each tag that a quest task names (TaskTriggerArea, column
TP_TargetTag). The server tests the accepted position of a character against the volumes of the tag of an active task. Without the file
the backend starts, and a task of a trigger area waits.

The sources (all in the pak files of the game copy):

  1. The chunk list of each region (sharedassets/coatlicue/WORLD/regions/REGION/region.chunks): the chunk slices of the layers of the
     region and the WorldPosition of each one. The transform of an entity of a chunk is relative to that position (the rule of the
     collision tool, collision_data.py).
  2. The chunk slices (coatgen/HASH/LAYER_..._chunk_X_Y_LEVEL.dynamicslice). A trigger of a quest is an entity with the component
     ObjectiveDetectionVolumeComponent (class 544d4993-...): its server facet has m_targetTag, the tag of the task (live check T1 of stage
     1.8b and research quest-progress.md 7.1: the entity "06_getoffbeach_Aelstrom Intro_WW_IGC_Trigger" has the tag Aelstrom_IGC_Trigger).
     The same entity has a DetectionVolumeComponent (class 41bc3bb3-...; a few have none and only the shape component below): m_center
     (an offset in the entity), m_shape (one query shape: QueryShapeAabb with m_aabb, QueryShapeBox with m_box, QueryShapeSphere with
     m_Radius, QueryShapeCylinder and QueryShapeCapsule with m_Height and m_Radius), m_ignoreHeight, and m_strFilter. A volume with no
     query shape uses the shape component of its entity (a decision of an agent: 88 trigger volumes of the build have no query shape and
     a shape component, the box with Dimensions, the full size, a sphere, a cylinder, or a capsule with Radius and Height; nav_slices.py
     reads the same components). The entity transform
     (m_worldTM: three basis vectors, then the translation) places it in its chunk.

  3. Stage QG3: the capitals of each region (regions/REGION/capitals/LAYER/LAYER.capitals.json): each placement (not "<PLOT>") names a
     slice that the game makes at run time, at the position, the rotation, and the scale of the placement (the rule of the collision
     tool, collision_data.py). The tool reads the trigger entities of that slice, and of each slice that a slice spawns at its start
     (the spawner component b52d0d52-..., m_sliceAsset, m_spawnOnInitialize 1; at most 12 levels), also of a spawner of a chunk. A
     spawner that does not spawn at its start (a script or an event spawns it) and an alias spawner are counted; their volumes are not
     in `volumes` (a volume of a spawner with no start is in `notAtStart`, with its tag).
  4. Stage QG3: the other worlds (an instance: a dungeon, a trial, the introduction worlds ftue_v2 and climaxftue_02): the same walk of
     their capitals. Their volumes are in `elsewhere`, each with its world, never in `volumes`: the backend never tests them in the main
     world. A volume belongs to its instance stage.

The client sends no message for these volumes (live check T1 of stage 1.8b: no message 560 at the entry of Aelstrom_IGC_Trigger; the
trigger is local, PlayerOnly, and not synchronized): the server tests the position itself.

Output (one line of JSON):
  {"format": 1, "world": TEXT,
   "volumes": [{"tag", "shape", "center": [X, Y, Z], "axes": [9 numbers: the unit axes X, Y, Z of the volume in world coordinates],
                "half": [X, Y, Z], "radius", "height", "ignoreHeight", "filter", "source", "via", "world"}, ...],
   "notAtStart": [{"tag", "source", "via"}, ...], "elsewhere": [{"tag", "world", "source", "via"}, ...],
   "tasks": {"tags": N, "found": N, "missing": [TAG, ...], "elsewhere": {TAG: [WORLD, ...]}, "notAtStart": [TAG, ...]},
   "counts": {...}}
  "source" is the slice of the entity, "via" the placement that put it in the world (a chunk slice, or a capitals file and its row);
  "world" the world of the volume (stage QG3).
  "shape" is aabb, box, sphere, cylinder, or capsule. "half" is the half size of a box on its axes (aabb: on the world axes); "radius" and
  "height" (the full height along the Z axis of the volume) are the sizes of a sphere, a cylinder, or a capsule. The sizes of a query shape
  are the authored sizes with the scale of the entity in them (a decision of an agent from the data: the m_aabb of TriggerVol_06_Reese is
  6.66, the half size of its box component 2 x 2 x 2 with the scale 6.66; Aelstrom_IGC_Trigger has 10, 10, 5 with the scale 1 and the box
  20 x 20 x 10). The size of a shape component has the scale of the entity applied. "tasks" compares the tags of the quest data
  (GAMEDATA_DIR/quest-data.json, or --quests) with the tags of the volumes.

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when a
trigger entity has no detection volume or a shape that the tool does not know, or when a number is not finite. A volume with no tag is
counted, not written. tools/gamedata/check_triggers_tool.py is the check of these rules, with made-up entities.

Usage: triggers.py [OUTPUT.json] [--region NAME]... [--quests QUEST_DATA.json]
Default output: <SOULWARDEN_GAMEDATA_DIR>/trigger-data.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see nav_assets.py), SOULWARDEN_NAVIGATION_WORK (the folder of the index of the
             pak files; default ~/Games/soulwarden/work/navigation-build).
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
import uuid
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from collision_data import PLOT, quaternion_matrix, slice_key  # noqa: E402
from nav_assets import CATALOG_PATH, Catalog, Element, Failure, Paks, asset_id, field_id, number, type_bytes, walk  # noqa: E402
from nav_shapes import IDENTITY, multiply  # noqa: E402
from nav_slices import ALIAS_FIELD, ON_START_FIELD, SLICE_ASSET_FIELD, SPAWNER_TYPE, matrix_of  # noqa: E402

OUTPUT_NAME = "trigger-data.json"
FORMAT = 1
DEFAULT_WORLD = "newworld_vitaeeterna"
# Stage QG3: the levels of spawners that the walk follows (the limit of the collision tool).
MAX_DEPTH = 12

ENTITY_TYPE = type_bytes("75651658-8663-478d-9090-2432dfcafa44")
TRANSFORM_TYPE = type_bytes("484ae67d-abd0-4d9c-b2c8-9bb0eec900e0")
VOLUME_TYPE = type_bytes("41bc3bb3-ef30-41e4-b751-12c4eab97c0e")            # DetectionVolumeComponent
OBJECTIVE_VOLUME_TYPE = type_bytes("544d4993-2ec6-4e33-aa92-7b3701981acd")  # ObjectiveDetectionVolumeComponent
# The query shapes of a detection volume (class ids of the client text table; spells.py and nav_slices.py read the same classes).
SHAPES = {
    type_bytes("27462017-fe0f-4b81-96e9-8875b750ec69"): "aabb",
    type_bytes("c6651a66-23d4-4508-b4ad-180c516655a8"): "box",
    type_bytes("7f2ef312-4089-4582-89c5-5d4156daa7fb"): "sphere",
    type_bytes("709b11ea-fd56-4fef-b841-7cea549368e6"): "cylinder",
    type_bytes("7495c65c-9193-4193-bbb2-de3343b9eb03"): "capsule",
}

# The shape components of the engine (the shape of a volume with no query shape; nav_slices.py has the same class ids).
ENGINE_SHAPES = {
    type_bytes("5edf4b9e-0d3d-40b8-8c91-5142bcfc30a6"): "box",
    type_bytes("e24cbff0-2531-4f8d-a8ab-47af4d54bcd2"): "sphere",
    type_bytes("b0c6aa97-e754-4e33-8d32-33e267db622f"): "cylinder",
    type_bytes("967ec13d-364d-4696-ab5c-c00cc05a2305"): "capsule",
}
DIMENSIONS_FIELD = field_id("Dimensions")
ENGINE_RADIUS_FIELD = field_id("Radius")
ENGINE_HEIGHT_FIELD = field_id("Height")
COMPONENTS_FIELD = field_id("Components")
NAME_FIELD = field_id("Name")
WORLD_FIELD = field_id("m_worldTM")
TAG_FIELD = field_id("m_targetTag")
CENTER_FIELD = field_id("m_center")
SHAPE_FIELD = field_id("m_shape")
AABB_FIELD = field_id("m_aabb")
BOX_FIELD = field_id("m_box")
RADIUS_FIELD = field_id("m_Radius")
HEIGHT_FIELD = field_id("m_Height")
IGNORE_HEIGHT_FIELD = field_id("m_ignoreHeight")
FILTER_FIELD = field_id("m_strFilter")
CHUNKS_FIELD = field_id("Chunks")
ASSET_FIELD = field_id("AssetId")
POSITION_FIELD = field_id("WorldPosition")


def vector(element: Element | None, what: str) -> tuple[float, float, float]:
    if element is None or element.value is None or len(element.value) != 12:
        raise Failure(f"{what} is not three 32-bit floats")
    values = struct.unpack(">3f", element.value)
    if not all(math.isfinite(value) for value in values):
        raise Failure(f"{what} is not finite")
    return values


def real(element: Element | None, what: str) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        raise Failure(f"{what} is not a 32-bit float")
    value = struct.unpack(">f", element.value)[0]
    if not math.isfinite(value):
        raise Failure(f"{what} is not finite")
    return value


def flag(element: Element | None) -> bool:
    return element is not None and element.value is not None and element.value[:1] == b"\x01"


def text(element: Element | None) -> str:
    return element.value.rstrip(b"\0").decode("utf-8", "replace") if element is not None and element.value else ""


def component(components: Element, kind: bytes) -> Element | None:
    for child in components.children:
        if child.type_id == kind:
            return child
    return None


def find(element: Element, name: int) -> Element | None:
    """@return The first element below `element` (its own children first) with this field name."""
    for node in walk(element):
        if node is not element and node.name == name:
            return node
    return None


def volume_of(entity: Element, base: tuple[float, ...], source: str) -> dict | None:
    """@return The volume of one trigger entity in world coordinates (`base`: the matrix of its chunk), or None with no tag."""
    components = entity.child(COMPONENTS_FIELD)
    marker = component(components, OBJECTIVE_VOLUME_TYPE) if components is not None else None
    if marker is None:
        return None
    name = text(entity.child(NAME_FIELD)) or "?"
    tag = text(find(marker, TAG_FIELD)).strip()
    if not tag:
        return None
    detection = component(components, VOLUME_TYPE)
    transform = component(components, TRANSFORM_TYPE)
    local = matrix_of(find(transform, WORLD_FIELD).value) if transform is not None and find(transform, WORLD_FIELD) is not None else None
    if local is None:
        raise Failure(f"{source}: the trigger {name} ({tag}) has no transform")
    matrix = multiply(base, local)
    center_local = vector(detection.child(CENTER_FIELD), f"{source}: m_center of {name}") \
        if detection is not None and detection.child(CENTER_FIELD) is not None else (0.0, 0.0, 0.0)
    shapes = detection.child(SHAPE_FIELD) if detection is not None else None
    shape = shapes.children[0] if shapes is not None and shapes.children else None
    kind = SHAPES.get(shape.type_id) if shape is not None else None
    engine = None
    if shape is None:
        engine = next((child for child in components.children if child.type_id in ENGINE_SHAPES), None)
        if engine is None:
            return {"tag": tag, "noShape": True}
        kind = ENGINE_SHAPES[engine.type_id]
    elif kind is None:
        raise Failure(f"{source}: the trigger {name} ({tag}) has a query shape that the tool does not know")
    cx, cy, cz = center_local
    center = (matrix[0] * cx + matrix[1] * cy + matrix[2] * cz + matrix[3], matrix[4] * cx + matrix[5] * cy + matrix[6] * cz + matrix[7],
              matrix[8] * cx + matrix[9] * cy + matrix[10] * cz + matrix[11])
    # The axes of the volume: the columns of the matrix, as unit vectors, and the scale of each one.
    columns = [(matrix[0], matrix[4], matrix[8]), (matrix[1], matrix[5], matrix[9]), (matrix[2], matrix[6], matrix[10])]
    scales = [math.sqrt(sum(part * part for part in column)) for column in columns]
    if any(scale <= 0.0 or not math.isfinite(scale) for scale in scales):
        raise Failure(f"{source}: the transform of the trigger {name} ({tag}) has an axis of no length")
    axes = [part / scales[index] for index, column in enumerate(columns) for part in column]
    volume = {"tag": tag, "shape": kind, "center": [round(value, 4) for value in center], "axes": [round(value, 6) for value in axes],
              "ignoreHeight": detection is not None and flag(detection.child(IGNORE_HEIGHT_FIELD)),
              "filter": text(detection.child(FILTER_FIELD)) if detection is not None else "",
              "source": source.rsplit("/", 1)[-1]}
    if engine is not None:
        volume["entityShape"] = True
        if kind == "box":
            size = vector(find(engine, DIMENSIONS_FIELD), f"{source}: Dimensions of {name}")
            volume["half"] = [round(size[index] / 2.0 * scales[index], 4) for index in range(3)]
        else:
            volume["radius"] = round(real(find(engine, ENGINE_RADIUS_FIELD), f"{source}: Radius of {name}") * max(scales[0], scales[1]), 4)
            if kind != "sphere":
                volume["height"] = round(real(find(engine, ENGINE_HEIGHT_FIELD), f"{source}: Height of {name}") * scales[2], 4)
        return volume
    if kind in ("aabb", "box"):
        half = vector(find(shape, AABB_FIELD if kind == "aabb" else BOX_FIELD), f"{source}: the half size of {name}")
        if kind == "aabb":
            volume["axes"] = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]  # An axis-aligned box keeps the world axes.
        volume["half"] = [round(value, 4) for value in half]
    else:
        volume["radius"] = round(real(find(shape, RADIUS_FIELD), f"{source}: m_Radius of {name}"), 4)
        if kind != "sphere":
            volume["height"] = round(real(find(shape, HEIGHT_FIELD), f"{source}: m_Height of {name}"), 4)
    return volume


def slice_volumes(roots: list[Element], base: tuple[float, ...], source: str, counts: Counter) -> list[dict]:
    volumes = []
    for root in roots:
        for node in walk(root):
            if node.type_id != ENTITY_TYPE:
                continue
            components = node.child(COMPONENTS_FIELD)
            if components is None or component(components, OBJECTIVE_VOLUME_TYPE) is None:
                continue
            volume = counted(volume_of(node, base, source), counts)
            if volume is not None:
                volumes.append(volume)
    return volumes


def counted(volume: dict | None, counts: Counter) -> dict | None:
    """@return The volume of a trigger entity, or None (counted) when it has no tag or no shape."""
    counts["triggerEntities"] += 1
    if volume is None:
        counts["triggersWithNoTag"] += 1
        return None
    if volume.get("noShape"):
        counts["triggersWithNoShape"] += 1
        return None
    counts["volumesWithAnEntityShape" if volume.get("entityShape") else "volumesWithAQueryShape"] += 1
    return volume


class Walk:
    """Stage QG3: the placed slices of one world (the chunk slices, the capitals placements, and the slices that they spawn) and the trigger
    volumes in them. A spawner follows the rule of the collision tool: m_sliceAsset with m_spawnOnInitialize 1, at most MAX_DEPTH
    levels, no slice that spawns itself. The volumes below a spawner with no start go to `not_at_start`."""

    def __init__(self, paks: Paks, catalog: Catalog, world: str, counts: Counter) -> None:
        self.paks = paks
        self.catalog = catalog
        self.world = world
        self.counts = counts
        self.cache: dict[str, tuple[list[Element], list[tuple]]] = {}
        self.volumes: list[dict] = []
        self.not_at_start: list[dict] = []

    def content(self, path: str, data: bytes | None = None) -> tuple[list[Element], list[tuple]]:
        """@return The trigger entities and the spawners (matrix, slice asset, alias asset, at start) of a slice."""
        found = self.cache.get(path)
        if found is not None:
            return found
        from nav_assets import parse  # noqa: E402 (the parser of the object streams)
        keep = data is None  # A chunk (its data given) is read one time; a slice of a capital or a spawner can be placed many times.
        data = self.paks.read(path) if data is None else data
        triggers: list[Element] = []
        spawners: list[tuple] = []
        if OBJECTIVE_VOLUME_TYPE in data or SPAWNER_TYPE in data:
            for root in parse(data):
                for node in walk(root):
                    if node.type_id != ENTITY_TYPE:
                        continue
                    components = node.child(COMPONENTS_FIELD)
                    if components is None:
                        continue
                    if component(components, OBJECTIVE_VOLUME_TYPE) is not None:
                        triggers.append(node)
                    spawner = component(components, SPAWNER_TYPE)
                    if spawner is not None:
                        transform = component(components, TRANSFORM_TYPE)
                        world = find(transform, WORLD_FIELD) if transform is not None else None
                        local = (matrix_of(world.value) if world is not None else None) or IDENTITY
                        spawners.append((local, asset_id(spawner.child(SLICE_ASSET_FIELD)), asset_id(spawner.child(ALIAS_FIELD)),
                                         number(spawner.child(ON_START_FIELD)) != 0))
        found = (triggers, spawners)
        if keep:
            self.cache[path] = found
        return found

    def place(self, path: str, matrix: tuple[float, ...], via: str, depth: int = 0, stack: tuple[str, ...] = (), at_start: bool = True,
              data: bytes | None = None) -> None:
        if depth > MAX_DEPTH or path in stack:
            self.counts["slicesThatSpawnThemselvesOrTooDeep"] += 1
            return
        triggers, spawners = self.content(path, data)
        for entity in triggers:
            volume = counted(volume_of(entity, matrix, path), self.counts)
            if volume is None:
                continue
            volume["via"] = via
            volume["world"] = self.world
            if depth > 0 and at_start:
                self.counts["volumesOfASpawnedSlice"] += 1  # a volume of `volumes` below a spawner
            (self.volumes if at_start else self.not_at_start).append(volume)
        for local, asset, alias, on_start in spawners:
            if asset is None:
                if alias is not None:
                    self.counts["aliasSpawners"] += 1  # a choice of slices at run time (a creature)
                continue
            child = self.catalog.path(asset)
            if child is None or not self.paks.has(child):
                self.counts["spawnersWithNoSlice"] += 1
                continue
            if not on_start:
                self.counts["spawnersNotAtStart"] += 1
            self.place(child, multiply(matrix, local), via, depth + 1, stack + (path,), at_start and on_start)

    def slice_path(self, name: str, asset: str) -> str | None:
        """@return The pak path of the slice of a capitals placement: from its asset id, or from its name (collision_data.py)."""
        match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", asset or "")
        if match:
            path = self.catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16)))
            if path is not None and self.paks.has(path):
                return path
        key = slice_key(name)
        for path in (key + ".dynamicslice", "slices/" + key + ".dynamicslice"):
            if self.paks.has(path):
                return path
        return None

    def chunks(self, folder: str, region: str, seen: set[str]) -> None:
        from nav_assets import parse  # noqa: E402
        if not self.paks.has(folder + region + "/region.chunks"):
            return
        root = parse(self.paks.read(folder + region + "/region.chunks"))[0]
        chunks = root.child(CHUNKS_FIELD)
        if chunks is None:
            raise Failure(f"{region}: region.chunks has no chunk list")
        for chunk in chunks.children:
            path = self.catalog.path(asset_id(chunk.child(ASSET_FIELD)))
            position = chunk.child(POSITION_FIELD)
            if path is None or position is None or position.value is None or len(position.value) != 12:
                raise Failure(f"{region}: a chunk of region.chunks has no slice in the asset catalog")
            self.counts["chunks"] += 1
            if path in seen:
                continue  # A chunk that two regions list (a border chunk) is read one time.
            seen.add(path)
            data = self.paks.read(path)
            if OBJECTIVE_VOLUME_TYPE not in data and SPAWNER_TYPE not in data:
                continue
            if OBJECTIVE_VOLUME_TYPE in data:
                self.counts["chunksWithTriggers"] += 1
            x, y, z = struct.unpack(">3f", position.value)
            self.place(path, (1.0, 0.0, 0.0, x, 0.0, 1.0, 0.0, y, 0.0, 0.0, 1.0, z), "chunk " + path, data=data)

    def capitals(self, folder: str, region: str) -> None:
        for path in self.paks.paths(f"{folder}{region}/capitals/", ".capitals.json"):
            try:
                document = json.loads(self.paks.read(path))
            except ValueError as error:
                raise Failure(f"{path}: not a JSON document: {error}") from error
            for row, placement in enumerate(document.get("Capitals") or []):
                name = str(placement.get("sliceName", ""))
                if name == PLOT:
                    continue
                self.counts["capitals"] += 1
                target = self.slice_path(name, str(placement.get("sliceAssetId", "")))
                if target is None:
                    self.counts["capitalsWithNoSlice"] += 1
                    continue
                try:
                    matrix = quaternion_matrix(placement.get("rotation", {}), placement["worldPosition"], float(placement.get("scale", 1.0)))
                except (KeyError, TypeError, ValueError) as error:
                    raise Failure(f"{path}: a placement has no position: {error}") from error
                self.place(target, matrix, f"capitals {path[len(folder):]} row {row}")


def build(paks: Paks, world: str, regions: list[str] | None, task_tags: set[str] | None) -> dict:
    if not paks.has(CATALOG_PATH):
        raise Failure(f"the game copy has no {CATALOG_PATH}")
    catalog = Catalog(paks.read(CATALOG_PATH), (".dynamicslice",))
    folder = f"sharedassets/coatlicue/{world}/regions/"
    names = sorted({path[len(folder):].split("/", 1)[0] for path in paks.paths(folder, "region.chunks")})
    if regions:
        unknown = [region for region in regions if region not in names]
        if unknown:
            raise Failure(f"the world {world} has no region {', '.join(unknown)}")
        names = regions
    if not names:
        raise Failure(f"the game copy has no region of the world {world}")
    counts: Counter = Counter()
    main = Walk(paks, catalog, world, counts)
    seen: set[str] = set()
    for region in names:
        counts["regions"] += 1
        main.chunks(folder, region, seen)
        main.capitals(folder, region)
    volumes = unique(main.volumes)
    # Stage QG3: the other worlds (the instances), with their world: never in `volumes`.
    elsewhere: list[dict] = []
    if not regions:
        others = Counter()
        worlds = sorted({path.split("/")[2] for path in paks.paths("sharedassets/coatlicue/", ".capitals.json")} - {world})
        for other in worlds:
            walker = Walk(paks, catalog, other, others)
            base = f"sharedassets/coatlicue/{other}/regions/"
            for region in sorted({path[len(base):].split("/", 1)[0] for path in paks.paths(base, ".capitals.json")}):
                walker.chunks(base, region, set())
                walker.capitals(base, region)
            elsewhere.extend({"tag": volume["tag"], "world": other, "source": volume["source"], "via": volume["via"],
                              "center": volume["center"]} for volume in unique(walker.volumes + walker.not_at_start))
        counts["otherWorlds"] = len(worlds)
        counts["elsewhereVolumes"] = len(elsewhere)
    not_at_start = [{"tag": volume["tag"], "source": volume["source"], "via": volume["via"], "center": volume["center"]}
                    for volume in unique(main.not_at_start)]
    counts["volumes"] = len(volumes)
    counts["tags"] = len({volume["tag"].lower() for volume in volumes})
    counts["notAtStartVolumes"] = len(not_at_start)
    document = {"format": FORMAT, "world": world, "volumes": volumes, "notAtStart": not_at_start,
                "elsewhere": sorted(elsewhere, key=lambda volume: (volume["tag"].lower(), volume["world"], volume["center"])),
                "counts": dict(counts)}
    if task_tags is not None:
        found = {volume["tag"].lower() for volume in volumes}
        missing = sorted(task_tags - found)
        worlds_of: dict[str, set[str]] = {}
        for volume in elsewhere:
            worlds_of.setdefault(volume["tag"].lower(), set()).add(volume["world"])
        later = {volume["tag"].lower() for volume in not_at_start}
        document["tasks"] = {"tags": len(task_tags), "found": len(task_tags & found), "missing": missing,
                             "elsewhere": {tag: sorted(worlds_of[tag]) for tag in missing if tag in worlds_of},
                             "notAtStart": [tag for tag in missing if tag in later]}
    return document


def unique(volumes: list[dict]) -> list[dict]:
    """@return The volumes with no second copy of one volume (the same tag, shape, center, and source), sorted by tag and center."""
    seen = set()
    result = []
    for volume in sorted(volumes, key=lambda volume: (volume["tag"].lower(), volume["center"], volume["source"], volume["via"])):
        key = (volume["tag"].lower(), volume.get("shape"), tuple(volume["center"]), volume["source"])
        if key in seen:
            continue
        seen.add(key)
        result.append(volume)
    return result


def quest_tags(path: Path) -> set[str] | None:
    """@return The lower-case TP_TargetTag of each TaskTriggerArea of a quest data file, or None when the file is not there."""
    if not path.is_file():
        return None
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise Failure(f"{path} is not a quest data file: {error}") from error
    tags = set()
    for task in document.get("tasks", {}).values():
        if task.get("type") == "tasktriggerarea" and task.get("cells", {}).get("TP_TargetTag", "").strip():
            tags.add(task["cells"]["TP_TargetTag"].strip().lower())
    return tags


def main() -> int:
    home = Path.home()
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    work = Path(os.environ.get("SOULWARDEN_NAVIGATION_WORK") or home / "Games/soulwarden/work/navigation-build")
    parser = argparse.ArgumentParser(description="Write the trigger volumes of the quests (one JSON file).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--region", action="append", help="only these regions (a file for a test)")
    parser.add_argument("--quests", default=str(gamedata / "quest-data.json"), help="the quest data for the comparison of the tags")
    parser.add_argument("--world", default=DEFAULT_WORLD)
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Paks(work / "pak-index.pickle"), arguments.world, arguments.region, quest_tags(Path(arguments.quests)))
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(f".{target.name}.{os.getpid()}.tmp")
        try:
            partial.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
            partial.replace(target)
        finally:
            partial.unlink(missing_ok=True)
    except Failure as error:
        print(f"triggers: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"triggers: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    tasks = document.get("tasks")
    print(f"{target}: {counts['volumes']} trigger volumes with {counts['tags']} tags in {counts.get('chunksWithTriggers', 0)} of "
          f"{counts['chunks']} chunks and {counts.get('capitals', 0)} capitals placements of {counts['regions']} regions "
          f"({counts.get('volumesOfASpawnedSlice', 0)} of a spawned slice); {counts.get('triggersWithNoTag', 0)} with no tag; "
          f"{counts.get('notAtStartVolumes', 0)} volumes of a spawner with no start; {counts.get('elsewhereVolumes', 0)} volumes of "
          f"{counts.get('otherWorlds', 0)} other worlds"
          + (f"; the quest data names {tasks['tags']} tags, {tasks['found']} with a volume" if tasks else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

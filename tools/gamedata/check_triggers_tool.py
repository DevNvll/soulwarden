#!/usr/bin/env python3
"""Check the rules of the trigger tool (triggers.py, stage 1.8b) with made-up entities: no game copy and no pak file.

Each case builds the object stream elements of one trigger entity and runs the reader of the tool on them:
  - a query shape QueryShapeAabb with m_center and a chunk position: the center in world coordinates, the world axes, the half size as
    authored (the scale of the entity is in m_aabb: TriggerVol_06_Reese of the build has 6.66 with the scale 6.66);
  - a turned entity with a sphere: the center turned, the radius as authored;
  - no query shape: the box component of the entity (Dimensions, the full size, with the scale of the entity);
  - no tag: no volume; no shape: a volume that the tool counts and does not write;
  - the tags of the TaskTriggerArea rows of a quest data file (lower case);
  - stage QG3: the walk of a capitals placement (its rotation), a spawner at its start, a spawner with no start (notAtStart), an alias
    spawner, a slice that spawns itself, a volume of an instance (elsewhere, with its world), and the comparison with the tasks;
  - the code review of stage QG3 (finding QG3R-R4, stage QG4): two volumes with one tag and one center and different axes are two volumes.
Status 0: each case passes. Usage: check_triggers_tool.py
"""

from __future__ import annotations

import json
import math
import struct
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import triggers as tool  # noqa: E402
from nav_assets import Element, field_id  # noqa: E402


def element(name, type_id: bytes = b"\0" * 16, value: bytes | None = None, children: list | None = None) -> Element:
    node = Element()
    node.name = field_id(name) if isinstance(name, str) else name
    node.type_id = type_id
    node.value = value
    node.children = children or []
    return node


def floats(*values: float) -> bytes:
    return struct.pack(">" + "f" * len(values), *values)


def transform(basis: tuple[float, ...], translation: tuple[float, float, float]) -> Element:
    # m_worldTM: the three basis vectors (columns), then the translation.
    return element(None, tool.TRANSFORM_TYPE, children=[element("m_worldTM", value=floats(*basis, *translation))])


def trigger_entity(tag: str | None, shape: Element | None, basis=(1, 0, 0, 0, 1, 0, 0, 0, 1), translation=(0.0, 0.0, 0.0),
                   center=(0.0, 0.0, 0.0), engine: Element | None = None) -> Element:
    components = [transform(basis, translation)]
    volume_children = [element("m_center", value=floats(*center)), element("m_ignoreHeight", value=b"\x00"),
                       element("m_strFilter", value=b"OnlyPlayerTarget")]
    if shape is not None:
        volume_children.append(element("m_shape", children=[shape]))
    else:
        volume_children.append(element("m_shape"))
    components.append(element(None, tool.VOLUME_TYPE, children=volume_children))
    if engine is not None:
        components.append(engine)
    marker_children = [element("m_serverFacetPtr", children=[element("m_targetTag", value=tag.encode())])] if tag is not None else []
    components.append(element(None, tool.OBJECTIVE_VOLUME_TYPE, children=marker_children))
    return element(None, tool.ENTITY_TYPE, children=[element("Name", value=b"Test_Trigger"), element("Components", children=components)])


def type_of(name: str) -> bytes:
    return next(key for key, value in tool.SHAPES.items() if value == name)


def engine_type(name: str) -> bytes:
    return next(key for key, value in tool.ENGINE_SHAPES.items() if value == name)


def near(a, b, tolerance=1e-3) -> bool:
    return all(math.isclose(x, y, abs_tol=tolerance) for x, y in zip(a, b))


def main() -> int:
    failures = 0

    def report(passed: bool, text: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'} {text}")
        failures += 0 if passed else 1

    chunk = (1.0, 0.0, 0.0, 9616.0, 0.0, 1.0, 0.0, 2320.0, 0.0, 0.0, 1.0, 41.7056)
    aabb = element(None, type_of("aabb"), children=[element("m_aabb", value=floats(10.0, 10.0, 5.0))])
    volume = tool.volume_of(trigger_entity("Test_IGC_Trigger", aabb, translation=(5.6171875, -10.517089, -1.4616089)), chunk, "chunk")
    report(volume is not None and volume["tag"] == "Test_IGC_Trigger" and volume["shape"] == "aabb"
           and near(volume["center"], (9621.6172, 2309.4829, 40.244)) and volume["half"] == [10.0, 10.0, 5.0]
           and volume["axes"] == [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0] and volume["filter"] == "OnlyPlayerTarget",
           "an aabb in a chunk: the center in world coordinates (the chunk position and the entity translation: the place of the beach trigger "
           "of the research), the half size as authored")
    scaled = tool.volume_of(trigger_entity("Scaled", element(None, type_of("aabb"), children=[element("m_aabb", value=floats(6.66, 6.66, 6.66))]),
                                           basis=(6.66, 0, 0, 0, 6.66, 0, 0, 0, 6.66)), chunk, "chunk")
    report(scaled is not None and scaled["half"] == [6.66, 6.66, 6.66], "a scaled entity: m_aabb keeps its size (the scale is in it)")
    angle = math.radians(35.0)
    turned_basis = (math.cos(angle), math.sin(angle), 0.0, -math.sin(angle), math.cos(angle), 0.0, 0.0, 0.0, 1.0)
    sphere = element(None, type_of("sphere"), children=[element("m_Radius", value=floats(40.0))])
    turned = tool.volume_of(trigger_entity("Turned", sphere, basis=turned_basis, translation=(1.0, 2.0, 0.0), center=(10.0, 0.0, 0.0)),
                            chunk, "chunk")
    expected = (9616.0 + 1.0 + 10.0 * math.cos(angle), 2320.0 + 2.0 + 10.0 * math.sin(angle), 41.7056)
    report(turned is not None and turned["shape"] == "sphere" and turned["radius"] == 40.0 and near(turned["center"], expected),
           "a turned entity: m_center turns with the entity; the radius of a sphere as authored")
    engine = element(None, engine_type("box"), children=[element("Configuration", children=[element("Dimensions", value=floats(2.0, 2.0, 1.0))])])
    box = tool.volume_of(trigger_entity("Engine_Box", None, basis=(20, 0, 0, 0, 20, 0, 0, 0, 20), engine=engine), chunk, "chunk")
    report(box is not None and box.get("entityShape") and box["shape"] == "box" and box["half"] == [20.0, 20.0, 10.0],
           "no query shape: the box component of the entity (Dimensions 2 x 2 x 1, the scale 20: a half size of 20, 20, 10)")
    no_tag = tool.volume_of(trigger_entity(None, aabb), chunk, "chunk")
    no_shape = tool.volume_of(trigger_entity("No_Shape", None), chunk, "chunk")
    counts: Counter = Counter()
    listed = tool.slice_volumes([trigger_entity("A", aabb), trigger_entity(None, aabb), trigger_entity("C", None)], chunk, "chunk", counts)
    report(no_tag is None and no_shape is not None and no_shape.get("noShape") and len(listed) == 1 and counts["triggersWithNoTag"] == 1
           and counts["triggersWithNoShape"] == 1 and counts["triggerEntities"] == 3,
           "no tag: no volume; no shape: counted and not written")
    unknown = element(None, b"\x01" * 16, children=[])
    try:
        tool.volume_of(trigger_entity("Unknown", unknown), chunk, "chunk")
        refused = False
    except tool.Failure:
        refused = True
    report(refused, "a query shape that the tool does not know stops the tool (no file)")
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "quest-data.json"
        path.write_text(json.dumps({"tasks": {"a": {"type": "tasktriggerarea", "cells": {"TP_TargetTag": " Aelstrom_IGC_Trigger "}},
                                              "b": {"type": "taskunlockachievement", "cells": {"TP_TargetTag": "Not_A_Trigger"}},
                                              "c": {"type": "tasktriggerarea", "cells": {}}}}), encoding="utf-8")
        tags = tool.quest_tags(path)
        report(tags == {"aelstrom_igc_trigger"} and tool.quest_tags(Path(folder) / "none.json") is None,
               "the tags of the TaskTriggerArea rows of a quest data file (lower case); no file: no comparison")
    walk_cases(report, aabb, sphere)
    # Code review of stage QG3, finding QG3R-R4: two boxes of one slice with one tag, one center, and different axes are two volumes; an
    # equal copy is one volume.
    query = element(None, type_of("box"), children=[element("m_box", value=floats(20.0, 2.0, 2.0))])
    first = tool.volume_of(trigger_entity("Review_Same_Tag", query), tool.IDENTITY, "slices/test/coincident.dynamicslice")
    second = tool.volume_of(trigger_entity("Review_Same_Tag", query, basis=(0.0, 1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 1.0)), tool.IDENTITY,
                            "slices/test/coincident.dynamicslice")
    for entry in (first, second):
        entry.update(via="capitals test row 0", world=tool.DEFAULT_WORLD)

    def holds(entry: dict, x: float, y: float, z: float) -> bool:
        offset = [x - entry["center"][0], y - entry["center"][1], z - entry["center"][2]]
        local = [sum(offset[column] * entry["axes"][row * 3 + column] for column in range(3)) for row in range(3)]
        return all(abs(local[row]) <= entry["half"][row] for row in range(3))
    kept = tool.unique([first, second])
    report(len(kept) == 2 and any(holds(entry, 0.0, 10.0, 0.0) for entry in kept) and not holds(first, 0.0, 10.0, 0.0)
           and len(tool.unique([first, dict(first)])) == 1,
           "QG3R-R4: two boxes with one tag and one center and different axes stay two volumes (the turned one holds (0, 10, 0)); an equal "
           "copy is one volume")
    return 1 if failures else 0


class MadeUpPaks:
    """Stage QG3: the pak index of the walk cases: the capitals files of a main world W and of an instance, and the names of the slices."""

    def __init__(self, files: dict[str, bytes]) -> None:
        self.files = files

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def has(self, path: str) -> bool:
        return path in self.files

    def read(self, path: str) -> bytes:
        return self.files[path]


class MadeUpCatalog:
    def __init__(self, data=None, endings=None) -> None:
        self.paths = {(b"A" * 16, 0): "slices/test/child.dynamicslice", (b"B" * 16, 0): "slices/test/later.dynamicslice",
                      (b"C" * 16, 0): "slices/test/loop.dynamicslice"}

    def path(self, asset):
        return self.paths.get(asset) if asset is not None else None


def walk_cases(report, aabb: Element, sphere: Element) -> None:
    """Stage QG3: the walk of the capitals and of the spawners, the other worlds, and the comparison with the tasks (made-up slices: the
    content of a slice is given, no object stream is parsed). `report` counts the failures."""
    identity = tool.IDENTITY
    move = (1.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    slices = {
        "slices/test/root.dynamicslice": ([trigger_entity("Capital_Trigger", aabb, translation=(1.0, 0.0, 0.0))],
                                          [(move, (b"A" * 16, 0), None, True), (identity, (b"B" * 16, 0), None, False),
                                           (identity, None, (b"Z" * 16, 0), True)]),
        "slices/test/child.dynamicslice": ([trigger_entity("Child_Trigger", sphere)], [(identity, (b"C" * 16, 0), None, True)]),
        "slices/test/later.dynamicslice": ([trigger_entity("Later_Trigger", sphere)], []),
        "slices/test/loop.dynamicslice": ([], [(identity, (b"C" * 16, 0), None, True)]),
        "slices/test/instance.dynamicslice": ([trigger_entity("Instance_Trigger", sphere)], []),
    }
    half = math.sqrt(0.5)
    capitals = {"Capitals": [{"sliceName": tool.PLOT}, {"sliceName": "test/root", "sliceAssetId": "",
                                                       "worldPosition": {"x": 1000.0, "y": 2000.0, "z": 30.0},
                                                       "rotation": {"x": 0.0, "y": 0.0, "z": half, "w": half}}]}
    instance = {"Capitals": [{"sliceName": "test/instance", "worldPosition": {"x": 50.0, "y": 60.0, "z": 5.0}}]}
    files = {name: b"" for name in slices}
    files["sharedassets/coatlicue/w/regions/r_+00_+00/region.chunks"] = b""
    files["sharedassets/coatlicue/w/regions/r_+00_+00/capitals/layer/layer.capitals.json"] = json.dumps(capitals).encode()
    files["sharedassets/coatlicue/nw_dungeon_x/regions/r_+00_+00/capitals/script/script.capitals.json"] = json.dumps(instance).encode()
    files[tool.CATALOG_PATH] = b""
    saved = (tool.Catalog, tool.Walk.content, tool.Walk.chunks)
    tool.Catalog = MadeUpCatalog
    tool.Walk.content = lambda self, path, data=None: slices[path]
    tool.Walk.chunks = lambda self, folder, region, seen: None
    try:
        document = tool.build(MadeUpPaks(files), "w", None, {"capital_trigger", "child_trigger", "later_trigger", "instance_trigger", "nowhere"})
    finally:
        tool.Catalog, tool.Walk.content, tool.Walk.chunks = saved
    volumes = {volume["tag"]: volume for volume in document["volumes"]}
    capital = volumes.get("Capital_Trigger", {})
    child = volumes.get("Child_Trigger", {})
    counts = document["counts"]
    checks = [
        (sorted(volumes) == ["Capital_Trigger", "Child_Trigger"] and near(capital.get("center", ()), (1000.0, 2001.0, 30.0))
         and capital.get("via") == "capitals r_+00_+00/capitals/layer/layer.capitals.json row 1" and capital.get("world") == "w"
         and capital.get("source") == "root.dynamicslice",
         "stage QG3: a capitals placement (not <PLOT>) places its slice: the rotation of 90 degrees turns the entity offset (1, 0, 0) to "
         "(0, 1, 0); the volume keeps its slice, its placement, and its world"),
        (near(child.get("center", ()), (1000.0, 2010.0, 30.0)) and counts.get("volumesOfASpawnedSlice") == 1,
         "stage QG3: a spawner with m_spawnOnInitialize 1 places its slice at the spawner place (the offset 10 m turns to the north)"),
        ([volume["tag"] for volume in document["notAtStart"]] == ["Later_Trigger"] and counts.get("spawnersNotAtStart") == 1
         and counts.get("aliasSpawners") == 1 and counts.get("slicesThatSpawnThemselvesOrTooDeep") == 1,
         "stage QG3: the volume of a spawner with no start is in notAtStart, not in volumes; an alias spawner and a slice that spawns "
         "itself are counted"),
        ([(volume["tag"], volume["world"]) for volume in document["elsewhere"]] == [("Instance_Trigger", "nw_dungeon_x")]
         and counts.get("otherWorlds") == 1,
         "stage QG3: a volume of another world (an instance) is in elsewhere with its world, never in volumes"),
        (document["tasks"] == {"tags": 5, "found": 2, "missing": ["instance_trigger", "later_trigger", "nowhere"],
                               "elsewhere": {"instance_trigger": ["nw_dungeon_x"]}, "notAtStart": ["later_trigger"]},
         "stage QG3: the comparison with the tasks names the instance of a missing tag and the tags of a spawner with no start"),
    ]
    for passed, text in checks:
        report(passed, text)


if __name__ == "__main__":
    sys.exit(main())

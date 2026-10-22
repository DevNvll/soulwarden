#!/usr/bin/env python3
"""Check the rules of the trigger tool (triggers.py, stage 1.8b) with made-up entities: no game copy and no pak file.

Each case builds the object stream elements of one trigger entity and runs the reader of the tool on them:
  - a query shape QueryShapeAabb with m_center and a chunk position: the center in world coordinates, the world axes, the half size as
    authored (the scale of the entity is in m_aabb: TriggerVol_06_Reese of the build has 6.66 with the scale 6.66);
  - a turned entity with a sphere: the center turned, the radius as authored;
  - no query shape: the box component of the entity (Dimensions, the full size, with the scale of the entity);
  - no tag: no volume; no shape: a volume that the tool counts and does not write;
  - the tags of the TaskTriggerArea rows of a quest data file (lower case).
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
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

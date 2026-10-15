#!/usr/bin/env python3
"""Check of the hit facts of the creature data tool (creature_data.py, stage 3.3b-S5), with made-up sources.

The tool reads, for each creature slice, the two angles of its DamageReceiver and its head volume (the HitVolume with
m_isHeadshot) on a bone of the skeleton of its character (the bind pose of the bone of the chunk CompiledBones of the .chr
file). The check gives the reader made-up slice nodes and a made-up skeleton file (no file of the game is necessary):

  case                 the source                                           expected result
  valid                the angles 2.0944 and 2.3562; a head capsule on a     the angles; the head segment in the frame of
                       bone 0.7 m in front and 1.2 m up, turned 90 degrees  the creature (the turn of the bone applied)
  skeleton-as-cdf      the character field names the .chr file itself        the same head
  no-skeleton          the character file is not in the game                no head, one count noSkeleton
  missing-bone         the skeleton has no bone of the volume                no head, one count noSkeleton
  other-shape          the head volume has a shape with no radius (a box)    no head, one count headOtherShape
  bad-angle            an angle above pi                                    the tool stops
  bad-chunk            the bone chunk has a size that is not a whole number  the tool stops
                       of records
  nonfinite-bone       a bone matrix with NaN                               the tool stops

Usage: check_hit_facts_tool.py      status 0: each case gives its result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import creature_data as cd  # noqa: E402
from appearance_tables import Failure  # noqa: E402

CDF = "objects/characters/test/wolf.cdf"
CHR = "objects/characters/test/wolf_skel.chr"


def node(name=None, value=None, children=(), kind=None):
    element = cd.Element()
    element.name = name
    element.value = value
    element.children = list(children)
    if kind is not None:
        element.type_id = kind
    return element


def real(value: float) -> bytes:
    return struct.pack(">f", value)


def skeleton(case: str) -> bytes:
    """@return A chunk file with one bone chunk: the root and bind_neck_2_jnt (0.7 m front, 1.2 m up; its x axis is the front)."""
    bones = []
    for name, rows in (("bind_xform", ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0))),
                       ("bind_neck_2_jnt" if case != "missing-bone" else "bind_tail_jnt",
                        ((0, -1, 0, 0.0), (1, 0, 0, 0.7), (0, 0, 1, 1.2)))):
        matrix = [float(value) for row in rows for value in row]
        if case == "nonfinite-bone" and name != "bind_xform":
            matrix[3] = math.nan
        record = struct.pack("<I", 0) + bytes(208) + struct.pack("<f", 0.0) + bytes(48) + struct.pack("<12f", *matrix)
        record += name.encode().ljust(256, b"\0") + struct.pack("<iiii", 0, 0, 0, 0)
        assert len(record) == cd.BONE_RECORD
        bones.append(record)
    chunk = bytes(32) + b"".join(bones)
    if case == "bad-chunk":
        chunk += bytes(7)
    header = b"CrCh" + struct.pack("<III", 0x746, 1, 16)
    table = struct.pack("<HHiII", cd.SKELETON_CHUNK, cd.SKELETON_VERSION, 1, len(chunk), 32)
    return header + table + chunk


def roots(case: str) -> list:
    receiver = node(children=[node(cd.BEHIND_FIELD, real(2.0944)), node(cd.BEHIND_CRIT_FIELD, real(4.0 if case == "bad-angle" else 2.3562))])
    shape = node(cd.SHAPE_FIELD, children=[node(children=[node(cd.CAPSULE_HEIGHT_FIELD, real(0.4)),
                                                          node(cd.CAPSULE_AXIS_FIELD, struct.pack(">3f", 1.0, 0.0, 0.0))]
                                                + ([] if case == "other-shape" else [node(cd.CAPSULE_RADIUS_FIELD, real(0.2))]))])
    volume = node(children=[node(cd.VOLUME_CENTER_FIELD, struct.pack(">3f", 0.0, -0.05, 0.0)), shape,
                            node(cd.HEADSHOT_FIELD, b"\x01"), node(cd.TARGET_BONE_FIELD, b"bind_neck_2_jnt")])
    body = node(children=[node(cd.HEADSHOT_FIELD, b"\x00"), node(cd.TARGET_BONE_FIELD, b"bind_spine_1_jnt")])
    path = CHR if case == "skeleton-as-cdf" else CDF
    character = node(cd.CDF_FIELD, children=[node(cd.ASSET_PATH_FIELD, path.encode())])
    return [node(children=[receiver, body, volume, character])]


class Sources:
    def __init__(self, case: str) -> None:
        self.case = case

    def has(self, path: str) -> bool:
        if self.case == "no-skeleton":
            return False
        return path in (CDF, CHR)

    def read(self, path: str):
        if path == CDF:
            return f'<CharacterDefinition><Model File="{CHR}"/></CharacterDefinition>'.encode()
        if path == CHR:
            return skeleton(self.case)
        return None


def run(case: str):
    counts: dict[str, int] = {}
    try:
        facts = cd.read_hit_facts(Sources(case), "slices/test/wolf.dynamicslice", roots(case), {}, counts)
    except Failure as error:
        return None, counts, str(error)
    return facts, counts, ""


def main() -> int:
    failures = 0
    # The bone turns the volume: its x axis is the front of the creature (y), its y axis is -x. The center (0, -0.05, 0) is
    # 0.05 m to the right (+x); the axis (1, 0, 0) of the capsule is the front; the height 0.4: 0.2 m to each side.
    expected = [[0.05, 0.5, 1.2], [0.05, 0.9, 1.2], 0.2]
    for case in ("valid", "skeleton-as-cdf"):
        facts, counts, error = run(case)
        head = facts.get("head") if facts else None
        passed = (facts is not None and facts.get("behind") == [2.0944, 2.3562] and head is not None
                  and all(math.isclose(a, b, abs_tol=1e-4) for a, b in zip(head[0] + head[1] + [head[2]], expected[0] + expected[1] + [expected[2]]))
                  and counts.get("heads") == 1)
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {facts} {error}")
        failures += 0 if passed else 1
    for case, count in (("no-skeleton", "noSkeleton"), ("missing-bone", "noSkeleton"), ("other-shape", "headOtherShape")):
        facts, counts, error = run(case)
        passed = facts is not None and "head" not in facts and facts.get("behind") == [2.0944, 2.3562] and counts.get(count) == 1
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {facts}, counts {counts} {error}")
        failures += 0 if passed else 1
    for case in ("bad-angle", "bad-chunk", "nonfinite-bone"):
        facts, counts, error = run(case)
        passed = facts is None and bool(error)
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {'the tool stops: ' + error if error else 'the tool did not stop'}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

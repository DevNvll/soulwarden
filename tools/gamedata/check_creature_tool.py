#!/usr/bin/env python3
"""Check of the patrol sources of the creature data tool (creature_data.py), with made-up sources.

The check runs the patrol reader, the world route, the document builder, and the writer of the tool
(main) with one made-up spawner for each case. The other readers of the tool (the pak files, the
catalog, the vitals tables, the slices, the terrain) are replaced: no file of the game is necessary.

  case                 the spawner                                         expected result
  none                 names no patrol                                     status 0, no route
  valid                a patrol of one waypoint                            status 0, one route
  empty                a patrol with no waypoint (a case of the retail     status 0, no route, one count
                       data that the tool leaves out)                      in routesLeftOut
  missing-reference    names a patrol entity that its slice does not have  status 1, the file stays
  missing-waypoint     a patrol names a waypoint entity that is not there  status 1, the file stays
  nonfinite-position   a waypoint transform with NaN in its place          status 1, the file stays
  nonfinite-basis      a waypoint transform with NaN in its rotation       status 1, the file stays

A case of status 1 must leave the file that was there before: the backend refuses a file with a
number that is not finite as a whole (code review of stage 3.4e, findings F5 and F6).

The ranged attacks (stage 3.4f-A, test case CRG1), with made-up rows and slices:

  case                 what                                                expected result
  ranged-dictionary    the attack index of four made-up attacks            the order of the profiles and then of the
                                                                           attacks, by their case-sensitive bytes
  ranged-release       the joins of a release (the profile, the ammunition the index, the ammunition key, the table
                       of the AmmoSlot, the damage table and its row)      key, the row place, the projectile index
  ranged-override      a release with a SliceOverride                       the slice of the clip, the ammunition key 0
  ranged-refused       no row, no profile, a hit scan, no ammunition       no release, one cause each
  ranged-projectile    a projectile slice with and with no Projectile      its asset, root, slot, flight values; no
                       component                                           entry with no component

Usage: check_creature_tool.py WORK_FOLDER     status 0: each case gives its result
"""

from __future__ import annotations

import contextlib
import io
import json
import math
import struct
import sys
import uuid
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import creature_data as cd  # noqa: E402

CASES = ("none", "valid", "empty", "missing-reference", "missing-waypoint", "nonfinite-position", "nonfinite-basis")
MARK = "the file of an earlier run\n"


def node(name=None, value=None, children=(), kind=None):
    element = cd.Element()
    element.name = name
    element.value = value
    element.children = list(children)
    if kind is not None:
        element.type_id = kind
    return element


def entity(identifier: int, components: list):
    return node(kind=cd.ENTITY_TYPE, children=[node(cd.ID_FIELD, identifier.to_bytes(8, "big")),
                                             node(cd.COMPONENTS_FIELD, children=components)])


def spawner(case: str) -> list:
    roots = [node(cd.SLICE_FIELD, bytes([0x44]) * 16 + (2).to_bytes(4, "big"))]
    if case == "none":
        return roots
    roots.append(node(cd.PATROL_PATH_FIELD, (101).to_bytes(8, "big")))
    if case == "missing-reference":
        return roots
    waypoint_ids = [] if case == "empty" else [node(value=(102).to_bytes(8, "big"))]
    patrol = node(kind=cd.PATROL_TYPE, children=[node(cd.WAYPOINT_IDS_FIELD, children=waypoint_ids),
                                                 node(cd.POLICY_FIELD, bytes([0]))])
    roots.append(entity(101, [patrol]))
    if case in ("empty", "missing-waypoint"):
        return roots
    matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    if case == "nonfinite-position":
        matrix[7] = math.nan
    if case == "nonfinite-basis":
        matrix[0] = math.nan
    transform = node(children=[node(cd.WORLD_TRANSFORM_FIELD, struct.pack(">12f", *matrix))])
    waypoint = node(kind=cd.WAYPOINT_TYPE, children=[node(cd.MIN_WAIT_FIELD, struct.pack(">f", 1.0)),
                                                     node(cd.MAX_WAIT_FIELD, struct.pack(">f", 2.0))])
    roots.append(entity(102, [transform, waypoint]))
    return roots


class Sources:
    case = "valid"
    used: dict = {}

    def __init__(self, path) -> None:
        pass

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return [cd.REGIONS_FOLDER + "r_+00_+00/capitals/test/test.metadata"] if suffix == ".metadata" else []

    def read(self, path: str) -> bytes:
        if path.endswith(".capitals.json"):
            return json.dumps({"Capitals": [{"id": "check-placement", "worldPosition": {"x": 1000, "y": 1000, "z": 50},
                                            "sliceName": "check-spawner.dynamicslice"}]}).encode()
        return b"made-up source"

    def stream(self, path: str) -> list:
        return spawner(self.case) if path == "check-spawner.dynamicslice" else []

    def has(self, path: str) -> bool:
        return True

    def check_complete(self) -> None:
        pass


class Catalog:
    def __init__(self, data) -> None:
        pass

    def path(self, asset):
        return "check-creature.dynamicslice" if asset else None


class Behavior:
    ranged_left_out: dict = {}

    def __init__(self, *arguments) -> None:
        pass

    def read(self, *arguments) -> dict:
        return {"behavior": {"sight": [75, 12], "combatSight": [180, 30], "leash": [20, 70],
                             "speed": [4, 8], "ambient": 2, "wander": [0, 0, 0]}}


class Terrain:
    def __init__(self, *arguments) -> None:
        pass

    def load(self, *arguments) -> None:
        pass

    def height(self, *arguments) -> float:
        return 50.0


def creature_slice(*arguments) -> dict:
    return {"path": "check-creature.dynamicslice", "asset": str(uuid.UUID(bytes=bytes([0x44]) * 16)),
            "sub": 2, "root": "1000000000000044", "interestRadius": 60, "movement": 5, "vitals": 9, "marker": 15,
            "slots": [[5, "ActionList", 11], [9, "Vitals", 15], [15, "Marker", 5399]],
            "components": [["1000000000000044", "0000000000000A01", "Root/Vitals"]]}


def check_ranged() -> int:
    """The checks of the ranged export (stage 3.4f-A). @return The number of failures."""
    import creature_behavior as cb
    import zlib
    failures = 0

    def report(case: str, passed: bool, detail: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1

    rows = [["b_Profile", "x", False, 30.0, 0.1, 30.0, 0.0, 1.0, 0, 0, 0, 0, False, False, [0, 0, -9.8], 0],
            ["AI_Bow", "Basic", False, 100.0, 0.0, 100.0, 0.2, 0.0, 0, 0, 0, 0, False, False, [0, 0, -9.8], 0],
            ["AI_Bow", "Arrow_Scan", True, 100.0, 0.0, 100.0, 0.2, 0.0, 0, 0, 0, 0, False, False, [0, 0, -9.8], 0],
            ["ai_lower", "A", False, 50.0, 0.0, 50.0, 0.0, 0.0, 0, 0, 0, 0, False, False, [0, 0, 0], 0]]
    dictionary = cd.ranged_dictionary(rows)
    order = [(entry["profile"], entry["attack"]) for entry in sorted(dictionary.values(), key=lambda entry: entry["index"])]
    report("ranged-dictionary", order == [("AI_Bow", "Arrow_Scan"), ("AI_Bow", "Basic"), ("ai_lower", "A"), ("b_Profile", "x")],
           f"{order}")

    class Tables:
        weapons = {"test_bow": {"tags": ["Bow"], "offhandTags": [], "animations": "", "profile": "AI_Bow"},
                   "test_club": {"tags": [], "offhandTags": [], "animations": "", "profile": ""}}
        types = {"Thrust": (5, "Physical")}
        ammo = {"test_arrow": {"id": "Test_Arrow", "slice": "slices/items/test_arrow.dynamicslice"},
                "test_noprefab": {"id": "Test_NoPrefab", "slice": ""}}

    behavior = cb.Behavior.__new__(cb.Behavior)
    behavior.tables = Tables()
    behavior.ranged_attacks = dictionary
    behavior.projectile_of = lambda path: {"slices/items/test_arrow.dynamicslice": 3,
                                           "slices/items/test_override.dynamicslice": 4}.get(path)
    behavior.ranged_left_out = {}
    damage = {"test_row": {"ordinal": 2, "table": "TestBowDamageTable", "id": "Test_Row", "type": "Thrust", "ranged": True,
                           "factor": 0.35, "added": 0.0, "charge": 0.16}}
    references = {"weapons": {24: "Test_Bow", 12: "Test_Arrow", 13: "Test_NoPrefab"}}
    clip = {"RangedAttackName": "Basic", "DamageTableRow": "Test_Row", "SliceOverride": "", "UseAmmo": "true",
            "AmmoSlot": "PaperdollSlotTypes::ARROW_AMMO_SLOT", "AILeadTarget": "true", "ProjectileSpeed": "50",
            "AILeadTargetMaxAngle": "10", "AIAimMaxAngle": "45", "AIMissMinDistance": "1.5", "AIMissMaxDistance": "3"}
    main_hand = Tables.weapons["test_bow"]
    release, cause = behavior.release(clip, references, main_hand, damage)
    expected = {"index": 1, "ammo": "Test_Arrow", "ammoKey": zlib.crc32(b"test_arrow"), "projectile": 3, "override": False,
                "table": "TestBowDamageTable", "tableKey": zlib.crc32(b"testbowdamagetable"), "row": "Test_Row", "rowIndex": 2,
                "wire": 5, "lead": True, "aimSpeed": 50.0, "leadAngle": 10.0, "aimAngle": 45.0, "miss": [1.5, 3.0], "range": 100.0}
    report("ranged-release", release is not None and all(release.get(key) == value for key, value in expected.items()),
           f"{release if release is None else {key: release[key] for key in expected}} {cause}")
    override, _ = behavior.release(dict(clip, SliceOverride="Items/Test_Override", UseAmmo="false"), references, main_hand, damage)
    report("ranged-override", override is not None and override["projectile"] == 4 and override["ammoKey"] == 0 and override["override"],
           f"{override if override is None else (override['projectile'], override['ammoKey'], override['override'])}")
    refusals = [behavior.release(dict(clip, DamageTableRow="Not_A_Row"), references, main_hand, damage)[1],
                behavior.release(clip, references, Tables.weapons["test_club"], damage)[1],
                behavior.release(dict(clip, RangedAttackName="Arrow_Scan"), references, main_hand, damage)[1],
                behavior.release(dict(clip, AmmoSlot="PaperdollSlotTypes::CARTRIDGE_AMMO_SLOT"), references, main_hand, damage)[1],
                behavior.release(dict(clip, UseAmmo="false"), references, main_hand, damage)[1]]
    report("ranged-refused", all(refusals) and len(set(refusals)) == 5, f"{refusals}")

    slot_field = node(cd.REPLICATION_FIELD, (1).to_bytes(4, "big"))
    projectile_component = node(kind=cd.PROJECTILE_CLASS, children=[slot_field])
    with_component = [entity(0x1234, [projectile_component, node(kind=uuid.uuid4(), children=[
        node(cd.LOCAL_TRANSFORM_FIELD, b""), node(cd.PARENT_FIELD, cd.NO_ENTITY.to_bytes(8, "big"))])])]
    flight = ["slices/items/test_arrow.dynamicslice", 50.0, 10.0, 1.5, [0.0, 0.0, -2.0], 1.5, 8.0]
    found = cd.read_projectile("slices/items/test_arrow.dynamicslice", (bytes([0x61]) * 16, 2), with_component, flight)
    missing = cd.read_projectile("slices/items/test_arrow.dynamicslice", (bytes([0x61]) * 16, 2),
                                 [entity(0x1234, [node(kind=uuid.uuid4(), children=[node(cd.LOCAL_TRANSFORM_FIELD, b""),
                                                                                    node(cd.PARENT_FIELD, cd.NO_ENTITY.to_bytes(8, "big"))])])],
                                 flight)
    report("ranged-projectile", found is not None and found[1] == str(uuid.UUID(bytes=bytes([0x61]) * 16)).upper() and found[3] == "0000000000001234"
           and found[4] == 1 and found[5:] == flight[1:] and missing is None, f"{found} {missing}")
    return failures


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    tables = {"variants": {}, "bases": {1: {"id": "Check_Kind", "family": "Wildlife", "creatureType": "Solo",
                                           "healthMod": 1.0, "damageMod": 1.0, "name": ""}},
              "categories": {}, "levels": {18: (1000.0, 100.0, 180.0)}, "modifiers": {"Solo": (1.0, 1.0)}}
    entries = [{"vitals": 1, "category": 0, "level": 18, "position": (1000.0, 1000.0, 50.0),
                "coatlicue": True, "override": False, "encounter": False}]
    failures = 0
    with contextlib.ExitStack() as stack:
        replacements = {"Sources": Sources, "Catalog": Catalog, "read_tables": lambda _: tables,
                        "read_ranged_attacks": lambda _: {}, "slice_assets": lambda _: {},
                        "read_names": lambda _: {}, "read_slice_lists": lambda: (set(), set()),
                        "read_spawn_entries": lambda _: entries, "read_creature_slice": creature_slice,
                        "read_action_table": lambda *arguments: [["Move", "Move", 1, False]]}
        for name, value in replacements.items():
            stack.enter_context(patch.object(cd, name, value))
        stack.enter_context(patch.object(cd.creature_behavior, "Behavior", Behavior))
        stack.enter_context(patch.object(cd.height_maps, "Terrain", Terrain))
        for case in CASES:
            Sources.case = case
            target = work / f"{case}.json"
            target.write_text(MARK, encoding="utf-8")
            errors = io.StringIO()
            with patch.object(sys, "argv", ["creature_data.py", str(target)]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                status = cd.main()
            text = target.read_text(encoding="utf-8")
            if case in ("none", "valid", "empty"):
                document = json.loads(text) if text != MARK else {}
                routes = len(document.get("routes", []))
                left_out = document.get("counts", {}).get("routesLeftOut", {})
                passed = (status == 0 and routes == (1 if case == "valid" else 0) and "NaN" not in text
                          and (left_out == {"the patrol has no waypoint": 1} if case == "empty" else left_out == {}))
                detail = f"status {status}, {routes} route(s), left out {left_out}"
            else:
                passed = status == 1 and text == MARK and not (work / f"{case}.json.tmp").exists()
                detail = f"status {status}, file {'kept' if text == MARK else 'replaced'}: {errors.getvalue().strip()}"
            print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
            failures += 0 if passed else 1
    failures += check_ranged()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

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

The corrections of the code review of stage 3.4f-A (findings CR-R5 and CR-R6; stage 3.4f-B, test case RR5), with the production ranged
reader, the joins, the projectile reader, the builder, and the writer of the tool (made-up sources):

  case                          what                                                expected result
  review-control                one FireBow release of 1.33333 s                    status 0, one ranged attack, the time
  review-control-cast           the same release in the action Cast_One_Projectile  status 0, one ranged attack (the basic cast)
  review-other-action           a release of an action that the backend does not   status 0, no ranged attack, one count
                                use (a charged shot) with no damage row
  review-missing-asset          the catalog has no asset of the arrow slice         status 1, the file stays (CR-R5)
  review-missing-source         the arrow slice has no source                       status 1, the file stays (CR-R5)
  review-missing-row            the damage table has no row of the release          status 1, the file stays (CR-R5)
  review-nonfinite-time         the ExitTime before the release is "nan"            status 1, the file stays (CR-R6)
  review-malformed-time         the ExitTime before the release is "1.3x"           status 1, the file stays (CR-R6)
  review-malformed-aim          the ProjectileSpeed of the release is "fast"        status 1, the file stays (CR-R6)

The correction of the code review of stage 3.4f-D (finding SU-R4; test case RSU4), with the production spell slice reader, AreaSpells.shape,
the cast joins, the builder, and the writer (made-up sources): a cast of a spell of the list of the proved area spells whose required join
breaks before the shape stops the tool.

  case                          what                                                expected result
  review-support-valid          a cast of the listed strike spell, with each source status 0, one area attack
  review-support-missing-row    the spell tables have no row of the spell           status 1, the file stays
  review-support-missing-asset  the catalog has no asset of the spell slice         status 1, the file stays
  review-support-missing-slice  the spell slice has no source                       status 1, the file stays

The time constraint of a spawner (stage 2.2, format 4; test case WT7), with the production spawner reader, the builder, and the writer
of the tool (made-up sources): the values 12 to 14 of the spawn point (the phase mask, the despawn flag, the cooldown flags).

  case                          the spawner                                         expected result
  phase-none                    no time constraint                                  status 0, format 4, [15, false, 0]
  phase-night-dusk              m_night, m_dusk, m_despawnOnTimeEnd                 status 0, [9, true, 0]
  phase-empty                   no phase, m_despawnOnTimeEnd                        status 0, [0, true, 0] (no automatic spawn)
  phase-cooldowns               m_day, both cooldown flags of the spawner true      status 0, [4, true, 3]
  phase-cut-flag                m_dusk with two bytes                               status 1, the file stays
  phase-missing-flag            no m_day                                            status 1, the file stays
  phase-two-constraints         two constraint components                           status 1, the file stays

The server trees of a slice (stage BT2; test case BB1 of the backend reads the member), with made-up asset values of the field
m_behaviorTreeAssets (the asset id, 16 bytes, the asset type, the length of the path, the path):

  case                          what                                                expected result
  trees-one                     one tree "SharedAssets/AI/BehaviorTrees/Wolf/..."   ["wolf/wolf_base"]
  trees-two                     a base tree, the explode tree, the base tree again  two names in the sequence of the slice
  trees-none                    no field                                            no name (no member in the file)
  trees-other-folder            a path that is not in sharedassets/ai/behaviortrees the tool stops (Failure)
  trees-cut-length              a length that is not the length of the path         the tool stops (Failure)

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


PHASE_CASES = {"phase-none": [15, False, 0], "phase-night-dusk": [9, True, 0], "phase-empty": [0, True, 0],
               "phase-cooldowns": [4, True, 3], "phase-cut-flag": None, "phase-missing-flag": None, "phase-two-constraints": None}


def constraint(flags: dict) -> object:
    """A TimeOfDayConstraintComponent with these bool fields (a value of bytes: a cut field)."""
    return node(kind=cd.TIME_CONSTRAINT_TYPE,
                children=[node(children=[node(cd.field_id(name), value if isinstance(value, bytes) else bytes([int(value)]))
                                         for name, value in flags.items()])])


def phase_spawner(case: str) -> list:
    roots = [node(cd.SLICE_FIELD, bytes([0x44]) * 16 + (2).to_bytes(4, "big"))]
    if case == "phase-none":
        return roots
    flags = {"m_night": 0, "m_dawn": 0, "m_day": 0, "m_dusk": 0, "m_despawnOnTimeEnd": 1}
    if case == "phase-night-dusk":
        flags.update(m_night=1, m_dusk=1)
    elif case in ("phase-cooldowns", "phase-two-constraints"):
        flags.update(m_day=1)
    elif case == "phase-cut-flag":
        flags.update(m_dusk=b"\x00\x01")
    elif case == "phase-missing-flag":
        del flags["m_day"]
    roots.append(constraint(flags))
    if case == "phase-two-constraints":
        roots.append(constraint(flags))
    if case == "phase-cooldowns":
        roots += [node(cd.COOLDOWN_FIELDS["m_cooldownAfterDespawn"], bytes([1])),
                  node(cd.COOLDOWN_FIELDS["m_cooldownAfterConstraintFail"], bytes([1]))]
    return roots


def spawner(case: str) -> list:
    if case.startswith("phase-"):
        return phase_spawner(case)
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
    areas_left_out: dict = {}

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


def check_areas() -> int:
    """The checks of the area export (stage 3.4f-C): the shapes of the spell slices (spells.area_shape), the list of the spells with a live
    check (spells.AreaSpells), and the joins of a cast (creature_behavior.Behavior.area_cast). @return The number of failures."""
    import creature_behavior as cb
    import spells
    failures = 0

    def report(case: str, passed: bool, detail: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1

    # The mushroom form: a place slice, an area status of the enemies with a cylinder, a destroy event at the init.
    mushroom = {"spellSlot": 0, "positionSlot": 1, "offenses": [], "autoSpell": None, "vitals": None,
                "events": [[2, 26.0, 0, "Destroy", "", True, "Root", True, True], [3, 1.0, 2, "Cloud", "", False, "Root", True, True]],
                "volumes": [["Cloud", 0.0, 1.0, "cylinder", 1.5, 4.0, -0.5, -1.0]],
                "areaStatuses": [["Cloud", "Cloud", 0.0, ["Test_Poison"], True, 2, 0.5, 1.0, True]]}
    # The strike form: a Spell slice, one volume with no shape, active from 0.9 s to 0.95 s.
    strike = {"spellSlot": 1, "positionSlot": 0, "offenses": [], "autoSpell": None, "vitals": None, "events": [],
              "volumes": [["Damage", 0.0, 0.9, "none", 0.0, 0.0, 0.0, 0.95]], "areaStatuses": []}
    enemy = {"TargetTypes": "Enemy"}
    strike_row = {"TargetTypes": "Enemy", "ShapeTypes": "Sphere", "Radius": "2.5", "DamageTable": "TestShamanTable",
                  "DamageTableRow": "Test_Strike"}
    cases = [("area-status", enemy, mushroom, "status"), ("area-strike", strike_row, strike, "strike"),
             ("area-friends", {"TargetTypes": "Friend"}, mushroom, ""),
             ("area-no-end", enemy, {**mushroom, "events": mushroom["events"][1:]}, ""),
             ("area-friend-status", enemy, {**mushroom, "areaStatuses": [mushroom["areaStatuses"][0][:5] + [1, 0.5, 1.0, True]]}, ""),
             ("area-strike-no-radius", {**strike_row, "Radius": ""}, strike, ""),
             ("area-strike-offense", strike_row, {**strike, "offenses": [["Damage", "Row", 2.0]]}, ""),
             ("area-strike-always", strike_row, {**strike, "volumes": [["Damage", 0.0, 0.0, "none", 0.0, 0.0, 0.0, -1.0]]}, "")]
    for case, cells, entry, expected in cases:
        shape, cause = spells.area_shape(cells, entry)
        report(case, shape == expected and (bool(cause) != bool(expected)), f"shape {shape!r}, cause {cause!r}")

    # The list of the spells with a live check (creature_slices.json): a spell of a known shape with no live check makes no area.
    class Sources:
        def has(self, path):
            return True

        def stream(self, path):
            return []

    for case, proved, expected in (("area-proved", {"test_bomb": "a check"}, "status"), ("area-not-proved", {}, "")):
        known = spells.AreaSpells(Sources(), proved)
        known._rows = {"test_bomb": {"SpellID": "Test_Bomb", "TargetTypes": "Enemy", "SpellPrefabPath": "Characters/Test_Bomb"}}
        known._by_path = {"slices/characters/test_bomb.dynamicslice": (b"x" * 16, 2)}
        with patch.object(spells, "read_spell_slice", lambda *arguments: mushroom):
            found, cause = known.shape("Test_Bomb")
        report(case, (found or {}).get("shape", "") == expected and (bool(cause) != bool(expected)), f"{found}, cause {cause!r}")

    # The joins of a cast: the aim values, the damage row of a strike in the table of the slice.
    behavior = cb.Behavior.__new__(cb.Behavior)
    behavior.tables = type("Tables", (), {"types": {"Lightning": (9, "Elemental")}})()
    known_casts = {"Test_Strike": ({"spell": "Test_Strike", "shape": "strike", "table": "TestShamanTable", "row": "Test_Strike"}, ""),
                   "Test_Bomb": ({"spell": "Test_Bomb", "shape": "status", "table": "", "row": ""}, "")}
    behavior.area_spells = type("Spells", (), {"shape": staticmethod(lambda spell: known_casts.get(spell, (None, "no such spell")))})()
    behavior.areas_left_out = {}
    # Stage B4: the attack type of the damage row (Enchanted Ward; research bow-build-2.md Q5) goes into the cast.
    rows = {"test_strike": {"ordinal": 1, "table": "TestShamanTable", "id": "Test_Strike", "type": "Lightning", "factor": 0.85,
                            "added": 0.0, "charge": 0.1, "attackType": "Magic"}}
    values = {"SpellName": "Test_Strike", "AIAimAtTarget": "true", "ProjectileSpeed": "100", "AILeadTargetMaxAngle": "20",
              "AIAimMaxAngle": "45", "AIMissMaxDistance": "2", "UseAllAvailableTargets": "true"}
    cast, cause, _ = behavior.area_cast(0.933333, values, rows)
    report("cast-strike", cast is not None and cast["shape"] == "strike" and cast["factor"] == 0.85 and cast["wire"] == 9
           and cast["attackType"] == "Magic"
           and cast["lead"] is False and cast["aimSpeed"] == 100.0 and cast["miss"] == [0.0, 2.0] and cast["allTargets"] is True,
           f"{cast}, cause {cause!r}")
    other = {"test_strike": {**rows["test_strike"], "table": "OtherTable"}}
    cast, cause, _ = behavior.area_cast(0.9, values, other)
    report("cast-other-table", cast is None and "damage table of the slice" in cause, f"cause {cause!r}")
    cast, cause, _ = behavior.area_cast(None, values, rows)
    report("cast-nonfinite-time", cast is None and "finite" in cause, f"cause {cause!r}")
    cast, cause, _ = behavior.area_cast(1.2, {**values, "SpellName": "Test_Bomb", "AILeadTarget": "true", "ProjectileSpeed": "x"}, rows)
    report("cast-malformed-aim", cast is None and "aim value" in cause, f"cause {cause!r}")
    cast, cause, _ = behavior.area_cast(1.2, {**values, "SpellName": "Test_Bomb", "AILeadTarget": "true"}, rows)
    report("cast-status", cast is not None and cast["shape"] == "status" and cast["lead"] is True and "factor" not in cast,
           f"{cast}, cause {cause!r}")
    return failures


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
                           "factor": 0.35, "added": 0.0, "charge": 0.16, "attackType": "Heavy"}}
    references = {"weapons": {24: "Test_Bow", 12: "Test_Arrow", 13: "Test_NoPrefab"}}
    clip = {"RangedAttackName": "Basic", "DamageTableRow": "Test_Row", "SliceOverride": "", "UseAmmo": "true",
            "AmmoSlot": "PaperdollSlotTypes::ARROW_AMMO_SLOT", "AILeadTarget": "true", "ProjectileSpeed": "50",
            "AILeadTargetMaxAngle": "10", "AIAimMaxAngle": "45", "AIMissMinDistance": "1.5", "AIMissMaxDistance": "3"}
    main_hand = Tables.weapons["test_bow"]
    release, cause = behavior.release(clip, references, main_hand, damage)
    expected = {"index": 1, "ammo": "Test_Arrow", "ammoKey": zlib.crc32(b"test_arrow"), "projectile": 3, "override": False,
                "table": "TestBowDamageTable", "tableKey": zlib.crc32(b"testbowdamagetable"), "row": "Test_Row", "rowIndex": 2,
                "wire": 5, "lead": True, "aimSpeed": 50.0, "leadAngle": 10.0, "aimAngle": 45.0, "miss": [1.5, 3.0], "range": 100.0,
                "attackType": "Heavy"}
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


REVIEW_CASES = ("review-control", "review-control-cast", "review-other-action", "review-missing-asset", "review-missing-source",
                "review-missing-row", "review-nonfinite-time", "review-malformed-time", "review-malformed-aim")
REVIEW_ARROW = "slices/items/test_arrow.dynamicslice"
REVIEW_ADB = "animations/test/archer.adb"
REVIEW_ASSET = (bytes([0x61]) * 16, 2)


def check_review_ranged(work: Path) -> int:
    """The cases of the findings CR-R5 and CR-R6 of the code review of stage 3.4f-A. @return The number of failures."""
    import creature_behavior as cb
    failures = 0
    state = {"case": "review-control"}
    Sources.case = "none"  # The spawner names no patrol: only the ranged sources decide a case.

    def arrow_roots() -> list:
        component = node(kind=cd.PROJECTILE_CLASS, children=[
            node(cd.REPLICATION_FIELD, (1).to_bytes(4, "big")),
            *[node(cd.field_id(name), struct.pack(">f", value)) for name, value in (
                ("m_baseSpeed", 50.0), ("m_extraSpeed", 10.0), ("m_timeForMaxSpeed", 1.5), ("m_flyingLifetime", 1.5),
                ("m_stuckLifetime", 8.0))],
            node(cd.field_id("m_accel"), struct.pack(">fff", 0.0, 0.0, -2.0))])
        transform = node(children=[node(cd.LOCAL_TRANSFORM_FIELD, b""), node(cd.PARENT_FIELD, cd.NO_ENTITY.to_bytes(8, "big"))])
        return [entity(0x1234, [component, transform])]

    def animation() -> bytes:
        case = state["case"]
        time = {"review-nonfinite-time": "nan", "review-malformed-time": "1.3x"}.get(case, "1.33333")
        speed = "fast" if case == "review-malformed-aim" else "50"
        parameters = {"RangedAttackName": "Basic", "DamageTableRow": "Test_Row", "UseAmmo": "true",
                      "AmmoSlot": "PaperdollSlotTypes::ARROW_AMMO_SLOT", "AILeadTarget": "true", "ProjectileSpeed": speed,
                      "AILeadTargetMaxAngle": "10", "AIAimMaxAngle": "45", "AIMissMinDistance": "1.5", "AIMissMaxDistance": "3"}
        return ("<AnimDB><FragmentList><Fire_Bow><Fragment Tags=\"Bow\"><AnimLayer><Blend ExitTime=\"2.46667\"/>"
                "<Animation name=\"\"/></AnimLayer><ProcLayer>"
                f"<Blend ExitTime=\"{time}\"/><Procedural type=\"CAGE-RangedAttack\"><ProceduralParams>"
                + "".join(f"<{key} value=\"{value}\"/>" for key, value in parameters.items())
                + "</ProceduralParams></Procedural></ProcLayer></Fragment></Fire_Bow></FragmentList></AnimDB>").encode()

    class ReviewSources(Sources):
        def stream(self, path: str) -> list:
            return arrow_roots() if path == REVIEW_ARROW else super().stream(path)

        def read(self, path: str) -> bytes:
            return animation() if path == REVIEW_ADB else super().read(path)

        def has(self, path: str) -> bool:
            return not (state["case"] == "review-missing-source" and path == REVIEW_ARROW)

    class ReviewCatalog(Catalog):
        def __init__(self, data) -> None:
            self.paths = {} if state["case"] == "review-missing-asset" else {REVIEW_ASSET: REVIEW_ARROW}

    class ReviewBehavior(cb.Behavior):
        def __init__(self, sources, folder, ranged_attacks, projectile_of, area_spells=None, status_actions=None) -> None:
            self.sources = sources
            self.animations = cb.Animations(sources)
            self.ranged_attacks = ranged_attacks
            self.projectile_of = projectile_of
            self.ranged_left_out = {}
            self.area_spells = None
            self.areas_left_out = {}
            self.tables = type("Tables", (), {"types": {"Thrust": (5, "Physical")},
                                              "ammo": {"test_arrow": {"id": "Test_Arrow", "slice": REVIEW_ARROW}}})()

        def read(self, roots, controller, actions, slice_path="", markers=None, reactions=None) -> dict:
            rows = {} if state["case"] in ("review-missing-row", "review-other-action") else {
                "test_row": {"ordinal": 0, "table": "TestBowDamageTable", "id": "Test_Row", "type": "Thrust", "factor": 0.35,
                             "added": 0.0, "charge": 0.0}}
            result = Behavior.read(self)
            result["ranged"] = self.read_ranged(actions, [REVIEW_ADB], {"bow"}, {"weapons": {12: "Test_Arrow"}}, {"profile": "AI_Bow"},
                                                rows, slice_path)
            if not result["ranged"]:
                del result["ranged"]
            return result

    tables = {"variants": {}, "bases": {1: {"id": "Check_Kind", "family": "Wildlife", "creatureType": "Solo", "healthMod": 1.0,
                                           "damageMod": 1.0, "name": ""}},
              "categories": {}, "levels": {18: (1000.0, 100.0, 180.0)}, "modifiers": {"Solo": (1.0, 1.0)}}
    entries = [{"vitals": 1, "category": 0, "level": 18, "position": (1000.0, 1000.0, 50.0), "coatlicue": True, "override": False,
                "encounter": False}]
    attack = [["AI_Bow", "Basic", False, 100.0, 0.0, 100.0, 0.0, 0.0, 0, 0, 0, 0, False, False, [0, 0, -9.8], 0]]
    for case in REVIEW_CASES:
        state["case"] = case
        action = {"review-control-cast": "Cast_One_Projectile", "review-other-action": "Archer_Charged_Ranged_Attack"}.get(case, "FireBow")
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        replacements = {"Sources": ReviewSources, "Catalog": ReviewCatalog, "read_tables": lambda _: tables,
                        "read_names": lambda _: {}, "read_slice_lists": lambda: (set(), set()),
                        "read_spawn_entries": lambda _: entries, "read_creature_slice": creature_slice,
                        "read_ranged_attacks": lambda _: cd.ranged_dictionary(attack),
                        "read_action_table": lambda *arguments, name=action: [[name, "Fire_Bow", 35, False]]}
        errors = io.StringIO()
        with contextlib.ExitStack() as stack:
            for name, value in replacements.items():
                stack.enter_context(patch.object(cd, name, value))
            stack.enter_context(patch.object(cd.creature_behavior, "Behavior", ReviewBehavior))
            stack.enter_context(patch.object(cd.height_maps, "Terrain", Terrain))
            stack.enter_context(patch.object(sys, "argv", ["creature_data.py", str(target)]))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                status = cd.main()
        text = target.read_text(encoding="utf-8")
        kept = text == MARK
        document = {} if kept else json.loads(text)
        ranged = sum(len(entry.get("ranged", [])) for entry in document.get("slices", []))
        times = [release["time"] for entry in document.get("slices", []) for item in entry.get("ranged", []) for release in item[2]]
        left_out = document.get("counts", {}).get("rangedLeftOut", {})
        if case in ("review-control", "review-control-cast"):
            passed = status == 0 and ranged == 1 and times == [1.33333]
        elif case == "review-other-action":
            passed = status == 0 and ranged == 0 and sum(left_out.values()) == 1
        else:
            passed = status == 1 and kept and not (work / f"{case}.json.tmp").exists()
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if kept else 'replaced'}, {ranged} ranged "
              f"attack(s), times {times}, left out {left_out} {errors.getvalue().strip()[:160]}")
        failures += 0 if passed else 1
    return failures


def check_status_actions() -> int:
    """The checks of the status actions (stage 3.4f-D): the marks of an action list (creature_data.marker_statuses), the time of the mark in
    the selected variant (creature_behavior.Behavior.read_status_actions), and the low-health part of a slice (read_creature_slice).
    @return The number of failures."""
    import xml.etree.ElementTree as ElementTree
    import creature_behavior as cb
    failures = 0

    def report(case: str, passed: bool, detail: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1

    action = ElementTree.fromstring(
        '<Action Name="InqCleric_Rekindle_Self" FragmentName="InqCleric_Rekindle_Self"><Activities>'
        '<IfThenElse><IfCondition><IfMannequinMarker MarkerName="Rekindle_Self"/></IfCondition><ThenActivity><ModifyStatusEffects>'
        '<Default><EffectsToApplyOnEnter><EffectId EffectId="Test_Rekindle"/></EffectsToApplyOnEnter></Default></ModifyStatusEffects>'
        '</ThenActivity></IfThenElse>'
        '<IfThenElse><IfCondition><And><IfMannequinMarker MarkerName="Other"/><IfMannequinTag TagName="X"/></And></IfCondition>'
        '<ThenActivity><ModifyStatusEffects><Default><EffectsToApplyOnEnter><EffectId EffectId="Test_Other"/></EffectsToApplyOnEnter>'
        '</Default></ModifyStatusEffects></ThenActivity></IfThenElse>'
        '<ModifyStatusEffects><Default><EffectsToRemoveOnExit><EffectId EffectId="Test_Removed"/></EffectsToRemoveOnExit></Default>'
        '</ModifyStatusEffects></Activities></Action>')
    linked = cd.marker_statuses(action)
    report("status-marker", linked == [("Rekindle_Self", ["Test_Rekindle"])],
           f"{linked} (one IfMannequinMarker with EffectsToApplyOnEnter; not a condition with more parts, not a removal)")

    def database(time: str) -> bytes:
        return ('<AnimDB><FragmentList><InqCleric_Rekindle_Self><Fragment Tags="Staff"><AnimLayer><Blend ExitTime="0"/>'
                '<Animation name="cast"/></AnimLayer><ProcLayer>' + f'<Blend ExitTime="{time}"/>'
                '<Procedural type="CAGE-Named"><ProceduralParams><Name value="Rekindle_Self"/></ProceduralParams></Procedural>'
                '</ProcLayer></Fragment></InqCleric_Rekindle_Self></FragmentList></AnimDB>').encode()

    for case, time, expected in (("status-action", "0.633333", [[0, 1.53333, [[0.633333, ["Test_Rekindle"]]]]]),
                                 ("status-action-nonfinite-mark", "nan", None)):
        class AnimationSources:
            def read(self, path, time=time):
                return database(time) if path == "animations/test/cleric.adb" else None

            def has(self, path):
                return True

        behavior = cb.Behavior.__new__(cb.Behavior)
        behavior.animations = cb.Animations(AnimationSources())
        behavior.status_actions = {"inqcleric_rekindle_self": "a check"}
        behavior.status_left_out = {}
        actions = [["InqCleric_Rekindle_Self", "InqCleric_Rekindle_Self", 468, False]]
        try:
            found = behavior.read_status_actions(actions, ["animations/test/cleric.adb"], {"staff"}, {"InqCleric_Rekindle_Self": linked},
                                                 "slices/test/cleric.dynamicslice")
        except cd.Failure as error:
            found = f"stop: {error}"
        passed = found == expected if expected is not None else isinstance(found, str) and "not a finite number" in found
        report(case, passed, f"{found}")
    behavior.status_left_out = {}
    found = behavior.read_status_actions(actions, ["animations/test/cleric.adb"], {"staff"}, {}, "slices/test/cleric.dynamicslice")
    report("status-action-no-mark", found == [] and sum(behavior.status_left_out.values()) == 1,
           f"{found}, left out {behavior.status_left_out} (a listed action with no status at a mark of its action list)")

    def roots(value: bytes) -> list:
        transform = node(children=[node(cd.LOCAL_TRANSFORM_FIELD, b""), node(cd.PARENT_FIELD, cd.NO_ENTITY.to_bytes(8, "big"))])
        vitals = node(children=[node(cd.LOW_HEALTH_FIELD, value)])
        return [entity(0x44, [transform, vitals])]

    for case, value, expected in (("low-health", struct.pack(">f", 0.15), 0.15), ("low-health-cut", b"\x01", None),
                                  ("low-health-above-one", struct.pack(">f", 1.5), None)):
        try:
            entry = cd.read_creature_slice("slices/test/cleric.dynamicslice", (bytes([0x44]) * 16, 2), roots(value))
            found = entry.get("lowHealth")
        except cd.Failure as error:
            found = f"stop: {error}"
        passed = found == expected if expected is not None else isinstance(found, str)
        report(case, passed, f"{found}")
    return failures


LENGTH_CASES = ("length-reactions", "length-attack-animation", "length-attack-end-blend", "length-attack-guess", "length-caf-cut")


def check_lengths() -> int:
    """The lengths of user test 2 (UT2-4, UT2-7): the reaction fragments of a slice and an attack with no end blend take the end of their
    animation (the motion parameters chunk of a made-up CAF file), a fragment with an end blend that end, a looping or unknown clip no
    length (-1; an attack: its last contact plus TIME_AFTER_CONTACT), and a CAF file with no motion chunk stops the tool.
    @return The number of failures."""
    import creature_behavior as cb
    failures = 0

    def report(case: str, passed: bool, detail: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1

    def caf(frames: int, motion: bool = True) -> bytes:
        # The chunk file: "CrCh", a version, the count of the chunks and the offset of the table; one entry (kind, version, id, size,
        # offset) and the motion parameters (flags, compression, ticks per frame, seconds per tick, start frame, end frame).
        payload = struct.pack("<IIifii", 0, 0, 1, 1.0 / 30.0, 0, frames)
        table = struct.pack("<HHIII", 0x3002 if motion else 0x3003, 0x925, 1, len(payload), 16 + 16)
        return b"CrCh" + struct.pack("<III", 0x746, 1, 16) + table + payload

    database = ('<AnimDB><FragmentList>'
                '<r_R1_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_loop" flags="Loop"/></AnimLayer></Fragment></r_R1_F>'
                '<r_R2_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_r2"/><Blend ExitTime="0.8"/><Animation name=""/>'
                '</AnimLayer></Fragment></r_R2_F>'
                '<r_R3_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_r3"/></AnimLayer></Fragment>'
                '<Fragment Tags="Staff"><AnimLayer><Blend ExitTime="0"/><Animation name="test_r3_staff" speed="2"/></AnimLayer></Fragment></r_R3_F>'
                '<Attack_Bite><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_bite"/></AnimLayer></Fragment></Attack_Bite>'
                '<Attack_Nofile><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_none"/></AnimLayer></Fragment></Attack_Nofile>'
                '</FragmentList></AnimDB>').encode()
    files = {"animations/test/wolf.adb": database,
             cb.CREATURE_ANIMATION_FOLDER + "test/test_r3.caf": caf(30),        # 1.0 s
             cb.CREATURE_ANIMATION_FOLDER + "test/test_r3_staff.caf": caf(45),  # 1.5 s, at the speed 2: 0.75 s
             cb.CREATURE_ANIMATION_FOLDER + "test/test_bite.caf": caf(39),      # 1.3 s
             cb.CREATURE_ANIMATION_FOLDER + "test/test_loop.caf": caf(60)}

    class LengthSources:
        def read(self, path):
            return files.get(path)

        def has(self, path):
            return path in files

        def paths(self, prefix, suffix):
            return sorted(name for name in files if name.startswith(prefix) and name.endswith(suffix))

    behavior = cb.Behavior.__new__(cb.Behavior)
    behavior.animations = cb.Animations(LengthSources())
    behavior.attack_lengths = {"endBlend": 0, "animation": 0, "guess": 0}
    behavior.reaction_lengths = {"known": 0, "unknown": 0}
    plain = behavior.reaction_lengths_of(["animations/test/wolf.adb"], set())
    staff = behavior.reaction_lengths_of(["animations/test/wolf.adb"], {"staff"})
    report("length-reactions", plain == [-1.0, -1.0, 0.8, 1.0, -1.0] and staff[3] == 0.75,
           f"{plain}, with the tag Staff r_R3_F {staff[3]} (an end blend 0.8 s, an animation of 1.0 s, a looping clip and no fragment -1, "
           f"an animation of 1.5 s at the speed 2)")
    variants = behavior.animations.variants("animations/test/wolf.adb")
    bite = cb.select(variants["attack_bite"], set(), False)
    found = behavior.attack_length(bite, 0.4)
    report("length-attack-animation", abs(found - 1.3) < 1e-6 and behavior.attack_lengths["animation"] == 1,
           f"{found} (an attack with no end blend takes the end of its animation, 1.3 s, not its contact plus 0.9 s)")
    found = behavior.attack_length(cb.select(variants["r_r2_f"], set(), False), 0.3)
    report("length-attack-end-blend", abs(found - 0.8) < 1e-6 and behavior.attack_lengths["endBlend"] == 1, f"{found} (an end blend wins)")
    found = behavior.attack_length(cb.select(variants["attack_nofile"], set(), False), 0.5)
    report("length-attack-guess", abs(found - (0.5 + cb.TIME_AFTER_CONTACT)) < 1e-6 and behavior.attack_lengths["guess"] == 1,
           f"{found} (a clip with no animation file: the last contact plus {cb.TIME_AFTER_CONTACT} s)")
    try:
        cb.caf_length(caf(30, motion=False), "test.caf")
        cut = "no stop"
    except cd.Failure as error:
        cut = f"stop: {error}"
    report("length-caf-cut", cut.startswith("stop:") and "motion parameters" in cut, cut)
    return failures


def check_u3() -> int:
    """The members of stage U3 (research creature-combat-feel.md; backend case CF11), with made-up sources:
      u3-homing          a Homing clip of an attack fragment (its start, its own duration, its values) and the member `homing`; a clip
                         with a value that is not a number gives no member
      u3-reaction-table  the rows of a reaction table (the side conditions of <Conditions>, a tag condition, the block that never holds),
                         the fragment of a name that the slice does not have (the fragment of the action), the length and the HitStun clip
      u3-additive-scope  the scope of the fragment of the additive action in the ScopeDefs of the controller, the push scales of
                         ReactionMotion, and ExitOffsetSeconds
      u3-kind-marks      the marks of the buff buckets: an AND bucket with the status, an inner bucket, an OR bucket where only some choices
                         give it (no mark)
    @return The number of failures."""
    import creature_behavior as cb
    from xml.etree import ElementTree
    failures = 0

    def report(case: str, passed: bool, detail: str) -> None:
        nonlocal failures
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1

    database = ('<AnimDB><FragmentList>'
                '<Attack_Lunge><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="test_lunge"/><Blend ExitTime="2.0"/>'
                '<Animation name=""/></AnimLayer><ProcLayer><Blend ExitTime="1.225"/><Procedural type="Homing" contextType="HomingContext">'
                '<ProceduralParams><MoveToTarget value="true"/><MoveToDistance value="0.75"/><MoveToDuration value="0.3333"/>'
                '<MaxMoveDistance value="6"/><TurnRate value="0"/><UseExponential value="true"/><Radius value="10"/><MaxAngle value="30"/>'
                '<Height value="5"/><UseRadiusforAITarget value="true"/></ProceduralParams></Procedural><Blend ExitTime="0.333333"/>'
                '<Procedural type=""/></ProcLayer></Fragment></Attack_Lunge>'
                '<Attack_Broken><Fragment Tags=""><ProcLayer><Blend ExitTime="0.2"/><Procedural type="Homing"><ProceduralParams>'
                '<MoveToDistance value="near"/></ProceduralParams></Procedural></ProcLayer></Fragment></Attack_Broken>'
                '<r_R3_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="x"/><Blend ExitTime="1.333"/><Animation name=""/>'
                '</AnimLayer><ProcLayer><Blend ExitTime="0.183333"/><Procedural type="HitStun"/></ProcLayer></Fragment></r_R3_F>'
                '<r_R3_R><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="x"/><Blend ExitTime="1.0"/><Animation name=""/>'
                '</AnimLayer></Fragment></r_R3_R>'
                '<r_R2_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="x"/><Blend ExitTime="0.9"/><Animation name=""/>'
                '</AnimLayer></Fragment></r_R2_F>'
                '<r_R0_F><Fragment Tags=""><AnimLayer><Blend ExitTime="0"/><Animation name="x"/><Blend ExitTime="0.9"/><Animation name=""/>'
                '</AnimLayer></Fragment></r_R0_F>'
                '</FragmentList></AnimDB>').encode()
    table = ('<ActionList><Conditions><Or Name="damaged_front"><IfDamagedByAngle Max="45" Min="0" Tolerance="0.01"/>'
             '<IfDamagedByAngle Max="0" Min="-45" Tolerance="0.01"/></Or><IfDamagedByAngle Name="damaged_right" Max="135" Min="45" '
             'Tolerance="0.01"/><IfDamaged Name="blocked" RequireDamageWasBlocked="true"/><IfMannequinTag Name="crawler_react" '
             'TagName="Crawler"/></Conditions><Reactions>'
             '<ReactionTableData PowerLevel="*" Reaction="r_R2_F"><IfExternalCondition ConditionName="crawler_react"/></ReactionTableData>'
             '<ReactionTableData PowerLevel="0" Reaction="r_B1_F" AlignToDirection="true"><IfExternalCondition ConditionName="blocked"/>'
             '</ReactionTableData>'
             '<ReactionTableData PowerLevel="3" Reaction="r_R3_F"><IfExternalCondition ConditionName="damaged_front"/></ReactionTableData>'
             '<ReactionTableData PowerLevel="3" Reaction="r_R3_R" AlignToDirection="true" AlignOffsetAngle="45">'
             '<IfExternalCondition ConditionName="damaged_right"/></ReactionTableData>'
             '<ReactionTableData PowerLevel="3" Reaction="r_R3_L"><IfDamagedByAngle Max="-45" Min="-135" Tolerance="0.01"/>'
             '</ReactionTableData>'
             '<ReactionTableData PowerLevel="5" Reaction="r_R4_F_Overhead"/>'
             '</Reactions></ActionList>').encode()
    files = {"animations/test/wolf.adb": database, "sharedassets/test/table.actionlist": table}

    class U3Sources:
        def read(self, path):
            return files.get(path)

        def has(self, path):
            return path in files

        def paths(self, prefix, suffix):
            return sorted(name for name in files if name.startswith(prefix) and name.endswith(suffix))

    behavior = cb.Behavior.__new__(cb.Behavior)
    behavior.animations = cb.Animations(U3Sources())
    behavior.reaction_table_counts = {"slices": 0, "rows": 0, "neverRows": 0, "noVariant": 0, "additive": 0, "hitStunClips": 0,
                                      "missingTables": 0}
    variants = behavior.animations.variants("animations/test/wolf.adb")
    lunge = cb.select(variants["attack_lunge"], set(), False)
    homing, cause = cb.homing_member(lunge["homing"])
    broken, broken_cause = cb.homing_member(cb.select(variants["attack_broken"], set(), False)["homing"])
    expected = [1.225, 0.333333, 1, 0.75, 0.3333, 0.0, 6.0, 0.0, 1, 10.0, 30.0, 5.0, 0, 0, 0, 1, 0]
    report("u3-homing", homing == [expected] and broken is None and "not a finite number" in broken_cause,
           f"{homing}; a clip with the distance 'near': {broken_cause}")
    rows = cd.read_reaction_table(U3Sources(), "sharedassets/test/table.actionlist", "a test")
    action = {"fragment": "r_R2_F", "fragmentIndex": 15, "table": rows, "motion": [["Default", 1.0, 1.0]], "exit": 0.0}
    positions = {"r_r2_f": 15, "r_r3_f": 26, "r_r3_r": 29, "r_r3_l": 28, "r_b1_f": 40}
    member = behavior.table_member(action, positions, ["animations/test/wolf.adb"], set())
    crawler = behavior.table_member(action, positions, ["animations/test/wolf.adb"], {"crawler"})
    got = member["rows"]
    report("u3-reaction-table",
           len(got) == 4 and got[0][:3] == [3, [[0.0, 45.0, 0.01], [-45.0, 0.0, 0.01]], 26] and got[0][3] == 1.333 and got[0][4] == 0.183333
           and got[1][:5] == [3, [[45.0, 135.0, 0.01]], 29, 1.0, -1.0] and got[1][5:] == [1, 45.0]
           and got[2][:3] == [3, [[-135.0, -45.0, 0.01]], 26] and got[3][:3] == [5, None, 15] and crawler["rows"][0][:3] == [-1, None, 15],
           f"{got}; with the tag Crawler the first row {crawler['rows'][0]} (the block never holds; a side fragment with no variant takes the "
           f"front fragment of its power; a name that the slice does not have: the fragment of the action)")
    controller = ElementTree.fromstring('<ControllerDef><FragmentDefs><r_R0_F scopes="Additive"/><r_R2_F scopes="FullBody+Audio"/>'
                                        '</FragmentDefs><ScopeDefs><FullBody/><Head/><Tail/><Mouth/><Additive/><LookPose/></ScopeDefs>'
                                        '</ControllerDef>')
    scope_defs = {child.tag.lower(): index for index, child in enumerate(controller.find("ScopeDefs"))}
    scopes = {child.tag.lower(): child.get("scopes", "") for child in controller.find("FragmentDefs")}
    additive = ElementTree.fromstring('<Action Name="Reaction_Additive" FragmentName="r_R0_F"><DeactivationCondition><IfFragmentDone '
                                      'ExitOffsetSeconds="0.2"/></DeactivationCondition><Activities><ReactionMotion><Default Scale="1.0"/>'
                                      '<Conditional Name="PromotedElite" Scale="1.0" PushBackScale="0.0"/></ReactionMotion></Activities>'
                                      '</Action>')
    motion = cd.reaction_motion(additive)
    scope = scope_defs.get(scopes["r_r0_f"].split("+")[0].lower(), -1)
    report("u3-additive-scope", scope == 4 and motion == [["Default", 1.0, 1.0], ["PromotedElite", 1.0, 0.0]],
           f"the scope of r_R0_F {scope}, the motion {motion}")
    buckets = {"Elite": {"type": "AND", "buffs": [("AI_Elite_Unbreakable_Grit_Buff", "statuseffect")]},
               "Outer": {"type": "AND", "buffs": [("Elite", "buffbucket"), ("Other_Buff", "statuseffect")]},
               "Roll": {"type": "OR", "buffs": [("Elite", "buffbucket"), ("Other_Buff", "statuseffect")]},
               "Both": {"type": "OR", "buffs": [("Elite", "buffbucket"), ("Outer", "buffbucket")]}}
    marks = [cd.kind_marks({"buffBuckets": text}, buckets) for text in ("Outer", "Roll", "Both", "")]
    report("u3-kind-marks", marks == [["promotedElite"], [], ["promotedElite"], []],
           f"{marks} (an inner AND bucket gives the mark; an OR bucket only when each choice gives it)")
    return failures


REVIEW_AREA_CASES = ("review-area-control", "review-area-nonfinite-time", "review-area-malformed-aim", "review-area-missing-row",
                     "review-area-unsupported")
REVIEW_SHAMAN_ADB = "animations/test/shaman.adb"


def check_review_areas(work: Path) -> int:
    """The cases of the finding AR-R7 of the code review of stage 3.4f-C (the writer of the tool with made-up casts): a cast of a supported
    area spell with a malformed event or a broken join stops the tool, and the file stays; a cast of a spell with no support is a count.
    @return The number of failures."""
    import creature_behavior as cb
    failures = 0
    state = {"case": REVIEW_AREA_CASES[0]}
    Sources.case = "none"

    def animation() -> bytes:
        case = state["case"]
        time = "nan" if case == "review-area-nonfinite-time" else "1.2"
        speed = "bad" if case == "review-area-malformed-aim" else "100"
        spell = "Test_Other" if case == "review-area-unsupported" else "Test_Strike"
        values = {"SpellName": spell, "AIAimAtTarget": "true", "ProjectileSpeed": speed, "AILeadTargetMaxAngle": "20",
                  "AIAimMaxAngle": "45", "AIMissMinDistance": "0", "AIMissMaxDistance": "2", "UseAllAvailableTargets": "true"}
        return ("<AnimDB><FragmentList><Shaman_Bomb><Fragment Tags=\"Bow\"><AnimLayer><Blend ExitTime=\"3.0\"/>"
                "<Animation name=\"\"/></AnimLayer><ProcLayer>" + f"<Blend ExitTime=\"{time}\"/>"
                "<Procedural type=\"CAGE-CastSpell\"><ProceduralParams>"
                + "".join(f"<{key} value=\"{value}\"/>" for key, value in values.items())
                + "</ProceduralParams></Procedural></ProcLayer></Fragment></Shaman_Bomb></FragmentList></AnimDB>").encode()

    class AreaSources(Sources):
        def read(self, path: str) -> bytes:
            return animation() if path == REVIEW_SHAMAN_ADB else super().read(path)

    class KnownSpells:
        @staticmethod
        def listed(name):
            return name == "Test_Strike"

        @staticmethod
        def shape(name):
            if name == "Test_Strike":
                return {"spell": "Test_Strike", "shape": "strike", "table": "TestShamanTable", "row": "Test_Strike"}, ""
            return None, "the record of the spell slice has no live check yet (creature_slices.json, areaSpells)"

    class AreaBehavior(cb.Behavior):
        def __init__(self, sources, folder, ranged_attacks, projectile_of, area_spells=None, status_actions=None) -> None:
            self.sources = sources
            self.animations = cb.Animations(sources)
            self.area_spells = KnownSpells()
            self.areas_left_out = {}
            self.ranged_left_out = {}
            self.tables = type("Tables", (), {"types": {"Lightning": (9, "Elemental")}})()

        def read(self, roots, controller, actions, slice_path="", markers=None, reactions=None) -> dict:
            rows = {} if state["case"] == "review-area-missing-row" else {
                "test_strike": {"ordinal": 0, "table": "TestShamanTable", "id": "Test_Strike", "type": "Lightning", "factor": 0.85,
                                "added": 0.0, "charge": 0.0}}
            result = Behavior.read(self)
            areas = self.read_areas(actions, [REVIEW_SHAMAN_ADB], {"bow"}, rows, slice_path)
            if areas:
                result["areas"] = areas
            return result

    tables = {"variants": {}, "bases": {1: {"id": "Check_Kind", "family": "Wildlife", "creatureType": "Solo", "healthMod": 1.0,
                                           "damageMod": 1.0, "name": ""}},
              "categories": {}, "levels": {18: (1000.0, 100.0, 180.0)}, "modifiers": {"Solo": (1.0, 1.0)}}
    entries = [{"vitals": 1, "category": 0, "level": 18, "position": (1000.0, 1000.0, 50.0), "coatlicue": True, "override": False,
                "encounter": False}]
    for case in REVIEW_AREA_CASES:
        state["case"] = case
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        replacements = {"Sources": AreaSources, "Catalog": Catalog, "read_tables": lambda _: tables, "read_names": lambda _: {},
                        "read_slice_lists": lambda: (set(), set()), "read_spawn_entries": lambda _: entries,
                        "read_creature_slice": creature_slice, "read_ranged_attacks": lambda _: {}, "slice_assets": lambda _: {},
                        "read_action_table": lambda *arguments: [["Shaman_Bomb", "Shaman_Bomb", 120, False]]}
        errors = io.StringIO()
        with contextlib.ExitStack() as stack:
            for name, value in replacements.items():
                stack.enter_context(patch.object(cd, name, value))
            stack.enter_context(patch.object(cd.creature_behavior, "Behavior", AreaBehavior))
            stack.enter_context(patch.object(cd.height_maps, "Terrain", Terrain))
            stack.enter_context(patch.object(sys, "argv", ["creature_data.py", str(target)]))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                status = cd.main()
        text = target.read_text(encoding="utf-8")
        kept = text == MARK
        document = {} if kept else json.loads(text)
        areas = sum(len(entry.get("areas", [])) for entry in document.get("slices", []))
        left_out = document.get("counts", {}).get("areasLeftOut", {})
        if case == "review-area-control":
            passed = status == 0 and areas == 1
        elif case == "review-area-unsupported":
            passed = status == 0 and areas == 0 and sum(left_out.values()) == 1
        else:
            passed = status == 1 and kept and not (work / f"{case}.json.tmp").exists()
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if kept else 'replaced'}, {areas} area attack(s), "
              f"left out {left_out} {errors.getvalue().strip()[:160]}")
        failures += 0 if passed else 1
    return failures


REVIEW_SUPPORT_CASES = ("review-support-valid", "review-support-missing-row", "review-support-missing-asset", "review-support-missing-slice")
REVIEW_SUPPORT_ADB = "animations/test/support-review.adb"
REVIEW_SUPPORT_PREFAB = "Characters/Test_Support_Strike"


def check_review_support(work: Path) -> int:
    """The cases of the finding SU-R4 of the code review of stage 3.4f-D: a cast of a spell of the list of the proved area spells whose spell
    row, slice asset, or slice is not there stops the tool, and the file stays (the tool replaced the export with no area attack).
    @return The number of failures."""
    import check_spell_tool as sf
    import creature_behavior as cb
    import spells
    failures = 0
    state = {"case": REVIEW_SUPPORT_CASES[0]}
    Sources.case = "none"
    spell_path = spells.slice_path(REVIEW_SUPPORT_PREFAB)

    def roots() -> list:
        # The slice of a strike: a Spell component on the root, a volume with no shape on the child, and a delayed event that activates the
        # child at 0.95 s.
        result = sf.slice_nodes(struct.pack(">f", 0.9))
        result[1].children[1].value = b"Damage"
        spells.find_field(result[1], spells.COMPONENTS_FIELD).children = [sf.component(spells.VOLUME_CLASS, 30, [])]
        delay_kind = uuid.UUID(next(key for key, value in spells.COMPONENT_CLASSES.items() if value[0] == "DelayedEvent"))
        event = [sf.element(spells.ENTITY_REF_FIELD, children=[sf.identifier(spells.ID_FIELD, 200)]),
                 sf.element(spells.EVENT_TYPE_FIELD, b"\x03")]
        delayed = sf.component(delay_kind, 13, [sf.element(spells.REPLICATION_FIELD, b"\x08"),
                                               sf.element(spells.DELAY_FIELD, struct.pack(">f", 0.95)),
                                               sf.element(spells.EVENT_FIELD, children=event)])
        spells.find_field(result[0], spells.COMPONENTS_FIELD).children.append(delayed)
        return result

    def animation() -> bytes:
        values = {"SpellName": "Test_Support_Strike", "AIAimAtTarget": "true", "ProjectileSpeed": "100", "AILeadTargetMaxAngle": "20",
                  "AIAimMaxAngle": "45", "AIMissMinDistance": "0", "AIMissMaxDistance": "2", "UseAllAvailableTargets": "true"}
        return ("<AnimDB><FragmentList><Shaman_Bomb><Fragment Tags=\"Bow\"><AnimLayer><Blend ExitTime=\"3.0\"/>"
                "<Animation name=\"\"/></AnimLayer><ProcLayer><Blend ExitTime=\"1.2\"/><Procedural type=\"CAGE-CastSpell\"><ProceduralParams>"
                + "".join(f"<{key} value=\"{value}\"/>" for key, value in values.items())
                + "</ProceduralParams></Procedural></ProcLayer></Fragment></Shaman_Bomb></FragmentList></AnimDB>").encode()

    class SupportSources(Sources):
        def read(self, path: str) -> bytes:
            return animation() if path == REVIEW_SUPPORT_ADB else super().read(path)

        def has(self, path: str) -> bool:
            return not (path == spell_path and state["case"] == "review-support-missing-slice")

        def stream(self, path: str) -> list:
            return roots() if path == spell_path else super().stream(path)

    class SupportBehavior(cb.Behavior):
        def __init__(self, sources, folder, ranged_attacks, projectile_of, area_spells=None, status_actions=None) -> None:
            self.sources = sources
            self.animations = cb.Animations(sources)
            self.area_spells = spells.AreaSpells(sources, {"test_support_strike": "a made-up live check"})
            self.area_spells._rows = {} if state["case"] == "review-support-missing-row" else {
                "test_support_strike": {"SpellID": "Test_Support_Strike", "SpellPrefabPath": REVIEW_SUPPORT_PREFAB, "TargetTypes": "Enemy",
                                        "ShapeTypes": "Sphere", "Radius": "2.5", "DamageTable": "TestShamanTable",
                                        "DamageTableRow": "Test_Strike"}}
            self.area_spells._by_path = {} if state["case"] == "review-support-missing-asset" else {spell_path: sf.ASSET}
            self.areas_left_out = {}
            self.ranged_left_out = {}
            self.tables = type("Tables", (), {"types": {"Lightning": (9, "Elemental")}})()

        def read(self, roots, controller, actions, slice_path="", markers=None, reactions=None) -> dict:
            rows = {"test_strike": {"ordinal": 0, "table": "TestShamanTable", "id": "Test_Strike", "type": "Lightning", "factor": 0.85,
                                    "added": 0.0, "charge": 0.0}}
            result = Behavior.read(self)
            areas = self.read_areas(actions, [REVIEW_SUPPORT_ADB], {"bow"}, rows, slice_path)
            if areas:
                result["areas"] = areas
            return result

    tables = {"variants": {}, "bases": {1: {"id": "Check_Kind", "family": "Wildlife", "creatureType": "Solo", "healthMod": 1.0,
                                           "damageMod": 1.0, "name": ""}},
              "categories": {}, "levels": {18: (1000.0, 100.0, 180.0)}, "modifiers": {"Solo": (1.0, 1.0)}}
    entries = [{"vitals": 1, "category": 0, "level": 18, "position": (1000.0, 1000.0, 50.0), "coatlicue": True, "override": False,
                "encounter": False}]
    for case in REVIEW_SUPPORT_CASES:
        state["case"] = case
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        replacements = {"Sources": SupportSources, "Catalog": Catalog, "read_tables": lambda _: tables, "read_names": lambda _: {},
                        "read_slice_lists": lambda: (set(), set()), "read_spawn_entries": lambda _: entries,
                        "read_creature_slice": creature_slice, "read_ranged_attacks": lambda _: {}, "slice_assets": lambda _: {},
                        "read_action_table": lambda *arguments: [["Shaman_Bomb", "Shaman_Bomb", 120, False]]}
        errors = io.StringIO()
        with contextlib.ExitStack() as stack:
            for name, value in replacements.items():
                stack.enter_context(patch.object(cd, name, value))
            stack.enter_context(patch.object(cd.creature_behavior, "Behavior", SupportBehavior))
            stack.enter_context(patch.object(cd.height_maps, "Terrain", Terrain))
            stack.enter_context(patch.object(sys, "argv", ["creature_data.py", str(target)]))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                status = cd.main()
        text = target.read_text(encoding="utf-8")
        kept = text == MARK
        document = {} if kept else json.loads(text)
        areas = sum(len(entry.get("areas", [])) for entry in document.get("slices", []))
        if case == "review-support-valid":
            passed = status == 0 and areas == 1
        else:
            passed = status == 1 and kept and not (work / f"{case}.json.tmp").exists()
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if kept else 'replaced'}, {areas} area attack(s) "
              f"{errors.getvalue().strip()[:160]}")
        failures += 0 if passed else 1
    return failures


def tree_asset(path: str, length: int | None = None) -> bytes:
    """@return One made-up asset value of m_behaviorTreeAssets: the asset id, 16 bytes, the asset type, the length, the path."""
    data = path.encode("utf-8")
    return bytes(range(16)) + bytes(16) + bytes(range(16, 32)) + (len(data) if length is None else length).to_bytes(8, "big") + data


def check_server_trees() -> int:
    """The server trees of a slice (stage BT2): creature_behavior.read_server_trees with made-up elements."""
    behavior = cd.creature_behavior
    base = "SharedAssets/AI/BehaviorTrees/Wolf/Wolf_Base.behaviortree"
    explode = "sharedassets/ai/behaviortrees/bomber/bomber_explode.behaviortree"

    def slice_roots(*values: bytes) -> list:
        assets = node(behavior.TREE_ASSETS_FIELD, children=[node(children=[node(cd.field_id("m_asset"), value)]) for value in values])
        return [node(children=[node(children=[assets])])]

    cases = [("trees-one", slice_roots(tree_asset(base)), ["wolf/wolf_base"]),
             ("trees-two", slice_roots(tree_asset("SharedAssets/AI/BehaviorTrees/Bomber/Bomber_Base.behaviortree"), tree_asset(explode),
                                       tree_asset("sharedassets/ai/behaviortrees/bomber/bomber_base.behaviortree")),
              ["bomber/bomber_base", "bomber/bomber_explode"]),
             ("trees-none", [node(children=[node(cd.field_id("m_name"), b"x")])], []),
             ("trees-other-folder", slice_roots(tree_asset("sharedassets/ai/other/wolf.behaviortree")), None),
             ("trees-cut-length", slice_roots(tree_asset(base, length=12)), None)]
    failures = 0
    for case, roots, expected in cases:
        try:
            names = behavior.read_server_trees(roots, "slices/check/creature.dynamicslice")
            passed = names == expected
            detail = f"{names}"
        except cd.Failure as error:
            passed = expected is None
            detail = f"Failure: {error}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return failures


def check_targets() -> int:
    """Stage QG5c1: the respawn values of an area spawner (two doubles), and the test of a slice of a stationary target."""
    failures = 0
    asset = node(cd.SLICE_FIELD, bytes([0x44]) * 16 + (2).to_bytes(4, "big"))
    area = [asset, node(cd.AREA_RESPAWN_FIELDS["m_minRespawnRange"], struct.pack(">d", 360.0)),
            node(cd.AREA_RESPAWN_FIELDS["m_maxRespawnRange"], struct.pack(">d", 360.0))]
    point = [asset, node(cd.SPAWNER_FIELDS["m_minRespawnCooldown"], struct.pack(">f", 90.0)),
             node(cd.SPAWNER_FIELDS["m_maxRespawnCooldown"], struct.pack(">f", 90.0)), *area[1:]]
    read = cd.read_spawner(area)
    passed = read["respawnMin"] == 360.0 and read["respawnMax"] == 360.0
    print(f"{'ok  ' if passed else 'FAIL'}  target-area-respawn: the area spawner gives {read['respawnMin']} and {read['respawnMax']}")
    failures += 0 if passed else 1
    read = cd.read_spawner(point)
    passed = read["respawnMin"] == 90.0 and read["respawnMax"] == 90.0
    print(f"{'ok  ' if passed else 'FAIL'}  target-point-first: the point spawner cooldown wins ({read['respawnMin']})")
    failures += 0 if passed else 1
    try:
        cd.read_spawner([asset, node(cd.AREA_RESPAWN_FIELDS["m_minRespawnRange"], struct.pack(">d", float("nan")))])
        passed = False
    except cd.Failure:
        passed = True
    print(f"{'ok  ' if passed else 'FAIL'}  target-area-nan: a respawn range that is not finite stops the tool")
    failures += 0 if passed else 1
    path = "slices/test/totem.dynamicslice"
    listed = {path: "a live check"}
    totem = {"path": path, "movement": 0, "vitals": 1, "place": 3}
    passed = (cd.is_target(totem, listed) and not cd.is_target({**totem, "movement": 5}, listed)
              and not cd.is_target({**totem, "place": 0}, listed) and not cd.is_target({**totem, "vitals": 0}, listed)
              and not cd.is_target(totem, {}))
    print(f"{'ok  ' if passed else 'FAIL'}  target-slice: a listed slice with a vitals and a place slot and no movement slot is a target")
    failures += 0 if passed else 1
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
        for case, expected in PHASE_CASES.items():
            Sources.case = case
            target = work / f"{case}.json"
            target.write_text(MARK, encoding="utf-8")
            errors = io.StringIO()
            with patch.object(sys, "argv", ["creature_data.py", str(target)]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
                status = cd.main()
            text = target.read_text(encoding="utf-8")
            if expected is not None:
                document = json.loads(text) if text != MARK else {}
                values = document["spawnPoints"][0][11:] if document.get("spawnPoints") else None
                passed = status == 0 and document.get("format") == 4 and values == expected
                detail = f"status {status}, format {document.get('format')}, values 12 to 14 {values}"
            else:
                passed = status == 1 and text == MARK and not (work / f"{case}.json.tmp").exists()
                detail = f"status {status}, file {'kept' if text == MARK else 'replaced'}: {errors.getvalue().strip()}"
            print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
            failures += 0 if passed else 1
        # Stage QG5c1: an encounter entry of a different vitals row is in the member keptOut with its cause; the name of an exported kind
        # is not.
        tables["bases"][2] = {"id": "Check_Encounter", "family": "Wildlife", "creatureType": "Solo", "healthMod": 1.0, "damageMod": 1.0,
                              "name": ""}
        entries.append({"vitals": 2, "category": 0, "level": 18, "position": (1010.0, 1000.0, 50.0), "coatlicue": True,
                        "override": False, "encounter": True})
        Sources.case = "valid"
        target = work / "kept-out.json"
        with patch.object(sys, "argv", ["creature_data.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = cd.main()
        kept = json.loads(target.read_text(encoding="utf-8")).get("keptOut", {}) if status == 0 else {}
        passed = status == 0 and kept.get("check_encounter") == cd.KEPT_ENCOUNTER and "check_kind" not in kept
        print(f"{'ok  ' if passed else 'FAIL'}  kept-out: status {status}, keptOut {kept}")
        failures += 0 if passed else 1
        entries.pop()
    failures += check_targets()
    failures += check_ranged()
    failures += check_review_ranged(work)
    failures += check_areas()
    failures += check_review_areas(work)
    failures += check_review_support(work)
    failures += check_status_actions()
    failures += check_lengths()
    failures += check_u3()
    failures += check_server_trees()
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

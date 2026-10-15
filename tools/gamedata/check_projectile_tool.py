#!/usr/bin/env python3
"""Check of the projectile data tool with made-up sources (no file of the game).

The projectile data tool (projectiles.py) must write no file from a source that is not complete. This check gives the entry
point of the tool four made-up tables (the ammunition, a bow, a spell table, the damage table), one ranged attack database
with one profile and one attack, one projectile slice, and one animation database with one CAGE-RangedAttack clip, with a
source provider that reads no game copy, and compares the status and the output file of each case. The test program of the
backend runs it (case PJ11).

  valid              status 0: the attack, the slice, the ammunition, the weapon, and the shot of the clip
  missing-column     the spell table has no column RangedAttackName: status 1, the old file stays
  range-not-finite   m_maxRange of the attack is not a finite number: status 1, the old file stays
  bad-charge         a ChargeValue of the clip that is not a number: status 1, the old file stays

Usage: check_projectile_tool.py WORK_FOLDER
Status 0: each case has the expected result.
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
import projectiles as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402
from slice_ids import Element  # noqa: E402

MARK = "earlier output\n"
TEXT = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")
ADB = "animations/mannequin/adb/player/test.adb"
SLICE = "slices/items/weapons/test_arrow.dynamicslice"


def element(name=None, value=None, kind=None, children=()) -> Element:
    node = Element()
    node.name = tool.field_id(name) if isinstance(name, str) else name
    node.value, node.type_id, node.children = value, kind or uuid.UUID(int=0), list(children)
    return node


def real(name: str, value: float) -> Element:
    return element(name, struct.pack(">f", value))


def radb(max_range: float) -> list[Element]:
    """@return The nodes of a ranged attack database with the profile P and its attack Basic."""
    fields = [real("m_maxRange", max_range), real("m_projectileRadius", 0.15), real("m_projectileHitScanRange", 2.0),
              real("m_projectilePenetrationDepth", 0.2), real("m_projectileAccelDelayDist", 5.0), real("m_projectileDamageFalloffDist", 30.0),
              real("m_distForProjectileDamageFalloffMax", 80.0), real("m_projectileDamageMaxFalloff", 1.0),
              element("m_isHitScan", b"\x00"), element("m_bounceCountMax", b"\x00\x00\x00\x00"), element("m_destroyProjectileOnBlock", b"\x01"),
              element("m_destroyProjectileOnUnsuccesfulHit", b"\x01"), element("m_predictedProjectileAccel", struct.pack(">3f", 0.0, 0.0, -9.8)),
              element("m_spreadshotConfig", children=[element("m_shotgunData", children=[
                  element("m_numRings", b"\x00\x00\x00\x00"), element("m_numSectorsPerRing", b"\x00\x00\x00\x00"),
                  element("m_numShotsPerSector", b"\x00\x00\x00\x00")])])]
    attack = element(children=[element("value1", b"Basic", TEXT), element("value2", children=fields)])
    profile = element(children=[element("value1", b"P", TEXT), element("value2", children=[element("m_rangedAttackData", children=[attack])])])
    return [element(children=[element("m_rangedAttackProfiles", children=[profile])])]


def projectile_slice() -> list[Element]:
    return [element(children=[real("m_baseSpeed", 100.0), real("m_extraSpeed", 25.0), real("m_timeForMaxSpeed", 1.0),
                              element("m_accel", struct.pack(">3f", 0.0, 0.0, -9.8)), real("m_flyingLifetime", 10.0),
                              real("m_stuckLifetime", 8.0)])]


def animation_database(charge: str) -> bytes:
    return (f'<AnimDB><FragmentList><Bow_Shoot><Fragment Tags="Bow"><ProcLayer><Procedural type="CAGE-RangedAttack">'
            f'<ProceduralParams><RangedAttackName value="Basic"/><ChargeValue value="{charge}"/><DamageTableRow value="TestRow"/>'
            f'<SliceOverride value=""/><UseAmmo value="true"/><AmmoSlot value="PaperdollSlotTypes::ARROW_AMMO_SLOT"/>'
            f'<ConsumeAmmo value="true"/></ProceduralParams></Procedural></ProcLayer></Fragment></Bow_Shoot></FragmentList>'
            f'</AnimDB>').encode()


def tables(case: str) -> dict:
    spell = {"SpellID": "TestSpell", "SpellPrefabPath": "", "CastingTypes": "AoE", "RangedAttackProfile": "", "RangedAttackName": "",
             "DamageTableRow": "", "ManaCost": ""}
    if case == "missing-column":
        del spell["RangedAttackName"]
    return {
        "ammo": (tool.AMMO_TYPE, FakeSheet("Ammo", [{"AmmoID": "DefaultArrow", "AmmoType": "Arrow", "DamageModifier": 1.2,
                                                     "AmmoPrefabPath": "Items/Weapons/test_arrow"}])),
        "weapons": (tool.WEAPON_TYPE, FakeSheet("Weapons", [{"WeaponID": "TestBow", "RangedAttackProfile": "P", "AmmoType": "Arrow",
                                                             "TooltipPrimaryAttackData": "TestRow", "TooltipAlternateAttackData": "TestHeavy"}])),
        "spells": (tool.SPELL_TYPE, FakeSheet("Spells", [spell])),
        "damage": (tool.DAMAGE_TYPE, FakeSheet(tool.DAMAGE_TABLE, [{"DamageID": "TestRow", "WeaponCategory": "Bow", "AttackType": "Light"},
                                                                   {"DamageID": "TestHeavy", "WeaponCategory": "Bow", "AttackType": "Heavy"}])),
    }


def run(work: Path, case: str) -> tuple[int, str]:
    sheets = tables(case)
    database = radb(math.nan if case == "range-not-finite" else 100.0)
    adb = animation_database("x" if case == "bad-charge" else "0")

    class Sources:
        used: dict = {}

        def __init__(self, *arguments) -> None:
            pass

        def paths(self, prefix: str, suffix: str):
            if suffix == ".datasheet":
                return list(sheets)
            return [ADB] if suffix == ".adb" else []

        def read(self, path: str):
            return adb if path == ADB else path.encode()

        def check_complete(self) -> None:
            pass

        def has(self, wanted: str) -> bool:
            return wanted == SLICE

        def stream(self, wanted: str):
            return database if wanted == tool.RADB_PATH else (projectile_slice() if wanted == SLICE else None)

    target = work / f"projectile-tool-check-{case}.json"
    target.write_text(MARK, encoding="utf-8")
    with patch.object(tool, "Sources", Sources), patch.object(tool, "sheet_type", lambda data: sheets[data.decode()][0]), \
            patch.object(tool, "Sheet", lambda data: sheets[data.decode()][1]), \
            patch.object(sys, "argv", ["projectiles.py", str(target)]), \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        status = tool.main()
    text = target.read_text(encoding="utf-8")
    target.unlink(missing_ok=True)
    return status, text


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    status, text = run(work, "valid")
    document = json.loads(text) if status == 0 and text != MARK else {}
    expected_shot = ["P", "Basic", "TestRow", 0.0, -1, "", "", False, 12, True]
    passed = (status == 0 and document.get("shots") == [expected_shot] and document.get("weapons") == [["TestBow", "P", "Arrow"]]
              and document.get("ammo") == [["DefaultArrow", "Arrow", 1.2, 0]] and len(document.get("slices", [])) == 1
              and document["attacks"][0][:6] == ["P", "Basic", False, 100.0, 0.15, 2.0])
    print(f"{'ok  ' if passed else 'FAIL'}  valid: status {status}, shots {document.get('shots')}")
    failures += 0 if passed else 1
    for case in ("missing-column", "range-not-finite", "bad-charge"):
        status, text = run(work, case)
        passed = status == 1 and text == MARK
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if text == MARK else 'replaced'}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

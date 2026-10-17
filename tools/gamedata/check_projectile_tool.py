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
  missing-ammo-prefab     the prefab of the ammunition of the shot (DefaultArrow) names no slice of the source: status 1,
                          the old file stays (code review of stage 3.3b-S3, finding F7; case RJ7)
  empty-projectile-slice  that slice has no projectile component: status 1, the old file stays (F7)
  no-animation-database   no animation database: the bow has no shot: status 1, the old file stays (F7)
  missing-slice-override  a SliceOverride of the clip names no slice: status 1, the old file stays (F7)
  partial-basic-sources   the animation database has the clip of the light row of the bow and no clip of its heavy row:
                          status 1, the old file stays (code review of stage 3.3b-S2b, finding F11; case RK11)
  missing-ability-prefab  the prefab of a projectile spell of an ability that the backend launches (the Beacon) names no
                          slice: status 1, the old file stays (F11)
  missing-required-row    the spell table has no row of the Pylon projectile (IceMagic_Pylon_Projectile), an ability spell
                          that the backend launches: status 1, the old file stays (code review of stage 1.1b, finding F4)
  missing-ability-action  the bow action list has no action Bow_RapidShot (REQUIRED_ABILITY_ACTIONS of the tool, stage B1):
                          status 1, the old file stays
  cast-condition          the condition of a cast of Explosive Arrow is a test of a tag, not an AND of learned-node tests: status 1,
                          the old file stays (stage B1)

The valid case also has the bow action list (stage B1): the actions Bow_Penetrating_Shoot (the variant Bow+PenetratingAim of the
fragment Bow_Shoot_UpperBody, by the tag that the action sets), Bow_SpreadShot (two casts with node conditions), and Bow_RapidShot
(three clips), with their transitions to an ability and the shots of their clips (the 11th value true, no spell).

Each case but missing-required-row has a row of each ability spell that the backend launches (REQUIRED_ABILITY_SHOTS of the
tool), with the slice of the test and an ability damage row: the valid case has their shots.

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
ADB = "animations/mannequin/adb/player/player_bow.adb"
ACTIONS = "sharedassets/springboardentitites/playerdata/cage/playeractions_ability_bow.actionlist"
SLICE = "slices/items/weapons/test_arrow.dynamicslice"


def element(name=None, value=None, kind=None, children=()) -> Element:
    node = Element()
    node.name = tool.field_id(name) if isinstance(name, str) else name
    node.value, node.type_id, node.children = value, kind or uuid.UUID(int=0), list(children)
    return node


def real(name: str, value: float) -> Element:
    return element(name, struct.pack(">f", value))


def radb(max_range: float) -> list[Element]:
    """@return The nodes of a ranged attack database with the profile Bow (a player profile) and its attack Basic."""
    fields = [real("m_maxRange", max_range), real("m_projectileRadius", 0.15), real("m_projectileHitScanRange", 2.0),
              real("m_projectilePenetrationDepth", 0.2), real("m_projectileAccelDelayDist", 5.0), real("m_projectileDamageFalloffDist", 30.0),
              real("m_distForProjectileDamageFalloffMax", 80.0), real("m_projectileDamageMaxFalloff", 1.0),
              element("m_isHitScan", b"\x00"), element("m_bounceCountMax", b"\x00\x00\x00\x00"), element("m_destroyProjectileOnBlock", b"\x01"),
              element("m_destroyProjectileOnUnsuccesfulHit", b"\x01"), element("m_predictedProjectileAccel", struct.pack(">3f", 0.0, 0.0, -9.8)),
              element("m_spreadshotConfig", children=[element("m_shotgunData", children=[
                  element("m_numRings", b"\x00\x00\x00\x00"), element("m_numSectorsPerRing", b"\x00\x00\x00\x00"),
                  element("m_numShotsPerSector", b"\x00\x00\x00\x00")])])]
    # Stage B1: the attacks of the clips and of the spell of the bow abilities of the action list of the check.
    attacks = [element(children=[element("value1", name, TEXT), element("value2", children=fields)])
               for name in (b"Basic", b"PenetratingShot", b"TripleShot", b"ExplosionShot")]
    profile = element(children=[element("value1", b"Bow", TEXT), element("value2", children=[element("m_rangedAttackData", children=attacks)])])
    return [element(children=[element("m_rangedAttackProfiles", children=[profile])])]


def projectile_slice() -> list[Element]:
    return [element(children=[real("m_baseSpeed", 100.0), real("m_extraSpeed", 25.0), real("m_timeForMaxSpeed", 1.0),
                              element("m_accel", struct.pack(">3f", 0.0, 0.0, -9.8)), real("m_flyingLifetime", 10.0),
                              real("m_stuckLifetime", 8.0)])]


def animation_database(charge: str, override: str = "", heavy: bool = True) -> bytes:
    """@return An animation database with a clip of the light row of the bow and (`heavy`) a clip of its heavy row."""
    def clip(fragment: str, row: str) -> str:
        return (f'<{fragment}><Fragment Tags="Bow"><ProcLayer><Procedural type="CAGE-RangedAttack">'
                f'<ProceduralParams><RangedAttackName value="Basic"/><ChargeValue value="{charge}"/><DamageTableRow value="{row}"/>'
                f'<SliceOverride value="{override}"/><UseAmmo value="true"/><AmmoSlot value="PaperdollSlotTypes::ARROW_AMMO_SLOT"/>'
                f'<ConsumeAmmo value="true"/></ProceduralParams></Procedural></ProcLayer></Fragment></{fragment}>')
    clips = clip("Bow_Shoot", "TestRow") + (clip("Bow_Heavy", "TestHeavy") if heavy else "")
    return f'<AnimDB><FragmentList>{clips}{ABILITY_FRAGMENTS}</FragmentList></AnimDB>'.encode()


def ranged(time: str, attack: str, row: str) -> str:
    return (f'<Blend ExitTime="{time}"/><Procedural type="CAGE-RangedAttack"><ProceduralParams><RangedAttackName value="{attack}"/>'
            f'<ChargeValue value="0"/><DamageTableRow value="{row}"/><UseAmmo value="true"/>'
            f'<AmmoSlot value="PaperdollSlotTypes::ARROW_AMMO_SLOT"/><ConsumeAmmo value="true"/></ProceduralParams></Procedural>')


def transition(kind: str, time: str, condition: str) -> str:
    return (f'<ProcLayer><Blend ExitTime="{time}"/><Procedural type="CAGE-Transition{kind}"><ProceduralParams>'
            f'<Condition value="{condition}"/></ProceduralParams></Procedural></ProcLayer>')


def cast(time: str, spell: str, condition: str) -> str:
    return (f'<ProcLayer><Blend ExitTime="{time}"/><Procedural type="CAGE-CastSpell"><ProceduralParams><SpellName value="{spell}"/>'
            f'<Condition value="{condition}"/></ProceduralParams></Procedural></ProcLayer>')


# Stage B1: the fragments of the three bow abilities of the backend (the times of player_bow.adb).
ABILITY_FRAGMENTS = (
    '<Bow_Shoot_UpperBody><Fragment Tags="Bow"><ProcLayer>' + ranged("0.0333", "Basic", "TestRow") + '</ProcLayer></Fragment>'
    '<Fragment Tags="Bow+PenetratingAim"><ProcLayer>' + ranged("0.0333", "PenetratingShot", "Bow_Penetrating_Attack") + '</ProcLayer>'
    + transition("Buffered", "0.2333", "CanAttack_SpreadShot") + transition("Instant", "0.7", "CanAttack_SpreadShot") + '</Fragment>'
    '</Bow_Shoot_UpperBody>'
    '<Ability_Bow_RapidShot><Fragment Tags="Bow"><ProcLayer>' + ranged("0.3333", "TripleShot", "Bow_RapidAttack_Standard")
    + ranged("0.5334", "TripleShot", "Bow_RapidAttack_Standard") + ranged("0.8666", "TripleShot", "Bow_RapidAttack_KnockBack") + '</ProcLayer>'
    + transition("Buffered", "1.8681", "CanAttack_SpreadShot") + transition("Instant", "2.6", "CanAttack_SpreadShot")
    + '<ProcLayer><Blend ExitTime="2.6"/><Procedural type="CAGE-Named"><ProceduralParams><Name value="CanTransition"/></ProceduralParams>'
    '</Procedural></ProcLayer></Fragment></Ability_Bow_RapidShot>'
    '<Ability_Bow_ExplosiveArrow><Fragment Tags="Bow">' + cast("0.4667", "Spell_Ability_Bow_Explosive_Base", "Bow_ExplosiveArrow_NoUpgrade")
    + cast("0.4667", "Spell_Ability_Bow_Explosive_Upgrade", "Bow_ExplosiveArrow_Upgraded") + '</Fragment></Ability_Bow_ExplosiveArrow>')


def action_list(case: str) -> bytes:
    def action(name: str, fragment: str, ability: str, tag: str = "") -> str:
        tags = f'<SetMannequinTag><Default TagName="{tag}" OnEnter="Set" OnExit="Unset"/></SetMannequinTag>' if tag else ""
        return (f'<Action Name="{name}" FragmentName="{fragment}"><Activities><SetCooldownTimer><Default AbilityID="{ability}"/>'
                f'</SetCooldownTimer>{tags}</Activities></Action>')
    actions = action("Bow_Penetrating_Shoot", "Bow_Shoot_UpperBody", "Ability_Bow_PenetratingShot", "PenetratingAim") \
        + action("Bow_SpreadShot", "Ability_Bow_ExplosiveArrow", "Ability_Bow_SplinterShot")
    if case != "missing-ability-action":
        actions += action("Bow_RapidShot", "Ability_Bow_RapidShot", "Ability_Bow_RapidShot")
    upgraded = ('<IfMannequinTag TagName="Bow"/>' if case == "cast-condition" else
                '<IfAbilityActive AbilityName="Upgrade_Bow_SplinterShot_FiveA"/>')
    conditions = ('<And Name="Bow_ExplosiveArrow_NoUpgrade"><IfAbilityActive AbilityName="Ability_Bow_SplinterShot"/><Not>'
                  '<IfAbilityActive AbilityName="Upgrade_Bow_SplinterShot_FiveA"/></Not></And>'
                  f'<And Name="Bow_ExplosiveArrow_Upgraded"><IfAbilityActive AbilityName="Ability_Bow_SplinterShot"/>{upgraded}</And>'
                  '<And Name="CanAttack_SpreadShot"><IfActiveAbilityMoveName AbilityName="Ability_Bow_SplinterShot"/></And>')
    return f'<ActionList><Actions>{actions}</Actions><Conditions>{conditions}</Conditions></ActionList>'.encode()


def tables(case: str) -> dict:
    spell = {"SpellID": "TestSpell", "SpellPrefabPath": "", "CastingTypes": "AoE", "RangedAttackProfile": "", "RangedAttackName": "",
             "DamageTableRow": "", "ManaCost": ""}
    damage = [{"DamageID": "TestRow", "WeaponCategory": "Bow", "AttackType": "Light"},
              {"DamageID": "TestHeavy", "WeaponCategory": "Bow", "AttackType": "Heavy"}]
    spells = [spell]
    # A row of each ability spell that the backend launches (code review of stage 1.1b, finding F4: each one must be a row of
    # the source). The case missing-ability-prefab gives the Beacon a prefab that names no slice; missing-required-row has no
    # row of the Pylon projectile.
    damage.append({"DamageID": "TestAbilityDamage", "WeaponCategory": "LifeMagic", "AttackType": "Ability"})
    for row in ("Bow_Penetrating_Attack", "Bow_RapidAttack_Standard", "Bow_RapidAttack_KnockBack"):
        damage.append({"DamageID": row, "WeaponCategory": "Bow", "AttackType": "Ability"})
    for name in tool.REQUIRED_ABILITY_SHOTS:
        if case == "missing-required-row" and name == "IceMagic_Pylon_Projectile":
            continue
        missing = case == "missing-ability-prefab" and name == "Spell_Ability_Lifestaff_Beacon_Projectile"
        spells.append({"SpellID": name, "SpellPrefabPath": "Items/Weapons/missing_beacon" if missing else "Items/Weapons/test_arrow",
                       "CastingTypes": "Projectile", "RangedAttackProfile": "Bow", "RangedAttackName": "Basic",
                       "DamageTableRow": "TestAbilityDamage", "ManaCost": ""})
    if case == "missing-column":
        for row in spells:
            del row["RangedAttackName"]
    return {
        "ammo": (tool.AMMO_TYPE, FakeSheet("Ammo", [{"AmmoID": "DefaultArrow", "AmmoType": "Arrow", "DamageModifier": 1.2,
                                                     "AmmoPrefabPath": "Items/Weapons/test_arrow"}])),
        "weapons": (tool.WEAPON_TYPE, FakeSheet("Weapons", [{"WeaponID": "TestBow", "RangedAttackProfile": "Bow", "AmmoType": "Arrow",
                                                             "TooltipPrimaryAttackData": "TestRow", "TooltipAlternateAttackData": "TestHeavy",
                                                             "MannequinTag": "Bow"}])),
        "spells": (tool.SPELL_TYPE, FakeSheet("Spells", spells)),
        "damage": (tool.DAMAGE_TYPE, FakeSheet(tool.DAMAGE_TABLE, damage)),
    }


def run(work: Path, case: str) -> tuple[int, str]:
    sheets = tables(case)
    database = radb(math.nan if case == "range-not-finite" else 100.0)
    adb = animation_database("x" if case == "bad-charge" else "0",
                             "Items/Weapons/missing_override" if case == "missing-slice-override" else "",
                             case != "partial-basic-sources")

    class Sources:
        used: dict = {}

        def __init__(self, *arguments) -> None:
            pass

        def paths(self, prefix: str, suffix: str):
            if suffix == ".datasheet":
                return list(sheets)
            if suffix == ".actionlist":
                return [ACTIONS]
            return [ADB] if suffix == ".adb" and case != "no-animation-database" else []

        def read(self, path: str):
            if path == ACTIONS:
                return action_list(case)
            return adb if path == ADB else path.encode()

        def check_complete(self) -> None:
            pass

        def has(self, wanted: str) -> bool:
            return wanted == SLICE and case != "missing-ammo-prefab"

        def stream(self, wanted: str):
            if wanted == tool.RADB_PATH:
                return database
            if wanted == SLICE:
                return [] if case == "empty-projectile-slice" else projectile_slice()
            return None

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
    expected_shots = [["Bow", "Basic", "TestHeavy", 0.0, -1, "", "", True, 12, True], ["Bow", "Basic", "TestRow", 0.0, -1, "", "", False, 12, True]]
    shots = document.get("shots", [])
    basic = [shot for shot in shots if not shot[5] and not (len(shot) > 10 and shot[10])]
    clips = sorted((shot[1], shot[2]) for shot in shots if not shot[5] and len(shot) > 10 and shot[10])
    actions = {entry[0]: entry for entry in document.get("abilityActions", [])}
    rapid = actions.get("Bow_RapidShot", [None] * 8)
    penetrating = actions.get("Bow_Penetrating_Shoot", [None] * 8)
    spread = actions.get("Bow_SpreadShot", [None] * 8)
    stage_b1 = (document.get("format") == 2 and clips == [("PenetratingShot", "Bow_Penetrating_Attack"), ("TripleShot", "Bow_RapidAttack_KnockBack"),
                                                         ("TripleShot", "Bow_RapidAttack_Standard")]
                and rapid[1] == "Ability_Bow_RapidShot" and rapid[4] == 2.6 and [clip[0] for clip in rapid[5]] == [0.3333, 0.8667, 1.7333]
                and rapid[7] == [["Ability_Bow_SplinterShot", "buffered", 1.8681], ["Ability_Bow_SplinterShot", "instant", 2.6]]
                and penetrating[3] == "Bow+PenetratingAim" and penetrating[5] == [[0.0333, "PenetratingShot", "Bow_Penetrating_Attack", 0.0, True]]
                and spread[6] == [[0.4667, "Spell_Ability_Bow_Explosive_Base", ["Ability_Bow_SplinterShot"], ["Upgrade_Bow_SplinterShot_FiveA"]],
                                  [0.4667, "Spell_Ability_Bow_Explosive_Upgrade", ["Ability_Bow_SplinterShot", "Upgrade_Bow_SplinterShot_FiveA"], []]])
    print(f"{'ok  ' if stage_b1 else 'FAIL'}  valid (stage B1): the ability actions {sorted(actions)}, the clip shots {clips}")
    failures += 0 if stage_b1 else 1
    ability = sorted(shot[5] for shot in shots if shot[5])
    passed = (status == 0 and basic == expected_shots and ability == sorted(tool.REQUIRED_ABILITY_SHOTS)
              and document.get("weapons") == [["TestBow", "Bow", "Arrow"]]
              and document.get("ammo") == [["DefaultArrow", "Arrow", 1.2, 0]] and len(document.get("slices", [])) == 1
              and document["attacks"][0][:6] == ["Bow", "Basic", False, 100.0, 0.15, 2.0])
    print(f"{'ok  ' if passed else 'FAIL'}  valid: status {status}, basic shots {basic}, ability shots {ability}")
    failures += 0 if passed else 1
    for case in ("missing-column", "range-not-finite", "bad-charge", "missing-ammo-prefab", "empty-projectile-slice",
                 "no-animation-database", "missing-slice-override", "partial-basic-sources", "missing-ability-prefab",
                 "missing-required-row", "missing-ability-action", "cast-condition"):
        status, text = run(work, case)
        passed = status == 1 and text == MARK
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if text == MARK else 'replaced'}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

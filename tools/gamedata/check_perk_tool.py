#!/usr/bin/env python3
"""Check of the perk data tool (perks.py) with made-up data tables.

The check gives the reader of the tool made-up tables of each type that the tool uses (the readers of the pak files
and of the base attributes file are replaced: no file of the game is necessary), and it runs the builder and the
writer of the tool (main). The cases are the corrections of the findings F5, F14, F15, and F16 of the code review of
stage 1.3b.

  case                   the tables                                                  expected result
  valid                  each table complete                                         status 0; the values (below)
  missing-damageLight    the encumbrance table has no EquipLoadDamageMultFast        status 1, the file stays (F14)
  missing-BaseDamage     an ability table has no column BaseDamage                   status 1, the file stays (F14)
  missing-StatusEffect   the affix table has no column StatusEffect                  status 1, the file stays (F14)
  conflicting-affix      two affix rows of one id with MODStrength 5 and 100         status 1, the file stays (F15)
  conflicting-ability    two ability rows of one id with BaseDamage 0.2 and 100      status 1, the file stays (F15)
  bucket-gap             a bucket with Perk1, an empty Perk2, and Perk3              status 0, one entry (F16)
  weight-gap             a bucket with two perks and an empty second weight          status 0, one entry (F16)
  weight-zero            a bucket with two perks and the weight 0 for the second     status 0, two entries (F16)
  missing-RequiredLevel  a master item table has no column RequiredLevel             status 1, the file stays
  consumable-no-item     a second consumable has no master item row                  status 0, no row for it, the count 1
  missing-ABSSlash       the affix table has no column ABSSlash                      status 1, the file stays
  affix-no-armor         the affix table has no PhysicalArmor (as in the game)       status 0

The last four cases are the corrections of the findings F11 and F12 of the code review of stage 3.3b-S1: a missing
column or a missing master row is not the level 0, and the damage type columns of an affix are required (the affix table
of the game has no armor column).

The values of the valid document: the format 2; the encumbrance values of the row Player; the affix and the ability
of the perk; the two entries of the bucket with their weights; the level 55 of the consumable (RequiredLevel of its
master item row, F5).

Usage: check_perk_tool.py WORK_FOLDER [CASE...]     status 0: each case (or each named case) gives its result
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import perks as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

MARK = "the file of an earlier run\n"
# The value columns of the affix and the ability tables of the game (the same lists as the tool; written here, so that
# the check runs also on a tool that does not have them).
AFFIX_COLUMNS = ("MODStrength", "MODDexterity", "MODIntelligence", "MODFocus", "MODConstitution", "MaxHealthMod", "StatusEffect",
                 "DamagePercentage", "DamageType", "ABSSlash", "ABSThrust", "ABSStrike", "ABSFire", "ABSIce", "ABSNature",
                 "ABSArcane", "ABSCorruption", "ABSLightning", "ABSStandard", "DMGSlash", "DMGThrust", "DMGStrike", "DMGFire",
                 "DMGIce", "DMGNature", "DMGArcane", "DMGCorruption", "DMGLightning", "DMGStandard")
ABILITY_COLUMNS = ("BaseDamage", "OnHit", "DmgPctToHealth", "CooldownTimer", "OtherApplyStatusEffect", "MaxHealth")


def sheets(case: str) -> dict:
    result = {kind: [] for kind in tool.USED_TYPES}
    for index, name in enumerate(tool.TABLE_ORDER):
        row = dict.fromkeys(tool.PERK_COLUMNS, "")
        row.update(PerkID=f"PerkID_Test{index}", PerkType="Generated", ConditionEvent="OnEquip", Channel=1)
        if index == 0:
            row.update(Affix="TestAffix", EquipAbility="TestAbility")
        result[tool.PERK_TYPE].append(FakeSheet(name, [row]))
    affix = dict.fromkeys(AFFIX_COLUMNS, "")
    affix.update(StatusID="TestAffix", MODStrength=5)
    if case == "missing-StatusEffect":
        del affix["StatusEffect"]
    if case == "missing-ABSSlash":
        del affix["ABSSlash"]
    if case == "affix-no-armor":
        affix.pop("PhysicalArmor", None)
    result[tool.AFFIX_TYPE] = [FakeSheet("Affix", [affix])]
    if case == "conflicting-affix":
        result[tool.AFFIX_TYPE].append(FakeSheet("OtherAffix", [dict(affix, MODStrength=100)]))
    ability = dict.fromkeys(ABILITY_COLUMNS, "")
    ability.update(AbilityID="TestAbility", BaseDamage=0.2)
    if case == "missing-BaseDamage":
        del ability["BaseDamage"]
    result[tool.ABILITY_TYPE] = [FakeSheet("Abilities", [ability])]
    if case == "conflicting-ability":
        result[tool.ABILITY_TYPE].append(FakeSheet("OtherAbilities", [dict(ability, BaseDamage=100)]))
    bucket = dict.fromkeys(tool.BUCKET_COLUMNS, "")
    bucket.update(PerkBucketID="TestBucket", PerkType="Generated", PerkChance=1, Perk1="PerkID_Test0", Perk2="PerkID_Test1",
                  Perk3="")
    weights = dict(bucket, PerkBucketID="TestBucket_Weights", Perk1=1, Perk2=3, Perk3="")
    if case == "bucket-gap":
        bucket.update(Perk2="", Perk3="PerkID_Test1")
        weights.update(Perk2="", Perk3=2)
    if case == "weight-gap":
        weights.update(Perk2="")
    if case == "weight-zero":
        weights.update(Perk2=0)
    result[tool.BUCKET_TYPE] = [FakeSheet("Buckets", [bucket, weights])]
    item = dict.fromkeys(tool.ITEM_COLUMNS, "")
    item.update(ItemID="TestItem", ItemStatsRef="TestArmor", PerkBucket1="TestBucket", CanHavePerks=1, RequiredLevel=0)
    potion = dict.fromkeys(tool.ITEM_COLUMNS, "")
    potion.update(ItemID="TestPotion", RequiredLevel=55)
    if case == "missing-RequiredLevel":
        del item["RequiredLevel"]
        del potion["RequiredLevel"]
    result[tool.ITEM_TYPE] = [FakeSheet("Items", [item, potion])]
    consume = dict.fromkeys(tool.CONSUMABLE_COLUMNS, "")
    consume.update(ConsumableID="TestPotion", CooldownId="Test", CooldownDuration=1)
    consumables = [consume]
    if case == "consumable-no-item":
        consumables.append(dict(consume, ConsumableID="TestFoodWithNoItem"))
    result[tool.CONSUMABLE_TYPE] = [FakeSheet("Consumables", consumables)]
    encumbrance = dict(zip(tool.ENCUMBRANCE_COLUMNS.values(), [500, 26, 46, 100, 0.15, 0, -0.15, 1.3, 1, 0.7]))
    encumbrance["ContainerTypeID"] = "Player"
    # Stage U3: the limit of the weight of the bag (EncumbranceBaseMax; the valid document has it).
    encumbrance["EncumbranceBaseMax"] = 2000
    if case == "missing-damageLight":
        del encumbrance["EquipLoadDamageMultFast"]
    result[tool.ENCUMBRANCE_TYPE] = [FakeSheet("Encumbrance", [encumbrance])]
    result[tool.ARMOR_TYPE] = [FakeSheet("Armor", [{"WeaponID": "TestArmor", "WeightOverride": 50}])]
    result[tool.RUNE_TYPE] = [FakeSheet("Weapons", [{"WeaponID": "TestRune", "EquipAbility": "TestAbility"}])]
    result[tool.LABEL_TYPE] = [FakeSheet("Labels", [{"ExclusiveLabelID": "Test", "Weight": 100, "ItemClassOverrides": ""}])]
    result[tool.SWAP_TYPE] = [FakeSheet("Swaps", [{"ID": 1, "KeyType": "ItemId", "Key": "TestItem", "OldPerk": "PerkID_Test0",
                                                   "NewPerk": "PerkID_Test1"}])]
    return result


def valid_values(document: dict, case: str) -> list[str]:
    """@return The values of a document that are not correct."""
    wrong = []
    entries = document["buckets"]["TestBucket"][2]
    if case == "valid":
        if document["format"] != 2:
            wrong.append(f"the format {document['format']}")
        if (document["encumbrance"]["damageLight"] != 0.15 or document["encumbrance"]["healHeavy"] != 0.7
                or document["encumbrance"].get("encumbranceMax") != 2000):
            wrong.append(f"the encumbrance {document['encumbrance']}")
        if document["affixes"] != {"TestAffix": {"MODStrength": 5.0}} or document["abilities"] != {"TestAbility": {"BaseDamage": 0.2}}:
            wrong.append(f"the affix and the ability {document['affixes']} {document['abilities']}")
        if entries != [["PerkID_Test0", 1.0], ["PerkID_Test1", 3.0]]:
            wrong.append(f"the bucket entries {entries}")
        if document["consumables"]["testpotion"].get("level") != 55:
            wrong.append(f"the consumable {document['consumables']['testpotion']}")
    elif case in ("bucket-gap", "weight-gap"):
        if entries != [["PerkID_Test0", 1.0]]:
            wrong.append(f"the bucket entries {entries} (the list ends at the first empty cell)")
    elif case == "weight-zero" and entries != [["PerkID_Test0", 1.0], ["PerkID_Test1", 0.0]]:
        wrong.append(f"the bucket entries {entries} (a weight 0 is an entry)")
    elif case == "consumable-no-item":
        if "testfoodwithnoitem" in document["consumables"] or document.get("consumablesWithNoItem") != 1 \
                or document["consumables"]["testpotion"].get("level") != 55:
            wrong.append(f"a consumable with no master item row: {sorted(document['consumables'])}, "
                         f"count {document.get('consumablesWithNoItem')}")
    elif case == "affix-no-armor" and document["affixes"] != {"TestAffix": {"MODStrength": 5.0}}:
        wrong.append(f"the affix {document['affixes']}")
    return wrong


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    cases = ("valid", "missing-damageLight", "missing-BaseDamage", "missing-StatusEffect", "conflicting-affix",
             "conflicting-ability", "bucket-gap", "weight-gap", "weight-zero", "missing-RequiredLevel", "consumable-no-item",
             "missing-ABSSlash", "affix-no-armor")
    for case in sys.argv[2:] or cases:
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(tool, "Sources", lambda path: type("S", (), {"used": {}})()), \
                patch.object(tool, "read_sheets", lambda sources, case=case: sheets(case)), \
                patch.object(tool, "read_generation", lambda sources: {"minLevelRollPerks": 7, "tiers": []}), \
                patch.object(sys, "argv", ["perks.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            try:
                status = tool.main()
            except Exception as error:  # noqa: BLE001 - a crash of the tool is a failure of the case
                status = f"exception {type(error).__name__}: {error}"
        text = target.read_text(encoding="utf-8")
        if case in ("valid", "bucket-gap", "weight-gap", "weight-zero", "consumable-no-item", "affix-no-armor"):
            try:
                wrong = valid_values(json.loads(text), case) if status == 0 and text != MARK else ["no document"]
            except (ValueError, KeyError, IndexError, TypeError) as error:
                wrong = [f"the document does not have the form of format 2 ({type(error).__name__})"]
            passed = status == 0 and not wrong
            detail = f"status {status}" + (f", not correct: {'; '.join(wrong)}" if wrong else ", each value is correct")
        else:
            passed = status == 1 and text == MARK and not (work / f"{case}.json.tmp").exists()
            detail = f"status {status}, file {'kept' if text == MARK else 'replaced'}: {errors.getvalue().strip()}"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

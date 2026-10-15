#!/usr/bin/env python3
"""Check of the ability data tool (abilities.py) with made-up data tables.

The check gives the reader of the tool made-up tables of each type that the tool uses (the reader of the pak
files is replaced: no file of the game is necessary), and it runs the builder and the writer of the tool (main).

  case                   the tables                                               expected result
  valid                  each table complete                                      status 0; the values (below)
  missing-Time           the cooldown table has no column Time                    status 1, the file stays
  missing-Stunned        a status table has no column Stunned                     status 1, the file stays
  missing-Radius         a spell table has no column Radius                       status 1, the file stays
  conflicting-status     two status rows with one id and different Stunned        status 1, the file stays
  conflicting-spell      two spell rows with one id and different Radius          status 1, the file stays
  game-duplicate         two rows of Sanctuary with different values              status 0, the first row stays
  missing-cost           an ability names a mana cost id that no row has          status 1, the file stays
  partial-cost-columns   an ability table with only ManaCostList                  status 1, the file stays
  negative-cost          a mana cost -5                                           status 1, the file stays
  missing-SelfApply      an ability table of the players has no SelfApplyStatusEffect   status 1, the file stays
  unknown-caster-status  a caster effect row names a status that no table has     status 1, the file stays
  bad-limit              a ValueLimits part with no number                        status 1, the file stays

The first six cases after valid are the corrections of the findings F11 and F12 of the code review of stage 3.3a
(a column that is not there is not 0 or false; two rows with one id must agree). The last three are the cost data of
stage 1.3b (criterion I14). The last three are the caster effect rows and the status categories of stage 3.3b-S1
(format 3). The values of the valid document: the format 3; the status row with its number among
the CRC values of all rows; its other cells (Health 400, TickRate 1, EffectCategories); the spell with its cost id
"SpecialAttack" (an id that the game itself does not resolve: no cost); the mana cost 25 of a cost id; the cost
columns of an ability row.

Usage: check_ability_tool.py WORK_FOLDER [CASE...]     status 0: each case (or each named case) gives its result
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
import abilities as tool  # noqa: E402

MARK = "the file of an earlier run\n"


class FakeSheet:
    """A data table with the interface of item_catalog.Sheet: each cell is a source text; a number cell is a number."""

    def __init__(self, name: str, rows: list[dict]) -> None:
        self.name = name
        columns: list[str] = []
        for row in rows:
            for column in row:
                if column not in columns:
                    columns.append(column)
        self.columns = {column: index for index, column in enumerate(columns)}
        self.row_count = len(rows)
        self._rows = rows

    def source(self, row: int, name: str) -> str:
        value = self._rows[row].get(name, "")
        if isinstance(value, bool):
            return "1" if value else "0"
        return str(value)

    def cell(self, row: int, name: str):
        value = self._rows[row].get(name, "")
        if isinstance(value, bool) or isinstance(value, str):
            return value
        return float(value)


def sheets(case: str) -> dict:
    cooldown = {"AbilityID": "Ability_WarHammer_Shockwave", "Time": 20, "MinTime": 0}
    status = {"StatusID": "WarHammer_Shockwave_Stun", "BaseDuration": 2, "MoveSpeedMod": 0, "Stunned": True, "NoNav": False,
              "StopOnHitCount": 1, "ShowInNameplates": True, "DMGStandard": 0, "DisplayName": "@Stun"}
    potion = {"StatusID": "PotionHeal", "BaseDuration": -1, "MoveSpeedMod": 0, "Stunned": False, "NoNav": False,
              "StopOnHitCount": 0, "ShowInNameplates": False, "DMGStandard": 0, "Health": 400, "TickRate": 1,
              "EffectCategories": "SurvivalPotion"}
    sanctuary = {"StatusID": "Sanctuary", "BaseDuration": -1, "MoveSpeedMod": 0, "Stunned": False, "NoNav": False,
                 "StopOnHitCount": 0, "ShowInNameplates": False, "DMGStandard": 0, "HealthModifierPercent": 0.005}
    spell = {"SpellID": "WarHammer_Spell_Shockwave", "Radius": 3, "Height": 0, "CastDistance": 0, "NumToSpawn": 0,
             "SpawnRate": 0, "Duration": 6000, "ManaCost": "SpecialAttack"}
    ability = {"AbilityID": "Passive_Test_ManaCost", "ManaCostList": "LifeStaff_TouchOfLife", "ManaCostMult": -0.25,
               "StaminaCostList": "", "StaminaCostFlatMod": 0, "SelfApplyStatusEffect": ""}
    caster = {"AbilityID": "Upgrade_Test_Fortify", "ManaCostList": "", "ManaCostMult": 0, "StaminaCostList": "",
              "StaminaCostFlatMod": 0, "SelfApplyStatusEffect": "PotionHeal", "OnHit": True, "DisplayName": "@Fortify",
              "DamageTableRow": "Test_Row"}
    category = {"StatusEffectCategoryID": "Haste", "ValueLimits": "MoveSpeedMod:0.4,SprintSpeedMod:0.4"}
    if case == "partial-cost-columns":
        del ability["StaminaCostList"]
        del caster["StaminaCostList"]
    if case == "missing-SelfApply":
        del ability["SelfApplyStatusEffect"]
        del caster["SelfApplyStatusEffect"]
    if case == "unknown-caster-status":
        caster["SelfApplyStatusEffect"] = "NoSuchStatus"
    if case == "bad-limit":
        category["ValueLimits"] = "MoveSpeedMod:fast"
    if case == "missing-Time":
        del cooldown["Time"]
    if case == "missing-Stunned":
        del status["Stunned"]
        del potion["Stunned"]
    if case == "missing-Radius":
        del spell["Radius"]
    if case == "missing-cost":
        ability["ManaCostList"] = "NoSuchCost"
    tables = {
        "CooldownData": [FakeSheet("Cooldowns", [cooldown])],
        "StatusEffectData": [FakeSheet("Status", [status, potion])],
        "SpellData": [FakeSheet("Spells", [spell])],
        "ManaData": [FakeSheet("ManaCosts", [{"CostID": "LifeStaff_TouchOfLife", "ManaCost": -5 if case == "negative-cost" else 25},
                                                 {"CostID": "Zero_Mana", "ManaCost": ""}])],
        "StaminaData": [FakeSheet("StaminaCosts", [{"CostID": "Attack_Warhammer_Special_1", "StaminaCost": 0}])],
        "AbilityData": [FakeSheet("Abilities", [ability, caster]),
                            FakeSheet("AIAbilities", [{"AbilityID": "AI_Bite"}])],
        "StatusEffectCategoryData": [FakeSheet("Categories", [category, {"StatusEffectCategoryID": "Buff", "ValueLimits": ""}])],
    }
    if case == "conflicting-status":
        tables["StatusEffectData"].append(FakeSheet("OtherStatus", [dict(status, Stunned=False)]))
    if case == "conflicting-spell":
        tables["SpellData"].append(FakeSheet("OtherSpells", [dict(spell, Radius=8)]))
    if case == "game-duplicate":
        tables["StatusEffectData"].append(FakeSheet("Common", [sanctuary]))
        tables["StatusEffectData"].append(FakeSheet("Catacombs", [dict(sanctuary, HealthModifierPercent=0.01)]))
    return tables


def valid_values(document: dict, case: str) -> list[str]:
    """@return The values of the valid document that are not correct."""
    wrong = []
    statuses = {row[0]: row for row in document["statusEffects"]}
    if document["format"] != 3:
        wrong.append(f"the format {document['format']}")
    potion = statuses.get("PotionHeal")
    if potion is None or potion[9] != {"Health": 400.0, "TickRate": 1.0, "EffectCategories": "SurvivalPotion"}:
        wrong.append(f"the other cells of a status row: {potion}")
    stun = statuses.get("WarHammer_Shockwave_Stun")
    if stun is None or not stun[4] or "DisplayName" in stun[9]:
        wrong.append(f"the stun row {stun}")
    if case == "game-duplicate":
        sanctuary = statuses.get("Sanctuary")
        if sanctuary is None or sanctuary[9].get("HealthModifierPercent") != 0.005 or document["statusRows"] != 4:
            wrong.append(f"the duplicate of the game: {sanctuary}, {document['statusRows']} rows")
    elif document["statusRows"] != 2:
        wrong.append(f"the number of the status rows {document['statusRows']}")
    spell = document["spells"][0] if document["spells"] else None
    if spell is None or spell[1] != 3.0 or spell[7].get("ManaCost") != "SpecialAttack":
        wrong.append(f"the spell {spell}")
    if document["manaCosts"] != [["LifeStaff_TouchOfLife", 25.0], ["Zero_Mana", 0.0]]:
        wrong.append(f"the mana costs {document['manaCosts']}")
    if document["abilityCosts"] != [["Passive_Test_ManaCost", ["LifeStaff_TouchOfLife"], -0.25, [], 0.0]]:
        wrong.append(f"the ability costs {document['abilityCosts']}")
    if document["abilityRows"] != [["Upgrade_Test_Fortify", {"SelfApplyStatusEffect": "PotionHeal", "OnHit": 1.0,
                                                             "DamageTableRow": "Test_Row"}]]:
        wrong.append(f"the caster effect rows {document['abilityRows']}")
    if document["statusCategories"] != [["Buff", {}], ["Haste", {"MoveSpeedMod": 0.4, "SprintSpeedMod": 0.4}]]:
        wrong.append(f"the status categories {document['statusCategories']}")
    return wrong


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    cases = ("valid", "missing-Time", "missing-Stunned", "missing-Radius", "conflicting-status", "conflicting-spell",
             "game-duplicate", "missing-cost", "partial-cost-columns", "negative-cost", "missing-SelfApply",
             "unknown-caster-status", "bad-limit")
    for case in sys.argv[2:] or cases:
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(tool, "Sources", lambda path: type("S", (), {"used": {}})()), \
                patch.object(tool, "read_sheets", lambda sources, case=case: sheets(case)), \
                patch.object(sys, "argv", ["abilities.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            try:
                status = tool.main()
            except Exception as error:  # noqa: BLE001 - a crash of the tool is a failure of the case
                status = f"exception {type(error).__name__}: {error}"
        text = target.read_text(encoding="utf-8")
        if case in ("valid", "game-duplicate"):
            try:
                wrong = valid_values(json.loads(text), case) if status == 0 and text != MARK else ["no document"]
            except (ValueError, KeyError, IndexError, TypeError) as error:
                wrong = [f"the document does not have the form of format 3 ({type(error).__name__})"]
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

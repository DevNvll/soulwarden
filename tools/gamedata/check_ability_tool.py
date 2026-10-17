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
  unknown-target-status  a rule names a target status (OtherApplyStatusEffect) that   status 1, the file stays
                         no table has (code review of stage 3.3b-S5, finding F10)
  bad-limit              a ValueLimits part with no number                        status 1, the file stays
  missing-StackMax       a status table has no column StackMax                    status 1, the file stays
  missing-caster-OnHit   an ability table of the players has no column OnHit      status 1, the file stays
  game-table-difference  a status table StatusEffects_AI with no ApplicationCooldown   status 0 (a difference of the game)
  missing-end-link       an OnEndStatusEffect names a status that no table has    status 1, the file stays
  game-end-link          the one OnEndStatusEffect link of the game with no row   status 0 (the named link of the game)
  missing-cast-spell     a CastSpell names a spell that no table has              status 1, the file stays
  action-or              an action list casts a spell below an Or of node tests   status 0: no selection of it, the cast is
                                                                                  in actionSpellsNotExported
  action-unknown-spell   an action list casts a spell that no table has            status 1, the file stays
  stack-link             a status with OnStackStatusEffect of a status of the data   status 0: the link is in the document
  missing-stack-link     an OnStackStatusEffect names a status that no table has  status 1, the file stays (code review of
                                                                                  stage B3, finding B3-R3)
  missing-OnStackStatusEffect, missing-AddOnStackSize, missing-AddOnStackSizeComparison
                         a status table has no such column                        status 1, the file stays (finding B3-R3)

Format 4 (stage 3.3b-S5): the valid document has each row of the ability tables of the players (also the row with no
caster effect), and the spell selections of a made-up action list of the player (a Not of a node test, a Not of an Or of
node tests: De Morgan, an Else branch) and the nodes that it tests.

The cases from missing-StackMax on are the corrections of the findings F13 and F8 of the code review of stage
3.3b-S1: each column of the rules of the status system and of a caster effect is in each table (with the named
differences of the game), and each link of a status row resolves (with the named link of the game).

The first six cases after valid are the corrections of the findings F11 and F12 of the code review of stage 3.3a
(a column that is not there is not 0 or false; two rows with one id must agree). The last three are the cost data of
stage 1.3b (criterion I14). The last three are the caster effect rows and the status categories of stage 3.3b-S1
(format 3). The values of the valid document: the format 6 (stage 3.3b-M2: the status actions of the player: a status that the action
applies at its enter and takes at its exit, with its condition of nodes and tags as terms; a tracker that stays is not one; stage 3.4f-B:
a hit-taken action with its tests, and a hit-taken action that also casts a spell is not exported; format 7, stage B3: a dodge status action
with its tests and the tag of its exit, a dodge action that also casts a spell is not exported, and a tag removal action); the status row
with its number among
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
    # Each column of the rules of the status system and of a caster effect (finding F13 of the review of stage 3.3b-S1).
    status["StackMax"] = 3
    for row in (status, potion, sanctuary):
        for column in tool.STATUS_RULE_COLUMNS:
            row.setdefault(column, "")
    for row in (ability, caster):
        for column in tool.CASTER_COLUMNS + getattr(tool, "CASTER_RULE_COLUMNS", ()):
            row.setdefault(column, "")
    # Code review of stage 3.3b-S2a, finding F4: a column of a status value and columns of a caster condition.
    for row in (status, potion, sanctuary):
        row.setdefault("ABSStandard", "")
    for row in (ability, caster):
        row.setdefault("RemoteDamageTableRow", "")
        row.setdefault("NumAroundMe", "")
    if case == "missing-ABSStandard":
        for row in (status, potion, sanctuary):
            row.pop("ABSStandard", None)
    if case in ("missing-RemoteDamageTableRow", "missing-NumAroundMe"):
        column = case.removeprefix("missing-")
        ability.pop(column, None)
        caster.pop(column, None)
    if case == "missing-StackMax":
        for row in (status, potion):
            del row["StackMax"]
    if case == "missing-caster-OnHit":
        del ability["OnHit"]
        del caster["OnHit"]
    if case == "missing-end-link":
        potion["OnEndStatusEffect"] = "NoSuchStatus"
    if case == "missing-cast-spell":
        potion["CastSpell"] = "NoSuchSpell"
    # Code review of stage B3, finding B3-R3: the stack chain of a status (the form of the haste of Archer's Speed and its timer).
    if case in ("stack-link", "missing-stack-link"):
        potion.update(OnStackStatusEffect="WarHammer_Shockwave_Stun" if case == "stack-link" else "NoSuchStatus", AddOnStackSize=1,
                      AddOnStackSizeComparison="Equal")
    if case in ("missing-OnStackStatusEffect", "missing-AddOnStackSize", "missing-AddOnStackSizeComparison"):
        for row in (status, potion, sanctuary):
            row.pop(case.removeprefix("missing-"), None)
    if case == "game-end-link":
        sanctuary["StatusID"] = "AI_IceDragon_EmpoweringLink_Stack_Apply"
        sanctuary["OnEndStatusEffect"] = "AI_IceDragon_EmpoweringLink_Stack"
    if case == "game-table-difference":
        sanctuary["StatusID"] = "Test_AI_Status"
        del sanctuary["ApplicationCooldown"]
    if case == "partial-cost-columns":
        del ability["StaminaCostList"]
        del caster["StaminaCostList"]
    if case == "missing-SelfApply":
        del ability["SelfApplyStatusEffect"]
        del caster["SelfApplyStatusEffect"]
    if case == "unknown-caster-status":
        caster["SelfApplyStatusEffect"] = "NoSuchStatus"
    if case == "unknown-target-status":
        caster["OtherApplyStatusEffect"] = "NoSuchStatus"
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
        "StatMultiplierData": [FakeSheet("StatMultipliers", [{"ID": "MoveSpeedMod", "Min": 0, "Max": 1.5},
                                                             {"ID": "SprintSpeedMod", "Min": 0, "Max": 1.5}])],
    }
    if case == "game-table-difference":
        tables["StatusEffectData"].append(FakeSheet("StatusEffects_AI", [sanctuary]))
    if case == "game-end-link":
        tables["StatusEffectData"].append(FakeSheet("Common", [sanctuary]))
    if case == "conflicting-status":
        tables["StatusEffectData"].append(FakeSheet("OtherStatus", [dict(status, Stunned=False)]))
    if case == "conflicting-spell":
        tables["SpellData"].append(FakeSheet("OtherSpells", [dict(spell, Radius=8)]))
    if case == "game-duplicate":
        tables["StatusEffectData"].append(FakeSheet("Common", [sanctuary]))
        tables["StatusEffectData"].append(FakeSheet("Catacombs", [dict(sanctuary, HealthModifierPercent=0.01)]))
    return tables


ACTION_PATH = "sharedassets/springboardentitites/playerdata/cage/playeractions_ability_test.actionlist"


def action_list(case: str) -> bytes:
    """@return A made-up action list of the player: two selections of the spell of the test by node tests."""
    first = ("<Or><IfAbilityActive AbilityName='Upgrade_Test_Small'/><IfAbilityActive AbilityName='Upgrade_Test_Tiny'/></Or>"
             if case == "action-or" else
             "<Not><Or><IfAbilityActive AbilityName='Upgrade_Test_Small'/><IfAbilityActive AbilityName='Upgrade_Test_Tiny'/></Or></Not>")
    spell = "NoSuchSpell" if case == "action-unknown-spell" else "WarHammer_Spell_Shockwave"
    return (f"<ActionList><Actions><Action Name='Ability_Test_Slam'><Activities>"
            f"<IfThenElse><IfCondition><And><IfMannequinMarker MarkerName='Slam'/>{first}</And></IfCondition>"
            f"<ThenActivity><CastSpell><Default SpellName='{spell}'/></CastSpell></ThenActivity><ElseActivity/></IfThenElse>"
            f"<IfThenElse><IfCondition><IfAbilityActive AbilityName='Upgrade_Test_Big'/></IfCondition><ThenActivity/>"
            f"<ElseActivity><CastSpell><Default SpellName='WarHammer_Spell_Shockwave'/></CastSpell></ElseActivity></IfThenElse>"
            f"</Activities></Action>"
            # Format 5 (stage 3.3b-M2): a status action (its status while a node, a movement tag, and the weapon tag hold), and a
            # tracker that the action does not take back at its exit (not a status of the action).
            f"<Action Name='Test_Sprint'><ActivationCondition><And><IfAbilityActive AbilityName='Passive_Test_Sprint'/>"
            f"<Or><IfMannequinTag TagName='ForwardNav'/><IfMannequinTag TagName='BackNav'/></Or><Not><IfMannequinTag TagName='Dodging'/></Not>"
            f"<IfMannequinTag TagName='Warhammer'/></And></ActivationCondition><Activities><ModifyStatusEffects><Default>"
            f"<EffectsToApplyOnEnter><EffectId EffectId='WarHammer_Shockwave_Stun'/><EffectId EffectId='PotionHeal'/></EffectsToApplyOnEnter>"
            f"<EffectsToRemoveOnExit><EffectId EffectId='WarHammer_Shockwave_Stun'/></EffectsToRemoveOnExit></Default></ModifyStatusEffects>"
            f"</Activities></Action>"
            # Format 6 (stage 3.4f-B): a hit-taken action (the form of GreatAxe_Passive_StamWhenLowHP_Detect), and one that also casts a
            # spell (the form of the HealingAura action of the life staff: not exported, with its cause).
            f"<Action Name='Test_Hit_Stamina'><ActivationCondition><And><IfAbilityActive AbilityName='Passive_Test_Sprint'/>"
            f"<IfMannequinTag TagName='Warhammer'/><IfHealthPercentage Comparison='LessThan' Percentage='0.5'/>"
            f"<IfStaminaValue ComparisonType='LessThan' ComparisonValue='1.0' ComparisonValueIsPercentage='true'/>"
            f"<Not><IfHasStatusEffect StatusEffect='PotionHeal'/></Not>"
            f"<IfDamagedByPowerLevel Comparison='GreaterThanOrEqual' Value='0' FailOnNoReactionDamage='true'/></And></ActivationCondition>"
            f"<Activities><ModifyStatusEffects><Default><EffectsToApplyOnEnter><EffectId EffectId='WarHammer_Shockwave_Stun'/>"
            f"<EffectId EffectId='PotionHeal'/></EffectsToApplyOnEnter></Default></ModifyStatusEffects></Activities></Action>"
            f"<Action Name='Test_Hit_Aura'><ActivationCondition><And><IfAbilityActive AbilityName='Passive_Test_Sprint'/>"
            f"<IfDamagedByPowerLevel Comparison='GreaterThan' Value='0'/></And></ActivationCondition>"
            f"<Activities><ModifyStatusEffects><Default><EffectsToApplyOnEnter><EffectId EffectId='PotionHeal'/></EffectsToApplyOnEnter>"
            f"</Default></ModifyStatusEffects><CastSpell><Default SpellName='WarHammer_Spell_Shockwave'/></CastSpell></Activities></Action>"
            # Format 7 (stage B3): a dodge status action (the form of Passive_Bow_Skirmish_HasteOnDodge: the status at the exit of the
            # action, in the Then branch of a tag test), a dodge action that also casts a spell (not exported, with its cause), and a tag
            # removal action (the form of Bow_Remove_StatusEffects_Sheathe).
            f"<Action Name='Test_Dodge_Haste'><ActivationCondition><And><IfAbilityActive AbilityName='Passive_Test_Sprint'/>"
            f"<IfMannequinTag TagName='Warhammer'/><IfActionStatus ActionName='Dodge' Status='Installed'/>"
            f"<IfStaminaValue ComparisonValue='0.99' ComparisonValueIsPercentage='true' ComparisonType='GreaterThanOrEqual'/>"
            f"<Not><IfHasStatusEffect StatusEffect='PotionHeal' OnlyCheckOwnedStatusEffects='false'/></Not></And></ActivationCondition>"
            f"<DeactivationCondition><Not><IfActionStatus ActionName='Dodge' Status='Installed'/></Not></DeactivationCondition>"
            f"<Activities><IfThenElse ResetWhenReinstalled='true'><IfCondition><IfMannequinTag TagName='Warhammer'/></IfCondition>"
            f"<ThenActivity><ModifyStatusEffects><Default><EffectsToApplyOnExit><EffectId EffectId='PotionHeal'/></EffectsToApplyOnExit>"
            f"</Default></ModifyStatusEffects></ThenActivity><ElseActivity/></IfThenElse></Activities></Action>"
            f"<Action Name='Test_Dodge_Spell'><ActivationCondition><And><IfAbilityActive AbilityName='Passive_Test_Sprint'/>"
            f"<IfActionStatus ActionName='Dodge' Status='Installed'/></And></ActivationCondition>"
            f"<Activities><ModifyStatusEffects><Default><EffectsToApplyOnExit><EffectId EffectId='PotionHeal'/></EffectsToApplyOnExit>"
            f"</Default></ModifyStatusEffects><CastSpell><Default SpellName='WarHammer_Spell_Shockwave'/></CastSpell></Activities></Action>"
            f"<Action Name='Test_Remove_Sheathe'><ActivationCondition><And><IfMannequinTag TagName='Warhammer'/></And></ActivationCondition>"
            f"<DeactivationCondition/><Activities><ModifyStatusEffects><Default RemoveNothingOnMigrate='true'><EffectsToRemoveOnExit>"
            f"<EffectId EffectId='PotionHeal'/></EffectsToRemoveOnExit></Default></ModifyStatusEffects></Activities></Action>"
            f"</Actions></ActionList>").encode()


class FakeSources:
    """The source provider of the check: no pak file; one made-up action list of the player."""

    def __init__(self, case: str) -> None:
        self.used: dict = {}
        self.case = case

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return [ACTION_PATH] if suffix == tool.ACTION_SUFFIX else []

    def has(self, path: str) -> bool:
        return path == ACTION_PATH

    def read(self, path: str) -> bytes | None:
        return action_list(self.case) if path == ACTION_PATH else None

    def check_complete(self) -> None:
        pass


def valid_values(document: dict, case: str) -> list[str]:
    """@return The values of the valid document that are not correct."""
    wrong = []
    statuses = {row[0]: row for row in document["statusEffects"]}
    if document["format"] != 7:
        wrong.append(f"the format {document['format']}")
    potion = statuses.get("PotionHeal")
    if potion is None or potion[9] != {"Health": 400.0, "TickRate": 1.0, "EffectCategories": "SurvivalPotion"}:
        wrong.append(f"the other cells of a status row: {potion}")
    stun = statuses.get("WarHammer_Shockwave_Stun")
    if stun is None or not stun[4] or "DisplayName" in stun[9] or stun[9].get("StackMax") != 3.0:
        wrong.append(f"the stun row {stun}")
    if case == "game-end-link":
        linked = statuses.get("AI_IceDragon_EmpoweringLink_Stack_Apply")
        if linked is None or linked[9].get("OnEndStatusEffect") != "AI_IceDragon_EmpoweringLink_Stack" or document["statusRows"] != 3:
            wrong.append(f"the named link of the game: {linked}")
    elif case == "game-table-difference":
        if "Test_AI_Status" not in statuses or document["statusRows"] != 3:
            wrong.append(f"the status of the table with a difference of the game: {document['statusRows']} rows")
    elif case == "game-duplicate":
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
    rows = {row[0]: row[1] for row in document["abilityRows"]}
    if (rows.get("Upgrade_Test_Fortify") != {"SelfApplyStatusEffect": "PotionHeal", "OnHit": 1.0, "DamageTableRow": "Test_Row"}
            or rows.get("Passive_Test_ManaCost", {}).get("ManaCostMult") != -0.25 or "AI_Bite" in rows):
        wrong.append(f"the ability rows {document['abilityRows']}")
    selections = [["playeractions_ability_test", "Ability_Test_Slam", "WarHammer_Spell_Shockwave", [],
                   ["Upgrade_Test_Small", "Upgrade_Test_Tiny"]],
                  ["playeractions_ability_test", "Ability_Test_Slam", "WarHammer_Spell_Shockwave", [], ["Upgrade_Test_Big"]]]
    if document.get("actionSpells") != selections:
        wrong.append(f"the action spells {document.get('actionSpells')}")
    if document.get("actionNodes") != [["playeractions_ability_test", ["Passive_Test_Sprint", "Upgrade_Test_Big", "Upgrade_Test_Small",
                                                                       "Upgrade_Test_Tiny"]]]:
        wrong.append(f"the action nodes {document.get('actionNodes')}")
    sprint = [["node", "Passive_Test_Sprint", False], ["tag", "Dodging", True], ["tag", "Warhammer", False]]
    if document.get("actionStatuses") != [["playeractions_ability_test", "Test_Sprint",
                                           [[sprint[0], ["tag", "ForwardNav", False], sprint[1], sprint[2]],
                                            [sprint[0], ["tag", "BackNav", False], sprint[1], sprint[2]]], ["WarHammer_Shockwave_Stun"]]]:
        wrong.append(f"the status actions (format 5) {document.get('actionStatuses')}")
    hit = {"nodes": ["Passive_Test_Sprint"], "tags": ["Warhammer"], "health": ["LessThan", 0.5], "stamina": ["LessThan", 1.0],
           "without": ["PotionHeal"], "power": ["GreaterThanOrEqual", 0.0, True]}
    if document.get("actionHitStatuses") != [["playeractions_ability_test", "Test_Hit_Stamina", hit, ["PotionHeal", "WarHammer_Shockwave_Stun"]]]:
        wrong.append(f"the hit-taken actions (format 6) {document.get('actionHitStatuses')}")
    if document.get("actionHitStatusesNotExported") != [["playeractions_ability_test", "Test_Hit_Aura",
                                                         "an activity other than the statuses (CastSpell)"]]:
        wrong.append(f"the hit-taken actions that are not exported {document.get('actionHitStatusesNotExported')}")
    dodge = {"dodge": "Dodge", "nodes": ["Passive_Test_Sprint"], "tags": ["Warhammer"], "stamina": ["GreaterThanOrEqual", 0.99],
             "without": ["PotionHeal"], "exitTags": ["Warhammer"]}
    if document.get("actionDodgeStatuses") != [["playeractions_ability_test", "Test_Dodge_Haste", dodge, ["PotionHeal"]]]:
        wrong.append(f"the dodge status actions (format 7) {document.get('actionDodgeStatuses')}")
    if document.get("actionDodgeStatusesNotExported") != [["playeractions_ability_test", "Test_Dodge_Spell",
                                                           "an activity other than the statuses (CastSpell)"]]:
        wrong.append(f"the dodge actions that are not exported {document.get('actionDodgeStatusesNotExported')}")
    if document.get("actionTagRemovals") != [["playeractions_ability_test", "Test_Remove_Sheathe", ["Warhammer"], ["PotionHeal"]]]:
        wrong.append(f"the tag removal actions (format 7) {document.get('actionTagRemovals')}")
    if document["statusCategories"] != [["Buff", {}], ["Haste", {"MoveSpeedMod": 0.4, "SprintSpeedMod": 0.4}]]:
        wrong.append(f"the status categories {document['statusCategories']}")
    if document.get("statMultipliers") != [["MoveSpeedMod", 0.0, 1.5], ["SprintSpeedMod", 0.0, 1.5]]:
        wrong.append(f"the stat multipliers {document.get('statMultipliers')}")
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
             "unknown-caster-status", "unknown-target-status", "bad-limit", "missing-StackMax", "missing-caster-OnHit", "game-table-difference",
             "missing-end-link", "game-end-link", "missing-cast-spell", "missing-ABSStandard", "missing-RemoteDamageTableRow",
             "missing-NumAroundMe", "action-or", "action-unknown-spell", "stack-link", "missing-stack-link", "missing-OnStackStatusEffect",
             "missing-AddOnStackSize", "missing-AddOnStackSizeComparison")
    for case in sys.argv[2:] or cases:
        target = work / f"{case}.json"
        target.write_text(MARK, encoding="utf-8")
        errors = io.StringIO()
        with patch.object(tool, "Sources", lambda path, case=case: FakeSources(case)), \
                patch.object(tool, "read_sheets", lambda sources, case=case: sheets(case)), \
                patch.object(sys, "argv", ["abilities.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(errors):
            try:
                status = tool.main()
            except Exception as error:  # noqa: BLE001 - a crash of the tool is a failure of the case
                status = f"exception {type(error).__name__}: {error}"
        text = target.read_text(encoding="utf-8")
        if case == "stack-link":
            document = json.loads(text) if status == 0 and text != MARK else {}
            potion = {row[0]: row for row in document.get("statusEffects", [])}.get("PotionHeal")
            passed = potion is not None and potion[9].get("OnStackStatusEffect") == "WarHammer_Shockwave_Stun"
            print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, the potion row {potion}")
            failures += 0 if passed else 1
            continue
        if case == "action-or":
            document = json.loads(text) if status == 0 and text != MARK else {}
            passed = (document.get("actionSpells") == [["playeractions_ability_test", "Ability_Test_Slam", "WarHammer_Spell_Shockwave",
                                                        [], ["Upgrade_Test_Big"]]]
                      and document.get("actionSpellsNotExported") == [["playeractions_ability_test", "Ability_Test_Slam",
                                                                       "WarHammer_Spell_Shockwave",
                                                                       "a condition that is not an AND of node tests"]])
            print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, selections {document.get('actionSpells')}, "
                  f"not exported {document.get('actionSpellsNotExported')}")
            failures += 0 if passed else 1
            continue
        if case in ("valid", "game-duplicate", "game-table-difference", "game-end-link"):
            try:
                wrong = valid_values(json.loads(text), case) if status == 0 and text != MARK else ["no document"]
            except (ValueError, KeyError, IndexError, TypeError) as error:
                wrong = [f"the document does not have the form of format 4 ({type(error).__name__})"]
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

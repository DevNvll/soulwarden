#!/usr/bin/env python3
"""Write the ability data of the game as one JSON file (stage 3.3a).

The backend reads this file and gets what the abilities of a player need (protocol notes 4.22):
the cooldown of each ability, the values of each status effect with its number on the wire, and
the shape of each spell. The damage rows of the abilities are in the combat tables
(tools/gamedata/combat.sh). Without the file the backend starts and works, and it refuses each
ability press.

Input: the game copy, from the same sources as the creature data tool (creature_data.py: the
pak entries, the pak entries with an Oodle library, or the files that tools/research/pakx.py
extracted).

  Source (table type)              What the file gets
  CooldownData                     for each ability: the time and the minimum time of its
                                   cooldown (columns AbilityID, Time, MinTime)
  StatusEffectData (each table)    for each status effect: its number on the wire, BaseDuration,
                                   MoveSpeedMod, Stunned, NoNav, StopOnHitCount, ShowInNameplates,
                                   DMGStandard
  SpellData (each table)           for each spell: Radius, Height, CastDistance, NumToSpawn,
                                   SpawnRate, Duration
  StatusEffectData, SpellData      (stage 1.3b) the other cells of each row that are not empty: the
                                   heal, the tick, the attributes, the armor, the absorption, the
                                   damage of an effect; the damage row, the mana cost id of a spell
  ManaData, StaminaData            (stage 1.3b, I14) the mana cost and the stamina cost of each cost id
  AbilityData (each table)         (stage 1.3b, I14) the cost columns of each ability row that has one:
                                   ManaCostList, ManaCostMult, StaminaCostList, StaminaCostFlatMod
  AbilityData (tables of players)  (stage 3.3b-S1) each row with a SelfApplyStatusEffect (a status on the
                                   caster: Safety Measures, Greed, Hammer Time): its cells that are not empty;
                                   (stage 3.3b-S5, format 4) each row with an id: the rules of the nodes of the
                                   weapon trees and of the perks for the evaluator of the backend
  the action lists of the player   (stage 3.3b-S5) playeractions_*.actionlist of the folder of the player actions:
                                   each CastSpell activity whose IfThenElse conditions test learned nodes
                                   (IfAbilityActive, also below Not), with the nodes that the spell needs and the
                                   nodes that it must not have ("actionSpells"), and the nodes that each action list
                                   tests ("actionNodes")
  StatusEffectCategoryData         (stage 3.3b-S1) the ValueLimits of each status category: the largest sum
                                   (a positive limit) or the smallest sum (a negative limit) of a value over
                                   the effects of the category ("Haste": MoveSpeedMod 0.4)
  StatMultiplierData               (stage 3.3b-S2a) Min and Max of each stat multiplier (the speed table of a
                                   character: MoveSpeedMod and SprintSpeedMod, 0 to 1.5)

The number of a status effect on the wire is the position of the CRC-32 of its lower-case id in
the list of the CRC values of all rows of all status tables, in the unsigned order, with each
duplicate (the list of the client: live check L5 of stage 3.3a, 4,517 rows of the supported
build). A duplicate id has the first position.

Output: {"format": 7, "statusRows": N,
"counts": {"cooldowns": N, "statusEffects": N, "spells": N, "manaCosts": N, "staminaCosts": N, "abilityCosts": N,
"abilityRows": N, "statusCategories": N, "statMultipliers": N},
"cooldowns": [[id, time, minTime], ...],
"statusEffects": [[id, index, duration, moveSpeed, stunned, root, stopOnHit, nameplate, damage, {cells}], ...],
"spells": [[id, radius, height, castDistance, count, spawnRate, duration, {cells}], ...],
"manaCosts": [[id, cost], ...], "staminaCosts": [[id, cost], ...],
"abilityCosts": [[id, [mana cost id, ...], manaCostMult, [stamina cost id, ...], staminaCostFlatMod], ...],
"abilityRows": [[id, {cells}], ...], "statusCategories": [[id, {value name: limit, ...}], ...],
"statMultipliers": [[id, min, max], ...],
"actionSpells": [[action list, action, spell, [node, ...], [node, ...]], ...],
"actionNodes": [[action list, [node, ...]], ...],
"actionSpellsNotExported": [[action list, action, spell, cause], ...],
"actionStatuses": [[action list, action, [[[kind, name, negated], ...], ...], [status, ...]], ...],
"actionHitStatuses": [[action list, action, {"nodes": [...], "tags": [...], "health": [comparison, part] or null,
  "stamina": [comparison, part] or null, "without": [status, ...], "power": [comparison, value, needs reaction]}, [status, ...]], ...],
"actionHitStatusesNotExported": [[action list, action, cause], ...],
"actionDodgeStatuses": [[action list, action, {"dodge": action, "nodes": [...], "tags": [...], "stamina": [comparison, part] or null,
  "without": [status, ...], "exitTags": [...]}, [status, ...]], ...],
"actionDodgeStatusesNotExported": [[action list, action, cause], ...],
"actionTagRemovals": [[action list, action, [tag, ...], [status, ...]], ...]}.
Format 7 (stage B3): `actionDodgeStatuses` has each action of the player lists that gives statuses at its exit while a dodge plays (an
activation condition with IfActionStatus of the dodge action, Status Installed; ModifyStatusEffects EffectsToApplyOnExit, also in the Then
branch of an IfThenElse whose condition is an IfMannequinTag and whose Else branch is empty: the tags at the exit). Its tests: an AND of
IfActionStatus, IfAbilityActive, IfMannequinTag, IfStaminaValue (a part of the maximum), and Not of IfHasStatusEffect. The node Dodge and
Weave of the bow (Passive_Bow_Skirmish_HasteOnDodge) is such an action. An action with a different test or activity is in
`actionDodgeStatusesNotExported` with its cause. `actionTagRemovals` has each action whose activation condition is only IfMannequinTag tests
and whose only activity removes statuses at its exit (EffectsToRemoveOnExit): the statuses end when the tag goes (the weapon leaves the
hand: Bow_Remove_StatusEffects_Sheathe ends the haste of Dodge and Weave).
Format 6 (stage 3.4f-B): `actionHitStatuses` has each action of the player lists that applies statuses at a hit that the character
takes (an activation condition with IfDamagedByPowerLevel, ModifyStatusEffects EffectsToApplyOnEnter) and that tests a learned node: an
AND of IfDamagedByPowerLevel, IfHealthPercentage, IfStaminaValue (a part of the maximum), IfAbilityActive, IfMannequinTag, and Not of
IfHasStatusEffect. The node Mauler's Stamina of the great axe (GreatAxe_Passive_StamWhenLowHP_Detect) is such an action. An action with
a different test is in `actionHitStatusesNotExported` with its cause.
Format 5 (stage 3.3b-M2): `actionStatuses` has each action of the player lists that applies statuses while it is active
(ModifyStatusEffects: EffectsToApplyOnEnter and the same status in EffectsToRemoveOnExit) and whose activation condition tests a
learned node: the condition as an OR of ANDs of
literals (kind "node": IfAbilityActive; kind "tag": IfMannequinTag), and the statuses. An action whose condition has a test of a
different kind is not exported. The backend gives the statuses while the condition holds (Guarded Sprint: the war hammer in the hand
and a movement).
`cells` has the other cells of the row that are not empty and not 0 (a number, or a text).

The tool writes no file (status 1) when a table type is not in the game copy, when a table does
not have a column that the tool reads (code review of stage 3.3a, finding F11: an absent column
is not 0 or false; the two creature ability tables have no cost column as a whole), when a row
has an empty id, when a number is not finite, when a cooldown or a cost is negative, when two
rows of one list have the same id with different values (finding F12; the duplicates of the game
in DUPLICATES_IN_GAME keep their first row), and when a cost id names no row of a cost table (but
the ids of COSTS_NOT_IN_GAME, which the game itself does not resolve: the client gives them no
cost).

Usage: abilities.py [OUTPUT.json]
       abilities.py --find TEXT...   print the rows with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/ability-data.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

import bisect
import json
import math
import os
import re
import struct
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources, read_xml  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "ability-data.json"
COOLDOWN_TYPE = "CooldownData"
STATUS_TYPE = "StatusEffectData"
SPELL_TYPE = "SpellData"
MANA_TYPE = "ManaData"
STAMINA_TYPE = "StaminaData"
ABILITY_TYPE = "AbilityData"
CATEGORY_TYPE = "StatusEffectCategoryData"
STATMULT_TYPE = "StatMultiplierData"
USED_TYPES = (COOLDOWN_TYPE, STATUS_TYPE, SPELL_TYPE, MANA_TYPE, STAMINA_TYPE, ABILITY_TYPE, CATEGORY_TYPE, STATMULT_TYPE)
# The limits of a stat multiplier (stage 3.3b-S2a, I10): the client bounds the value of the table of slot 43 to Min and Max
# (getter 0x3FEA370; MoveSpeedMod and SprintSpeedMod: 0 to 1.5 in the supported build).
STATMULT_COLUMNS = ("ID", "Min", "Max")
# The columns that the tool reads: each one must be in each source table (finding F11 of the review of stage 3.3a).
COOLDOWN_COLUMNS = ("AbilityID", "Time", "MinTime")
STATUS_COLUMNS = ("StatusID", "BaseDuration", "MoveSpeedMod", "Stunned", "NoNav", "StopOnHitCount", "ShowInNameplates",
                  "DMGStandard")
# The columns of the rules of the status system of the backend (world/statuses.cpp, gear.cpp; code review of stage
# 3.3b-S1, finding F13): each status table must have them, but the differences of the game below. An absent column
# must not become a rule of 0 or a default stack limit.
STATUS_RULE_COLUMNS = ("StackMax", "InitialStackSize", "RefreshDuration", "DurationMax", "DontReplaceStack",
                       "ApplicationCooldown", "TickRate", "DelayInitialTick", "Health", "HealthModifierPercent", "Mana",
                       "RemoveOnDeath", "AllowAddIfDead", "OnEndStatusEffect", "DontApplyOnEndEffectOnRemove", "CastSpell",
                       # Stage B3 (code review of stage B3, finding B3-R3): the stack chain (the timer of Archer's Speed). Each status
                       # table of the supported build has the three columns.
                       "OnStackStatusEffect", "AddOnStackSize", "AddOnStackSizeComparison",
                       "ShowInUITray", "AlwaysReplicate", "ForceReplicateToRemotes", "EffectCategories", "MaxHealthMod",
                       "PhysicalArmor", "ElementalArmor", "SprintSpeedMod", "MODStrength", "MODDexterity", "MODIntelligence",
                       "MODFocus", "MODConstitution", "ABSSlash", "ABSThrust", "ABSStrike", "ABSFire", "ABSIce", "ABSNature",
                       "ABSArcane", "ABSCorruption", "ABSLightning", "DMGSlash", "DMGThrust", "DMGStrike", "DMGFire", "DMGIce",
                       "DMGNature", "DMGArcane", "DMGCorruption", "DMGLightning",
                       # The values of the damage types that the status values read too (world/statuses.cpp: ABS and DMG of
                       # each damage type of the combat tables; code review of stage 3.3b-S2a, finding F4).
                       "ABSStandard", "ABSSiege", "ABSAcid", "DMGSiege", "DMGAcid")
# The status tables of the supported build that do not have some of these columns (the creature tables of the game).
STATUS_TABLE_DIFFERENCES = {
    "StatusEffects_AI": frozenset({"ApplicationCooldown", "AllowAddIfDead", "ForceReplicateToRemotes", "ABSAcid"}),
    "StatusEffects_AI_IsleOfNight": frozenset({"AllowAddIfDead", "ForceReplicateToRemotes", "ABSAcid"}),
    "StatusEffects_Common_IsleOfNight": frozenset({"ForceReplicateToRemotes"}),
}
# The columns of a caster effect row that the backend reads (the event and the conditions of world/gear.cpp; finding F13):
# each ability table of the players (a table with the cost columns) has them in the supported build.
CASTER_COLUMNS = ("AbilityID", "SelfApplyStatusEffect", "SelfApplyStatusEffectDurations", "OnHit", "OnHitTaken", "InSequence",
                  "AttackType", "DamageTableRow", "ActivationCooldown", "TargetHealthPercent", "TargetComparisonType",
                  "MyHealthPercent", "MyComparisonType", "MyManaPercent", "MyManaComparisonType", "MyStaminaPercent",
                  "MyStaminaComparisonType", "TargetVitalsCategory", "AttackerVitalsCategory", "TargetStatusEffectCategory",
                  "TargetStatusEffectComparison", "DontHaveStatusEffect", "HasGritActive", "TargetHasGritActive")
# The other columns of the ability tables of the players in the supported build (each of the 27 tables has them; code
# review of stage 3.3b-S2a, finding F4). The backend copies each cell of a caster row, and a value in a column whose rule it
# does not evaluate disables the row (world/gear.cpp, Gear::conditions_pass): an absent column would give a row with a
# condition (NumAroundMe, RemoteDamageTableRow) no condition. Each one must be in the source. The columns that only some
# tables of the game have (AbilityTableTagsToTriggerOn, DamageTableTagsToTriggerOn, OnNotLegShot,
# PostHitSlayerScriptCondition, Tags) are not required.
CASTER_RULE_COLUMNS = ("AbilitiesOnCooldown", "AbilityCooldownComparisonType", "AbilityIdToCheckForTrackedHits",
                       "AbilityList", "AbilityOnCooldownOptions", "AbilityTrigger", "AdjustPointsAccumulatorSelf",
                       "AfterAction", "AfterSequence", "AllowSelfDamageForHitEvents", "ArmorPenetration",
                       "AttachedTargetSpellIds", "Azoth", "BaseDamage", "BaseDamageReduction", "BlockDamage",
                       "BlockDamageReduction", "CDRImmediatelyOptions", "CanBeUnapplied", "CanBlockRangedOverride",
                       "CastSpell", "CheckOwnedStatusEffects", "CheckStatusEffectsOnTargetOwned",
                       "ConsumableCooldownForReset", "ConsumableHealMod", "ConsumeTargetStatusEffectMult",
                       "CooldownId", "CooldownTimer", "CritArmorPenetration", "CritChance", "CritDamage",
                       "CritDamageReduction", "DamageCategory", "DamageIsMelee", "DamageIsRanged",
                       "DamageTableRowOverride", "DamageTableStatusEffectOverride", "DamageTypes",
                       "DisableApplyPerStatusEffectStack", "DisableCastSpellDurability", "DisableConsecutivePotency",
                       "DisableScalePerTargetSEStack", "DismountReasons", "DistComparisonType", "DistFromDefender",
                       "DmgPctToHealth", "DoNotUnequipSelfAppliedSE", "Duration", "ElementalArmor", "EnableCrit",
                       "EnableHoldConditionIfTrackedSpellExistsOfType", "EnableTaunt", "EquipLoadCategory",
                       "EquipWhenUnsheathed", "ExcludeFromGameModes", "FastTravelAzothCostMod",
                       "FastTravelInnCooldownMod", "ForceShowNameTag", "ForceStatusEffectDamageTableRow",
                       "ForceTakeStaminaDamage", "GatheringTradeskill", "GiveAzothPercentChance",
                       "HeadshotArmorPenetration", "HeadshotDamage", "HealScalingValueMultiplier", "Health",
                       "HealthDamageMitigation", "HealthDamageMitigation_Endvalue", "HealthPenaltyPerStackRemoved",
                       "HitFromBehindArmorPenetration", "HitFromBehindDamage", "HitRateDmg", "HitRateDmgReduction",
                       "HoldConditionButtonIcon", "IgnoreDisabledAttackTypes", "IgnoreResetConsecutiveOnDeath",
                       "InAction", "InActionTime", "InSequenceTime", "IsConsumableIds", "IsGlobalAbility",
                       "IsInCombatState", "IsNonCombatAbility", "IsNotConsumableIds", "IsNotInCombatState",
                       "IsStackableAbility", "IsStackableDiminishingModifier", "IsStackableMax", "Knockback",
                       "KnockbackReduction", "LimitOnHealedToFirstCast", "LinearlyScaleToDistance",
                       "LoadedAmmoCount", "LoadedAmmoCountComparisonType", "Mana", "ManaRate", "MaxConsecutiveHits",
                       "MaxHealth", "MaxHitCountMultiplier", "MaxMana", "MaxNumAroundMe", "MaxTrackedHitCounts",
                       "ModifySelfApplyStatusEffectDuration", "MultiplyRemovedByPotency",
                       "MyAfflictionComparisonType", "MyAfflictionPercent", "MyAfflictionType", "MyMarker",
                       "MySequenceMarker", "NumAroundComparisonType", "NumAroundMe", "NumConsecutiveHits",
                       "NumFreeCooldownsPerUse", "NumStatusEffectStacksToRemove", "NumStatusEffectsToRemove",
                       "NumStatusEffectsToTransfer", "NumSuccessfulHits", "NumTargetStatusEffectsToRemove",
                       "NumberOfHitsComparisonType", "NumberOfTrackedHits", "OnAfflictionsUpdated",
                       "OnAttachedSpellTargetDied", "OnBlockBreak", "OnBlockedHit", "OnBlockedHitTaken",
                       "OnContributedKill", "OnCrit", "OnCritTaken", "OnDeath", "OnDeathsDoor", "OnDismount",
                       "OnEquipStatusEffect", "OnEventConditionalActivationChance", "OnEventPassiveConditionsPass",
                       "OnExecuted", "OnFatalDamageTaken", "OnGatheringComplete", "OnHasDied", "OnHeadShot",
                       "OnHealed", "OnHealthChanged", "OnHitBehind", "OnHitTakenWhileInvulnerable",
                       "OnInActionLongEnough", "OnKill", "OnLegShot", "OnManaChanged", "OnProjPassedThrough",
                       "OnSelfDamage", "OnStatusEffectApplied", "OnStatusEffectRemoved", "OnTargetBlockBreak",
                       "OnTargetSet", "OnTargetStatusEffectRemoved", "OnUsedConsumable", "OnWeaponSwap",
                       "OnlyChangeOwnedStatusEffects", "OtherApplyStatusEffect", "OtherApplyStatusEffectDurations",
                       "PerStatusEffectOnSelf", "PerStatusEffectOnSelfMax", "PerStatusEffectOnTarget",
                       "PerStatusEffectOnTargetMax", "PerStatusEffectStackOnSelf", "PerStatusEffectStackOnSelfMax",
                       "PhysicalArmor", "PhysicalArmorMaxHealthMod", "PowerLevelOverride", "RangedAttackName",
                       "RangedAttackNameOverride", "RefundAmmoPercentChance", "RefundConsumablePercentChance",
                       "RemoteDamageTableRow", "RemoveStatusEffects", "RemoveTargetStatusEffectCats",
                       "RemoveTargetStatusEffectsList", "RemoveTargetStatusEffectsNumStacks", "RepairDustYieldMod",
                       "RequireReaction", "RequiredAbilityId", "RequiredEquipment", "RequiredEquippedAbilityId",
                       "ResetConsecutiveOnSuccess", "ResetConsumableCooldowns", "ResetCooldownTimers",
                       "ResetTrackedOnSuccess", "ScalingConstitution", "ScalingDexterity", "ScalingIntelligence",
                       "ScalingSpirit", "ScalingStrength", "SetHealthOnFatalDamageTaken", "SetMannequinTag",
                       "SetMannequinTagStatus", "SetPotencyAsCount", "SlayerScriptCondition", "Sound",
                       "StaggerDamage", "StaggerDamageReduction", "Stamina", "StatusEffect",
                       "StatusEffectBeingApplied", "StatusEffectBeingRemoved", "StatusEffectCategories",
                       "StatusEffectCategoriesList", "StatusEffectCategoryBeingRemoved",
                       "StatusEffectCategoryToTransfer", "StatusEffectComparison",
                       "StatusEffectDamageTableIdForRowOverride", "StatusEffectDamageTableRowOverride",
                       "StatusEffectDurationCats", "StatusEffectDurationMod", "StatusEffectDurationMult",
                       "StatusEffectDurationReduction", "StatusEffectIcon", "StatusEffectStackSize",
                       "StatusEffectsList", "SupportsSmartcast", "TargetCollisionFilters",
                       "TargetCooldownsExtensionModifier", "TargetCooldownsExtensionMultiplier", "TargetMarker",
                       "TargetSequenceMarker", "TargetStatusEffect", "TargetStatusEffectDurationCats",
                       "TargetStatusEffectDurationList", "TargetStatusEffectDurationMod",
                       "TargetStatusEffectDurationMult", "TargetStatusEffectPotency",
                       "TargetStatusEffectPotencyCats", "TargetStatusEffectPotencyComparison",
                       "TargetStatusEffectPotencyList", "TargetStatusEffectPotencyMod",
                       "TargetStatusEffectPotencyMult", "TargetStatusEffectStackSize", "TelegraphCountOffset",
                       "TelegraphStatusEffect", "ThreatDamage", "ToolDurabilityLossMod", "TrackHitCount",
                       "UISpellForManaCheck", "UnlockDefault", "UseDefenderWeaponOnApplyOtherSE",
                       "UseMinAttackInfoForSelfAppliedSE", "UseQuickslotItem", "WeaponAccuracy", "WeaponTag")
# The OnEndStatusEffect links of the game that name no status row (finding F8 of the review of stage 3.3b-S1): only these
# may stay; the backend accepts the same list (gamedata/ability_data.cpp, kMissingEndLinks).
END_LINKS_NOT_IN_GAME = frozenset({("ai_icedragon_empoweringlink_stack_apply", "ai_icedragon_empoweringlink_stack")})
SPELL_COLUMNS = ("SpellID", "Radius", "Height", "CastDistance", "NumToSpawn", "SpawnRate", "Duration")
COST_COLUMNS = ("ManaCostList", "ManaCostMult", "StaminaCostList", "StaminaCostFlatMod")
# The other cells of a status row and of a spell row (stage 1.3b) have no text for a person.
STATUS_SKIP = {"StatusID", "DisplayName", "Description", "Notes", "PlaceholderIcon", "FxScriptOn", "FxScriptOff", "WindowHeader",
               "LocTextTagForDeathRecap", "ExpansionForDeathRecap", "ObjectiveLocTextTagForDeathRecap", "XPIncreasesTooltip",
               "EffectDurationMods_Tooltip", "DMGVitalsCategory_Tooltip", "ModDmgBasedMultValue_ForTooltip"} | set(STATUS_COLUMNS)
# The cells of an ability row with no rule: the texts for a person and the place in the tree.
ABILITY_SKIP = {"AbilityID", "DisplayName", "Description", "Icon", "UICategory", "Notes", "TreeId", "TreeRowPosition",
                "TreeColumnPosition", "IsActiveAbility"}
CATEGORY_COLUMNS = ("StatusEffectCategoryID", "ValueLimits")
SPELL_SKIP = {"SpellID", "SpellPrefabPath", "SecondaryPrefabPath", "TertiaryPrefabPath", "BeamEffect", "IndicatorVFX",
              "FireJointName"} | set(SPELL_COLUMNS)
# Ids with two rows of different values in the game (the first row in the byte order of the paths stays): the status
# effect Sanctuary (the common and the catacombs table: equal values but its name text) and one spell of a raid.
DUPLICATES_IN_GAME = frozenset({"sanctuary", "raid_monarchsbluffs00_interactbarrel"})
# Cost ids that the cost tables of the game do not have (stage 1.3b, I14). The client gives such an id no cost: its
# lookup (image RVA 0x6801430) has no default row and no other table, and the callers pay nothing (cost analysis of
# stage 1.3b). "All" in a ManaCostList is a key of the modifier for each cost id (0x67EAED0).
COSTS_NOT_IN_GAME = frozenset({"specialattack", "elemental_fire_staff_heavy", "lifestaff_holyshield_mana_upgrade",
                               "lifestaff_speed_intro", "siphon_healing_check_to_cast", "all"})
# The action lists of the player (stage 3.3b-S5): the folder and the start of the file names of the ability lists.
ACTION_FOLDER = "sharedassets/springboardentitites/playerdata/cage/"
ACTION_PREFIX = "playeractions_"
ACTION_SUFFIX = ".actionlist"
# Largest number of a status effect on the wire (16 bits).
MAX_STATUS_INDEX = 0xFFFF


def crc(text: str) -> int:
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def number(sheet: Sheet, row: int, column: str) -> float:
    """@return The number of a cell: 0 for an empty cell, 1 or 0 for a flag. A text that is not a number stops the tool."""
    value = sheet.cell(row, column)
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    if isinstance(value, str):
        if value.strip() == "":
            return 0.0
        try:
            value = float(value)
        except ValueError as error:
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a number: {value!r}") from error
    if not math.isfinite(value):
        raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a finite number")
    return float(value)


# The texts of a flag cell (a table can have a flag as a text cell). Each other text stops the tool.
FLAG_TEXTS = {"": False, "0": False, "1": True, "false": False, "true": True}


def flag(sheet: Sheet, row: int, column: str) -> bool:
    value = sheet.cell(row, column)
    if isinstance(value, str):
        text = value.strip().lower()
        if text not in FLAG_TEXTS:
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a flag: {value!r}")
        return FLAG_TEXTS[text]
    return number(sheet, row, column) != 0.0


def rounded(value: float) -> float:
    return float(f"{value:.6g}")


def read_sheets(sources: Sources) -> dict[str, list[Sheet]]:
    """@return The data tables of the types that the tool uses, by type, in the byte order of their paths."""
    sheets: dict[str, list[Sheet]] = {kind: [] for kind in USED_TYPES}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind not in sheets:
            continue
        try:
            sheets[kind].append(Sheet(data))
        except (struct.error, ValueError, IndexError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    sources.check_complete()
    for kind, tables in sheets.items():
        if not tables:
            raise Failure(f"the game copy has no data table of the type {kind}")
    return sheets


def add_row(rows: dict[str, list], row: list, what: str, duplicates: frozenset[str] = frozenset()) -> None:
    """Adds a row by its lower-case id. The same id again with the same values is one row; with different values it
    stops the tool (finding F12), but for an id of `duplicates` (the duplicates of the game): its first row stays."""
    key = row[0].lower()
    if key in rows and rows[key][1:] != row[1:] and key not in duplicates:
        raise Failure(f"two rows of the {what} have the id {row[0]} with different values")
    rows.setdefault(key, row)


def require(sheet: Sheet, columns) -> None:
    """Stops the tool when a column that the tool reads is not in its table (code review of stage 3.3a, finding
    F11): an absent column is not 0 or false."""
    missing = [column for column in columns if column not in sheet.columns]
    if missing:
        raise Failure(f"the table {sheet.name} has no column {', '.join(missing)}")


def cells_of(sheet: Sheet, row: int, skip: set[str]) -> dict:
    """@return The other cells of a row that are not empty and not 0: a number as a number, each other cell as a text."""
    values: dict = {}
    for column in sheet.columns:
        if column in skip:
            continue
        source = str(sheet.source(row, column)).strip()
        if source in ("", "0"):
            continue
        try:
            value = float(source)
        except ValueError:
            values[column] = source
            continue
        if not math.isfinite(value):
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a finite number")
        if value != 0.0:
            values[column] = rounded(value)
    return values


def build_data(sources: Sources) -> dict:
    sheets = read_sheets(sources)
    cooldowns: dict[str, list] = {}
    for sheet in sheets[COOLDOWN_TYPE]:
        require(sheet, COOLDOWN_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "AbilityID")).strip()
            if not identifier:
                raise Failure(f"row {row} of the cooldown table {sheet.name} has no AbilityID")
            time = number(sheet, row, "Time")
            minimum = number(sheet, row, "MinTime")
            if time < 0.0 or minimum < 0.0:
                raise Failure(f"the cooldown of {identifier} is negative")
            add_row(cooldowns, [identifier, rounded(time), rounded(minimum)], "cooldown tables")

    # The numbers of the status effects: the sorted CRC values of all rows, with each duplicate.
    all_crcs: list[int] = []
    status_rows: list[tuple[Sheet, int, str]] = []
    for sheet in sheets[STATUS_TYPE]:
        require(sheet, STATUS_COLUMNS)
        absent = STATUS_TABLE_DIFFERENCES.get(sheet.name, frozenset())
        require(sheet, [column for column in STATUS_RULE_COLUMNS if column not in absent])
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "StatusID")).strip()
            if not identifier:
                continue  # The client adds only a row with an id (loader 0x44B4430).
            all_crcs.append(crc(identifier))
            status_rows.append((sheet, row, identifier))
    all_crcs.sort()
    statuses: dict[str, list] = {}
    for sheet, row, identifier in status_rows:
        index = bisect.bisect_left(all_crcs, crc(identifier))
        if index > MAX_STATUS_INDEX:
            raise Failure(f"the status effect {identifier} has the number {index}, above 16 bits")
        entry = [identifier, index, rounded(number(sheet, row, "BaseDuration")), rounded(number(sheet, row, "MoveSpeedMod")),
                 flag(sheet, row, "Stunned"), flag(sheet, row, "NoNav"), int(number(sheet, row, "StopOnHitCount")),
                 flag(sheet, row, "ShowInNameplates"), rounded(number(sheet, row, "DMGStandard")),
                 cells_of(sheet, row, STATUS_SKIP)]
        if entry[6] < 0:
            raise Failure(f"the status effect {identifier} has a negative StopOnHitCount")
        # Two rows with one id must have the same values (finding F12), but the duplicates of the game: the
        # first row stays, and each row stays in the count of the numbers.
        add_row(statuses, entry, "status tables", DUPLICATES_IN_GAME)

    spells: dict[str, list] = {}
    for sheet in sheets[SPELL_TYPE]:
        require(sheet, SPELL_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "SpellID")).strip()
            if not identifier:
                continue
            count = number(sheet, row, "NumToSpawn")
            if count < 0 or count != int(count):
                raise Failure(f"the spell {identifier} has a NumToSpawn that is not a whole number")
            entry = [identifier, rounded(number(sheet, row, "Radius")), rounded(number(sheet, row, "Height")),
                     rounded(number(sheet, row, "CastDistance")), int(count), rounded(number(sheet, row, "SpawnRate")),
                     rounded(number(sheet, row, "Duration")), cells_of(sheet, row, SPELL_SKIP)]
            add_row(spells, entry, "spell tables", DUPLICATES_IN_GAME)

    # The links of the status rows (code review of stage 3.3b-S1, finding F8): each status of OnEndStatusEffect and the spell
    # of CastSpell are rows of the data, but the links of END_LINKS_NOT_IN_GAME. The backend refuses a file with another one.
    for entry in statuses.values():
        cells = entry[9]
        for part in re.split(r"[+,]", str(cells.get("OnEndStatusEffect", ""))):
            target = part.strip()
            if target and target.lower() not in statuses and (entry[0].lower(), target.lower()) not in END_LINKS_NOT_IN_GAME:
                raise Failure(f"the status effect {entry[0]} names the end status {target}, which the status tables do not have")
        spell = str(cells.get("CastSpell", "")).strip()
        if spell and spell.lower() not in spells:
            raise Failure(f"the status effect {entry[0]} names the spell {spell} (CastSpell), which the spell tables do not have")
        # Code review of stage B3, finding B3-R3: each status of OnStackStatusEffect is a row of the data (the 46 links of the game resolve).
        for part in re.split(r"[+,]", str(cells.get("OnStackStatusEffect", ""))):
            target = part.strip()
            if target and target.lower() not in statuses:
                raise Failure(f"the status effect {entry[0]} names the stack status {target} (OnStackStatusEffect), which the status tables "
                              "do not have")

    # The costs (stage 1.3b, criterion I14): the two cost tables of the players and the cost columns of the
    # ability rows. A cost id of an ability or a spell names a row of a cost table.
    mana_costs: dict[str, list] = {}
    for sheet in sheets[MANA_TYPE]:
        require(sheet, ("CostID", "ManaCost"))
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "CostID")).strip()
            cost = number(sheet, row, "ManaCost")
            if identifier and cost < 0.0:
                raise Failure(f"the mana cost {identifier} is negative")
            if identifier:
                add_row(mana_costs, [identifier, rounded(cost)], "mana cost tables")
    stamina_costs: dict[str, list] = {}
    for sheet in sheets[STAMINA_TYPE]:
        require(sheet, ("CostID", "StaminaCost"))
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "CostID")).strip()
            cost = number(sheet, row, "StaminaCost")
            if identifier and cost < 0.0:
                raise Failure(f"the stamina cost {identifier} is negative")
            if identifier:
                # The two stamina tables (players, Damned) can have one id: the table of the players is first in
                # the byte order of the paths, and it stays.
                stamina_costs.setdefault(identifier.lower(), [identifier, rounded(cost)])
    ability_costs: dict[str, list] = {}
    for sheet in sheets[ABILITY_TYPE]:
        require(sheet, ("AbilityID",))
        present = [column for column in COST_COLUMNS if column in sheet.columns]
        if not present:
            continue  # The tables of the creatures (AI) have no cost column: their abilities have no cost.
        require(sheet, COST_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "AbilityID")).strip()
            mana = [part.strip() for part in str(sheet.source(row, "ManaCostList")).split(",") if part.strip()]
            stamina = [part.strip() for part in str(sheet.source(row, "StaminaCostList")).split(",") if part.strip()]
            multiplier = number(sheet, row, "ManaCostMult")
            flat = number(sheet, row, "StaminaCostFlatMod")
            if identifier and (mana or stamina or multiplier != 0.0 or flat != 0.0):
                add_row(ability_costs, [identifier, mana, rounded(multiplier), stamina, rounded(flat)], "ability tables")
    references = [(entry[0], cost) for entry in ability_costs.values() for cost in entry[1]]
    references += [(entry[0], entry[7]["ManaCost"]) for entry in spells.values() if "ManaCost" in entry[7]]
    for owner, cost in references:
        if cost.lower() not in mana_costs and cost.lower() not in COSTS_NOT_IN_GAME:
            raise Failure(f"{owner} names the mana cost {cost}, which the mana cost table does not have")
    for entry in ability_costs.values():
        for cost in entry[3]:
            if cost.lower() not in stamina_costs and cost.lower() not in COSTS_NOT_IN_GAME:
                raise Failure(f"{entry[0]} names the stamina cost {cost}, which the stamina cost tables do not have")

    # The caster effects (stage 3.3b-S1): each row of an ability table of the players (a table with the cost
    # columns) that puts a status on its caster, with its cells (the event and the conditions of the row).
    ability_rows: dict[str, list] = {}
    for sheet in sheets[ABILITY_TYPE]:
        if not any(column in sheet.columns for column in COST_COLUMNS):
            continue
        require(sheet, CASTER_COLUMNS + CASTER_RULE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "AbilityID")).strip()
            # Format 4 (stage 3.3b-S5): each row with an id, also a row with no cell (a node whose rule the action data has).
            if identifier:
                add_row(ability_rows, [identifier, cells_of(sheet, row, ABILITY_SKIP)], "ability tables")
    # Each status that a rule puts on its caster or on its target is a status row (code review of stage 3.3b-S5, finding F10: a
    # rule with a target status that the data does not have would be an applied rule with no effect). The supported game has no
    # such link of a target status; a caster status link with no row stops the tool too.
    for entry in ability_rows.values():
        for column in ("SelfApplyStatusEffect", "OtherApplyStatusEffect"):
            for status in [part.strip() for part in str(entry[1].get(column, "")).split(",") if part.strip()]:
                if status.lower() not in statuses:
                    raise Failure(f"the ability {entry[0]} names the status {status} ({column}), which the status tables do not have")
    action_spells, action_nodes, action_skipped = read_action_lists(sources, spells)
    action_statuses = read_action_statuses(sources)
    action_hit_statuses, action_hit_skipped = read_action_hit_statuses(sources)
    action_dodge_statuses, action_dodge_skipped = read_action_dodge_statuses(sources)
    action_tag_removals = read_action_tag_removals(sources)

    # The status categories (stage 3.3b-S1): the ValueLimits of each category ("MoveSpeedMod:0.4,SprintSpeedMod:0.4").
    categories: dict[str, list] = {}
    for sheet in sheets[CATEGORY_TYPE]:
        require(sheet, CATEGORY_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "StatusEffectCategoryID")).strip()
            if not identifier:
                continue
            limits: dict[str, float] = {}
            for part in str(sheet.source(row, "ValueLimits")).split(","):
                part = part.strip()
                if not part:
                    continue
                name, separator, value = part.partition(":")
                try:
                    limit = float(value)
                except ValueError as error:
                    raise Failure(f"the category {identifier} has a limit that is not a number: {part!r}") from error
                if not separator or not name.strip() or not math.isfinite(limit):
                    raise Failure(f"the category {identifier} has a limit that is not a name and a number: {part!r}")
                limits[name.strip()] = rounded(limit)
            add_row(categories, [identifier, limits], "status category tables")

    # The limits of the stat multipliers (stage 3.3b-S2a): the speed table of a character (slot 43) is in them.
    multipliers: dict[str, list] = {}
    for sheet in sheets[STATMULT_TYPE]:
        require(sheet, STATMULT_COLUMNS)
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "ID")).strip()
            if identifier:
                add_row(multipliers, [identifier, rounded(number(sheet, row, "Min")), rounded(number(sheet, row, "Max"))],
                        "stat multiplier tables")

    if not cooldowns:
        raise Failure("the cooldown tables have no row")
    # A status of an action names a row of the status tables (else the tool stops).
    for entry in action_statuses + action_hit_statuses + action_dodge_statuses + action_tag_removals:
        for status in entry[3]:
            if status.lower() not in statuses:
                raise Failure(f"the action {entry[1]} of {entry[0]} applies the status {status}, which the status tables do not have")
    for entry in action_hit_statuses + action_dodge_statuses:
        for status in entry[2]["without"]:
            if status.lower() not in statuses:
                raise Failure(f"the action {entry[1]} of {entry[0]} tests the status {status}, which the status tables do not have")
    data = {"format": 7, "statusRows": len(all_crcs),
            "counts": {"cooldowns": len(cooldowns), "statusEffects": len(statuses), "spells": len(spells),
                       "manaCosts": len(mana_costs), "staminaCosts": len(stamina_costs), "abilityCosts": len(ability_costs),
                       "abilityRows": len(ability_rows), "statusCategories": len(categories),
                       "statMultipliers": len(multipliers), "actionSpells": len(action_spells),
                       "actionNodes": len(action_nodes), "actionSpellsNotExported": len(action_skipped),
                       "actionStatuses": len(action_statuses), "actionHitStatuses": len(action_hit_statuses),
                       "actionDodgeStatuses": len(action_dodge_statuses), "actionTagRemovals": len(action_tag_removals)},
            "cooldowns": sorted(cooldowns.values()), "statusEffects": sorted(statuses.values(), key=lambda entry: entry[1]),
            "spells": sorted(spells.values()), "manaCosts": sorted(mana_costs.values()),
            "staminaCosts": sorted(stamina_costs.values()), "abilityCosts": sorted(ability_costs.values()),
            "abilityRows": sorted(ability_rows.values()), "statusCategories": sorted(categories.values()),
            "statMultipliers": sorted(multipliers.values()), "actionSpells": action_spells, "actionNodes": action_nodes,
            "actionSpellsNotExported": action_skipped, "actionStatuses": action_statuses,
            "actionHitStatuses": action_hit_statuses, "actionHitStatusesNotExported": action_hit_skipped,
            "actionDodgeStatuses": action_dodge_statuses, "actionDodgeStatusesNotExported": action_dodge_skipped,
            "actionTagRemovals": action_tag_removals}
    return data


def condition_tests(element, negated: bool, tests: list[tuple[str, bool]]) -> bool:
    """Adds the learned-node tests of a condition (IfAbilityActive, with its sign below Not) to `tests`: the tests of an And,
    and of an Or below a Not (De Morgan: Not(Or(a, b)) is And(Not a, Not b)). @return False for a condition that an AND of
    node tests cannot give (an Or with a node test in it, or a Not of an And of more than one part with a node test)."""
    for child in element:
        has_test = any(node.tag == "IfAbilityActive" for node in child.iter())
        if child.tag == "IfAbilityActive":
            tests.append((child.get("AbilityName", "").strip(), negated))
        elif child.tag == "Not":
            if not condition_tests(child, not negated, tests):
                return False
        elif child.tag in ("And", "Or"):
            # An And keeps the AND when it is not negated; an Or gives an AND when it is negated.
            if (child.tag == "And") == (not negated) or len(list(child)) == 1:
                if not condition_tests(child, negated, tests):
                    return False
            elif has_test:
                return False
        elif has_test:
            return False
    return True


def condition_terms(element, negated: bool = False) -> list | None:
    """@return An activation condition as an OR of ANDs of literals [kind, name, negated] (kinds: node, tag), or None for a test of a
    different kind."""
    if element.tag == "Not":
        children = list(element)
        return condition_terms(children[0], not negated) if len(children) == 1 else None
    if element.tag in ("And", "Or", "ActivationCondition"):
        parts = [condition_terms(child, negated) for child in element]
        if any(part is None for part in parts):
            return None
        if (element.tag != "Or") != negated:
            terms: list = [[]]
            for part in parts:
                terms = [left + right for left in terms for right in part]
            return terms
        return [term for part in parts for term in part]
    if element.tag == "IfAbilityActive" and element.get("AbilityName", "").strip() and element.get("External", "").lower() != "true":
        return [[["node", element.get("AbilityName").strip(), negated]]]
    if element.tag == "IfMannequinTag" and element.get("TagName", "").strip():
        return [[["tag", element.get("TagName").strip(), negated]]]
    return None


def read_action_statuses(sources: Sources) -> list:
    """@return The status actions of the player lists (format 5, stage 3.3b-M2): [list, action, terms, statuses] for each action that
    applies statuses on its enter and whose activation condition tests a learned node (only nodes and mannequin tags)."""
    found: list = []
    for path in sources.paths(ACTION_FOLDER, ACTION_SUFFIX):
        stem = path.rsplit("/", 1)[-1][:-len(ACTION_SUFFIX)]
        if not stem.startswith(ACTION_PREFIX):
            continue
        root = read_xml(sources, path, "the folder of the player actions")
        if root is None:
            continue
        for action in root.iter("Action"):
            activation = action.find("ActivationCondition")
            activities = action.find("Activities")
            if activation is None or activities is None:
                continue
            # The statuses of the action while it is active: applied on its enter and removed on its exit (a tracker that stays is
            # not such a status).
            applied = {effect.get("EffectId", "").strip() for modify in activities.findall("ModifyStatusEffects")
                       for enter in modify.iter("EffectsToApplyOnEnter") for effect in enter.iter("EffectId")}
            removed = {effect.get("EffectId", "").strip() for modify in activities.findall("ModifyStatusEffects")
                       for leave in modify.iter("EffectsToRemoveOnExit") for effect in leave.iter("EffectId")}
            statuses = sorted((applied & removed) - {""})
            if not statuses:
                continue
            terms = condition_terms(activation)
            if not terms or not any(literal[0] == "node" and not literal[2] for term in terms for literal in term):
                continue
            found.append([stem, action.get("Name", ""), terms, statuses])
    return found


HIT_COMPARISONS = ("LessThan", "LessThanOrEqual", "Equal", "GreaterThanOrEqual", "GreaterThan", "NotEqual")


def hit_conditions(activation) -> tuple[dict | None, str]:
    """@return The tests of the activation condition of a hit-taken action (format 6), or no value and the cause."""
    tests = {"nodes": [], "tags": [], "health": None, "stamina": None, "without": [], "power": None}
    parts = list(activation)
    while len(parts) == 1 and parts[0].tag == "And":
        parts = list(parts[0])
    for part in parts:
        if part.tag == "IfDamagedByPowerLevel":
            comparison = part.get("Comparison", "")
            if comparison not in HIT_COMPARISONS:
                return None, f"IfDamagedByPowerLevel with the comparison {comparison!r}"
            try:
                value = float(part.get("Value", ""))
            except ValueError:
                return None, "IfDamagedByPowerLevel with a value that is not a number"
            if not math.isfinite(value):
                return None, "IfDamagedByPowerLevel with a value that is not finite"
            tests["power"] = [comparison, rounded(value), str(part.get("FailOnNoReactionDamage", "")).lower() == "true"]
        elif part.tag == "IfHealthPercentage":
            comparison = part.get("Comparison", "")
            try:
                value = float(part.get("Percentage", ""))
            except ValueError:
                return None, "IfHealthPercentage with a part that is not a number"
            if comparison not in HIT_COMPARISONS or not math.isfinite(value):
                return None, "IfHealthPercentage with a comparison or a part that the reader does not know"
            tests["health"] = [comparison, rounded(value)]
        elif part.tag == "IfStaminaValue":
            test, cause = stamina_test(part)
            if test is None:
                return None, cause
            tests["stamina"] = test
        elif part.tag == "IfAbilityActive" and part.get("AbilityName", "").strip():
            tests["nodes"].append(part.get("AbilityName").strip())
        elif part.tag == "IfMannequinTag" and part.get("TagName", "").strip():
            tests["tags"].append(part.get("TagName").strip())
        elif part.tag == "Not" and len(part) == 1 and part[0].tag == "IfHasStatusEffect" and part[0].get("StatusEffect", "").strip():
            tests["without"].append(part[0].get("StatusEffect").strip())
        else:
            return None, f"a test {part.tag} that the reader does not know"
    if tests["power"] is None:
        return None, "no IfDamagedByPowerLevel"
    if not tests["nodes"]:
        return None, "no test of a learned node"
    return tests, ""


def read_action_hit_statuses(sources: Sources) -> tuple[list, list]:
    """@return The hit-taken actions of the player lists (format 6, stage 3.4f-B): [list, action, tests, statuses] for each action that
    applies statuses on its enter at a hit that the character takes (IfDamagedByPowerLevel) and that tests a learned node; and each such
    action with a test of a different kind, [list, action, cause]."""
    found: list = []
    skipped: list = []
    for path in sources.paths(ACTION_FOLDER, ACTION_SUFFIX):
        stem = path.rsplit("/", 1)[-1][:-len(ACTION_SUFFIX)]
        if not stem.startswith(ACTION_PREFIX):
            continue
        root = read_xml(sources, path, "the folder of the player actions")
        if root is None:
            continue
        for action in root.iter("Action"):
            activation = action.find("ActivationCondition")
            activities = action.find("Activities")
            if activation is None or activities is None or not any(node.tag == "IfDamagedByPowerLevel" for node in activation.iter()):
                continue
            applied = sorted({effect.get("EffectId", "").strip() for modify in activities.findall("ModifyStatusEffects")
                              for enter in modify.iter("EffectsToApplyOnEnter") for effect in enter.iter("EffectId")} - {""})
            if not applied:
                continue
            if not any(node.tag == "IfAbilityActive" for node in activation.iter()):
                continue
            others = sorted({child.tag for child in activities} - {"ModifyStatusEffects"})
            if others:
                # The action does more than its statuses (HealingAura casts a spell): the backend does not have it.
                skipped.append([stem, action.get("Name", ""), f"an activity other than the statuses ({', '.join(others)})"])
                continue
            tests, cause = hit_conditions(activation)
            if tests is None:
                skipped.append([stem, action.get("Name", ""), cause])
                continue
            found.append([stem, action.get("Name", ""), tests, applied])
    return found, skipped


def stamina_test(part) -> tuple[list | None, str]:
    """@return The test of an IfStaminaValue as a part of the maximum ([comparison, part]), or no value and the cause."""
    comparison = part.get("ComparisonType", "")
    try:
        value = float(part.get("ComparisonValue", ""))
    except ValueError:
        return None, "IfStaminaValue with a value that is not a number"
    if comparison not in HIT_COMPARISONS or not math.isfinite(value) or str(part.get("ComparisonValueIsPercentage", "")).lower() != "true":
        return None, "IfStaminaValue that is not a part of the maximum"
    return [comparison, rounded(value)], ""


def dodge_conditions(activation) -> tuple[dict | None, str]:
    """@return The tests of the activation condition of a dodge status action (format 7), or no value and the cause."""
    tests = {"dodge": None, "nodes": [], "tags": [], "stamina": None, "without": []}
    parts = list(activation)
    while len(parts) == 1 and parts[0].tag == "And":
        parts = list(parts[0])
    for part in parts:
        if part.tag == "IfActionStatus":
            if part.get("Status", "") != "Installed" or not part.get("ActionName", "").strip() or tests["dodge"] is not None:
                return None, "IfActionStatus that is not one test of an installed action"
            tests["dodge"] = part.get("ActionName").strip()
        elif part.tag == "IfStaminaValue":
            test, cause = stamina_test(part)
            if test is None:
                return None, cause
            tests["stamina"] = test
        elif part.tag == "IfAbilityActive" and part.get("AbilityName", "").strip():
            tests["nodes"].append(part.get("AbilityName").strip())
        elif part.tag == "IfMannequinTag" and part.get("TagName", "").strip():
            tests["tags"].append(part.get("TagName").strip())
        elif part.tag == "Not" and len(part) == 1 and part[0].tag == "IfHasStatusEffect" and part[0].get("StatusEffect", "").strip():
            tests["without"].append(part[0].get("StatusEffect").strip())
        else:
            return None, f"a test {part.tag} that the reader does not know"
    if tests["dodge"] is None:
        return None, "no IfActionStatus"
    if not tests["nodes"]:
        return None, "no test of a learned node"
    return tests, ""


def exit_statuses(activities) -> tuple[list[str] | None, list[str], str]:
    """@return The statuses that the activities give at the exit of the action (EffectsToApplyOnExit), the mannequin tags of the
    IfThenElse that holds them, and a cause when the activities have a different form."""
    statuses: list[str] = []
    tags: list[str] = []
    for child in activities:
        if child.tag == "ModifyStatusEffects":
            holder = child
        elif child.tag == "IfThenElse":
            condition = child.find("IfCondition")
            then = child.find("ThenActivity")
            otherwise = child.find("ElseActivity")
            tests = list(condition) if condition is not None else []
            if (len(tests) != 1 or tests[0].tag != "IfMannequinTag" or not tests[0].get("TagName", "").strip() or then is None
                    or (otherwise is not None and len(otherwise) != 0) or any(node.tag != "ModifyStatusEffects" for node in then)):
                return None, [], "an IfThenElse that is not a test of one tag with statuses"
            tags.append(tests[0].get("TagName").strip())
            holder = then
        else:
            return None, [], f"an activity other than the statuses ({child.tag})"
        for modify in holder.iter("ModifyStatusEffects"):
            for default in modify:
                for group in default:
                    if group.tag != "EffectsToApplyOnExit":
                        return None, [], f"a status change other than at the exit ({group.tag})"
                    statuses.extend(effect.get("EffectId", "").strip() for effect in group.iter("EffectId"))
    statuses = sorted(set(statuses) - {""})
    return statuses, tags, ""


def read_action_dodge_statuses(sources: Sources) -> tuple[list, list]:
    """@return The dodge status actions of the player lists (format 7, stage B3): [list, action, tests, statuses] for each action that
    gives statuses at its exit while a dodge plays (IfActionStatus ... Installed, EffectsToApplyOnExit) and that tests a learned node; and
    each such action with a test or an activity of a different kind, [list, action, cause]."""
    found: list = []
    skipped: list = []
    for path in sources.paths(ACTION_FOLDER, ACTION_SUFFIX):
        stem = path.rsplit("/", 1)[-1][:-len(ACTION_SUFFIX)]
        if not stem.startswith(ACTION_PREFIX):
            continue
        root = read_xml(sources, path, "the folder of the player actions")
        if root is None:
            continue
        for action in root.iter("Action"):
            activation = action.find("ActivationCondition")
            activities = action.find("Activities")
            if activation is None or activities is None or not any(node.tag == "IfActionStatus" for node in activation.iter()):
                continue
            if not any(node.tag == "EffectsToApplyOnExit" for node in activities.iter()):
                continue
            if not any(node.tag == "IfAbilityActive" for node in activation.iter()):
                continue
            statuses, exit_tags, cause = exit_statuses(activities)
            if statuses is None:
                skipped.append([stem, action.get("Name", ""), cause])
                continue
            tests, cause = dodge_conditions(activation)
            if tests is None:
                skipped.append([stem, action.get("Name", ""), cause])
                continue
            if not statuses:
                continue
            tests["exitTags"] = exit_tags
            found.append([stem, action.get("Name", ""), tests, statuses])
    return found, skipped


def read_action_tag_removals(sources: Sources) -> list:
    """@return The tag removal actions of the player lists (format 7, stage B3): [list, action, tags, statuses] for each action whose
    activation condition is only IfMannequinTag tests (in an And) and whose only activity removes statuses at its exit."""
    found: list = []
    for path in sources.paths(ACTION_FOLDER, ACTION_SUFFIX):
        stem = path.rsplit("/", 1)[-1][:-len(ACTION_SUFFIX)]
        if not stem.startswith(ACTION_PREFIX):
            continue
        root = read_xml(sources, path, "the folder of the player actions")
        if root is None:
            continue
        for action in root.iter("Action"):
            activation = action.find("ActivationCondition")
            activities = action.find("Activities")
            if activation is None or activities is None:
                continue
            tests = [node for node in activation.iter() if node.tag not in ("ActivationCondition", "And")]
            if not tests or any(node.tag != "IfMannequinTag" or not node.get("TagName", "").strip() for node in tests):
                continue
            if any(child.tag != "ModifyStatusEffects" for child in activities):
                continue
            groups = [group for modify in activities for default in modify for group in default]
            if not groups or any(group.tag != "EffectsToRemoveOnExit" for group in groups):
                continue
            statuses = sorted({effect.get("EffectId", "").strip() for group in groups for effect in group.iter("EffectId")} - {""})
            if statuses:
                found.append([stem, action.get("Name", ""), [node.get("TagName").strip() for node in tests], statuses])
    return found


def read_action_lists(sources: Sources, spells: dict[str, list]) -> tuple[list, list]:
    """@return The spell selections and the node tests of the action lists of the player (stage 3.3b-S5): for each CastSpell
    activity below IfThenElse activities whose conditions test learned nodes, [action list, action, spell, nodes that it
    needs, nodes that it must not have] (an Else branch takes the tests of its condition with the other sign); for each action
    list the nodes that its conditions test; and each cast whose condition is not an AND of node tests (an Or with a test of the
    state of the client), which is no selection of node rules. A spell of a selection that the spell tables do not have stops
    the tool."""
    selections: list[list] = []
    tested: list[list] = []
    skipped: list[list] = []
    for path in sources.paths(ACTION_FOLDER, ACTION_SUFFIX):
        stem = path.rsplit("/", 1)[-1][:-len(ACTION_SUFFIX)]
        if not stem.startswith(ACTION_PREFIX):
            continue
        root = read_xml(sources, path, "the folder of the player actions")
        if root is None:
            continue
        names = sorted({node.get("AbilityName", "").strip() for node in root.iter("IfAbilityActive")} - {""}, key=str.lower)
        if names:
            tested.append([stem, names])

        def visit(element, action: str, tests: list[tuple[str, bool]]) -> None:
            for child in element:
                current = child.get("Name", action) if child.tag == "Action" else action
                if child.tag == "IfThenElse":
                    condition = child.find("IfCondition")
                    found: list[tuple[str, bool]] = []
                    simple = condition is None or condition_tests(condition, False, found)
                    tested_nodes = condition is not None and any(node.tag == "IfAbilityActive" for node in condition.iter())
                    for branch, flip in (("ThenActivity", False), ("ElseActivity", True)):
                        part = child.find(branch)
                        if part is None:
                            continue
                        # A condition that is not an AND of node tests (an Or with a node test and a test of the state of the
                        # client: the mana of the ice storm), and an Else branch of more than one node test (the negation of an
                        # AND): no selection of node rules. Each such cast is in the list "actionSpellsNotExported".
                        if (tested_nodes and not simple) or (flip and len(found) > 1):
                            for node in part.iter("CastSpell"):
                                default = node.find("Default")
                                spell = default.get("SpellName", "").strip() if default is not None else ""
                                entry = [stem, current, spell, "a condition that is not an AND of node tests"]
                                if spell and entry not in skipped:
                                    skipped.append(entry)
                            continue
                        visit(part, current, tests + [(name, negated != flip) for name, negated in found])
                    continue
                if child.tag == "CastSpell" and any(True for _ in tests):
                    default = child.find("Default")
                    spell = default.get("SpellName", "").strip() if default is not None else ""
                    if not spell:
                        continue
                    if spell.lower() not in spells:
                        raise Failure(f"{path}: the action {current} casts the spell {spell}, which the spell tables do not have")
                    needs = sorted({name for name, negated in tests if not negated}, key=str.lower)
                    excludes = sorted({name for name, negated in tests if negated}, key=str.lower)
                    entry = [stem, current, spell, needs, excludes]
                    if entry not in selections:
                        selections.append(entry)
                    continue
                visit(child, current, tests)

        visit(root, "", [])
    return selections, tested, skipped


def print_found(data: dict, texts: list[str]) -> None:
    wanted = [text.lower() for text in texts]
    for name in ("cooldowns", "statusEffects", "spells", "manaCosts", "staminaCosts", "abilityCosts", "abilityRows",
                 "statusCategories", "statMultipliers"):
        for row in data[name]:
            if any(text in row[0].lower() for text in wanted):
                print(f"{name}: {row}")


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets")
        data = build_data(sources)
        if find is not None:
            print_found(data, find)
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(data, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"abilities: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"abilities: {target}: {error}", file=sys.stderr)
        return 1
    counts = data["counts"]
    print(f"{target}: {counts['cooldowns']} cooldowns, {counts['statusEffects']} status effects (numbers of "
          f"{data['statusRows']} rows), {counts['spells']} spells, {counts['manaCosts']} mana costs, "
          f"{counts['staminaCosts']} stamina costs, {counts['abilityCosts']} ability rows with a cost, "
          f"{counts['abilityRows']} ability rows, {counts['actionSpells']} spell selections of the action lists "
          f"({counts['actionSpellsNotExported']} casts with a condition that is not an AND of node tests), {counts['statusCategories']} status categories "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

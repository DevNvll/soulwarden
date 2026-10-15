#pragma once

// The world of the cases of the critical hits and of the evaluator of the passive rules (stage 3.3b-S5): the itemization shop
// (support/shop_fixture.h) with the critical values of its weapons and rows, the rows of the nodes of the great axe and the
// war hammer as the game data has them (the ids and the cells of the rows of the ability tables; the cells of a sound are left
// out), their statuses and tracking spells, the spell selections of an action of the data, and a wolf with the angles of a hit
// from behind and a head volume. The ids of the characters and the values of the other rows are made up.

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/shop_fixture.h"

namespace soulwarden::backend::tests::passive {

using namespace shop;

// Generated from the ability data of the game (tools/gamedata/abilities.sh format 4): the cells of each row as in the data.
inline std::vector<gamedata::AbilityRow> data_rows() {
    return {
        {"Passive_Greataxe_Reaper_Crit", cells({{"CritDamage", 0.1F}}, {{"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Reaper_OnCritHeal", cells({{"ActivationCooldown", 0.1F}, {"DmgPctToHealth", 0.1F}, {"OnCrit", 1.0F}, {"RequireReaction", 1.0F}}, {{"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Reaper_CritVsLowHP", cells({{"CritChance", 0.15F}, {"OnCrit", 1.0F}, {"TargetHealthPercent", 30.0F}}, {{"WeaponTag", "GreatAxe"}})},
        {"Ability_Greataxe_Executioner", cells({{"BaseDamage", 0.5F}, {"OnHit", 1.0F}, {"TargetHealthPercent", 50.0F}}, {{"CooldownId", "Executioner"}, {"DamageTableRow", "GreatAxe_Executioner"}, {"WeaponTag", "GreatAxe"}})},
        {"Upgrade_Greataxe_Executioner_CritVsLowHP", cells({{"CritChance", 1.0F}, {"OnHit", 1.0F}, {"TargetHealthPercent", 30.0F}}, {{"DamageTableRow", "GreatAxe_Executioner"}, {"RequiredAbilityId", "Ability_Greataxe_Executioner"}, {"WeaponTag", "GreatAxe"}})},
        {"Ultimate_Greataxe_Mauler", cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}}, {{"SelfApplyStatusEffect", "Status_Ultimate_Greataxe_Mauler"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Mauler_DmgWhenFoesNear", cells({{"NumAroundMe", 2.0F}}, {{"NumAroundComparisonType", "GreaterThan"}, {"SelfApplyStatusEffect", "Status_Passive_Greataxe_Mauler_DmgWhenFoesNear"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Mauler_HeavyAddGrit", cells({{"BaseDamageReduction", 0.12F}, {"HasGritActive", 1.0F}, {"OnHitTaken", 1.0F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Reaper_HasteOnLight", cells({{"NumSuccessfulHits", 2.0F}, {"OnHit", 1.0F}, {"RequireReaction", 1.0F}}, {{"AttackType", "Light"}, {"DontHaveStatusEffect", "Status_Passive_Greataxe_Reaper_HasteOnLight"}, {"SelfApplyStatusEffect", "Status_Passive_Greataxe_Reaper_HasteOnLight"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Reaper_ArmorPenVsLowHP", cells({{"ArmorPenetration", 0.1F}, {"OnHit", 1.0F}, {"TargetHealthPercent", 50.0F}}, {{"WeaponTag", "GreatAxe"}})},
        {"Upgrade_Greataxe_GravityWell_DmgPerFoe", cells({{"BaseDamage", 0.1F}, {"MaxNumAroundMe", 5.0F}, {"NumAroundMe", 1.0F}}, {{"DamageTableRow", "GreatAxe_GravityWell_Burst"}, {"NumAroundComparisonType", "GreaterThanOrEqual"}, {"RequiredAbilityId", "Ability_Greataxe_GravityWell"}, {"WeaponTag", "GreatAxe"}})},
        {"Upgrade_Greataxe_Maelstorm_Rng", cells({}, {{"RequiredAbilityId", "Ability_Greataxe_Maelstorm"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_Greataxe_Mauler_DmgOnBlock", cells({{"OnBlockedHitTaken", 1.0F}}, {{"SelfApplyStatusEffect", "Status_Passive_Greataxe_Mauler_DmgOnBlock"}, {"WeaponTag", "GreatAxe"}})},
        {"Passive_WarHammer_Juggernaut_ExhaustOnHit", cells({{"OnHit", 1.0F}}, {{"AttackType", "Ability"}, {"OtherApplyStatusEffect", "Warhammer_Pass_ExhaustOnHit"}, {"TargetStatusEffectCategory", "IgnoreDebuffs"}, {"TargetStatusEffectComparison", "NotEqual"}, {"WeaponTag", "Warhammer"}})},
        {"Passive_WarHammer_Juggernaut_HeavyvsLowHP", cells({{"BaseDamage", 0.15F}, {"OnHit", 1.0F}, {"TargetHealthPercent", 30.0F}}, {{"AttackType", "Heavy"}, {"TargetComparisonType", "LessThan"}, {"WeaponTag", "Warhammer"}})},
        {"Passive_WarHammer_Juggernaut_LightHeavyPenetrate", cells({{"ArmorPenetration", 0.1F}, {"OnBlockedHit", 1.0F}, {"OnHit", 1.0F}}, {{"AttackType", "Light,Heavy"}, {"WeaponTag", "Warhammer"}})},
        {"Passive_WarHammer_Juggernaut_CDROnHit", cells({{"CooldownTimer", -0.07F}, {"NumberOfTrackedHits", 5.0F}, {"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"TrackHitCount", 1.0F}}, {{"AttackType", "Heavy"}, {"CDRImmediatelyOptions", "ActiveWeapon"}, {"NumberOfHitsComparisonType", "LessThanOrEqual"}, {"WeaponTag", "Warhammer"}})},
        {"Upgrade_WarHammer_PathOfDestiny_CDRPerHit", cells({{"CooldownTimer", -0.05F}, {"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}, {"NumberOfTrackedHits", 5.0F}, {"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"TrackHitCount", 1.0F}}, {{"CDRImmediatelyOptions", "AbilitySpecific"}, {"CooldownId", "Ability_WarHammer_PathOfDestiny"}, {"InAction", "Ability_WarHammer_PathOfDestiny"}, {"InSequence", "Ability_WarHammer_PathOfDestiny"}, {"NumberOfHitsComparisonType", "LessThanOrEqual"}, {"RequiredAbilityId", "Upgrade_WarHammer_PathOfDestiny_Stagger"}, {"WeaponTag", "Warhammer"}})},
        {"Ult_Passive_WarHammer_SlowOnCC", cells({{"OnBlockedHit", 1.0F}, {"OnHit", 1.0F}}, {{"AttackType", "Ability"}, {"OtherApplyStatusEffect", "WarHammer_Ult_SlowOnCC"}, {"TargetStatusEffectCategory", "IgnoreDebuffs"}, {"TargetStatusEffectComparison", "NotEqual"}, {"WeaponTag", "Warhammer"}})},
        {"Upgrade_WarHammer_Shockwave_Weaken", cells({{"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}, {"OnHit", 1.0F}}, {{"CooldownId", "Ability_WarHammer_Shockwave"}, {"InAction", "Ability_WarHammer_Shockwave"}, {"InSequence", "Ability_WarHammer_Shockwave"}, {"OtherApplyStatusEffect", "WarHammer_Shockwave_Weaken"}, {"RequiredAbilityId", "Ability_WarHammer_Shockwave"}, {"TargetStatusEffectCategory", "IgnoreDebuffs"}, {"TargetStatusEffectComparison", "NotEqual"}, {"WeaponTag", "Warhammer"}})},
        {"Upgrade_WarHammer_Shockwave_SizeIncrease", cells({{"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}, {"OnBlockedHit", 1.0F}, {"OnHit", 1.0F}}, {{"CooldownId", "Ability_WarHammer_Shockwave"}, {"InAction", "Ability_WarHammer_Shockwave"}, {"InSequence", "Ability_WarHammer_Shockwave"}, {"RequiredAbilityId", "Upgrade_WarHammer_Shockwave_Weaken"}, {"WeaponTag", "Warhammer"}})},
        {"Passive_WarHammer_Juggernaut_DmgAfterTakingDmg", cells({{"NumSuccessfulHits", 1.0F}, {"OnHitTaken", 1.0F}}, {{"SelfApplyStatusEffect", "WarHammer_Pass_EmpowerOnDmgTaken"}, {"WeaponTag", "Warhammer"}})},
        {"Ability_Warhammer_MightyGavel_Upg", cells({{"ActivationCooldown", 1.0F}, {"CooldownTimer", -0.1F}, {"CritChance", 0.2F}, {"OnHit", 1.0F}, {"RequireReaction", 1.0F}}, {{"CDRImmediatelyOptions", "AllExcept"}, {"CooldownId", "Ability_WarHammer_MightyGavel"}, {"DamageTableRow", "Warhammer_Damage_MightyGavel"}, {"RequiredAbilityId", "Upgrade_WarHammer_Gavel_Haste"}, {"WeaponTag", "Warhammer"}})},
        {"Upgrade_Sword_LeapingStrike_Extra", cells({{"BaseDamage", 0.5F}, {"OnHit", 1.0F}, {"TargetHealthPercent", 30.0F}}, {{"DamageTableRow", "1H_Sword_Attack_Charge"}, {"RequiredAbilityId", "Ability_Sword_LeapingStrike"}, {"WeaponTag", "Sword"}})},
        {"Ability_WarHammer_Shockwave", cells({{"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}}, {{"CooldownId", "Ability_WarHammer_Shockwave"}, {"InAction", "Ability_WarHammer_Shockwave"}, {"InSequence", "Ability_WarHammer_Shockwave"}, {"OtherApplyStatusEffect", "WarHammer_Shockwave_Stun"}, {"WeaponTag", "Warhammer"}})},
        {"Ability_WarHammer_PathOfDestiny", cells({{"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}}, {{"CooldownId", "Ability_WarHammer_PathOfDestiny"}, {"InAction", "Ability_WarHammer_PathOfDestiny"}, {"InSequence", "Ability_WarHammer_PathOfDestiny"}, {"WeaponTag", "Warhammer"}})},
        {"Ability_WarHammer_WreckingBall", cells({{"InSequenceTime", 0.1F}, {"OnBlockedHit", 1.0F}, {"OnHit", 1.0F}}, {{"CooldownId", "Ability_WarHammer_WreckingBall"}, {"InSequence", "Ability_WarHammer_WreckingBall"}, {"WeaponTag", "Warhammer"}})},
        {"Ability_Greataxe_GravityWell", cells({}, {{"CooldownId", "GravityWell"}, {"WeaponTag", "GreatAxe"}})},
        {"Ability_Greataxe_Charge", cells({}, {{"CooldownId", "Engaged"}, {"DamageTableRow", "GreatAxe_Engage"}, {"WeaponTag", "GreatAxe"}})},
        {"Ability_Greataxe_Maelstorm", cells({}, {{"CooldownId", "Maelstrom"}, {"WeaponTag", "GreatAxe"}})},
    };
}
inline std::vector<gamedata::StatusRow> data_statuses() {
    return {
        status("Status_Ultimate_Greataxe_Mauler", 3.0F, cells({{"DMGAcid", 0.03F}, {"DMGArcane", 0.03F}, {"DMGCorruption", 0.03F}, {"DMGFire", 0.03F}, {"DMGIce", 0.03F}, {"DMGLightning", 0.03F}, {"DMGNature", 0.03F}, {"DMGSiege", 0.03F}, {"DMGSlash", 0.03F}, {"DMGStrike", 0.03F}, {"DMGThrust", 0.03F}, {"DMGVitalsCategory", 0.03F}, {"DurationMax", 6.0F}, {"IgnoreDiminishingReturns", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"StackMax", 10.0F}, {"TickRate", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Buff+Empower"}, {"RefreshDuration", "TRUE"}}), true),
        status("Status_Passive_Greataxe_Reaper_HasteOnLight", 3.0F, cells({{"DurationMax", 6.0F}, {"InheritTotalDuration", 1.0F}, {"PotencyMax", 1.0F}, {"RefreshDuration", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveOnGameModeExit", 1.0F}, {"ShowInUITray", 1.0F}, {"SprintSpeedMod", 0.2F}, {"StackMax", 1.0F}, {"TickRate", 1.0F}, {"UIPriority", 2.0F}, {"UiNameplatePriority", 2.0F}}, {{"EffectCategories", "Buff+Haste"}}), false),
        status("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear", -1.0F, cells({{"AlwaysReplicate", 1.0F}, {"DMGAcid", 0.1F}, {"DMGArcane", 0.1F}, {"DMGCorruption", 0.1F}, {"DMGFire", 0.1F}, {"DMGIce", 0.1F}, {"DMGLightning", 0.1F}, {"DMGNature", 0.1F}, {"DMGSiege", 0.1F}, {"DMGSlash", 0.1F}, {"DMGStrike", 0.1F}, {"DMGThrust", 0.1F}, {"DMGVitalsCategory", 0.1F}, {"DurationMax", 1.0F}, {"IgnoreDiminishingReturns", 1.0F}, {"InheritDuration", 1.0F}, {"PotencyMax", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"StackMax", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Buff+Empower"}, {"RefreshDuration", "TRUE"}}), true),
        status("Warhammer_Pass_ExhaustOnHit", 5.0F, cells({{"DurationMax", 10.0F}, {"IsNegative", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"StackMax", 1.0F}, {"StaminaRate", -0.2F}, {"TickRate", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Debuff+Exhaust+Warhammer+Debilitate"}}), true),
        status("WarHammer_Ult_SlowOnCC", 4.0F, cells({{"DurationMax", 10.0F}, {"IsNegative", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"SprintSpeedMod", -0.2F}, {"StackMax", 1.0F}, {"TickRate", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Debuff+Slow+Warhammer+CC"}}), true),
        status("WarHammer_Pass_EmpowerOnDmgTaken", 1.0F, cells({{"DMGAcid", 0.35F}, {"DMGArcane", 0.35F}, {"DMGCorruption", 0.35F}, {"DMGFire", 0.35F}, {"DMGIce", 0.35F}, {"DMGLightning", 0.35F}, {"DMGNature", 0.35F}, {"DMGSiege", 0.35F}, {"DMGSlash", 0.35F}, {"DMGStrike", 0.35F}, {"DMGThrust", 0.35F}, {"DMGVitalsCategory", 0.35F}, {"DurationMax", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"StackMax", 1.0F}, {"TickRate", 1.0F}}, {{"EffectCategories", "Buff+Empower"}}), false),
        status("TSE_CritChance_Inc_10PCT", -1.0F, cells({{"CritChanceModifier", 0.1F}, {"ShowTextInNameplates", 1.0F}, {"StackMax", 25.0F}, {"UIPriority", 1.0F}}, {}), false),
        status("Status_Passive_Greataxe_Mauler_DmgOnBlock", 5.0F, cells({{"DMGAcid", 0.05F}, {"DMGArcane", 0.05F}, {"DMGCorruption", 0.05F}, {"DMGFire", 0.05F}, {"DMGIce", 0.05F}, {"DMGLightning", 0.05F}, {"DMGNature", 0.05F}, {"DMGSiege", 0.05F}, {"DMGSlash", 0.05F}, {"DMGStrike", 0.05F}, {"DMGThrust", 0.05F}, {"DMGVitalsCategory", 0.05F}, {"DurationMax", 5.0F}, {"IgnoreDiminishingReturns", 1.0F}, {"InheritDuration", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"StackMax", 3.0F}, {"TickRate", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Buff+Empower"}, {"RefreshDuration", "TRUE"}}), true),
        status("WarHammer_Shockwave_Weaken", 10.0F, cells({{"DMGAcid", -0.1F}, {"DMGArcane", -0.1F}, {"DMGCorruption", -0.1F}, {"DMGFire", -0.1F}, {"DMGIce", -0.1F}, {"DMGLightning", -0.1F}, {"DMGNature", -0.1F}, {"DMGSiege", -0.1F}, {"DMGSlash", -0.1F}, {"DMGStrike", -0.1F}, {"DMGThrust", -0.1F}, {"DMGVitalsCategory", -0.1F}, {"DurationMax", 20.0F}, {"IsNegative", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"StackMax", 1.0F}, {"TickRate", 1.0F}, {"UIPriority", 2.0F}, {"UiNameplatePriority", 2.0F}}, {{"EffectCategories", "Debuff+Weaken+Warhammer+Debilitate"}}), true),
        status("WarHammer_Shockwave_Stun", 2.0F, cells({{"DurationMax", 10.0F}, {"IsNegative", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RemoveOnDeathsDoor", 1.0F}, {"RemoveUnappliedStacks", 1.0F}, {"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"StackMax", 1.0F}, {"TickRate", 1.0F}, {"UIPriority", 1.0F}, {"UiNameplatePriority", 1.0F}}, {{"EffectCategories", "Debuff+Stun+Warhammer+CC"}, {"HitCondition", "OnHitTaken"}}), true),
    };
}
inline std::vector<gamedata::SpellRow> data_spells() {
    return {
        {"Spell_Passive_Greataxe_Mauler_DmgWhenFoesNear", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, cells({{"AttachToOwner", 1.0F}, {"ClearOnEnd", 1.0F}, {"ClearOnExit", 1.0F}, {"IgnoreOverheadCollision", 1.0F}, {"TrackNumInVolume", 1.0F}}, {{"AbilityId", "Passive_Greataxe_Mauler_DmgWhenFoesNear"}, {"CastingTypes", "AoE"}, {"ShapeTypes", "Sphere"}, {"SpellTypes", "Ability"}, {"TargetTypes", "Enemy"}})},
        {"Spell_GreatAxe_GravityWell_Vortex", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, cells({{"CheckObstructions", 1.0F}, {"ClearOnEnd", 1.0F}, {"ClearOnExit", 1.0F}, {"IgnoreOverheadCollision", 1.0F}, {"TrackNumInVolume", 1.0F}, {"UseStatusEffectDuration", 1.0F}}, {{"AbilityId", "Upgrade_Greataxe_GravityWell_DmgPerFoe"}, {"CastingTypes", "AoE"}, {"ShapeTypes", "Sphere"}, {"SpellTypes", "Ability"}, {"StatusEffects", "GreatAxe_Vortex"}, {"TargetTypes", "Enemy"}})},
    };
}

/** The ability data of the cases: the shop data with the rows, statuses, and spells of the data above, and two spell selections. */
inline gamedata::AbilityTables passive_tables() {
    gamedata::AbilityTables tables = shop_abilities();
    tables.abilityRows = data_rows();
    std::vector<gamedata::StatusRow> statuses = tables.statusEffects;
    for (gamedata::StatusRow& row : data_statuses()) {
        // The row of the data in the place of a made-up row of the shop with the same id.
        statuses.erase(std::remove_if(statuses.begin(), statuses.end(), [&](const gamedata::StatusRow& old) { return old.id == row.id; }), statuses.end());
        statuses.push_back(std::move(row));
    }
    tables.statusEffects = numbered(std::move(statuses));
    for (gamedata::SpellRow& spell : data_spells()) {
        tables.spells.push_back(std::move(spell));
    }
    tables.spells[0].cells.texts["DamageTableRow"] = "Warhammer_Damage_Shockwave";
    tables.spells[1].cells.texts["DamageTableRow"] = "Warhammer_Damage_Shockwave_L2";
    // The rule of an action (the action list of the war hammer of the data): the spell of Shockwave by Meteoric Crater.
    tables.actionSpells = {{"playeractions_ability_warhammer", "Ability_WarHammer_Shockwave", "WarHammer_Spell_Shockwave", {},
                            {"Upgrade_WarHammer_Shockwave_SizeIncrease"}},
                           {"playeractions_ability_warhammer", "Ability_WarHammer_Shockwave", "WarHammer_Spell_Shockwave_L2",
                            {"Upgrade_WarHammer_Shockwave_SizeIncrease"}, {}}};
    tables.actionNodes = {{"playeractions_ability_warhammer", {"Upgrade_WarHammer_Shockwave_SizeIncrease"}},
                          {"playeractions_ability_greataxe", {"Upgrade_Greataxe_Maelstorm_Rng", "Passive_Greataxe_Mauler_DmgWhenFoesNear"}}};
    return tables;
}

/** The combat tables of the cases: the shop tables with the critical values of the weapons (the values of 2HAxeT2 and 2HhammerT2). */
inline gamedata::CombatData crit_combat() {
    gamedata::CombatData data = shop_combat();
    data.damageRows.push_back({"GreatAxe_Executioner", "Ability", 1, 2.0F, 0.0F, 0.0F, 0.0F});
    data.damageRows.push_back({"1H_Sword_Attack_Charge", "Ability", 1, 1.0F, 0.0F, 0.0F, 0.0F});
    // A made-up row with a critical coefficient of its own (DmgCoefCrit), and one with NoBackstab and NoHeadshot.
    data.damageRows.push_back({"Test_CritRow", "Light", 1, 1.0F, 0.0F, 0.0F, 0.0F});
    data.damageRows.back().critFactor = 1.5F;
    data.damageRows.push_back({"Test_NoBackstab", "Light", 1, 1.0F, 0.0F, 0.0F, 0.0F});
    data.damageRows.back().noBackstab = true;
    data.damageRows.back().noHeadshot = true;
    for (gamedata::DamageRow& row : data.damageRows) {
        row.headFactor = 1.3F;
    }
    data.weapons[0].critical = true; // TestAxeRow: 2HAxeT2
    data.weapons[0].critChance = 0.07F;
    data.weapons[0].critMultiplier = 1.35F;
    data.weapons[1].critical = true; // TestHammerRow: 2HhammerT2
    data.weapons[1].critChance = 0.06F;
    data.weapons[1].critMultiplier = 1.2F;
    gamedata::WeaponRow sword = data.weapons[0];
    sword.id = "TestSwordRow";
    sword.mastery = "SwordAbilityTable";
    data.weapons.push_back(sword);
    data.items["testsword"] = static_cast<std::uint32_t>(data.weapons.size() - 1);
    return data;
}

/** The progression of the cases: the shop progression with each row above as a node of its tree, and the sword tree. */
inline gamedata::ProgressionData passive_progression() {
    gamedata::ProgressionData data = shop_progression();
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank < 20; ++rank) {
        ranks.experience.push_back(10);
        ranks.resetCost.push_back(0);
    }
    if (std::none_of(data.progressions.begin(), data.progressions.end(), [](const gamedata::ProgressionRow& row) { return row.id == "SwordAbilityTable"; })) {
        data.progressions.push_back({"SwordAbilityTable", 0, gamedata::ProgressionKind::weapon, 19, ranks});
    }
    std::uint32_t column = 10;
    for (const gamedata::AbilityRow& row : data_rows()) {
        if (std::any_of(data.nodes.begin(), data.nodes.end(), [&](const gamedata::AbilityNode& node) { return node.id == row.id; })) {
            continue;
        }
        gamedata::AbilityNode node;
        node.id = row.id;
        std::string text = row.cells.text("WeaponTag").empty() ? row.id : std::string(row.cells.text("WeaponTag"));
        std::transform(text.begin(), text.end(), text.begin(), [](char letter) { return static_cast<char>(std::tolower(letter)); });
        node.table = text.find("sword") != std::string::npos ? kSword : text.find("hammer") != std::string::npos ? kWarHammer : kGreatAxe;
        node.column = column++;
        node.active = row.id.rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    return data;
}

/** The wolf with the angles of its DamageReceiver and the head volume of the slice of the Grey Wolf (game data). */
inline gamedata::CreatureSlice crit_wolf() {
    gamedata::CreatureSlice slice = wolf_slice();
    slice.capsule = gamedata::CreatureCapsule{0.7F, 0.9F, {0.0F, 1.0F, 0.0F}, {0.0F, 0.1F, 0.0F}};
    slice.behind = gamedata::CreatureBehind{2.0944F, 2.3562F};
    slice.head = gamedata::CreatureHead{{0.0F, 0.5915F, 1.0205F}, {0.0F, 0.9056F, 1.2682F}, 0.2F};
    return slice;
}

/** The world of a case of this stage. */
struct PassiveShop : Shop {
    explicit PassiveShop(bool observe = false, bool rules = true) : Shop(true, false, observe) {
        if (!host.combat.set(crit_combat())) {
            throw std::runtime_error("passive test: the combat tables are not valid: " + host.combat.error());
        }
        gamedata::AbilityTables tables = passive_tables();
        if (!rules) {
            tables.abilityRows.clear();
            tables.actionSpells.clear();
            tables.actionNodes.clear();
        }
        if (!host.abilities.set(tables)) {
            throw std::runtime_error("passive test: the ability data is not valid: " + host.abilities.error());
        }
        host.progression.set(passive_progression());
        if (!host.creatures.set_full({crit_wolf()}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}})) {
            throw std::runtime_error("passive test: the creature data is not valid");
        }
    }
    /** Gives A the node of a tree (no point rule: a fixture). */
    void learn(const char* node, world::Entity* who = nullptr) {
        world::Entity& own = who != nullptr ? *who : *character();
        const gamedata::AbilityNode* row = host.progression.node(node);
        if (row == nullptr) {
            throw std::runtime_error(std::string("passive test: no node ") + node);
        }
        own.skills.pools[row->table][gamedata::progression_id(node)].level = 1;
    }
    /** Puts an item into the first weapon slot of A and selects it. */
    void hold(const char* item) {
        if (!wear(item, world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error(std::string("passive test: the item ") + item);
        }
        character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
        world().gear().refresh(*character());
    }
    /** One hit of A on a creature with a row, with the facts of the hit (its kind, its attack, a cast, a head contact, a source). */
    world::HitResult hit(world::Entity& target, const char* row, const char* item, world::HitKind kind = world::HitKind::contact,
                         std::uint64_t attack = 0, const char* ability = "", bool head = false, const state::WorldPosition* source = nullptr,
                         world::Entity* who = nullptr) {
        world::Entity& own = who != nullptr ? *who : *character();
        world::HitWeapon use{item, world::Combat::weapon_slot_in_hand(own).value_or(0), host.combat.weapon_of(item), 100, ability};
        use.kind = kind;
        use.attack = attack;
        use.head = head;
        return host.world.combat().strike(own, target, use, *host.combat.row_index(row), 1.0F, "a test hit", nullptr, source);
    }
    const world::ActiveStatus* find(const char* id, world::Entity* who = nullptr) {
        const world::Entity& entity = who != nullptr ? *who : *character();
        for (const world::ActiveStatus& status : entity.statuses) {
            if (status.status == id) {
                return &status;
            }
        }
        return nullptr;
    }
    bool creature_has(const world::Entity& creature, const char* id) {
        const auto now = host.clock.now();
        return std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                           [&](const world::CreatureEffect& effect) { return effect.status == id && now < effect.ends; });
    }
    world::StatusVerdict give(const char* id, float seconds, unsigned count = 1, world::Entity* who = nullptr) {
        world::StatusApply how;
        how.seconds = seconds;
        how.source = world::StatusSource{world::StatusSourceKind::command, "test", 0, 0};
        world::StatusVerdict verdict = world::StatusVerdict::unknown;
        for (unsigned index = 0; index < count; ++index) {
            verdict = world().statuses().apply(who != nullptr ? *who : *character(), id, how);
        }
        return verdict;
    }
};

} // namespace soulwarden::backend::tests::passive

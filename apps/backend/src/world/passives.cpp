#include "world/passives.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/data_rules.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using data_rules::compare;
using data_rules::list_has;
using data_rules::lower;
using data_rules::parts_of;
using data_rules::percent_of;
using data_rules::same_text;

/** The events that the backend has a source for (stage 3.3b-S5; OnHealed: stage 3.3b-S6, a heal that the character gives). */
constexpr const char* kEvents[] = {"OnHit", "OnCrit", "OnKill", "OnHitTaken", "OnHeadShot", "OnHitBehind", "OnHealed",
                                   "OnHitTakenWhileInvulnerable",
                                   // Stage B3: a change of the weapon slot with the key of a weapon (Archer's Speed; Passives::weapon_changed).
                                   "OnWeaponSwap",
                                   // Stage B4: the use of a consumable (the Toast perks of the earring; Passives::consumable_used).
                                   "OnUsedConsumable"};

/**
 * The columns of a rule that are marks of the data, not a condition or an effect (the place of a node, the stack rule of a
 * perk, a sound, the time window of InAction and InSequence: the backend takes the hit of the cast as the action).
 */
const std::set<std::string, std::less<>> kMarks = {
    "WeaponTag", "RequiredAbilityId", "UnlockDefault", "CanBeUnapplied", "IsStackableAbility", "IsStackableMax",
    "IsStackableDiminishingModifier", "StatusEffectDamageTableIdForRowOverride", "StatusEffectDamageTableRowOverride",
    "DoNotUnequipSelfAppliedSE", "IgnoreDisabledAttackTypes", "InSequenceTime", "InActionTime", "Sound", "CooldownId",
    "ActivationCooldown", "SelfApplyStatusEffectDurations", "OtherApplyStatusEffectDurations",
    // Marks of the user interface and of the scope of a rule (the evaluator takes the rules of the tree of the weapon of a hit, and
    // of the weapon in the hand for a rule with no hit): no condition of a hit.
    "IsGlobalAbility", "EquipWhenUnsheathed", "ExcludeFromGameModes", "SupportsSmartcast", "UISpellForManaCheck",
    // The durability cost of a spell of a rule (stage 3.3b-M2: CastSpell of Power Cleaner): the backend has no durability use.
    "DisableCastSpellDurability",
    // Stage B1: the hit count of a volley does not end at a death (Rapid Accuracy; the backend counts the arrows of one cast on one target).
    "IgnoreResetConsecutiveOnDeath",
    // Stage B2: the potency of a rule is not multiplied by the stacks of the status of the target (the bolt of the Boltcaster). The backend
    // multiplies no rule by such stacks: the mark changes nothing.
    "DisableScalePerTargetSEStack",
    // Stage B3: the time window of a rule after an action (AfterAction, AfterSequence: Evasive Tactics 2, Closing In -1 with no action). The
    // backend applies a rule with no event once at the end of the dodge, and a row with no action has no window. The client getter of the time
    // factors (0x68308E0) does not read StatusEffectDurationCats: the rule matches a status by StatusEffect or StatusEffectCategories.
    "Duration", "StatusEffectDurationCats"};

/** The columns of a value of a rule (a fixed stat, or a part of a hit). */
const std::set<std::string, std::less<>> kValues = {
    "BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage", "HitFromBehindDamage", "PhysicalArmor",
    "ElementalArmor", "MaxHealth", "MaxHealthMod", "BaseDamageReduction", "MODStrength", "MODDexterity", "MODIntelligence",
    "MODFocus", "MODConstitution",
    // Stage B4: the received heal parts of a fixed row (Gear::values).
    "HealMod", "ConsumableHealMod", "NonConsumableHealMod", "DmgPctToHealthHealMod"};

/** The conditions that the evaluator has (Passives::conditions_pass, counts_pass). */
const std::set<std::string, std::less<>> kConditions = {
    "AttackType", "DamageTableRow", "RemoteDamageTableRow", "TargetStatusEffectCategory", "TargetStatusEffectComparison",
    "TargetHealthPercent", "TargetComparisonType", "MyHealthPercent", "MyComparisonType", "MyStaminaPercent",
    "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType", "HasGritActive", "TargetHasGritActive",
    "AttackerVitalsCategory", "TargetVitalsCategory", "DontHaveStatusEffect", "InSequence", "InAction", "RequireReaction",
    "TrackHitCount", "NumberOfTrackedHits", "NumberOfHitsComparisonType", "NumSuccessfulHits", "NumConsecutiveHits",
    "MaxConsecutiveHits", "ResetConsecutiveOnSuccess", "DamageIsMelee", "DamageIsRanged", "NumAroundMe", "MaxNumAroundMe",
    "NumAroundComparisonType", "EquipLoadCategory",
    // Stage B2: the damage types of the hit, a status of the target with its stacks (each source, or the attacker's with
    // CheckStatusEffectsOnTargetOwned: client 0x67A18F0), a status of the attacker with its stacks (client 0x67A0130), and the node of a
    // helper row.
    "DamageTypes", "TargetStatusEffect", "TargetStatusEffectStackSize", "CheckStatusEffectsOnTargetOwned", "StatusEffect",
    "StatusEffectStackSize", "StatusEffectComparison", "RequiredEquippedAbilityId",
    // Stage B3: a status of a category on the character (client self test 0x67A0130: Speedy, +0.1 BaseDamage while hasted), and the end of an
    // action (AfterAction, AfterSequence: only the end of a dodge, Passives::dodge_ended; a hit does not pass such a row).
    "StatusEffectCategories", "AfterAction", "AfterSequence",
    // Stage B4: the distance from the attacker to the target (Long Range: DistFromDefender 10 with GreaterThanOrEqual; client hit test 0x67A18F0,
    // the loader squares the value: metres), and the item of a consumable rule (IsConsumableIds, IsNotConsumableIds; Passives::consumable_used).
    "DistFromDefender", "DistComparisonType", "IsConsumableIds", "IsNotConsumableIds"};

/** The effects that the evaluator has (before the damage, after the hit, or a fixed stat through the gear values). */
const std::set<std::string, std::less<>> kEffects = {
    "HealScalingValueMultiplier",
    "BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage", "HitFromBehindDamage", "SelfApplyStatusEffect",
    "OtherApplyStatusEffect", "DmgPctToHealth", "CooldownTimer", "CDRImmediatelyOptions", "PhysicalArmor", "ElementalArmor", "CastSpell",
    "MaxHealth", "MaxHealthMod", "BaseDamageReduction", "MODStrength", "MODDexterity", "MODIntelligence", "MODFocus",
    "MODConstitution",
    // Stage 3.4f-B: a stamina gain of a hit rule (the runeglass rows Runeglass_Just_Stamina_*: OnHit, RequireReaction, 1 or 4 stamina).
    "Stamina",
    // Stage B1: the damage of the status of a rule is the damage of a row (Flame Arrow: Common_DamageOverTime_Override), and the row of the
    // shots of an ability for a node (Blood Soaked Arrow: the plan of Penetrating Shot applies it, Abilities::resolve_volley).
    "ForceStatusEffectDamageTableRow", "DamageTableRowOverride",
    // Stage B2: the end of statuses of the target (RemoveTargetStatusEffectsList: the charges of the Boltcaster) and of the attacker
    // (RemoveStatusEffects: a list, or AbilitySpecific with the list of StatusEffectsList: the tracker of Shirking Shock II).
    "RemoveTargetStatusEffectsList", "RemoveStatusEffects", "StatusEffectsList",
    // Stage B3: the time factor of a status on the character (Speedy, Alacrity II; Passives::duration_factor).
    "StatusEffectDurationReduction",
    // Stage B4: the outgoing time factor (a status that the character gives a different entity: Battle Precision, Dex_Bonus_150_1, the part "haste
    // you apply" of Alacrity II; Passives::outgoing_factor), and the received heal parts of a fixed row (Alchemist Reprieve, The Thirst: Gear::values).
    "TargetStatusEffectDurationCats", "TargetStatusEffectDurationMult", "HealMod", "ConsumableHealMod", "NonConsumableHealMod",
    "DmgPctToHealthHealMod"};

/**
 * Stage B3: the conditions that a rule with no hit has a source for (a swap, the end of a dodge, a time factor): the values of the character,
 * grit (none: such a row does not pass), a status that it must not have, its load class, and its statuses (StatusEffect, StatusEffectCategories).
 */
const std::set<std::string, std::less<>> kSelfConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "EquipLoadCategory", "StatusEffect", "StatusEffectStackSize", "StatusEffectComparison",
    "StatusEffectCategories"};

/** Stage B3: the effects that a rule with no hit has (a swap, the end of a dodge): a status on the character, a cooldown change, an end of statuses. */
const std::set<std::string, std::less<>> kSelfEffects = {"SelfApplyStatusEffect", "CooldownTimer", "CDRImmediatelyOptions",
                                                         "RemoveStatusEffects", "StatusEffectsList"};

/**
 * Stage B3: the columns of the outgoing time factor (a status that the character gives a different entity: Alacrity II "haste you apply",
 * Battle Precision). The backend does not apply them; a time factor row with them is applied for its own part.
 */
const std::set<std::string, std::less<>> kOutgoingDuration = {"TargetStatusEffectDurationCats", "TargetStatusEffectDurationMult"};

/**
 * The conditions that a hit taken has a source for (code review of stage 3.3b-S5, finding F6): the values of the character (its
 * health after the hit, its stamina, its mana, its statuses), grit (none: such a row does not pass), the damage of the hit
 * (RequireReaction), and the hit counts. A rule with OnHitTaken and a different condition (the attacker, the row, the target) is
 * not applied: Passives::describe_row names the column.
 */
const std::set<std::string, std::less<>> kTakenConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "RequireReaction", "TrackHitCount", "NumberOfTrackedHits", "NumberOfHitsComparisonType",
    "NumSuccessfulHits", "NumConsecutiveHits", "MaxConsecutiveHits", "ResetConsecutiveOnSuccess",
    // Stage B4: the attack type of the damage row of the creature attack (game data: AttackType Light, Heavy, Magic, or empty of each creature
    // damage row; research bow-build-2.md Q5; the creature data tool exports it).
    "AttackType"};

/**
 * The conditions that a hit in the immune window of a dodge has a source for (stage 3.4f-B, OnHitTakenWhileInvulnerable): the values of
 * the character, its statuses, grit (none), the damage that the hit would do (RequireReaction), the load class of the character
 * (EquipLoadCategory), the dodge as its action (InAction Dodge) and the fragment of the dodge as its sequence (InSequence), and the
 * category of the attacker and of the target (a creature hit: AttackerVitalsCategory Player does not pass).
 */
const std::set<std::string, std::less<>> kEvadedConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "RequireReaction", "EquipLoadCategory", "InAction", "InSequence", "AttackerVitalsCategory",
    "TargetVitalsCategory"};

/** @return The condition columns of a rule that a hit in the immune window of a dodge has no source for (stage 3.4f-B). */
std::vector<std::string> evaded_unsourced(const gamedata::CellValues& cells) {
    std::vector<std::string> columns;
    const auto check = [&](const std::string& column) {
        if (kConditions.count(column) != 0 && kEvadedConditions.count(column) == 0) {
            columns.push_back(column);
        }
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return columns;
}

/** @return The name of the load class of a character in the data (EquipLoadCategory: Fast, Normal, Slow, Overburdened). */
std::string_view load_category(const Entity& character) {
    constexpr std::string_view kNames[] = {"Fast", "Normal", "Slow", "Overburdened"};
    return kNames[std::min<std::size_t>(character.gear.loadClass, 3)];
}

/**
 * The conditions that a heal has a source for (stage 3.3b-S6, a rule with OnHealed and HealScalingValueMultiplier): the health of the
 * target before the heal (TargetHealthPercent: Divine Blessing), the values of the healer, the row of the spell of the heal
 * (DamageTableRow), the categories of the statuses of the target, a status that the healer must not have, and grit (none).
 */
const std::set<std::string, std::less<>> kHealConditions = {
    "TargetHealthPercent", "TargetComparisonType", "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType",
    "MyManaPercent", "MyManaComparisonType", "DamageTableRow", "TargetStatusEffectCategory", "TargetStatusEffectComparison",
    "DontHaveStatusEffect", "HasGritActive"};

/** @return The condition columns of a rule that a heal has no source for (stage 3.3b-S6). */
std::vector<std::string> heal_unsourced(const gamedata::CellValues& cells) {
    std::vector<std::string> columns;
    const auto check = [&](const std::string& column) {
        if (kConditions.count(column) != 0 && kHealConditions.count(column) == 0) {
            columns.push_back(column);
        }
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return columns;
}

/** @return The effect columns of a rule with OnHealed other than HealScalingValueMultiplier: effects after a heal (not in the backend). */
std::vector<std::string> heal_after_effects(const gamedata::CellValues& cells) {
    std::vector<std::string> columns;
    for (const auto& [column, value] : cells.numbers) {
        if (kEffects.count(column) != 0 && column != "HealScalingValueMultiplier" && value != 0.0F) {
            columns.push_back(column);
        }
    }
    for (const auto& [column, value] : cells.texts) {
        if (kEffects.count(column) != 0 && !value.empty()) {
            columns.push_back(column);
        }
    }
    return columns;
}

/** @return The condition columns of a rule that a hit taken has no source for (finding F6). */
std::vector<std::string> taken_unsourced(const gamedata::CellValues& cells) {
    std::vector<std::string> columns;
    const auto check = [&](const std::string& column) {
        if (kConditions.count(column) != 0 && kTakenConditions.count(column) == 0) {
            columns.push_back(column);
        }
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return columns;
}

/** Stage B3: @return The condition columns of a rule with no hit (a swap, the end of a dodge) that have no source (`after`: AfterAction too). */
std::vector<std::string> self_unsourced(const gamedata::CellValues& cells, bool after = false) {
    std::vector<std::string> columns;
    const auto check = [&](const std::string& column) {
        const bool afterColumn = column == "AfterAction" || column == "AfterSequence";
        if (kConditions.count(column) != 0 && kSelfConditions.count(column) == 0 && !(after && afterColumn)) {
            columns.push_back(column);
        }
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return columns;
}

/** Stage B3: @return The effect columns of a rule with no hit that the backend does not apply for it (a value, a heal, a spell, a target status). */
std::vector<std::string> self_unsupported(const gamedata::CellValues& cells) {
    std::vector<std::string> columns;
    for (const auto& [column, value] : cells.numbers) {
        if ((kEffects.count(column) != 0 || column.rfind("DMG", 0) == 0 || column.rfind("ABS", 0) == 0) && kSelfEffects.count(column) == 0
            && column != "StatusEffectDurationReduction" && value != 0.0F) {
            columns.push_back(column);
        }
    }
    for (const auto& [column, value] : cells.texts) {
        if (kEffects.count(column) != 0 && kSelfEffects.count(column) == 0 && !value.empty()) {
            columns.push_back(column);
        }
    }
    return columns;
}

/** Stage B3: @return True when a rule with no hit has an effect that the backend applies for it (a status, a cooldown change, an end of statuses). */
bool self_effect(const gamedata::CellValues& cells) {
    return !cells.text("SelfApplyStatusEffect").empty() || !cells.text("RemoveStatusEffects").empty()
           || (cells.number("CooldownTimer") < 0.0F && !cells.text("CDRImmediatelyOptions").empty());
}

/** Stage B3: @return True when a row has an event of each kind (a column On<event> with a value; also an event that the backend does not have). */
bool any_event(const gamedata::CellValues& cells) {
    return std::any_of(cells.numbers.begin(), cells.numbers.end(),
                       [](const auto& cell) { return cell.first.size() > 2 && cell.first[0] == 'O' && cell.first[1] == 'n' && cell.second != 0.0F; })
           || std::any_of(cells.texts.begin(), cells.texts.end(),
                          [](const auto& cell) { return cell.first.size() > 2 && cell.first[0] == 'O' && cell.first[1] == 'n' && !cell.second.empty(); });
}

/**
 * Stage B3: @return True for a rule of the time of a status on the character (client getter 0x68308E0): no event, StatusEffectDurationReduction,
 *         no column that the evaluator does not have but the outgoing time factor (`unknown`), and only conditions of a rule with no hit.
 */
bool duration_rule(const gamedata::CellValues& cells, const std::vector<std::string>& unknown) {
    return !any_event(cells) && cells.number("StatusEffectDurationReduction") != 0.0F
           && std::all_of(unknown.begin(), unknown.end(), [](const std::string& column) { return kOutgoingDuration.count(column) != 0; })
           && self_unsourced(cells).empty() && !cells.has("AfterAction") && !cells.has("AfterSequence");
}

/** Stage B4: the conditions of a target with no event that make a row a rule of each hit (Finishing Shot: TargetHealthPercent 50, BaseDamage 0.15). */
const std::set<std::string, std::less<>> kTargetConditions = {"TargetHealthPercent", "TargetStatusEffect", "TargetStatusEffectCategory", "DistFromDefender"};

/** Stage B4: the conditions of an outgoing time factor: the values of the character and the statuses of the target (Battle Precision). */
const std::set<std::string, std::less<>> kOutgoingConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "EquipLoadCategory", "StatusEffect", "StatusEffectStackSize", "StatusEffectComparison",
    "StatusEffectCategories", "TargetStatusEffectCategory", "TargetStatusEffectComparison", "TargetStatusEffect", "TargetStatusEffectStackSize",
    "CheckStatusEffectsOnTargetOwned", "RequireReaction"};

/** Stage B4: the conditions of a rule of the use of a consumable (Passives::consumable_used). */
const std::set<std::string, std::less<>> kConsumableConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "EquipLoadCategory", "StatusEffect", "StatusEffectStackSize", "StatusEffectComparison",
    "StatusEffectCategories", "IsConsumableIds", "IsNotConsumableIds"};

/** @return The condition columns of a row that are not in a set of conditions with a source (`allowed`). */
std::vector<std::string> conditions_outside(const gamedata::CellValues& cells, const std::set<std::string, std::less<>>& allowed) {
    std::vector<std::string> columns;
    const auto check = [&](const std::string& column) {
        if (kConditions.count(column) != 0 && allowed.count(column) == 0) {
            columns.push_back(column);
        }
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return columns;
}

/**
 * Stage B4: the perk scripts of the client (function 0x238DEF0, called from the perk tick 0x238B500; research bow-build-2.md Q6): the statuses of
 * Vital Synergy while the stamina is below half its maximum (HealMod, "Anointed") and while the health is below half (StaminaRate). The rows of the
 * perk in the data are empty: the client has the rule in code (CRC-32 values of the lower-case ids). Code constants of the supported client build.
 */
struct PerkScript {
    const char* ability;
    const char* lowStamina;
    const char* lowHealth;
};
constexpr PerkScript kPerkScripts[] = {
    {"Global_Chest_HealthLowStamRegen", "Status_Perk_Chest_StamLowHealthRegen", "Status_Perk_Chest_HealthLowStamRegen"},
    {"Ability_Perk_Chest_HealthLowStamRegen", "Status_ItemPerk_Chest_StamLowHealthRegen", "Status_ItemPerk_Chest_HealthLowStamRegen"}};
/** The limit of the two tests of the script (0.5 of the maximum; client code 0x238E2E8 to 0x238E4A3). */
constexpr float kPerkScriptLimit = 0.5F;
/** The rows of the perk of Vital Synergy that the script reads (Global_Chest_StamLowHealthRegen: the second empty row of the perk). */
bool perk_script_row(std::string_view id) {
    for (const PerkScript& script : kPerkScripts) {
        if (same_text(id, script.ability)) {
            return true;
        }
    }
    return same_text(id, "Global_Chest_StamLowHealthRegen") || same_text(id, "Ability_Perk_Chest_StamLowHealthRegen");
}

/** Stage B3: the values that a rule gives to a hit before its damage (Passives::before_hit). */
const std::set<std::string, std::less<>> kHitValues = {"BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage",
                                                       "HitFromBehindDamage"};

/** The conditions of a hit: a row with no event and one of them is a rule of that hit (Crowded Well: the row of the burst). */
constexpr const char* kHitConditions[] = {"DamageTableRow", "AttackType", "RemoteDamageTableRow"};

bool is_event(std::string_view column) {
    return column.size() > 2 && column[0] == 'O' && column[1] == 'n';
}

bool value_column(std::string_view column) {
    return column.rfind("DMG", 0) == 0 || column.rfind("ABS", 0) == 0;
}

bool hit_rule(const gamedata::CellValues& cells) {
    return std::any_of(std::begin(kHitConditions), std::end(kHitConditions), [&](const char* column) { return cells.has(column); });
}

/**
 * @return True for a row with no event that names its ability (InAction, InSequence) and has a value and no status (stage 3.3b-M2: the row
 *         of Armor Breaker, ArmorPenetration 0.35; the game text "ignores 35% armor"): its values count at each hit of its ability. A row
 *         with a status and no event (Shockwave: its stun) stays a rule of its action (the plan gives the status).
 */
bool ability_value_rule(const gamedata::CellValues& cells) {
    return (cells.has("InAction") || cells.has("InSequence")) && cells.text("SelfApplyStatusEffect").empty()
           && cells.text("OtherApplyStatusEffect").empty()
           && std::any_of(kValues.begin(), kValues.end(), [&](const std::string& column) { return cells.number(column) != 0.0F; });
}

/** @return True when the row has a cell of a condition (no fixed stat then). */
bool has_condition(const gamedata::CellValues& cells);

/**
 * Stage B4: @return True for a rule of each hit with a condition of the target and no event (Finishing Shot, Passive_Bow_Hunter_DmgVsLow:
 *         TargetHealthPercent 50, BaseDamage 0.15; the form of a hit rule of the client hit test 0x67A18F0, which tests the conditions of the
 *         target with no event): no event, a condition of kTargetConditions, only values of a hit, no status, no other effect.
 */
bool target_value_rule(const gamedata::CellValues& cells) {
    if (any_event(cells) || hit_rule(cells)
        || std::none_of(kTargetConditions.begin(), kTargetConditions.end(), [&](const std::string& column) { return cells.has(column); })) {
        return false;
    }
    bool value = false;
    for (const auto& [column, number] : cells.numbers) {
        if (number == 0.0F) {
            continue;
        }
        if (kHitValues.count(column) != 0) {
            value = true;
        } else if (kEffects.count(column) != 0 || kValues.count(column) != 0 || value_column(column)) {
            return false;
        }
    }
    for (const auto& [column, text] : cells.texts) {
        if (kEffects.count(column) != 0 && !text.empty()) {
            return false;
        }
    }
    return value;
}

/**
 * Stage B4: @return True for a fixed cooldown rule (Refreshing, Global_Common_CDR: CooldownTimer -0.02, CDRImmediatelyOptions ActiveWeapon,
 *         IsStackableMax 3; the client tooltip of an ability shows the cooldown with it, live check L1): no event, a negative CooldownTimer, no
 *         action window, no condition of a hit or a target, and a scope of CDRImmediatelyOptions that the backend has (none, ActiveWeapon, All,
 *         AbilitySpecific and AllExcept with CooldownId).
 */
bool fixed_cooldown_rule(const gamedata::CellValues& cells) {
    if (any_event(cells) || !(cells.number("CooldownTimer") < 0.0F) || cells.has("AfterAction") || cells.has("AfterSequence") || hit_rule(cells)
        || cells.has("InAction") || cells.has("InSequence") || !cells.text("SelfApplyStatusEffect").empty()
        || !cells.text("OtherApplyStatusEffect").empty()) {
        return false;
    }
    const std::string_view options = cells.text("CDRImmediatelyOptions");
    return options.empty() || same_text(options, "ActiveWeapon") || same_text(options, "All") || same_text(options, "AbilitySpecific")
           || same_text(options, "AllExcept");
}

/** Stage B4: @return True for an outgoing time factor (TargetStatusEffectDurationMult with TargetStatusEffectDurationCats, no event). */
bool outgoing_rule(const gamedata::CellValues& cells) {
    return !any_event(cells) && cells.number("TargetStatusEffectDurationMult") != 0.0F && !cells.text("TargetStatusEffectDurationCats").empty();
}

bool has_condition(const gamedata::CellValues& cells) {
    for (const auto& [column, value] : cells.numbers) {
        if (kConditions.count(column) != 0) {
            return true;
        }
    }
    for (const auto& [column, value] : cells.texts) {
        if (kConditions.count(column) != 0) {
            return true;
        }
    }
    return false;
}

/**
 * @return True when the tracking spell of a rule is a sphere (ShapeTypes Sphere or blank). The data has three tracking spells with
 * the shape Box and a Length (Blood Lust Ultimate_Greataxe_Reaper: 7 m long, the radius 1.25 m). Stage 3.3b-S5 did not apply them
 * (code review finding F9); stage 3.3b-M2 counts a box in front of the look (tracking_supported).
 */
bool sphere_tracking(const gamedata::SpellRow& spell) {
    const std::string_view shape = spell.cells.text("ShapeTypes");
    return shape.empty() || same_text(shape, "Sphere");
}

/**
 * @return True when the backend counts the entities of a tracking spell: a sphere, or (stage 3.3b-M2) a box with a Length in front of the
 *         look of the character (Blood Lust, Ultimate_Greataxe_Reaper: the game text "when looking at a foe within 14 m": the box from the
 *         character to two times its Length, the Radius at each side, the Height up and down; a decision of an agent from the text).
 */
bool tracking_supported(const gamedata::SpellRow& spell) {
    return sphere_tracking(spell) || (same_text(spell.cells.text("ShapeTypes"), "Box") && spell.cells.number("Length") > 0.0F);
}

/**
 * @return True when the condition of a status action of the data holds for a character (stage 3.3b-M2, Guarded Sprint): a node literal
 *         is a learned node; a tag literal is a tag of the weapon in the hand (its MannequinTag), a tag of the movement (ForwardNav,
 *         BackNav, LeftNav, RightNav: the character moves, Entity::moving), the tag Dodging (stage 3.4f-B: a dodge of the stamina system
 *         plays, from its start to its mark CanTransition, `dodging`), or a different tag of the weapon.
 */
bool action_holds(const gamedata::ActionStatus& action, const Entity& character, const gamedata::WeaponRow* weapon, bool dodging) {
    const auto literal_true = [&](const gamedata::ActionStatus::Literal& literal) {
        if (!literal.tag) {
            for (const auto& [pool, nodes] : character.skills.pools) {
                const auto found = nodes.find(literal.id);
                if (found != nodes.end() && found->second.level != 0) {
                    return true;
                }
            }
            return false;
        }
        for (const char* movement : {"ForwardNav", "BackNav", "LeftNav", "RightNav"}) {
            if (same_text(literal.name, movement)) {
                return character.moving;
            }
        }
        if (same_text(literal.name, "Dodging")) {
            return dodging;
        }
        return weapon != nullptr && list_has(weapon->tags, literal.name);
    };
    return std::any_of(action.terms.begin(), action.terms.end(), [&](const std::vector<gamedata::ActionStatus::Literal>& term) {
        return std::all_of(term.begin(), term.end(), [&](const gamedata::ActionStatus::Literal& literal) { return literal_true(literal) != literal.negated; });
    });
}

std::chrono::milliseconds milliseconds_of(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(seconds * 1000.0F)));
}

} // namespace

std::string Passives::event_of(const gamedata::CellValues& cells) {
    for (const char* event : kEvents) {
        if (cells.number(event) != 0.0F) {
            return event;
        }
    }
    return {};
}

bool Passives::fixed(const gamedata::CellValues& cells) {
    return event_of(cells).empty() && !has_condition(cells) && unknown_columns(cells).empty();
}

bool Passives::conditional_value(const gamedata::CellValues& cells) {
    if (any_event(cells) || hit_rule(cells) || (!cells.has("StatusEffect") && !cells.has("StatusEffectCategories")) || !unknown_columns(cells).empty()
        || !self_unsourced(cells).empty()) {
        return false;
    }
    // Only values of a hit (no armor, no health, no attribute, no status, no cooldown change): the gear values cannot hold a condition.
    bool value = false;
    for (const auto& [column, number] : cells.numbers) {
        if (number == 0.0F) {
            continue;
        }
        if (kHitValues.count(column) != 0) {
            value = true;
        } else if (kValues.count(column) != 0 || kEffects.count(column) != 0 || value_column(column)) {
            return false;
        }
    }
    for (const auto& [column, text] : cells.texts) {
        if (kEffects.count(column) != 0 && !text.empty()) {
            return false;
        }
    }
    return value;
}

std::vector<std::string> Passives::unknown_columns(const gamedata::CellValues& cells) {
    std::vector<std::string> unknown;
    const auto check = [&](const std::string& column) {
        if (kMarks.count(column) != 0 || kConditions.count(column) != 0 || kEffects.count(column) != 0 || value_column(column)) {
            return;
        }
        if (is_event(column)) {
            // OnBlockedHit with OnHit: the OnHit part has its effect (the backend has no block).
            if (column == "OnBlockedHit" && cells.number("OnHit") != 0.0F) {
                return;
            }
            if (std::any_of(std::begin(kEvents), std::end(kEvents), [&](const char* event) { return column == event; })) {
                return;
            }
        }
        unknown.push_back(column);
    };
    for (const auto& [column, value] : cells.numbers) {
        check(column);
    }
    for (const auto& [column, value] : cells.texts) {
        check(column);
    }
    return unknown;
}

std::vector<PassiveRule> Passives::rules_of(const Entity& character, std::uint32_t table) const {
    std::vector<PassiveRule> rules;
    if (character.kind != EntityKind::player) {
        return rules;
    }
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::ProgressionTables* progression = m_world.options().progressionTables;
    if (abilities != nullptr && progression != nullptr && table != 0) {
        if (const auto pool = character.skills.pools.find(table); pool != character.skills.pools.end()) {
            for (const auto& [id, state] : pool->second) {
                const gamedata::AbilityNode* node = state.level > 0 ? progression->node(id) : nullptr;
                const gamedata::AbilityRow* row = node != nullptr ? abilities->ability_row(node->id) : nullptr;
                if (row != nullptr) {
                    rules.push_back(PassiveRule{row->id, &row->cells, 1.0F, node->active ? RuleOrigin::active : RuleOrigin::node});
                }
                // Stage B2: the helper rows of the node (RequiredEquippedAbilityId; the tracker of Surprise Attack), not a node themselves.
                if (node != nullptr) {
                    for (const gamedata::AbilityRow* helper : abilities->helpers_of(node->id)) {
                        if (progression->node(helper->id) == nullptr) {
                            rules.push_back(PassiveRule{helper->id, &helper->cells, 1.0F, RuleOrigin::helper});
                        }
                    }
                }
            }
        }
    }
    // Stage B2: the rows that the statuses of the character equip (EquipAbility of the status row), with the potency of the status, for a hit
    // of each weapon (the tracker of Shirking Shock II equips Ability_Perk_Weapon_DodgeSuccess_LightningDMG while it runs).
    for (const PassiveRule& rule : status_rules(character)) {
        rules.push_back(rule);
    }
    // Stage B4: the threshold abilities of the attributes (game data: Dex_Bonus_25_1 CritChance 0.05 at dexterity 25, Str_Bonus_25_1 +0.03 of a
    // light hit at strength 25, ...), for each weapon.
    for (const PassiveRule& rule : attribute_rules(character)) {
        rules.push_back(rule);
    }
    if (const gamedata::PerkData* perks = m_world.perk_data()) {
        for (const GearPerk& perk : m_world.gear().perks_of(character)) {
            for (const std::string& name : perk.perk->abilities) {
                if (const gamedata::CellValues* cells = perks->ability(name)) {
                    rules.push_back(PassiveRule{name, cells, perk.potency, RuleOrigin::perk});
                }
            }
        }
    }
    return rules;
}

std::vector<PassiveRule> Passives::status_rules(const Entity& character) const {
    std::vector<PassiveRule> rules;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (abilities == nullptr || character.kind != EntityKind::player) {
        return rules;
    }
    const gamedata::PerkData* perks = m_world.perk_data();
    const WorldClock::time_point now = m_world.clock().now();
    for (const ActiveStatus& status : character.statuses) {
        const gamedata::StatusRow* row = status.permanent || now < status.ends ? abilities->status(status.status) : nullptr;
        if (row == nullptr) {
            continue;
        }
        for (const std::string_view part : parts_of(row->cells.text("EquipAbility"))) {
            if (const gamedata::AbilityRow* ability = abilities->ability_row(part)) {
                rules.push_back(PassiveRule{ability->id, &ability->cells, status.potency, RuleOrigin::status});
            } else if (const gamedata::CellValues* cells = perks != nullptr ? perks->ability(part) : nullptr) {
                rules.push_back(PassiveRule{std::string(part), cells, status.potency, RuleOrigin::status});
            }
        }
    }
    return rules;
}

std::vector<PassiveRule> Passives::attribute_rules(const Entity& character, bool nonCombat) const {
    std::vector<PassiveRule> rules;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::ProgressionTables* progression = m_world.options().progressionTables;
    if (abilities == nullptr || progression == nullptr || character.kind != EntityKind::player) {
        return rules;
    }
    // The threshold abilities of the attributes (attributes.abilities of progression-tables.json; the client attribute screen tests the value of
    // the attribute with its bonus against each threshold: attributes.lua of the client, GetAttributeBaseLevel plus GetAttributeBonusLevel).
    static constexpr std::array<std::pair<unsigned, const char*>, attribute::kCount> kNames{
        {{attribute::kStrength, "strength"}, {attribute::kDexterity, "dexterity"}, {attribute::kIntelligence, "intelligence"},
         {attribute::kFocus, "focus"}, {attribute::kConstitution, "constitution"}}};
    const auto& thresholds = progression->data().attributeAbilities;
    for (const auto& [type, name] : kNames) {
        const auto found = thresholds.find(name);
        if (found == thresholds.end()) {
            continue;
        }
        const std::uint32_t value = character.attributes.value(type);
        for (const gamedata::AttributeThreshold& threshold : found->second) {
            if (threshold.value > value) {
                break;
            }
            for (const std::string& id : threshold.abilities) {
                const gamedata::AbilityRow* row = abilities->ability_row(id);
                // A row of the gathering or of the bag (IsNonCombatAbility with OnEquipStatusEffect: Str_Bonus_25_2, EFFMining) is not a rule
                // of the evaluator: the gathering system reads its value (stage 3.6a). The list names it.
                if (row != nullptr && (nonCombat || row->cells.number("IsNonCombatAbility") == 0.0F)) {
                    rules.push_back(PassiveRule{row->id, &row->cells, 1.0F, RuleOrigin::attribute});
                }
            }
        }
    }
    return rules;
}

std::vector<PassiveRule> Passives::merged(std::vector<PassiveRule> rules) {
    // The sources of one row (the perks of each item with the same ability, a row of a node and of a status) give one rule. Client potency
    // 0x67E9A70: the sources count to IsStackableMax (+0x944, 0: no limit), and with IsStackableDiminishingModifier (+0x948) the factor of each
    // next source is that part lower; the count stops at a factor of 0 or less. The largest potencies first, as the gear values (Gear::values).
    std::vector<PassiveRule> result;
    std::vector<std::vector<float>> potencies;
    for (PassiveRule& rule : rules) {
        auto found = std::find_if(result.begin(), result.end(), [&](const PassiveRule& other) { return same_text(other.id, rule.id); });
        if (found == result.end()) {
            potencies.push_back({rule.potency});
            result.push_back(std::move(rule));
        } else {
            potencies[static_cast<std::size_t>(found - result.begin())].push_back(rule.potency);
        }
    }
    for (std::size_t index = 0; index < result.size(); ++index) {
        std::vector<float>& list = potencies[index];
        if (list.size() == 1) {
            continue;
        }
        std::sort(list.rbegin(), list.rend());
        const float most = result[index].cells->number("IsStackableMax");
        const float diminishing = result[index].cells->number("IsStackableDiminishingModifier");
        float sum = 0.0F;
        float factor = 1.0F;
        for (std::size_t source = 0; source < list.size(); ++source) {
            if (most > 0.0F && static_cast<float>(source) >= most) {
                break;
            }
            sum += list[source] * factor;
            if (diminishing > 0.0F) {
                factor -= diminishing;
                if (factor <= 0.0F) {
                    break;
                }
            }
        }
        result[index].potency = sum;
    }
    return result;
}

bool Passives::target_status_pass(const gamedata::CellValues& row, const Entity& attacker, const Entity& target, const HitContext& hit) const {
    const std::string_view named = row.text("TargetStatusEffect");
    const std::string_view categories = row.text("TargetStatusEffectCategory");
    if (named.empty() && categories.empty()) {
        return true;
    }
    // The effects of the target before the hit (the snapshot of Combat::strike), or as it is now (a test, a hit with no strike).
    std::vector<HitContext::TargetEffect> live;
    const std::vector<HitContext::TargetEffect>* effects = &hit.targetEffects;
    if (!hit.targetKnown) {
        const WorldClock::time_point now = m_world.clock().now();
        const gamedata::AbilityData* abilities = m_world.options().abilityData;
        for (const CreatureEffect& effect : target.creature.effects) {
            if (now < effect.ends) {
                const gamedata::StatusRow* data = effect.row != nullptr ? effect.row : abilities != nullptr ? abilities->status(effect.status) : nullptr;
                live.push_back(HitContext::TargetEffect{effect.status, data, effect.stacks, effect.source});
            }
        }
        effects = &live;
    }
    // Client hit test 0x67A18F0: CheckStatusEffectsOnTargetOwned (bit 1 of +0xC4) counts only the statuses of the attacker, else each source
    // (decision D10 of the orchestrator: the charges and the tracker of Surprise Attack belong to the target).
    const bool owned = row.flag("CheckStatusEffectsOnTargetOwned");
    const auto in_categories = [&](const HitContext::TargetEffect& effect) {
        if (effect.row == nullptr || categories.empty()) {
            return false;
        }
        for (const std::string_view category : parts_of(effect.row->cells.text("EffectCategories"))) {
            if (list_has(categories, category)) {
                return true;
            }
        }
        return false;
    };
    std::uint32_t namedStacks = 0;
    std::uint32_t categoryStacks = 0;
    bool hasNamed = false;
    bool hasCategory = false;
    for (const HitContext::TargetEffect& effect : *effects) {
        if (owned && effect.source != attacker.id) {
            continue;
        }
        if (!named.empty() && list_has(named, effect.status)) {
            hasNamed = true;
            namedStacks += std::max<std::uint32_t>(effect.stacks, 1);
        }
        if (in_categories(effect)) {
            hasCategory = true;
            categoryStacks += std::max<std::uint32_t>(effect.stacks, 1);
        }
    }
    const std::string_view comparison = row.text("TargetStatusEffectComparison");
    // TargetStatusEffectStackSize 1 or more: the stacks of TargetStatusEffect, or of the categories when no status is named, with the
    // comparison (0x146624CD0). With no size: the target has the status or a status of a category; NotEqual: it has none.
    if (const float size = row.number("TargetStatusEffectStackSize"); size >= 1.0F) {
        return compare(static_cast<float>(named.empty() ? categoryStacks : namedStacks), size, comparison);
    }
    const bool wanted = lower(comparison) != "notequal";
    if (!named.empty() && hasNamed != wanted) {
        return false;
    }
    if (!categories.empty() && hasCategory != wanted) {
        return false;
    }
    return true;
}

bool Passives::own_status_pass(const gamedata::CellValues& row, const Entity& character, const gamedata::StatusRow* adding) const {
    const std::string_view named = row.text("StatusEffect");
    const std::string_view categories = row.text("StatusEffectCategories");
    if (named.empty() && categories.empty()) {
        return true;
    }
    // Client self test 0x67A0130: with no StatusEffectStackSize the character must have the status (each source); with a size, its stacks
    // with StatusEffectComparison. Stage B3: StatusEffectCategories: the character has a status of a category of the list (each source);
    // StatusEffectComparison NotEqual (the comparison 1 of the client): none.
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const auto in_categories = [&](const gamedata::StatusRow* data) {
        if (data == nullptr) {
            return false;
        }
        for (const std::string_view category : parts_of(data->cells.text("EffectCategories"))) {
            if (list_has(categories, category)) {
                return true;
            }
        }
        return false;
    };
    std::uint32_t stacks = 0;
    bool has = false;
    bool hasCategory = false;
    for (const ActiveStatus& status : character.statuses) {
        if (!status.permanent && now >= status.ends) {
            continue;
        }
        if (!named.empty() && list_has(named, status.status)) {
            has = true;
            stacks += std::max<std::uint32_t>(status.stacks, 1);
        }
        if (!categories.empty() && !hasCategory && abilities != nullptr) {
            hasCategory = in_categories(abilities->status(status.status));
        }
    }
    // A status that comes now counts as present (stage B3, the time factor of a new haste: an agent decision with research bow-build-2.md Q1;
    // the text of Speedy and of Alacrity says that each haste is longer, also the first one).
    if (adding != nullptr) {
        if (!named.empty() && list_has(named, adding->id)) {
            has = true;
            stacks += 1;
        }
        hasCategory = hasCategory || (!categories.empty() && in_categories(adding));
    }
    if (!categories.empty() && hasCategory != (lower(row.text("StatusEffectComparison")) != "notequal")) {
        return false;
    }
    if (named.empty()) {
        return true;
    }
    if (const float size = row.number("StatusEffectStackSize"); size >= 1.0F) {
        return compare(static_cast<float>(stacks), size, row.text("StatusEffectComparison"));
    }
    return has;
}

bool Passives::self_pass(const PassiveRule& rule, const Entity& character, const gamedata::StatusRow* adding) const {
    const gamedata::CellValues& row = *rule.cells;
    if (row.number("HasGritActive") != 0.0F) {
        return false; // The backend has no grit.
    }
    if (row.has("MyHealthPercent")
        && !compare(percent_of(character.vitals.health, character.vitals.healthMax), row.number("MyHealthPercent"), row.text("MyComparisonType"))) {
        return false;
    }
    if (row.has("MyStaminaPercent")
        && !compare(percent_of(character.vitals.stamina, character.vitals.staminaMax), row.number("MyStaminaPercent"),
                    row.text("MyStaminaComparisonType"))) {
        return false;
    }
    if (row.has("MyManaPercent")
        && !compare(percent_of(character.vitals.mana, character.vitals.manaMax), row.number("MyManaPercent"), row.text("MyManaComparisonType"))) {
        return false;
    }
    if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty() && m_world.statuses().has(character, status)) {
        return false;
    }
    if (const std::string_view loads = row.text("EquipLoadCategory"); !loads.empty() && !list_has(loads, load_category(character))) {
        return false;
    }
    return own_status_pass(row, character, adding);
}

std::uint32_t Passives::table_in_hand(const Entity& character) const {
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    return weapon != nullptr && !weapon->mastery.empty() ? gamedata::progression_id(weapon->mastery) : 0;
}

bool Passives::conditions_pass(const PassiveRule& rule, const Entity& attacker, const Entity& target, const HitContext& hit) const {
    const gamedata::CellValues& row = *rule.cells;
    if (!unknown_columns(row).empty()) {
        return false;
    }
    // Stage B3: a rule after the end of an action (AfterAction, AfterSequence: Evasive Tactics) acts only at the end of a dodge
    // (Passives::dodge_ended); a hit has no window after an action in this backend.
    if (row.has("AfterAction") || row.has("AfterSequence")) {
        return false;
    }
    if (const std::string_view types = row.text("AttackType"); !types.empty() && !list_has(types, hit.attackType)) {
        return false;
    }
    if (const std::string_view rows = row.text("DamageTableRow"); !rows.empty() && !list_has(rows, hit.row)) {
        return false;
    }
    if (const std::string_view rows = row.text("RemoteDamageTableRow"); !rows.empty() && !list_has(rows, hit.row)) {
        return false;
    }
    // The statuses of the target (TargetStatusEffect with its stacks, TargetStatusEffectCategory) before the hit (stage B2), and a status of
    // the attacker (StatusEffect, stage B2).
    if (!target_status_pass(row, attacker, target, hit) || !own_status_pass(row, attacker)) {
        return false;
    }
    // DamageTypes (stage B2): the hit has a part of a type of the list (the Boltcaster: 99 percent Lightning; decision K6 of the research).
    if (const std::string_view types = row.text("DamageTypes");
        !types.empty() && std::none_of(hit.types.begin(), hit.types.end(), [&](const std::string& type) { return list_has(types, type); })) {
        return false;
    }
    // RequiredEquippedAbilityId (stage B2): the character has the node of a helper row.
    if (const std::string_view node = row.text("RequiredEquippedAbilityId"); !node.empty()) {
        const std::uint32_t id = gamedata::progression_id(node);
        const bool learned = std::any_of(attacker.skills.pools.begin(), attacker.skills.pools.end(), [&](const auto& pool) {
            const auto found = pool.second.find(id);
            return found != pool.second.end() && found->second.level != 0;
        });
        if (!learned) {
            return false;
        }
    }
    // DistFromDefender with DistComparisonType (stage B4, Long Range: 10 m, GreaterThanOrEqual): the distance from the place of the attacker at the
    // hit to the target (client hit test 0x67A18F0: the squared distance from the position +0x230 of the hit data; the loader squares the
    // value, thus metres). The comparator of the data (a blank kind: LessThanOrEqual).
    if (row.has("DistFromDefender")) {
        const state::WorldPosition& from = hit.hasOrigin ? hit.origin : attacker.position;
        const float dx = target.position.x - from.x;
        const float dy = target.position.y - from.y;
        const float dz = target.position.z - from.z;
        if (!compare(std::sqrt(dx * dx + dy * dy + dz * dz), row.number("DistFromDefender"), row.text("DistComparisonType"))) {
            return false;
        }
    }
    // The health of the target before the hit (a decision of this project: one value for each rule of a hit).
    const float health = hit.targetHealth >= 0.0F ? hit.targetHealth : target.vitals.health;
    const float healthMax = hit.targetHealth >= 0.0F ? hit.targetHealthMax : target.vitals.healthMax;
    if (row.has("TargetHealthPercent") && !compare(percent_of(health, healthMax), row.number("TargetHealthPercent"), row.text("TargetComparisonType"))) {
        return false;
    }
    if (row.has("MyHealthPercent")
        && !compare(percent_of(attacker.vitals.health, attacker.vitals.healthMax), row.number("MyHealthPercent"), row.text("MyComparisonType"))) {
        return false;
    }
    if (row.has("MyStaminaPercent")
        && !compare(percent_of(attacker.vitals.stamina, attacker.vitals.staminaMax), row.number("MyStaminaPercent"),
                    row.text("MyStaminaComparisonType"))) {
        return false;
    }
    if (row.has("MyManaPercent")
        && !compare(percent_of(attacker.vitals.mana, attacker.vitals.manaMax), row.number("MyManaPercent"), row.text("MyManaComparisonType"))) {
        return false;
    }
    // The backend has no grit (a rule of this project): a row that needs grit does not pass.
    if (row.number("HasGritActive") != 0.0F || row.number("TargetHasGritActive") != 0.0F) {
        return false;
    }
    // The target of a hit is a creature, not a player.
    if (same_text(row.text("TargetVitalsCategory"), "Player")) {
        return false;
    }
    if (const std::string_view category = row.text("AttackerVitalsCategory"); !category.empty() && !same_text(category, "Player")) {
        return false;
    }
    if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty() && m_world.statuses().has(attacker, status)) {
        return false;
    }
    // EquipLoadCategory (stage 3.4f-B): the load class of the attacker is in the list (Fast, Normal, Slow).
    if (const std::string_view loads = row.text("EquipLoadCategory"); !loads.empty() && !list_has(loads, load_category(attacker))) {
        return false;
    }
    // InSequence and InAction: the hit is a hit of the cast of an ability of the list (the action of the ability), or (stage 3.3b-M2) a
    // hit of a strike of a fragment of the list (Armor Breaker: InSequence Ability_WarHammer_BlockBreaker, the fragment of its action in
    // the weapon actions; game data).
    // Stage B1: also the action of the cast in the ability action list (Rapid Accuracy and Final Blow: InAction Bow_RapidShot).
    for (const char* column : {"InSequence", "InAction"}) {
        if (const std::string_view sequence = row.text(column);
            !sequence.empty() && (hit.ability.empty() || !list_has(sequence, hit.ability)) && (hit.fragment.empty() || !list_has(sequence, hit.fragment))
            && (hit.action.empty() || !list_has(sequence, hit.action))) {
            return false;
        }
    }
    // DamageIsMelee and DamageIsRanged: a melee contact, a projectile (an area is neither: a decision of this project).
    if (row.number("DamageIsMelee") != 0.0F && hit.kind != HitKind::contact) {
        return false;
    }
    if (row.number("DamageIsRanged") != 0.0F && hit.kind != HitKind::projectile) {
        return false;
    }
    // NumAroundMe: the entities in the tracking spell of the rule (its AbilityId), or the creatures of the area of the hit.
    if (row.has("NumAroundMe")) {
        const gamedata::AbilityData* abilities = m_world.options().abilityData;
        if (abilities == nullptr || abilities->tracking_spell(rule.id) == nullptr || !tracking_supported(*abilities->tracking_spell(rule.id))) {
            return false;
        }
        const std::uint32_t count = around_count(row, rule.id, attacker, &hit);
        if (!compare(static_cast<float>(count), row.number("NumAroundMe"), row.text("NumAroundComparisonType"))) {
            return false;
        }
    }
    return true;
}

std::uint32_t Passives::around_count(const gamedata::CellValues& cells, std::string_view rule, const Entity& character,
                                     const HitContext* hit) const {
    (void)cells;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* spell = abilities != nullptr ? abilities->tracking_spell(rule) : nullptr;
    if (spell == nullptr) {
        return 0;
    }
    // A rule that the spell of the area of the hit tracks (Crowded Well: the vortex): the creatures of that area. The spell of the
    // hit is the spell row of the area itself (code review of stage 3.3b-S5, finding F4: the two vortex rows, also the upgraded
    // one of Unyielding, have the AbilityId of the rule and TrackNumInVolume; the index keeps only the first of them).
    if (hit != nullptr && !hit->spell.empty()) {
        const gamedata::SpellRow* area = abilities->spell(hit->spell);
        if (area != nullptr && same_text(area->cells.text("AbilityId"), rule) && area->cells.number("TrackNumInVolume") != 0.0F) {
            return hit->areaCount;
        }
    }
    if (!list_has(spell->cells.text("TargetTypes"), "Enemy")) {
        return 0; // A friend in the volume: the backend has no friend relation in a world with one player (a group of one).
    }
    // The creatures that live and are not on their way back, in the radius of the spell (and 3 m up or down).
    std::uint32_t count = 0;
    const float height = spell->height > 0.0F ? spell->height : 3.0F;
    if (!sphere_tracking(*spell)) {
        // A box in front of the look of the character (stage 3.3b-M2: Blood Lust): forward 0 to 2 x Length, Radius to each side.
        const float length = 2.0F * spell->cells.number("Length");
        const float ahead[2] = {-std::sin(character.look), std::cos(character.look)};
        m_world.for_each_near(character.space, character.position, length, [&](Entity& entity) {
            if (entity.kind != EntityKind::creature || entity.creature.dead || entity.creature.immune()) {
                return;
            }
            const float dx = entity.position.x - character.position.x;
            const float dy = entity.position.y - character.position.y;
            const float along = dx * ahead[0] + dy * ahead[1];
            const float side = std::fabs(dx * ahead[1] - dy * ahead[0]);
            if (along >= 0.0F && along <= length && side <= spell->radius && std::fabs(entity.position.z - character.position.z) <= height) {
                ++count;
            }
        });
        return count;
    }
    m_world.for_each_near(character.space, character.position, spell->radius, [&](Entity& entity) {
        if (entity.kind == EntityKind::creature && !entity.creature.dead && !entity.creature.immune()
            && horizontal_distance(entity.position, character.position) <= spell->radius
            && std::fabs(entity.position.z - character.position.z) <= height) {
            ++count;
        }
    });
    return count;
}

bool Passives::has_count_gate(const gamedata::CellValues& row) {
    return (row.number("TrackHitCount") != 0.0F && row.has("NumberOfTrackedHits")) || row.has("NumSuccessfulHits") || row.has("NumConsecutiveHits");
}

bool Passives::counts_peek(const PassiveRule& rule, const Entity& attacker, const HitContext& hit) const {
    // The same rule as counts_pass on a copy of the values of this rule: nothing is spent (code review of stage 3.3b-S5, finding F8:
    // the numeric bonuses of a rule with a hit count before the damage).
    CharacterState scratch;
    if (const auto found = m_characters.find(attacker.characterId); found != m_characters.end()) {
        const CharacterState& state = found->second;
        for (const std::string& key : {rule.id, rule.id + "/successful"}) {
            if (const auto entry = state.attackHits.find({key, hit.attack}); entry != state.attackHits.end()) {
                scratch.attackHits.insert(*entry);
            }
        }
        if (state.fired.count({rule.id, hit.attack}) != 0) {
            scratch.fired.insert({rule.id, hit.attack});
        }
        if (const auto entry = state.consecutive.find(rule.id); entry != state.consecutive.end()) {
            scratch.consecutive.insert(*entry);
        }
        if (const auto entry = state.volleyHits.find({rule.id, hit.volley, hit.target}); entry != state.volleyHits.end()) {
            scratch.volleyHits.insert(*entry);
        }
    }
    return count_rule(rule, scratch, hit, false);
}

bool Passives::counts_pass(const PassiveRule& rule, const Entity& attacker, const HitContext& hit, bool spend) {
    return count_rule(rule, m_characters[attacker.characterId], hit, spend);
}

bool Passives::count_rule(const PassiveRule& rule, CharacterState& state, const HitContext& hit, bool spend) const {
    const gamedata::CellValues& row = *rule.cells;
    const WorldClock::time_point now = m_world.clock().now();
    const std::uint64_t attack = hit.attack;
    // TrackHitCount: the hits of one attack, to NumberOfTrackedHits (client text: "Max 5 reductions per attack").
    if (row.number("TrackHitCount") != 0.0F && row.has("NumberOfTrackedHits")) {
        auto& entry = state.attackHits[{rule.id, attack}];
        const std::uint32_t count = (attack == 0 ? 0 : entry.first) + 1;
        if (!compare(static_cast<float>(count), row.number("NumberOfTrackedHits"), row.text("NumberOfHitsComparisonType"))) {
            if (spend) {
                entry = {count, now};
            }
            return false;
        }
        if (spend) {
            entry = {count, now};
        }
    }
    // NumSuccessfulHits: the attack hit this number of targets (client text: "Hitting at least 2 targets"): one time for each attack.
    if (row.has("NumSuccessfulHits") && row.number("TrackHitCount") == 0.0F) {
        const auto required = static_cast<std::uint32_t>(std::max(1.0F, row.number("NumSuccessfulHits")));
        const std::pair<std::string, std::uint64_t> key{rule.id + "/successful", attack};
        auto& entry = state.attackHits[key];
        const std::uint32_t count = (attack == 0 ? 0 : entry.first) + 1;
        if (spend) {
            entry = {count, now};
        }
        if (count < required || (attack != 0 && state.fired.count({rule.id, attack}) != 0)) {
            return false;
        }
        if (spend && attack != 0) {
            state.fired.insert({rule.id, attack});
        }
    }
    // NumConsecutiveHits of a hit of a volley (stage B1, decision D11 of the orchestrator): the arrows of one cast (Rapid Shot) that hit one
    // target. The client tests the target of the hit data for such a row (hit test 0x67A18F0, field +0x88: the same target). Each arrow is
    // one attack; a miss or a hit of a different target does not count for this target.
    if (row.has("NumConsecutiveHits") && hit.volley != 0) {
        auto& entry = state.volleyHits[{rule.id, hit.volley, hit.target}];
        std::set<std::uint64_t> attacks = entry.first;
        attacks.insert(attack);
        const auto most = row.has("MaxConsecutiveHits") ? static_cast<std::size_t>(row.number("MaxConsecutiveHits")) : std::size_t{1000};
        const std::size_t count = std::min(attacks.size(), most);
        const bool reached = count >= static_cast<std::size_t>(std::max(1.0F, row.number("NumConsecutiveHits")));
        if (spend) {
            entry = {reached && row.number("ResetConsecutiveOnSuccess") != 0.0F ? std::set<std::uint64_t>{} : attacks, now};
        }
        return reached;
    }
    // NumConsecutiveHits: the hits of this number of attacks one after the other (client text: "After 2 light attacks").
    if (row.has("NumConsecutiveHits")) {
        auto& [count, last] = state.consecutive[rule.id];
        std::uint32_t next = count;
        if (attack == 0 || attack != last) {
            next = std::min<std::uint32_t>(count + 1, row.has("MaxConsecutiveHits") ? static_cast<std::uint32_t>(row.number("MaxConsecutiveHits"))
                                                                                    : 1000U);
        }
        const bool reached = next >= static_cast<std::uint32_t>(std::max(1.0F, row.number("NumConsecutiveHits")));
        if (spend) {
            count = reached && row.number("ResetConsecutiveOnSuccess") != 0.0F ? 0 : next;
            last = attack;
        }
        if (!reached) {
            return false;
        }
    }
    return true;
}

bool Passives::activation_ready(const std::string& characterId, const PassiveRule& rule, bool start) {
    const WorldClock::time_point now = m_world.clock().now();
    const std::string key = characterId + "/" + rule.id;
    if (const auto found = m_activations.find(key); found != m_activations.end() && now < found->second) {
        return false;
    }
    if (start) {
        if (const float wait = rule.cells->number("ActivationCooldown"); wait > 0.0F) {
            m_activations[key] = now + milliseconds_of(wait);
        }
    }
    return true;
}

HitBonus Passives::before_hit(const Entity& attacker, const Entity& target, const HitContext& hit) const {
    HitBonus bonus;
    if (attacker.kind != EntityKind::player) {
        return bonus;
    }
    // The statuses of the character (CritChanceModifier, CritDamageModifier with the limits of their categories).
    bonus.critChance = attacker.gear.critChance;
    bonus.critDamage = attacker.gear.critDamage;
    for (const PassiveRule& rule : rules_of(attacker, hit.table)) {
        ++bonus.looked;
        const gamedata::CellValues& row = *rule.cells;
        if (!unknown_columns(row).empty()) {
            continue;
        }
        const std::string event = event_of(row);
        // A fixed stat (no event, no condition): its critical parts at each hit; its other values are in the gear values.
        if (event.empty() && !has_condition(row)) {
            bonus.critChance += row.number("CritChance") * rule.potency;
            bonus.critDamage += row.number("CritDamage") * rule.potency;
            continue;
        }
        // Stage B3: a value with a condition of a status of the character and no event (the form of BonusDamageSelfCCPerk1: StatusEffectCategories
        // Root,Slow, BaseDamage 0.12) is a value of each hit while the condition holds (an agent decision with the client self test 0x67A0130).
        // Stage B4: a rule with a condition of the target and no event is a rule of each hit (Finishing Shot); a row with OnHeadShot and
        // OnHitBehind (Dex_Bonus_200_1: HeadshotDamage and HitFromBehindDamage 0.05) takes a headshot and a hit from behind.
        const bool takes = event == "OnHit" || event == "OnCrit"
                           || (event.empty() && (hit_rule(row) || ability_value_rule(row) || conditional_value(row) || target_value_rule(row)))
                           || (row.number("OnHeadShot") != 0.0F && hit.head) || (row.number("OnHitBehind") != 0.0F && hit.behind);
        if (!takes || !conditions_pass(rule, attacker, target, hit)) {
            continue;
        }
        // A hit count (NumConsecutiveHits, TrackHitCount, NumSuccessfulHits) gates the values of the row too (code review of stage
        // 3.3b-S5, finding F8: Ability_Spear_BonusHvyDmgConsec gives BaseDamage only from the second heavy attack). after_hit spends
        // the count of the hit.
        if (has_count_gate(row) && !counts_peek(rule, attacker, hit)) {
            continue;
        }
        // A rule with OnCrit and CritChance changes the roll (client text of Critical Condition: "increased critical chance").
        // Its damage cells count only for a critical hit, one cell for each row (code review of stage 3.3b-S5, finding F3: Vicious
        // has OnCrit, OnHeadShot, CritDamage, and HeadshotDamage): a headshot takes HeadshotDamage when the row has OnHeadShot,
        // each other critical hit CritDamage.
        if (event == "OnCrit") {
            bonus.critChance += row.number("CritChance") * rule.potency;
            if (hit.head && row.number("OnHeadShot") != 0.0F) {
                bonus.critDamage += row.number("HeadshotDamage") * rule.potency;
            } else {
                bonus.critOnCrit += row.number("CritDamage") * rule.potency;
            }
            continue;
        }
        float times = 1.0F;
        // MaxNumAroundMe: the BaseDamage of each entity in the volume, to that number (client text of Crowded Well).
        if (row.has("MaxNumAroundMe") && row.has("NumAroundMe")) {
            times = static_cast<float>(std::min<std::uint32_t>(around_count(row, rule.id, attacker, &hit),
                                                               static_cast<std::uint32_t>(row.number("MaxNumAroundMe"))));
        }
        bonus.damage += row.number("BaseDamage") * rule.potency * times;
        bonus.critChance += row.number("CritChance") * rule.potency;
        bonus.critDamage += row.number("CritDamage") * rule.potency;
        bonus.penetration += row.number("ArmorPenetration") * rule.potency;
        if (hit.head) {
            bonus.critDamage += row.number("HeadshotDamage") * rule.potency;
        }
        if (hit.behind) {
            bonus.damage += row.number("HitFromBehindDamage") * rule.potency;
        }
    }
    return bonus;
}

void Passives::after_hit(Entity& attacker, Entity& target, const HitContext& hit) {
    if (attacker.kind != EntityKind::player) {
        return;
    }
    ++m_counts.hits;
    const EntityId attackerId = attacker.id;
    const EntityId targetId = target.id;
    const std::string characterId = attacker.characterId;
    const std::vector<PassiveRule> rules = rules_of(attacker, hit.table);
    for (const PassiveRule& rule : rules) {
        ++m_counts.rulesLooked;
        Entity* own = m_world.find(attackerId);
        Entity* hurt = m_world.creatures().find(targetId);
        if (own == nullptr || hurt == nullptr) {
            return;
        }
        const gamedata::CellValues& row = *rule.cells;
        const std::string event = event_of(row);
        const bool takes = event == "OnHit" || (event == "OnCrit" && hit.critical) || (event == "OnKill" && hit.killed)
                           || (event == "OnHeadShot" && hit.head) || (event == "OnHitBehind" && hit.behind)
                           || (event.empty() && hit_rule(row));
        // A rule with a hit count and only values before the damage (finding F8) spends its count here, one time for each hit.
        const bool counted = has_count_gate(row)
                             && std::any_of(kValues.begin(), kValues.end(), [&](const std::string& column) { return row.number(column) != 0.0F; });
        const bool effect = !row.text("OtherApplyStatusEffect").empty() || !row.text("SelfApplyStatusEffect").empty()
                            || row.number("DmgPctToHealth") > 0.0F || (row.number("CooldownTimer") < 0.0F && !row.text("CDRImmediatelyOptions").empty())
                            || !row.text("CastSpell").empty() || row.number("Stamina") != 0.0F || counted
                            || !row.text("RemoveTargetStatusEffectsList").empty() || !row.text("RemoveStatusEffects").empty();
        // A rule with OnCrit and only CritChance changed the roll (before_hit): it has no effect after the hit.
        if (!takes || !effect) {
            continue;
        }
        if (!conditions_pass(rule, *own, *hurt, hit)) {
            // NumConsecutiveHits: a hit that the rule does not take ends the sequence of the hits (a volley counts its own arrows: stage B1).
            if (row.has("NumConsecutiveHits") && unknown_columns(row).empty() && hit.volley == 0) {
                m_characters[characterId].consecutive[rule.id] = {0, hit.attack};
            }
            continue;
        }
        // RequireReaction: the creature took the hit (a decision of this project: damage above 0).
        if (row.number("RequireReaction") != 0.0F && !(hit.dealt > 0.0F)) {
            continue;
        }
        if (!activation_ready(characterId, rule, false) || !counts_pass(rule, *own, hit, true)) {
            continue;
        }
        (void)activation_ready(characterId, rule, true);
        ++m_counts.applied;
        apply_effects(rule, *own, *hurt, hit);
    }
    // The values of old attacks go (a bound of the cost).
    const WorldClock::time_point now = m_world.clock().now();
    if (now >= m_nextSweep) {
        m_nextSweep = now + passive_rules::kAttackKeep;
        for (auto& [id, state] : m_characters) {
            for (auto entry = state.attackHits.begin(); entry != state.attackHits.end();) {
                if (now - entry->second.second > passive_rules::kAttackKeep) {
                    state.fired.erase(entry->first);
                    entry = state.attackHits.erase(entry);
                } else {
                    ++entry;
                }
            }
            for (auto entry = state.volleyHits.begin(); entry != state.volleyHits.end();) {
                entry = now - entry->second.second > passive_rules::kAttackKeep ? state.volleyHits.erase(entry) : std::next(entry);
            }
        }
    }
}

void Passives::apply_effects(const PassiveRule& rule, Entity& attacker, Entity& target, const HitContext& hit) {
    const gamedata::CellValues& row = *rule.cells;
    const EntityId attackerId = attacker.id;
    const EntityId targetId = target.id;
    if (rule.origin == RuleOrigin::perk) {
        ++m_world.gear().m_counts.perkHits;
    }
    if (const float part = row.number("DmgPctToHealth"); part > 0.0F && hit.dealt > 0.0F) {
        // A life steal: a heal of a part of the damage of the hit (Insatiable Gravity Well, Prevailing Spirit, Critical Gains: the text
        // "10% of damage done"), with the heal factor of the equip load of the character (stage 1.3b), through the one heal path
        // (stage 3.3b-S6: the received factor of the kind lifeSteal, the number of the owner).
        HealRequest request;
        request.kind = HealKind::lifeSteal;
        request.amount = part * rule.potency * hit.dealt * m_world.gear().heal_factor(attacker);
        request.what = rule.id.c_str();
        m_world.health().heal(attacker, request);
        ++m_counts.heals;
        ++m_world.gear().m_counts.perkHeals;
    }
    if (const float stamina = row.number("Stamina"); stamina != 0.0F) {
        // A stamina gain of the rule (stage 3.4f-B; the runeglass gems: 1 or 4 stamina at a hit with damage, one time in 3 s), times the
        // potency, through the one stamina path.
        m_world.stamina().change(attacker, stamina * rule.potency, rule.id.c_str());
    }
    cooldown_effect(rule, attacker);
    if (const std::string_view statuses = row.text("OtherApplyStatusEffect"); !statuses.empty()) {
        for (const std::string_view status : parts_of(statuses)) {
            if (Entity* hurt = m_world.creatures().find(targetId); hurt != nullptr) {
                if (Entity* own = m_world.find(attackerId)) {
                    target_status(rule, *own, *hurt, status, hit);
                }
            }
        }
    }
    // RemoveTargetStatusEffectsList (stage B2): the statuses of the list end on the target, of each source (the bolt of the Boltcaster takes
    // the charges).
    if (const std::string_view list = row.text("RemoveTargetStatusEffectsList"); !list.empty()) {
        if (Entity* hurt = m_world.creatures().find(targetId)) {
            (void)m_world.creatures().remove_statuses(*hurt, list, {}, rule.id);
        }
    }
    if (Entity* own = m_world.find(attackerId)) {
        remove_own(rule, *own);
    }
    if (Entity* own = m_world.find(attackerId); own != nullptr && !row.text("SelfApplyStatusEffect").empty()) {
        const std::uint64_t before = m_world.gear().counts().casterEffects;
        m_world.gear().caster_effect(*own, row, rule.id, rule.potency);
        m_counts.casterStatuses += m_world.gear().counts().casterEffects - before;
    }
    if (rule.origin == RuleOrigin::perk) {
        if (const Entity* own = m_world.find(attackerId)) {
            m_world.gear().perk_burst_of(*own, rule.id);
        }
    }
    // CastSpell (stage 3.3b-M2: Power Cleaner of Clear Out casts WarHammer_Spell_ClearOut_Fortify, the fortify of the friends in 6 m).
    if (const std::string_view spell = row.text("CastSpell"); !spell.empty()) {
        if (Entity* own = m_world.find(attackerId)) {
            m_world.abilities().rule_spell(*own, spell, rule.id);
        }
    }
}

void Passives::cooldown_effect(const PassiveRule& rule, Entity& character) {
    const gamedata::CellValues& row = *rule.cells;
    const float part = row.number("CooldownTimer");
    if (!(part < 0.0F)) {
        return;
    }
    // CDRImmediatelyOptions: ActiveWeapon (each ability of the bar of the weapon in the hand), AbilitySpecific (the ability of CooldownId),
    // AllExcept (each ability of the bar but the one of CooldownId). A part of the time of the data.
    const std::string_view options = row.text("CDRImmediatelyOptions");
    const std::string_view cooldown = row.text("CooldownId");
    if (same_text(options, "ActiveWeapon")) {
        m_world.abilities().reduce_weapon_cooldowns(character, -part * rule.potency);
    } else if (same_text(options, "AbilitySpecific")) {
        m_world.abilities().reduce_weapon_cooldowns(character, -part * rule.potency, cooldown);
    } else if (same_text(options, "AllExcept")) {
        m_world.abilities().reduce_weapon_cooldowns(character, -part * rule.potency, {}, cooldown);
    }
    ++m_counts.cooldownReductions;
    ++m_world.gear().m_counts.cooldownReductions;
}

void Passives::remove_own(const PassiveRule& rule, Entity& character) {
    // RemoveStatusEffects (stage B2): statuses of the character end: the statuses of the list, or with AbilitySpecific the statuses of
    // StatusEffectsList (game data: Shirking Shock II takes its tracker; Momentum of the rapier, Rapier_BonusDMGAfterAbility). A name that is
    // not a status of the character ends nothing (Global_Weapon_DodgeSuccess_LightningDMG names an ability row).
    const gamedata::CellValues& row = *rule.cells;
    const std::string_view removed = row.text("RemoveStatusEffects");
    if (removed.empty()) {
        return;
    }
    const std::string_view list = same_text(removed, "AbilitySpecific") ? row.text("StatusEffectsList") : removed;
    std::vector<std::string> ids;
    for (const std::string_view part : parts_of(list)) {
        ids.emplace_back(part);
    }
    if (!ids.empty()) {
        (void)m_world.statuses().remove_rows(character, ids, {}, StatusEnd::replaced);
    }
}

void Passives::self_effects(const PassiveRule& rule, Entity& character) {
    const std::string characterId = character.characterId;
    if (rule.origin == RuleOrigin::perk) {
        ++m_world.gear().m_counts.perkHits;
    }
    cooldown_effect(rule, character);
    if (Entity* own = m_world.find_character(characterId)) {
        remove_own(rule, *own);
    }
    if (Entity* own = m_world.find_character(characterId); own != nullptr && !rule.cells->text("SelfApplyStatusEffect").empty()) {
        const std::uint64_t before = m_world.gear().counts().casterEffects;
        m_world.gear().caster_effect(*own, *rule.cells, rule.id, rule.potency);
        m_counts.casterStatuses += m_world.gear().counts().casterEffects - before;
    }
}

void Passives::weapon_changed(Entity& character, const gamedata::WeaponRow* before, bool swap) {
    if (character.kind != EntityKind::player) {
        return;
    }
    const std::string characterId = character.characterId;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    // The tag removal actions of the data (game data: Bow_Remove_StatusEffects_Sheathe of playeractions_ability_bow, EffectsToRemoveOnExit
    // while the tag Bow holds; client code 0x1E140A0 also removes the haste of Dodge and Weave when the weapon in the hand is not a bow): the
    // action was active with the weapon before and is not active with the new one, thus its exit ends its statuses (of each source).
    if (abilities != nullptr) {
        const auto has_tags = [](const gamedata::WeaponRow* row, const std::vector<std::string>& tags) {
            return row != nullptr && std::all_of(tags.begin(), tags.end(), [&](const std::string& tag) { return list_has(row->tags, tag); });
        };
        for (const gamedata::ActionTagRemoval& action : abilities->tables().actionTagRemovals) {
            if (!has_tags(before, action.tags) || has_tags(weapon, action.tags)) {
                continue;
            }
            if (Entity* own = m_world.find_character(characterId)) {
                m_counts.tagRemovals += m_world.statuses().remove_rows(*own, action.statuses, {}, StatusEnd::weapon);
            }
        }
    }
    if (!swap) {
        return;
    }
    // The rules with OnWeaponSwap (game data: Archer's Speed, Passive_Bow_Skirmish_HasteOnSwap) of the tree of the new weapon and of the perks:
    // a change of the weapon slot with the key of a weapon (research bow-build-2.md K2; an agent decision: an equip that changes the item of the
    // slot in the hand is not a swap). The conditions with no hit, the activation cooldown, then the effects on the character.
    Entity* own = m_world.find_character(characterId);
    if (own == nullptr || own->life.dead) {
        return;
    }
    // Code review of stage B3, finding B3-R2: one rule for each row, with the cap of its sources (merged).
    const std::vector<PassiveRule> rules = merged(rules_of(*own, table_in_hand(*own)));
    for (const PassiveRule& rule : rules) {
        const gamedata::CellValues& row = *rule.cells;
        if (event_of(row) != "OnWeaponSwap" || !unknown_columns(row).empty() || !self_unsourced(row).empty() || !self_unsupported(row).empty()
            || !self_effect(row)) {
            continue;
        }
        Entity* current = m_world.find_character(characterId);
        if (current == nullptr || current->life.dead) {
            return;
        }
        if (!self_pass(rule, *current) || !activation_ready(characterId, rule, false)) {
            continue;
        }
        (void)activation_ready(characterId, rule, true);
        ++m_counts.swapRules;
        ++m_counts.applied;
        self_effects(rule, *current);
    }
}

void Passives::dodge_started(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    CharacterState& state = m_characters[character.characterId];
    state.dodgeActions.clear();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::StaminaData* stamina = m_world.options().staminaData;
    if (abilities == nullptr || stamina == nullptr || character.life.dead) {
        return;
    }
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    const float part = character.vitals.staminaMax > 0.0F ? character.vitals.stamina / character.vitals.staminaMax : 0.0F;
    const auto learned = [&](const std::string& node) {
        const std::uint32_t id = gamedata::progression_id(node);
        return std::any_of(character.skills.pools.begin(), character.skills.pools.end(), [&](const auto& pool) {
            const auto found = pool.second.find(id);
            return found != pool.second.end() && found->second.level != 0;
        });
    };
    // The start tests of each dodge status action of the data (game data: Passive_Bow_Skirmish_HasteOnDodge: the node, the tag Bow, the stamina
    // 0.99 of the maximum or more, no status Status_Passive_Bow_Skirmish_DodgeHaste of each source): the stamina before the payment of the dodge
    // (the payment comes at its mark PayStaminaCost, 0.1 s; research bow-build-2.md Q1: "after dodging with full stamina").
    const std::vector<gamedata::ActionDodgeStatus>& actions = abilities->tables().actionDodgeStatuses;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const gamedata::ActionDodgeStatus& action = actions[index];
        if (!same_text(action.dodge, stamina->tables().dodgeAction) || !std::all_of(action.nodes.begin(), action.nodes.end(), learned)
            || !std::all_of(action.tags.begin(), action.tags.end(), [&](const std::string& tag) { return weapon != nullptr && list_has(weapon->tags, tag); })
            || (action.stamina && !compare(part, action.stamina->value, action.stamina->comparison))
            || std::any_of(action.without.begin(), action.without.end(), [&](const std::string& status) { return m_world.statuses().has(character, status); })) {
            continue;
        }
        state.dodgeActions.push_back(index);
    }
}

void Passives::dodge_cancelled(const std::string& characterId) {
    if (const auto found = m_characters.find(characterId); found != m_characters.end()) {
        found->second.dodgeActions.clear();
    }
}

void Passives::dodge_ended(Entity& character, std::string_view fragment) {
    if (character.kind != EntityKind::player) {
        return;
    }
    const std::string characterId = character.characterId;
    std::vector<std::size_t> pending;
    if (const auto found = m_characters.find(characterId); found != m_characters.end()) {
        pending.swap(found->second.dodgeActions);
    }
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::StaminaData* stamina = m_world.options().staminaData;
    if (abilities == nullptr || stamina == nullptr || character.life.dead) {
        return;
    }
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    // 1. The statuses of the dodge status actions whose start tests passed, at the exit of the action, when the tags of the exit hold (game data:
    // the IfThenElse IfMannequinTag Bow of Passive_Bow_Skirmish_HasteOnDodge: the bow still in the hand), with the action as the source.
    const std::vector<gamedata::ActionDodgeStatus>& actions = abilities->tables().actionDodgeStatuses;
    for (const std::size_t index : pending) {
        if (index >= actions.size()) {
            continue;
        }
        const gamedata::ActionDodgeStatus& action = actions[index];
        if (!std::all_of(action.exitTags.begin(), action.exitTags.end(),
                         [&](const std::string& tag) { return weapon != nullptr && list_has(weapon->tags, tag); })) {
            continue;
        }
        for (const std::string& status : action.statuses) {
            Entity* own = m_world.find_character(characterId);
            if (own == nullptr) {
                return;
            }
            StatusApply how;
            how.source = StatusSource{StatusSourceKind::ability, action.action, 0, own->id};
            const StatusVerdict verdict = m_world.statuses().apply(*own, status, how);
            if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
                ++m_counts.dodgeStatuses;
            }
        }
    }
    // 2. The rules after the end of a dodge (game data: Evasive Tactics, Ultimate_Bow_Skirmish: AfterAction Dodge, AfterSequence the dodge
    // fragments, SelfApplyStatusEffect; the CDR rows of the perks): no event, the action of the stamina data and the fragment of the last dodge.
    // Duration (the window after the action) is a mark: a rule with no event acts one time at the end (an agent decision).
    Entity* own = m_world.find_character(characterId);
    if (own == nullptr) {
        return;
    }
    const std::string dodgeAction = stamina->tables().dodgeAction;
    // Code review of stage B3, finding B3-R2: one rule for each row, with the cap of its sources (Refreshing Evasion has IsStackableMax 1).
    const std::vector<PassiveRule> rules = merged(rules_of(*own, table_in_hand(*own)));
    for (const PassiveRule& rule : rules) {
        const gamedata::CellValues& row = *rule.cells;
        if ((!row.has("AfterAction") && !row.has("AfterSequence")) || any_event(row) || !unknown_columns(row).empty()
            || !self_unsourced(row, true).empty() || !self_unsupported(row).empty() || !self_effect(row)) {
            continue;
        }
        if ((row.has("AfterAction") && !list_has(row.text("AfterAction"), dodgeAction))
            || (row.has("AfterSequence") && !list_has(row.text("AfterSequence"), fragment))) {
            continue;
        }
        Entity* current = m_world.find_character(characterId);
        if (current == nullptr || current->life.dead) {
            return;
        }
        if (!self_pass(rule, *current) || !activation_ready(characterId, rule, false)) {
            continue;
        }
        (void)activation_ready(characterId, rule, true);
        ++m_counts.afterDodgeRules;
        ++m_counts.applied;
        self_effects(rule, *current);
    }
}

float Passives::duration_factor(const Entity& character, const gamedata::StatusRow& row) const {
    if (character.kind != EntityKind::player) {
        return 1.0F;
    }
    // Client getter 0x68308E0 (AbilityComponent::GetStatusEffectDurationMultipliers; research bow-build-2.md Q1): for each ability of the
    // character with StatusEffectDurationReduction, whose own conditions pass (0x67A0130) and that names the status (StatusEffect) or a
    // category of it (StatusEffectCategories), the value times the potency of the ability, summed. The use of the sum (time x (1 + sum)) and
    // the new status as present for the condition StatusEffectCategories are agent decisions (the server code is not in the client).
    const std::string_view categories = row.cells.text("EffectCategories");
    float sum = 0.0F;
    // Code review of stage B3, finding B3-R2: the potency of each row with the cap of its sources (Alacrity II has IsStackableMax 5; research Q1:
    // the client getter takes the potency of 0x67E9A70, which counts the sources to that limit).
    for (const PassiveRule& rule : merged(rules_of(character, table_in_hand(character)))) {
        const gamedata::CellValues& cells = *rule.cells;
        if (!duration_rule(cells, unknown_columns(cells))) {
            continue;
        }
        bool matches = list_has(cells.text("StatusEffect"), row.id);
        for (const std::string_view category : parts_of(cells.text("StatusEffectCategories"))) {
            matches = matches || list_has(categories, category);
        }
        if (!matches || !self_pass(rule, character, &row)) {
            continue;
        }
        sum += cells.number("StatusEffectDurationReduction") * rule.potency;
    }
    return std::max(1.0F + sum, 0.0F);
}

float Passives::outgoing_factor(const Entity& source, const Entity& target, const gamedata::StatusRow& row) const {
    if (source.kind != EntityKind::player || source.id == target.id) {
        return 1.0F;
    }
    // The outgoing part of a time factor (TargetStatusEffectDurationCats with TargetStatusEffectDurationMult): Battle Precision
    // (Passive_Bow_Skirmish_DebuffDur: +0.2 for a Debilitate or a DoT on a target with a Debuff), Dex_Bonus_150_1 (+0.05 for a DoT), Alacrity II
    // (+0.094 for a haste that the character gives a different player). The client has no reader of the columns (research bow-build-2.md Q1):
    // the sum and its use (time x (1 + sum), then DurationMax) are agent decisions with the form of the incoming factor (client getter
    // 0x68308E0). The new status counts as present for a condition of a status of the target (the decision of stage B3 for the new haste).
    const std::string_view categories = row.cells.text("EffectCategories");
    float sum = 0.0F;
    for (const PassiveRule& rule : merged(rules_of(source, table_in_hand(source)))) {
        const gamedata::CellValues& cells = *rule.cells;
        if (!outgoing_rule(cells) || !unknown_columns(cells).empty() || !conditions_outside(cells, kOutgoingConditions).empty()) {
            continue;
        }
        bool matches = false;
        for (const std::string_view category : parts_of(cells.text("TargetStatusEffectDurationCats"))) {
            matches = matches || list_has(categories, category);
        }
        if (!matches || !self_pass(rule, source)) {
            continue;
        }
        // TargetStatusEffectCategory and TargetStatusEffect: the target has the status or a status of a category (the new one too).
        if (const std::string_view wanted = cells.text("TargetStatusEffectCategory"); !wanted.empty()) {
            bool has = false;
            for (const std::string_view category : parts_of(categories)) {
                has = has || list_has(wanted, category);
            }
            const WorldClock::time_point now = m_world.clock().now();
            const gamedata::AbilityData* abilities = m_world.options().abilityData;
            const auto row_has = [&](const gamedata::StatusRow* data) {
                bool found = false;
                for (const std::string_view category : parts_of(data != nullptr ? data->cells.text("EffectCategories") : std::string_view{})) {
                    found = found || list_has(wanted, category);
                }
                return found;
            };
            if (target.kind == EntityKind::creature) {
                for (const CreatureEffect& effect : target.creature.effects) {
                    has = has || (now < effect.ends && row_has(effect.row != nullptr ? effect.row : abilities != nullptr ? abilities->status(effect.status) : nullptr));
                }
            } else {
                for (const ActiveStatus& status : target.statuses) {
                    has = has || ((status.permanent || now < status.ends) && abilities != nullptr && row_has(abilities->status(status.status)));
                }
            }
            if (has == same_text(cells.text("TargetStatusEffectComparison"), "NotEqual")) {
                continue;
            }
        }
        sum += cells.number("TargetStatusEffectDurationMult") * rule.potency;
    }
    if (sum != 0.0F) {
        ++m_counts.outgoingFactors;
    }
    return std::max(1.0F + sum, 0.0F);
}

float Passives::taken_reduction(const Entity& character, std::string_view attackType) const {
    if (character.kind != EntityKind::player || character.life.dead) {
        return 0.0F;
    }
    // Enchanted Ward (Global_Armor_DefBasic: OnHitTaken, RequireReaction, AttackType Light,Heavy, BaseDamageReduction 0.01 x potency; the II form
    // Ability_Perk_Armor_DefBasic 0.016) before the damage of a creature hit: the attack type of the damage row of the creature attack (game data,
    // research bow-build-2.md Q5), the values of the character before the hit, its statuses. RequireReaction: the hit has damage (a hit with no
    // damage needs no reduction). The sources of a row with their cap (merged).
    float part = 0.0F;
    for (const PassiveRule& rule : merged(rules_of(character, table_in_hand(character)))) {
        const gamedata::CellValues& row = *rule.cells;
        if (event_of(row) != "OnHitTaken" || row.number("BaseDamageReduction") == 0.0F || !unknown_columns(row).empty()
            || !taken_unsourced(row).empty() || row.number("HasGritActive") != 0.0F || has_count_gate(row)) {
            continue;
        }
        if (const std::string_view types = row.text("AttackType"); !types.empty() && !list_has(types, attackType)) {
            continue;
        }
        if (!self_pass(rule, character)) {
            continue;
        }
        part += row.number("BaseDamageReduction") * rule.potency;
    }
    if (part != 0.0F) {
        ++m_counts.takenReductions;
    }
    return part;
}

float Passives::cooldown_factor(const Entity& character, std::string_view ability) const {
    if (character.kind != EntityKind::player) {
        return 1.0F;
    }
    // The fixed cooldown rules (Refreshing, Global_Common_CDR: CooldownTimer -0.02 x potency, IsStackableMax 3; Foc_Bonus_25_1: -0.03) for the
    // scope of CDRImmediatelyOptions: none, ActiveWeapon, All (the ability of a cast is of the weapon in the hand), AbilitySpecific (CooldownId),
    // AllExcept (not CooldownId); and the cooldown reduction line of the jewelry, in one sum (live check L1 of stage B4: the tooltip of the client
    // shows 22 x (1 - 2 x 0.0272) = 20.8 s with Refreshing, and 19.7 s with CooldownReduction 1.05 too).
    float part = -character.gear.cooldownReduction;
    for (const PassiveRule& rule : merged(rules_of(character, table_in_hand(character)))) {
        const gamedata::CellValues& row = *rule.cells;
        if (!fixed_cooldown_rule(row) || !unknown_columns(row).empty() || !self_unsourced(row).empty() || !self_pass(rule, character)) {
            continue;
        }
        const std::string_view options = row.text("CDRImmediatelyOptions");
        const std::string_view cooldown = row.text("CooldownId");
        if ((same_text(options, "AbilitySpecific") && !same_text(cooldown, ability)) || (same_text(options, "AllExcept") && same_text(cooldown, ability))) {
            continue;
        }
        part += row.number("CooldownTimer") * rule.potency;
    }
    if (part != 0.0F) {
        ++m_counts.cooldownFactors;
    }
    return std::max(1.0F + part, 0.0F);
}

float Passives::consumable_cooldown_factor(const Entity& character, std::string_view itemId, std::string_view cooldownId) const {
    if (character.kind != EntityKind::player) {
        return 1.0F;
    }
    // CooldownTimer of a rule with OnUsedConsumable and no CDRImmediatelyOptions (game data: The Thirst, GlobalPerk_Artifact_Set1_Earring1_2 to _5,
    // +0.25 for one cooldown id each; Global_Earring_PotionCDR -0.04 with no id): the cooldown of the consumable times 1 plus the sum. The cooldown
    // id of the row must be the one of the item (no id: each). Before stage B4 the backend read only a negative value and no id.
    float part = 0.0F;
    for (const PassiveRule& rule : merged(rules_of(character, table_in_hand(character)))) {
        const gamedata::CellValues& row = *rule.cells;
        if (row.number("OnUsedConsumable") == 0.0F || row.number("CooldownTimer") == 0.0F || !row.text("CDRImmediatelyOptions").empty()
            || !unknown_columns(row).empty() || !conditions_outside(row, kConsumableConditions).empty()) {
            continue;
        }
        if (const std::string_view id = row.text("CooldownId"); !id.empty() && !same_text(id, cooldownId)) {
            continue;
        }
        if (const std::string_view items = row.text("IsConsumableIds"); !items.empty() && !list_has(items, itemId)) {
            continue;
        }
        if (const std::string_view items = row.text("IsNotConsumableIds"); !items.empty() && list_has(items, itemId)) {
            continue;
        }
        if (!self_pass(rule, character)) {
            continue;
        }
        part += row.number("CooldownTimer") * rule.potency;
    }
    return std::max(1.0F + part, 0.0F);
}

void Passives::consumable_used(Entity& character, std::string_view itemId, std::string_view cooldownId) {
    if (character.kind != EntityKind::player || character.life.dead) {
        return;
    }
    // The rules with OnUsedConsumable and an effect on the character (game data: the Toast perks of the earring, Ability_Perk_Earring_OnPotionDmg
    // and _OnPotionFort: IsConsumableIds of the potions, SelfApplyStatusEffect the empower or the fortify, ActivationCooldown 8): the item in
    // IsConsumableIds (the compare ignores the case: the list has "serumregent5"), not in IsNotConsumableIds, the cooldown id of CooldownId, the
    // values of the character, the activation cooldown; then the status with the potency of the rule.
    const std::string characterId = character.characterId;
    for (const PassiveRule& rule : merged(rules_of(character, table_in_hand(character)))) {
        const gamedata::CellValues& row = *rule.cells;
        if (row.number("OnUsedConsumable") == 0.0F || (row.text("SelfApplyStatusEffect").empty() && row.text("RemoveStatusEffects").empty())
            || !unknown_columns(row).empty() || !conditions_outside(row, kConsumableConditions).empty() || row.number("CooldownTimer") != 0.0F) {
            continue;
        }
        if (const std::string_view id = row.text("CooldownId"); !id.empty() && !same_text(id, cooldownId)) {
            continue;
        }
        if (const std::string_view items = row.text("IsConsumableIds"); !items.empty() && !list_has(items, itemId)) {
            continue;
        }
        if (const std::string_view items = row.text("IsNotConsumableIds"); !items.empty() && list_has(items, itemId)) {
            continue;
        }
        Entity* current = m_world.find_character(characterId);
        if (current == nullptr || current->life.dead) {
            return;
        }
        if (!self_pass(rule, *current) || !activation_ready(characterId, rule, false)) {
            continue;
        }
        (void)activation_ready(characterId, rule, true);
        ++m_counts.consumableRules;
        ++m_counts.applied;
        self_effects(rule, *current);
    }
}

void Passives::target_status(const PassiveRule& rule, Entity& attacker, Entity& target, std::string_view status, const HitContext& hit) {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::StatusRow* row = abilities != nullptr ? abilities->status(status) : nullptr;
    if (row == nullptr || target.creature.dead) {
        return;
    }
    // Stage B1: a rule whose status takes its damage from a row (ForceStatusEffectDamageTableRow with StatusEffectDamageTableRowOverride:
    // Flame Arrow, Common_DamageOverTime_Override, DmgCoef 1): the status goes through the one status path of a creature (its icon at the
    // name plate for each observer, its time, a later end at a new application), and each tick does HealthModifierDamageBased of the
    // status row (the burn: 0.08, Fire) times the potency times the damage of that row with the weapon of the hit before the defense (a
    // decision of an agent: the weapon damage with the attributes and the level, no part of the gear). The direct hit gives it only: the
    // DamageTableRow of the rule (Bow_ExplosionShot_Projectile) is the condition.
    if (rule.cells->number("ForceStatusEffectDamageTableRow") != 0.0F) {
        const float based = -row->cells.number("HealthModifierDamageBased");
        const std::string_view forced = rule.cells->text("StatusEffectDamageTableRowOverride");
        const gamedata::CombatTables* tables = m_world.combat().tables();
        const std::optional<std::uint32_t> index = tables != nullptr && !forced.empty() ? tables->row_index(forced) : std::nullopt;
        StatusDamage damage;
        if (based > 0.0F && index && hit.weapon != nullptr) {
            const Damage base = m_world.combat().damage(attacker, *hit.weapon, hit.gearScore, tables->damage_row(*index), nullptr);
            damage.amount = based * rule.potency * base.raw;
            damage.type = std::string(row->cells.text("DamageType"));
            damage.charge = row->cells.number("SourceRuneChargeOnTick");
        }
        if (m_world.creatures().give_status(target, *row, attacker.id, std::nullopt, damage.amount > 0.0F ? &damage : nullptr)) {
            ++m_counts.targetStatuses;
            log::write("world: rule %s gives %s to creature %016llx: %.1f %s damage each tick", rule.id.c_str(), row->id.c_str(),
                       static_cast<unsigned long long>(target.id), static_cast<double>(damage.amount), damage.type.c_str());
        }
        return;
    }
    // Stage B2: a status with no duration whose damage is a part of the damage of the hit (a negative HealthModifierDamageBased with a
    // DamageType, no rend, no spell: the bolt of the Boltcaster, Status_Perk_Artifact_Set1_Bow_StaticDischarge, -0.8 Lightning) goes through
    // the one status path of a creature: its ApplicationCooldown on the target (3 s: one bolt in 3 s on each target), its dispel (the
    // charges), its icon and its visual effect for its DurationMax, and one damage of `-HealthModifierDamageBased x potency x the raw damage
    // of the hit` of its type. The damage base is the project rule of a damage-based status of a perk (research bow-build-2.md K5: the row
    // has no ForceStatusEffectDamageTableRow; the game text "a bolt of lightning, dealing 80% damage").
    if (row->duration <= 0.0F && row->cells.number("HealthModifierDamageBased") < 0.0F && !row->cells.text("DamageType").empty()
        && row->cells.text("CastSpell").empty() && row->cells.number("PhysicalArmor") == 0.0F && row->cells.number("ElementalArmor") == 0.0F) {
        StatusDamage damage;
        damage.amount = -row->cells.number("HealthModifierDamageBased") * rule.potency * hit.raw;
        damage.type = std::string(row->cells.text("DamageType"));
        damage.charge = row->cells.number("SourceRuneChargeOnApply");
        if (m_world.creatures().give_status(target, *row, attacker.id, std::nullopt, damage.amount > 0.0F ? &damage : nullptr)) {
            ++m_counts.targetStatuses;
            log::write("world: rule %s gives %s to creature %016llx: one damage of %.1f %s", rule.id.c_str(), row->id.c_str(),
                       static_cast<unsigned long long>(target.id), static_cast<double>(damage.amount), damage.type.c_str());
        }
        return;
    }
    // A rend, a damage over time, or a chain: the rules of a perk status (stage 1.3b); each other status through the creature
    // system with the values of its row (a slow, a weaken, an exhaust).
    if (Gear::perk_status_kind(*row)) {
        ++m_world.gear().m_counts.perkEffects;
        m_world.gear().perk_status(attacker, target, status, rule.potency, hit);
    } else {
        std::optional<float> seconds;
        if (const float duration = rule.cells->number("OtherApplyStatusEffectDurations"); duration > 0.0F) {
            seconds = duration;
        }
        if (!m_world.abilities().apply_status(target, row->id, attacker.id, seconds)) {
            return;
        }
    }
    ++m_counts.targetStatuses;
}

float Passives::heal_bonus(const Entity& healer, const Entity& target, const HealSource& source) const {
    if (healer.kind != EntityKind::player) {
        return 0.0F;
    }
    float part = 0.0F;
    for (const PassiveRule& rule : rules_of(healer, source.table)) {
        const gamedata::CellValues& row = *rule.cells;
        if (row.number("OnHealed") == 0.0F || row.number("HealScalingValueMultiplier") == 0.0F || !unknown_columns(row).empty()
            || !heal_unsourced(row).empty()) {
            continue;
        }
        // The conditions of a heal (stage 3.3b-S6), with the comparator of the client: the target before the heal, the healer, the
        // row of the spell of the heal, the categories of the statuses of the target.
        if (row.number("HasGritActive") != 0.0F) {
            continue;
        }
        if (row.has("TargetHealthPercent")
            && !compare(percent_of(target.vitals.health, target.vitals.healthMax), row.number("TargetHealthPercent"), row.text("TargetComparisonType"))) {
            continue;
        }
        if (row.has("MyHealthPercent")
            && !compare(percent_of(healer.vitals.health, healer.vitals.healthMax), row.number("MyHealthPercent"), row.text("MyComparisonType"))) {
            continue;
        }
        if (row.has("MyStaminaPercent")
            && !compare(percent_of(healer.vitals.stamina, healer.vitals.staminaMax), row.number("MyStaminaPercent"), row.text("MyStaminaComparisonType"))) {
            continue;
        }
        if (row.has("MyManaPercent")
            && !compare(percent_of(healer.vitals.mana, healer.vitals.manaMax), row.number("MyManaPercent"), row.text("MyManaComparisonType"))) {
            continue;
        }
        if (const std::string_view rows = row.text("DamageTableRow"); !rows.empty() && !list_has(rows, source.row)) {
            continue;
        }
        if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty() && m_world.statuses().has(healer, status)) {
            continue;
        }
        if (const std::string_view categories = row.text("TargetStatusEffectCategory"); !categories.empty()) {
            const gamedata::AbilityData* abilities = m_world.options().abilityData;
            bool has = false;
            for (const ActiveStatus& status : target.statuses) {
                const gamedata::StatusRow* data = abilities != nullptr && m_world.statuses().has(target, status.status) ? abilities->status(status.status) : nullptr;
                for (const std::string_view category : parts_of(categories)) {
                    has = has || (data != nullptr && list_has(data->cells.text("EffectCategories"), category));
                }
            }
            if (has != (lower(row.text("TargetStatusEffectComparison")) != "notequal")) {
                continue;
            }
        }
        part += row.number("HealScalingValueMultiplier") * rule.potency;
    }
    return part;
}

void Passives::hit_taken(Entity& character, float dealt, std::string_view attackType) {
    if (character.kind != EntityKind::player || character.life.dead) {
        return;
    }
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    const std::uint32_t table = weapon != nullptr && !weapon->mastery.empty() ? gamedata::progression_id(weapon->mastery) : 0;
    HitContext hit; // A hit taken: each one is its own attack.
    for (const PassiveRule& rule : rules_of(character, table)) {
        const gamedata::CellValues& row = *rule.cells;
        if (event_of(row) != "OnHitTaken" || row.text("SelfApplyStatusEffect").empty() || !unknown_columns(row).empty()) {
            continue;
        }
        // Stage B4: the attack type of the damage row of the creature attack (AttackType of the rule).
        if (const std::string_view types = row.text("AttackType"); !types.empty() && !list_has(types, attackType)) {
            continue;
        }
        // The conditions of a hit taken (code review of stage 3.3b-S5, finding F6), each one before a count or the activation
        // cooldown is spent: a condition with no source in a hit taken (the row does not pass), grit (none), the values of the
        // character after the damage of the hit (its health, stamina, and mana with the comparator of the client; a decision of an
        // agent: the event comes after the damage), a status that it must not have, and RequireReaction (damage above 0).
        if (!taken_unsourced(row).empty() || row.number("HasGritActive") != 0.0F) {
            continue;
        }
        if (row.has("MyHealthPercent")
            && !compare(percent_of(character.vitals.health, character.vitals.healthMax), row.number("MyHealthPercent"), row.text("MyComparisonType"))) {
            continue;
        }
        if (row.has("MyStaminaPercent")
            && !compare(percent_of(character.vitals.stamina, character.vitals.staminaMax), row.number("MyStaminaPercent"),
                        row.text("MyStaminaComparisonType"))) {
            continue;
        }
        if (row.has("MyManaPercent")
            && !compare(percent_of(character.vitals.mana, character.vitals.manaMax), row.number("MyManaPercent"), row.text("MyManaComparisonType"))) {
            continue;
        }
        if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty() && m_world.statuses().has(character, status)) {
            continue;
        }
        if (row.number("RequireReaction") != 0.0F && !(dealt > 0.0F)) {
            continue;
        }
        if (!activation_ready(character.characterId, rule, false) || !counts_pass(rule, character, hit, true)) {
            continue;
        }
        (void)activation_ready(character.characterId, rule, true);
        ++m_counts.hitTakenRules;
        m_world.gear().caster_effect(character, row, rule.id, rule.potency);
    }
    // The hit-taken status actions of the data (stage 3.4f-B, ability-data.json format 6: Mauler's Stamina of the great axe,
    // GreatAxe_Passive_StamWhenLowHP_Detect). The tests of the action after the damage of the hit: a hit with damage (IfDamagedByPowerLevel
    // with FailOnNoReactionDamage: a decision of an agent, a creature hit with damage above 0 has a reaction), the health and the stamina as a
    // part of the maximum (the comparator of the client), each learned node, each tag (the tags of the weapon in the hand), and no status of
    // the list. The statuses come with the duration of their rows, with the action as the source.
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (abilities == nullptr) {
        return;
    }
    for (const gamedata::ActionHitStatus& action : abilities->tables().actionHitStatuses) {
        if (action.reaction && !(dealt > 0.0F)) {
            continue;
        }
        if (!compare(dealt, action.power.value, action.power.comparison)) {
            continue;
        }
        if (action.health && !compare(character.vitals.healthMax > 0.0F ? character.vitals.health / character.vitals.healthMax : 0.0F,
                                      action.health->value, action.health->comparison)) {
            continue;
        }
        if (action.stamina && !compare(character.vitals.staminaMax > 0.0F ? character.vitals.stamina / character.vitals.staminaMax : 0.0F,
                                       action.stamina->value, action.stamina->comparison)) {
            continue;
        }
        const bool nodes = std::all_of(action.nodes.begin(), action.nodes.end(), [&](const std::string& node) {
            const std::uint32_t id = gamedata::progression_id(node);
            return std::any_of(character.skills.pools.begin(), character.skills.pools.end(), [&](const auto& pool) {
                const auto found = pool.second.find(id);
                return found != pool.second.end() && found->second.level != 0;
            });
        });
        const bool tags = std::all_of(action.tags.begin(), action.tags.end(),
                                      [&](const std::string& tag) { return weapon != nullptr && list_has(weapon->tags, tag); });
        const bool without = std::none_of(action.without.begin(), action.without.end(),
                                          [&](const std::string& status) { return m_world.statuses().has(character, status); });
        if (!nodes || !tags || !without) {
            continue;
        }
        ++m_counts.hitTakenRules;
        for (const std::string& status : action.statuses) {
            StatusApply how;
            how.source = StatusSource{StatusSourceKind::ability, action.action, 0, character.id};
            (void)m_world.statuses().apply(character, status, how);
        }
    }
}

void Passives::hit_evaded(Entity& character, float wouldDeal) {
    if (character.kind != EntityKind::player || character.life.dead) {
        return;
    }
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    const std::uint32_t table = weapon != nullptr && !weapon->mastery.empty() ? gamedata::progression_id(weapon->mastery) : 0;
    const std::optional<Stamina::DodgeView> dodge = m_world.stamina().dodge_of(character.characterId);
    const gamedata::StaminaData* stamina = m_world.options().staminaData;
    HitContext hit;
    for (const PassiveRule& rule : rules_of(character, table)) {
        const gamedata::CellValues& row = *rule.cells;
        if (event_of(row) != "OnHitTakenWhileInvulnerable" || row.text("SelfApplyStatusEffect").empty() || !unknown_columns(row).empty()
            || !evaded_unsourced(row).empty() || row.number("HasGritActive") != 0.0F) {
            continue;
        }
        if (row.has("MyHealthPercent")
            && !compare(percent_of(character.vitals.health, character.vitals.healthMax), row.number("MyHealthPercent"), row.text("MyComparisonType"))) {
            continue;
        }
        if (row.has("MyStaminaPercent")
            && !compare(percent_of(character.vitals.stamina, character.vitals.staminaMax), row.number("MyStaminaPercent"),
                        row.text("MyStaminaComparisonType"))) {
            continue;
        }
        if (row.has("MyManaPercent")
            && !compare(percent_of(character.vitals.mana, character.vitals.manaMax), row.number("MyManaPercent"), row.text("MyManaComparisonType"))) {
            continue;
        }
        if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty() && m_world.statuses().has(character, status)) {
            continue;
        }
        if (row.number("RequireReaction") != 0.0F && !(wouldDeal > 0.0F)) {
            continue;
        }
        if (const std::string_view loads = row.text("EquipLoadCategory"); !loads.empty() && !list_has(loads, load_category(character))) {
            continue;
        }
        // The attacker is a creature: a rule of a hit between players does not pass.
        if (!row.text("AttackerVitalsCategory").empty() || same_text(row.text("TargetVitalsCategory"), "Player")) {
            continue;
        }
        // The immunity comes from a dodge (the action Dodge of the data and its fragment).
        if (const std::string_view actions = row.text("InAction");
            !actions.empty() && (stamina == nullptr || !list_has(actions, stamina->tables().dodgeAction))) {
            continue;
        }
        if (const std::string_view sequences = row.text("InSequence"); !sequences.empty() && (!dodge || !list_has(sequences, dodge->fragment))) {
            continue;
        }
        if (!activation_ready(character.characterId, rule, false)) {
            continue;
        }
        (void)activation_ready(character.characterId, rule, true);
        ++m_counts.evadedRules;
        m_world.gear().caster_effect(character, row, rule.id, rule.potency);
    }
    (void)hit;
}

void Passives::recheck(const std::string& characterId) {
    if (const auto found = m_characters.find(characterId); found != m_characters.end()) {
        found->second.nextAround = {};
    }
}

void Passives::tick() {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (abilities == nullptr) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<std::string> characters;
    m_world.for_each_character([&](const Entity& entity) { characters.push_back(entity.characterId); });
    for (const std::string& characterId : characters) {
        Entity* character = m_world.find_character(characterId);
        if (character == nullptr) {
            continue;
        }
        CharacterState& state = m_characters[characterId];
        if (now < state.nextAround) {
            continue;
        }
        state.nextAround = now + passive_rules::kAroundStep;
        const ItemInstance* item = Combat::weapon_in_hand(*character);
        const gamedata::CombatTables* tables = m_world.combat().tables();
        const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
        const std::uint32_t table = weapon != nullptr && !weapon->mastery.empty() ? gamedata::progression_id(weapon->mastery) : 0;
        std::set<std::string> wanted;
        std::vector<PassiveRule> around;
        // The status actions of the data (stage 3.3b-M2: Guarded Sprint gives WarHammer_Pass_SprintDmgReduction while the war hammer is
        // in the hand and the character moves): their statuses while the condition holds, with the action as the source.
        std::vector<const gamedata::ActionStatus*> actions;
        for (const gamedata::ActionStatus& action : abilities->tables().actionStatuses) {
            if (!character->life.dead && action_holds(action, *character, weapon, m_world.stamina().dodging(*character, now))) {
                wanted.insert(action.action);
                actions.push_back(&action);
            }
        }
        for (const PassiveRule& rule : rules_of(*character, table)) {
            const gamedata::CellValues& row = *rule.cells;
            if (!event_of(row).empty() || hit_rule(row) || !row.has("NumAroundMe") || row.text("SelfApplyStatusEffect").empty()
                || !unknown_columns(row).empty() || abilities->tracking_spell(rule.id) == nullptr
                || !tracking_supported(*abilities->tracking_spell(rule.id))) {
                continue;
            }
            ++m_counts.aroundLooked;
            const std::uint32_t count = character->life.dead ? 0 : around_count(row, rule.id, *character, nullptr);
            if (compare(static_cast<float>(count), row.number("NumAroundMe"), row.text("NumAroundComparisonType"))) {
                wanted.insert(rule.id);
                around.push_back(rule);
            }
        }
        // Stage B4: the perk scripts of the client (Vital Synergy, client code 0x238DEF0): a status while the stamina, and one while the health,
        // is below half its maximum (the test of the stamina: method +0x1008, the fraction of the maximum; of the health: method +0x1370 with the
        // arguments 1 and 4 and 0.5, an inference of the research, K13), only while the character has the perk row (HasAbility), with the row
        // as the source. Each step of kAroundStep (the period of the perk tick of the client is not known: a decision of an agent).
        std::vector<std::pair<std::string, std::string>> scripts;
        if (!character->life.dead) {
            const std::vector<PassiveRule> rules = rules_of(*character, table);
            for (const PerkScript& script : kPerkScripts) {
                if (std::none_of(rules.begin(), rules.end(), [&](const PassiveRule& rule) { return same_text(rule.id, script.ability); })) {
                    continue;
                }
                const float stamina = character->vitals.staminaMax > 0.0F ? character->vitals.stamina / character->vitals.staminaMax : 1.0F;
                const float health = character->vitals.healthMax > 0.0F ? character->vitals.health / character->vitals.healthMax : 1.0F;
                if (stamina < kPerkScriptLimit) {
                    scripts.emplace_back(std::string(script.ability) + "#" + script.lowStamina, script.lowStamina);
                }
                if (health < kPerkScriptLimit) {
                    scripts.emplace_back(std::string(script.ability) + "#" + script.lowHealth, script.lowHealth);
                }
            }
        }
        for (const auto& [key, status] : scripts) {
            wanted.insert(key);
        }
        ++m_counts.aroundSteps;
        // A status of a rule comes while its condition holds, with no end, and goes when it does not hold (the action data:
        // ClearOnExit and ClearOnEnd of the tracking spell).
        for (auto rule = state.around.begin(); rule != state.around.end();) {
            if (wanted.count(*rule) == 0) {
                m_world.statuses().remove_source(*character, StatusSource{StatusSourceKind::ability, *rule, 0, character->id}, StatusEnd::source);
                rule = state.around.erase(rule);
            } else {
                ++rule;
            }
        }
        for (const PassiveRule& rule : around) {
            if (state.around.count(rule.id) != 0) {
                continue;
            }
            for (const std::string_view status : parts_of(rule.cells->text("SelfApplyStatusEffect"))) {
                StatusApply how;
                how.permanent = true;
                how.potency = rule.potency;
                how.source = StatusSource{StatusSourceKind::ability, rule.id, 0, character->id};
                const StatusVerdict verdict = m_world.statuses().apply(*character, status, how);
                if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
                    ++m_counts.aroundStatuses;
                }
            }
            state.around.insert(rule.id);
        }
        for (const auto& [key, status] : scripts) {
            if (state.around.count(key) != 0) {
                continue;
            }
            StatusApply how;
            how.permanent = true;
            how.source = StatusSource{StatusSourceKind::ability, key, 0, character->id};
            const StatusVerdict verdict = m_world.statuses().apply(*character, status, how);
            if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
                ++m_counts.scriptStatuses;
            }
            state.around.insert(key);
        }
        for (const gamedata::ActionStatus* action : actions) {
            if (state.around.count(action->action) != 0) {
                continue;
            }
            for (const std::string& status : action->statuses) {
                StatusApply how;
                how.permanent = true;
                how.source = StatusSource{StatusSourceKind::ability, action->action, 0, character->id};
                const StatusVerdict verdict = m_world.statuses().apply(*character, status, how);
                if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
                    ++m_counts.aroundStatuses;
                }
            }
            state.around.insert(action->action);
        }
    }
}

void Passives::left(Entity& character) {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end()) {
        return;
    }
    for (const std::string& rule : state->second.around) {
        m_world.statuses().remove_source(character, StatusSource{StatusSourceKind::ability, rule, 0, character.id}, StatusEnd::leave);
    }
    m_characters.erase(state);
}

RuleView Passives::describe_row(const std::string& id, const gamedata::CellValues* cells, RuleOrigin origin) const {
    RuleView view;
    view.id = id;
    view.origin = origin;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (cells == nullptr) {
        view.note = "no row in the ability data (a file of an older tool)";
        return view;
    }
    const gamedata::CellValues& row = *cells;
    const std::string event = event_of(row);
    // Each event of the row, also an event that the evaluator does not have (OnBlockedHit, OnHealed).
    const bool anyEvent = std::any_of(row.numbers.begin(), row.numbers.end(), [](const auto& cell) { return is_event(cell.first) && cell.second != 0.0F; });
    const std::vector<std::string> unknown = unknown_columns(row);
    const bool hasValue = std::any_of(kValues.begin(), kValues.end(), [&](const std::string& column) { return row.has(column); })
                          || std::any_of(row.numbers.begin(), row.numbers.end(), [](const auto& cell) { return value_column(cell.first); });
    const bool hasEffect = hasValue || std::any_of(kEffects.begin(), kEffects.end(), [&](const std::string& column) { return row.has(column); });
    // The kinds (research abilities-shared.md Q6: fixed stat, status on condition, ability change, basic attack change, event rule).
    const auto kind = [&](const char* text) { view.kinds.emplace_back(text); };
    if (!anyEvent && !has_condition(row) && hasValue) {
        kind("fixed stat");
    }
    if (row.has("SelfApplyStatusEffect")) {
        kind("status on the caster");
    }
    if (row.has("OtherApplyStatusEffect")) {
        kind("status on the target");
    }
    if (row.has("InAction") || row.has("InSequence") || (row.has("DamageTableRow") && !row.has("AttackType"))) {
        kind("change of an ability");
    }
    if (const std::string_view types = row.text("AttackType"); list_has(types, "Light") || list_has(types, "Heavy")) {
        kind("change of a basic attack");
    }
    if (row.number("CooldownTimer") != 0.0F) {
        kind("change of a cooldown");
    }
    if (row.number("TrackHitCount") != 0.0F || row.has("NumSuccessfulHits") || row.has("NumConsecutiveHits")) {
        kind("a rule that counts hits");
    }
    if (row.has("CritChance") || row.has("CritDamage")) {
        kind("critical hit");
    }
    if (row.has("BaseDamage") && anyEvent) {
        kind("damage part");
    }
    if (row.has("ArmorPenetration")) {
        kind("armor penetration");
    }
    if (row.has("DmgPctToHealth")) {
        kind("heal of a part of the damage");
    }
    if (row.has("Stamina")) {
        kind("stamina gain");
    }
    if (row.has("NumAroundMe")) {
        kind("entities near");
    }
    // Stage B3: the time factor of a status, a weapon swap, a rule after an action.
    if (row.number("StatusEffectDurationReduction") != 0.0F) {
        kind("time of a status");
    }
    if (row.number("OnWeaponSwap") != 0.0F) {
        kind("weapon swap");
    }
    if (row.has("AfterAction") || row.has("AfterSequence")) {
        kind("after an action");
    }
    const bool actionTested = abilities != nullptr && abilities->action_tests(id);
    if (actionTested && !hasEffect) {
        kind("rule of an action");
    }
    // Code review of stage B4, finding B4-R5: the outgoing time factor (TargetStatusEffectDurationCats, TargetStatusEffectDurationMult) has a
    // consumer only for a row with no event (Passives::outgoing_factor). A row with an event and these columns (Global_Weapon_HercyneRaid_1:
    // OnHit) is not applied for that part: with no other effect the whole row is not applied, else the note of the row names the part.
    const bool eventDuration = anyEvent && (row.has("TargetStatusEffectDurationCats") || row.has("TargetStatusEffectDurationMult"));
    if (eventDuration) {
        const bool otherEffect = hasValue || std::any_of(kEffects.begin(), kEffects.end(), [&](const std::string& column) {
                                     return column != "TargetStatusEffectDurationCats" && column != "TargetStatusEffectDurationMult" && row.has(column);
                                 });
        if (!otherEffect) {
            view.note = "not applied: the outgoing time factor of an event (" + event_of(row)
                        + ": TargetStatusEffectDurationCats, TargetStatusEffectDurationMult) has no path in the backend (only a row with no event)";
            return view;
        }
    }
    // 1. An active ability: its plan (the plans of this backend), and its rules of a hit only with a plan.
    if (origin == RuleOrigin::active) {
        if (!Abilities::has_plan(id)) {
            view.note = "not applied yet: the active ability has no plan in this backend (its later weapon part)";
            return view;
        }
        if (!anyEvent && ability_value_rule(row)) {
            view.applied = true;
            view.note = "the active ability: its plan, and the values of its row at each hit of the ability (the evaluator)";
            return view;
        }
        if (!hasEffect || (!anyEvent && !hit_rule(row))) {
            view.applied = true;
            view.note = "the active ability: its plan (stage 3.3a and later) applies its effects";
            return view;
        }
    }
    // The uses of a node in the data of the actions (stage 3.3b-M2): the weapon actions of format 2 (a row of a strike, a transition inside
    // an ability, a fragment that an action selects, a spell cast; a grit, which the backend does not have; a conditional of the movement
    // of an action, which the client applies), and a status action (Guarded Sprint: its statuses while its condition holds). @return True
    // when the view has its result.
    const auto data_use = [&]() {
        if (const gamedata::WeaponActions* weapons = m_world.combat().weapon_actions()) {
            std::string grit;
            std::string motion;
            for (const gamedata::NodeUse* use : weapons->uses_of(gamedata::progression_id(id))) {
                if (use->use == "grit") {
                    grit = grit.empty() ? use->place : grit;
                } else if (use->use == "motion") {
                    motion = use->place;
                } else if (!view.applied) {
                    view.applied = true;
                    view.note = "applied: the weapon actions data (the " + use->use + " " + use->place + ")";
                }
            }
            if (view.applied) {
                return true;
            }
            if (!motion.empty()) {
                view.applied = true;
                view.note = "applied by the client: the conditional of the movement of the action (" + motion + "); the position check accepts its speed";
                return true;
            }
            if (!grit.empty()) {
                view.note = "not applied: grit (the action ActivateGrit of the fragment " + grit + "): the backend has no grit";
                return true;
            }
        }
        if (abilities == nullptr) {
            return false;
        }
        for (const gamedata::ActionStatus& action : abilities->tables().actionStatuses) {
            const bool names = std::any_of(action.terms.begin(), action.terms.end(), [&](const auto& term) {
                return std::any_of(term.begin(), term.end(), [&](const auto& literal) { return !literal.tag && same_text(literal.name, id); });
            });
            if (names) {
                view.applied = true;
                view.note = "applied by the evaluator: the status action " + action.action + " of the data (a status while its condition holds)";
                return true;
            }
        }
        // The hit-taken status actions (stage 3.4f-B: Mauler's Stamina, GreatAxe_Passive_StamWhenLowHP_Detect).
        for (const gamedata::ActionHitStatus& action : abilities->tables().actionHitStatuses) {
            if (std::any_of(action.nodes.begin(), action.nodes.end(), [&](const std::string& node) { return same_text(node, id); })) {
                view.applied = true;
                view.note = "applied by the evaluator: the hit-taken action " + action.action + " of the data (its statuses at a hit taken)";
                return true;
            }
        }
        // The dodge status actions (stage B3: Dodge and Weave, Passive_Bow_Skirmish_HasteOnDodge).
        for (const gamedata::ActionDodgeStatus& action : abilities->tables().actionDodgeStatuses) {
            if (std::any_of(action.nodes.begin(), action.nodes.end(), [&](const std::string& node) { return same_text(node, id); })) {
                view.applied = true;
                view.note = "applied by the evaluator: the dodge status action " + action.action + " of the data (its statuses at the end of a dodge)";
                return true;
            }
        }
        return false;
    };
    // Stage B3: a rule of the time of a status on the character (Speedy, Alacrity II; client getter 0x68308E0), a rule after the end of a dodge
    // (Evasive Tactics), and a rule of a weapon swap (Archer's Speed). Each one names the part that the backend does not apply.
    const auto columns_text = [](const std::vector<std::string>& columns) {
        std::string list;
        for (const std::string& column : columns) {
            list += (list.empty() ? "" : ", ") + column;
        }
        return list;
    };
    if (!anyEvent && row.number("StatusEffectDurationReduction") != 0.0F) {
        const bool outgoing = row.has("TargetStatusEffectDurationMult") || row.has("TargetStatusEffectDurationCats");
        if (duration_rule(row, unknown)) {
            if (row.text("StatusEffect").empty() && row.text("StatusEffectCategories").empty()) {
                view.note = "not applied: the time factor names no status and no category (the client getter 0x68308E0 matches a status by "
                            "StatusEffect or StatusEffectCategories; it does not read StatusEffectDurationCats)";
                return view;
            }
            view.applied = true;
            view.note = "applied by the evaluator: the time of a status on the character (StatusEffectDurationReduction x potency in the time factor)";
            if (outgoing) {
                // Stage B4: the outgoing part acts for a status that the character gives a different entity (Passives::outgoing_factor).
                view.note += "; the outgoing part TargetStatusEffectDurationMult for a status that the character gives a different entity";
            }
            return view;
        }
    }
    if (conditional_value(row)) {
        view.applied = true;
        view.note = "applied by the evaluator: a value of each hit while the character has the status (StatusEffect, StatusEffectCategories)";
        return view;
    }
    // Stage B4: the rules of the bow build of this stage, each one with what the backend does, or the column that it does not have.
    if (perk_script_row(id)) {
        view.applied = true;
        view.note = "applied by the evaluator: the perk script of the client 0x238DEF0 (Vital Synergy: a status while the stamina or the health is "
                    "below half its maximum); the row has no cell";
        return view;
    }
    if (origin == RuleOrigin::attribute && row.number("IsNonCombatAbility") != 0.0F) {
        const std::string_view status = row.text("OnEquipStatusEffect");
        view.applied = !status.empty();
        view.note = status.empty() ? "not applied: a row of an attribute threshold with no combat use (IsNonCombatAbility) and an event or an effect "
                                     "that the backend does not have (" + columns_text(unknown) + ")"
                                   : "a value of the gathering or of the bag (IsNonCombatAbility): the gathering system reads the EFF, ROL, MULT values of "
                                         + std::string(status) + "; a different value (Encumbrance, ItemClassWeightMods) is not in the backend";
        return view;
    }
    // A row with grit gets the note of grit below (the backend has no grit).
    const bool grit = row.number("HasGritActive") != 0.0F || row.number("TargetHasGritActive") != 0.0F;
    if (target_value_rule(row) && unknown.empty() && !grit) {
        view.applied = true;
        view.note = "applied by the evaluator: a rule of each hit with a condition of the target and no event";
        return view;
    }
    if (outgoing_rule(row) && !row.has("StatusEffectDurationReduction") && !grit) {
        if (!unknown.empty()) {
            view.note = "not applied: no rule in the backend for " + columns_text(unknown);
        } else if (const std::vector<std::string> columns = conditions_outside(row, kOutgoingConditions); !columns.empty()) {
            view.note = "not applied: an outgoing time factor has no source for the condition " + columns_text(columns);
        } else {
            view.applied = true;
            view.note = "applied by the evaluator: the outgoing time factor (TargetStatusEffectDurationMult x potency for a status of the categories "
                        + std::string(row.text("TargetStatusEffectDurationCats")) + " that the character gives a different entity)";
        }
        return view;
    }
    if (fixed_cooldown_rule(row) && !grit) {
        if (!unknown.empty()) {
            view.note = "not applied: no rule in the backend for " + columns_text(unknown);
        } else if (const std::vector<std::string> columns = self_unsourced(row); !columns.empty()) {
            view.note = "not applied: a fixed cooldown rule has no source for the condition " + columns_text(columns);
        } else {
            view.applied = true;
            view.note = "applied by the evaluator: a fixed cooldown reduction (CooldownTimer x potency of the cooldown of each ability of its scope "
                        "at the cast, with the cooldown line of the jewelry)";
        }
        return view;
    }
    if (event == "OnUsedConsumable" && !grit) {
        if (!unknown.empty()) {
            view.note = "not applied: no rule in the backend for " + columns_text(unknown);
        } else if (const std::vector<std::string> columns = conditions_outside(row, kConsumableConditions); !columns.empty()) {
            view.note = "not applied: the use of a consumable has no source for the condition " + columns_text(columns);
        } else if (row.number("CooldownTimer") != 0.0F && row.text("CDRImmediatelyOptions").empty()) {
            view.applied = true;
            view.note = "applied by the evaluator (OnUsedConsumable): the cooldown of a consumable"
                        + std::string(row.text("CooldownId").empty() ? "" : " of the cooldown id " + std::string(row.text("CooldownId")))
                        + " times 1 + CooldownTimer x potency";
        } else if (row.number("CooldownTimer") == 0.0F && (!row.text("SelfApplyStatusEffect").empty() || !row.text("RemoveStatusEffects").empty())) {
            view.applied = true;
            view.note = "applied by the evaluator (OnUsedConsumable): a status on the character at the use of a consumable of its list";
        } else {
            view.note = "not applied: the effect of the use of a consumable (CDRImmediatelyOptions, a cooldown change and a status together) is not in "
                        "the backend";
        }
        return view;
    }
    if (event == "OnHitTaken" && row.number("BaseDamageReduction") != 0.0F && row.text("SelfApplyStatusEffect").empty() && unknown.empty() && !grit) {
        if (const std::vector<std::string> columns = taken_unsourced(row); !columns.empty()) {
            view.note = "not applied: a hit taken has no source for the condition " + columns_text(columns);
        } else {
            view.applied = true;
            view.note = "applied by the evaluator (OnHitTaken): a part of the damage of a creature hit that the character does not take"
                        + std::string(row.has("AttackType") ? " (the attack type of the damage row of the attack: " + std::string(row.text("AttackType")) + ")"
                                                            : "");
        }
        return view;
    }
    if ((row.has("AfterAction") || row.has("AfterSequence")) && unknown.empty() && (anyEvent || self_effect(row) || !self_unsupported(row).empty())) {
        const gamedata::StaminaData* stamina = m_world.options().staminaData;
        const std::string dodgeAction = stamina != nullptr ? stamina->tables().dodgeAction : std::string("Dodge");
        bool dodge = !row.has("AfterAction") || list_has(row.text("AfterAction"), dodgeAction);
        for (const std::string_view fragment : parts_of(row.text("AfterSequence"))) {
            dodge = dodge && stamina != nullptr
                    && std::any_of(stamina->tables().fragments.begin(), stamina->tables().fragments.end(),
                                   [&](const gamedata::DodgeFragment& known) { return same_text(known.name, fragment); });
        }
        if (anyEvent) {
            view.note = "not applied: an event in the window after an action (AfterAction, AfterSequence, Duration) is not in the backend";
        } else if (!dodge) {
            view.note = "not applied: a rule after the action " + std::string(row.text("AfterAction")) + " (the backend has only the end of a dodge)";
        } else if (const std::vector<std::string> columns = self_unsourced(row, true); !columns.empty()) {
            view.note = "not applied: the end of a dodge has no source for the condition " + columns_text(columns);
        } else if (const std::vector<std::string> effects = self_unsupported(row); !effects.empty()) {
            view.note = "not applied: the effect " + columns_text(effects) + " after the end of a dodge is not in the backend";
        } else {
            view.applied = true;
            view.note = "applied by the evaluator: after the end of a dodge (AfterAction, AfterSequence; one time at the mark CanTransition of the last dodge)";
        }
        return view;
    }
    if (event == "OnWeaponSwap" && unknown.empty() && (self_effect(row) || !self_unsupported(row).empty())) {
        if (const std::vector<std::string> columns = self_unsourced(row); !columns.empty()) {
            view.note = "not applied: a weapon swap has no source for the condition " + columns_text(columns);
        } else if (const std::vector<std::string> effects = self_unsupported(row); !effects.empty()) {
            view.note = "not applied: the effect " + columns_text(effects) + " of a weapon swap is not in the backend";
        } else {
            view.applied = true;
            view.note = "applied by the evaluator (OnWeaponSwap: a change of the weapon slot with the key of a weapon)";
        }
        return view;
    }
    // 2. A node with no cell of an effect: the action data tests it (a rule of an action: Meteoric Crater has the marks OnHit and
    // InAction and no effect), the data of the actions uses it, or it has no effect of its own.
    if (!hasEffect && unknown.empty()) {
        // Stage B1: a cast of the fragment of an ability action of the projectile data with a condition on the node (Contained
        // Detonation: the cast of Spell_Ability_Bow_Explosive_Upgrade in Ability_Bow_ExplosiveArrow), for an ability with a plan.
        if (const gamedata::ProjectileData* projectiles = m_world.options().projectileData;
            projectiles != nullptr && projectiles->loaded()) {
            const auto names = [&](const std::vector<std::string>& nodes) {
                return std::any_of(nodes.begin(), nodes.end(), [&](const std::string& node) { return same_text(node, id); });
            };
            // The cast that needs the node first (its spell is the change of the node); a cast that only forbids it is the base.
            for (const bool needed : {true, false}) {
                for (const gamedata::AbilityActionData& action : projectiles->tables().actions) {
                    for (const gamedata::ActionCast& cast : action.casts) {
                        if (!view.applied && Abilities::has_plan(action.ability) && names(needed ? cast.needs : cast.forbids)) {
                            view.applied = true;
                            view.note = "applied: the fragment " + action.fragment + " of the action " + action.action + " casts " + cast.spell
                                        + (needed ? " with the node" : " only with no node") + " (the projectile data)";
                        }
                    }
                }
            }
        }
        if (view.applied) {
            return view;
        }
        if (actionTested) {
            const std::vector<std::string> lists = abilities->action_lists_of(id);
            bool selects = false;
            for (const gamedata::ActionSpell& action : abilities->tables().actionSpells) {
                const auto names = [&](const std::vector<std::string>& nodes) {
                    return std::any_of(nodes.begin(), nodes.end(), [&](const std::string& node) { return same_text(node, id); });
                };
                if (names(action.needs) || names(action.excludes)) {
                    selects = true;
                    if (Abilities::action_selected(action.action)) {
                        view.applied = true;
                        view.note = "applied: the action " + action.action + " of the data selects the spell " + action.spell;
                    }
                }
            }
            if (!view.applied && Abilities::plan_node(id)) {
                view.applied = true;
                view.note = "applied: the plan of the ability (stage 3.3a) selects its row, as the action data does";
            }
            if (!view.applied && data_use()) {
                return view;
            }
            if (!view.applied) {
                view.note = std::string("not applied yet: the action data (") + (lists.empty() ? std::string("?") : lists.front())
                            + (selects ? ") selects a spell of an ability that this backend does not have"
                                       : ") applies it in an action of the client that this backend does not have");
            }
            return view;
        }
        if (data_use()) {
            return view;
        }
        view.note = anyEvent || has_condition(row) ? "not applied: the row has a condition and no effect that the backend has"
                                                    : "no cell of a rule in the data";
        return view;
    }
    if (!unknown.empty()) {
        std::string list;
        for (const std::string& column : unknown) {
            list += (list.empty() ? "" : ", ") + column;
        }
        view.note = "not applied: no rule in the backend for " + list;
        return view;
    }
    if (row.number("HasGritActive") != 0.0F || row.number("TargetHasGritActive") != 0.0F) {
        view.note = "not applied: the backend has no grit";
        return view;
    }
    // 3. The ability of the rule (InAction, InSequence, or the chain of RequiredAbilityId): a rule of an ability with no plan here
    // has no hit that it can change.
    std::string required(row.text("RequiredAbilityId"));
    for (int step = 0; step < 6 && !required.empty() && abilities != nullptr; ++step) {
        if (Abilities::has_plan(required)) {
            break;
        }
        const gamedata::AbilityRow* parent = abilities->ability_row(required);
        if (parent == nullptr || parent->cells.text("RequiredAbilityId").empty()) {
            view.note = "not applied yet: its ability " + required + " has no plan in this backend";
            return view;
        }
        required = std::string(parent->cells.text("RequiredAbilityId"));
    }
    // A name of the list is an ability with a plan, or (stage 3.3b-M2) the fragment of the action of such an ability in the weapon
    // actions (Armor Breaker: Ability_WarHammer_BlockBreaker).
    const auto planned_name = [&](std::string_view name) {
        if (Abilities::has_plan(name)) {
            return true;
        }
        // Stage B1: the action or the fragment of an ability action of the projectile data whose ability has a plan (Bow_RapidShot).
        if (const gamedata::ProjectileData* projectiles = m_world.options().projectileData; projectiles != nullptr && projectiles->loaded()) {
            for (const gamedata::AbilityActionData& action : projectiles->tables().actions) {
                if (Abilities::has_plan(action.ability) && (same_text(action.action, name) || same_text(action.fragment, name))) {
                    return true;
                }
            }
        }
        const gamedata::WeaponActions* weapons = m_world.combat().weapon_actions();
        if (weapons == nullptr) {
            return false;
        }
        for (const gamedata::WeaponActionSet& weapon : weapons->tables().weapons) {
            for (const gamedata::AbilityAction& action : weapon.abilities) {
                if (!Abilities::has_plan(action.ability)) {
                    continue;
                }
                if (same_text(action.fragmentName, name)
                    || std::any_of(action.selections.begin(), action.selections.end(),
                                   [&](const gamedata::AbilityAction::Selection& selection) { return same_text(selection.fragmentName, name); })) {
                    return true;
                }
            }
        }
        return false;
    };
    // A hit in the immune window of a dodge (stage 3.4f-B): the action Dodge and the dodge fragments of the stamina data have their rule
    // in the stamina system (Passives::hit_evaded tests them).
    const gamedata::StaminaData* stamina = m_world.options().staminaData;
    const auto dodge_name = [&](std::string_view name) {
        if (event != "OnHitTakenWhileInvulnerable" || stamina == nullptr || !stamina->loaded()) {
            return false;
        }
        const gamedata::StaminaTables& tables = stamina->tables();
        return same_text(tables.dodgeAction, name)
               || std::any_of(tables.fragments.begin(), tables.fragments.end(),
                              [&](const gamedata::DodgeFragment& fragment) { return same_text(fragment.name, name); });
    };
    for (const char* column : {"InAction", "InSequence"}) {
        if (const std::string_view list = row.text(column); !list.empty()) {
            const std::vector<std::string_view> names = parts_of(list);
            if (std::none_of(names.begin(), names.end(), planned_name) && std::none_of(names.begin(), names.end(), dodge_name)) {
                view.note = "not applied yet: its ability " + std::string(list) + " has no plan in this backend";
                return view;
            }
        }
    }
    if (row.number("OnHealed") != 0.0F && row.number("HealScalingValueMultiplier") != 0.0F && (event == "OnHealed" || hit_rule(row) == false)) {
        // A rule of a heal (stage 3.3b-S6): its HealScalingValueMultiplier is a part of the outgoing heal (Passives::heal_bonus) with the
        // conditions that a heal has a source for; the effects after a heal are not in the backend.
        if (const std::vector<std::string> columns = heal_unsourced(row); !columns.empty()) {
            std::string list;
            for (const std::string& column : columns) {
                list += (list.empty() ? "" : ", ") + column;
            }
            view.note = "not applied: a heal has no source for the condition " + list;
            return view;
        }
        if (event == "OnHealed") {
            if (const std::vector<std::string> columns = heal_after_effects(row); !columns.empty()) {
                std::string list;
                for (const std::string& column : columns) {
                    list += (list.empty() ? "" : ", ") + column;
                }
                view.note = "not applied: an effect after a heal (" + list + ") is not in the backend";
                return view;
            }
            view.applied = true;
            view.note = "applied by the evaluator (OnHealed): a part of the outgoing heal of the character";
            return view;
        }
    }
    if (event == "OnHealed") {
        view.note = "not applied: an effect after a heal is not in the backend (the backend applies HealScalingValueMultiplier of OnHealed)";
        return view;
    }
    if (event == "OnHitTakenWhileInvulnerable") {
        if (row.text("SelfApplyStatusEffect").empty()) {
            view.note = "not applied: a hit in the immune window of a dodge with an effect other than a status on the character";
            return view;
        }
        if (!row.text("AttackerVitalsCategory").empty() || same_text(row.text("TargetVitalsCategory"), "Player")) {
            view.note = "not applied: a hit between players (the attacker of a hit in a dodge is a creature)";
            return view;
        }
        if (const std::vector<std::string> columns = evaded_unsourced(row); !columns.empty()) {
            std::string list;
            for (const std::string& column : columns) {
                list += (list.empty() ? "" : ", ") + column;
            }
            view.note = "not applied: a hit in the immune window of a dodge has no source for the condition " + list;
            return view;
        }
    }
    if (event == "OnHitTaken" && row.text("SelfApplyStatusEffect").empty()) {
        view.note = "not applied: a hit taken with an effect other than a status on the caster";
        return view;
    }
    if (event == "OnHitTaken") {
        if (const std::vector<std::string> columns = taken_unsourced(row); !columns.empty()) {
            std::string list;
            for (const std::string& column : columns) {
                list += (list.empty() ? "" : ", ") + column;
            }
            view.note = "not applied: a hit taken has no source for the condition " + list;
            return view;
        }
    }
    if (row.has("NumAroundMe")) {
        const gamedata::SpellRow* spell = abilities != nullptr ? abilities->tracking_spell(id) : nullptr;
        if (spell == nullptr) {
            view.note = "not applied: no tracking spell of the rule in the data";
            return view;
        }
        if (!tracking_supported(*spell)) {
            view.note = "not applied: the tracking spell " + spell->id + " has the shape " + std::string(spell->cells.text("ShapeTypes"))
                        + " (the backend counts the entities in a sphere and in a box with a Length only)";
            return view;
        }
        if (!list_has(spell->cells.text("TargetTypes"), "Enemy") && !(event.empty() && hit_rule(row))) {
            view.note = "not applied: the tracking spell counts friends (no friend relation in the backend)";
            return view;
        }
    }
    // 4. A row with no event that is not a rule of a hit (no DamageTableRow, AttackType, or RemoteDamageTableRow), not the values of an
    // ability, and no rule of the entities near (stage B0, the list of the bow build: "no silent rule"). A condition of a hit with no
    // event (Finishing Shot, Passive_Bow_Hunter_DmgVsLow: TargetHealthPercent 50 and BaseDamage) has no hit in the evaluator
    // (before_hit takes such a row only as a rule of a hit), and the gear values take a node row only with no condition
    // (Passives::fixed). An effect of a fixed row other than its values (Refreshing, Global_Common_CDR: CooldownTimer -0.02 with
    // CDRImmediatelyOptions) is not in the backend: the gear values take only the values of a fixed stat.
    if (event.empty() && !hit_rule(row) && !ability_value_rule(row) && !row.has("NumAroundMe") && !row.has("SelfApplyStatusEffect")
        && !row.has("OtherApplyStatusEffect")) {
        std::string conditions;
        for (const std::string& column : kConditions) {
            if (column != "EquipLoadCategory" && row.has(column)) {
                conditions += (conditions.empty() ? "" : ", ") + column;
            }
        }
        if (!conditions.empty() && origin != RuleOrigin::perk) {
            view.note = "not applied: the condition " + conditions + " with no event is not a rule of a hit in the evaluator";
            return view;
        }
        std::string effects;
        for (const char* column : {"CooldownTimer", "CDRImmediatelyOptions", "DmgPctToHealth", "CastSpell", "Stamina", "HealScalingValueMultiplier"}) {
            if (row.number(column) != 0.0F || !row.text(column).empty()) {
                effects += (effects.empty() ? "" : ", ") + std::string(column);
            }
        }
        if (!effects.empty()) {
            view.note = "not applied: the effect " + effects + " of a row with no event (the gear values take only the values of a fixed stat)";
            return view;
        }
    }
    // 5. A status with no event and no condition (Unyielding: the fortify of the vortex): the action or the spell of the data gives
    // it; the evaluator does not.
    if (event.empty() && !hit_rule(row) && !row.has("NumAroundMe") && (row.has("SelfApplyStatusEffect") || row.has("OtherApplyStatusEffect"))) {
        // A status that an area of a spell slice gives to the friends in it (stage 3.3b-M2: Unyielding, the AreaStatusEffectComponent of
        // the upgraded vortex of Gravity Well; the spell system gives it).
        if (const gamedata::SpellData* spells = m_world.options().spellData; spells != nullptr && spells->loaded()) {
            const std::string_view status = row.text("SelfApplyStatusEffect");
            for (const gamedata::SpellSlice& slice : spells->tables().slices) {
                for (const gamedata::SpellAreaStatus& area : slice.areaStatuses) {
                    if (!status.empty() && std::any_of(area.statuses.begin(), area.statuses.end(),
                                                       [&](const std::string& given) { return same_text(given, status); })) {
                        view.applied = true;
                        view.note = "applied: the area of the spell slice " + slice.path + " gives its status to the friends in it";
                        return view;
                    }
                }
            }
        }
        view.note = "not applied: a status of the row with no event (the action or the spell of the data gives it)";
        if (actionTested && Abilities::plan_node(id)) {
            view.note = "the plan selects the spell of the node (the action data); " + view.note;
        }
        return view;
    }
    view.applied = true;
    view.note = "applied by the evaluator"
                + std::string(event.empty() ? (hit_rule(row) ? " (a rule of a hit)"
                                                             : ability_value_rule(row) ? " (the values of the row at each hit of its ability)"
                                                             : (row.has("NumAroundMe") ? " (a status while the condition holds)" : " (fixed)"))
                                            : " (" + event + ")");
    if (eventDuration) {
        view.note += "; not applied: its outgoing time factor (TargetStatusEffectDurationCats, TargetStatusEffectDurationMult: no path for an event)";
    }
    return view;
}

std::vector<RuleView> Passives::describe(const Entity& character) const {
    std::vector<RuleView> views;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::ProgressionTables* progression = m_world.options().progressionTables;
    if (progression != nullptr) {
        for (const auto& [table, nodes] : character.skills.pools) {
            for (const auto& [id, state] : nodes) {
                const gamedata::AbilityNode* node = state.level > 0 ? progression->node(id) : nullptr;
                if (node == nullptr) {
                    continue;
                }
                const gamedata::AbilityRow* row = abilities != nullptr ? abilities->ability_row(node->id) : nullptr;
                views.push_back(describe_row(node->id, row != nullptr ? &row->cells : nullptr, node->active ? RuleOrigin::active : RuleOrigin::node));
                // Stage B2: the helper rows of the node (RequiredEquippedAbilityId).
                if (abilities != nullptr) {
                    for (const gamedata::AbilityRow* helper : abilities->helpers_of(node->id)) {
                        if (progression->node(helper->id) == nullptr) {
                            views.push_back(describe_row(helper->id, &helper->cells, RuleOrigin::helper));
                        }
                    }
                }
            }
        }
    }
    if (const gamedata::PerkData* perks = m_world.perk_data()) {
        for (const GearPerk& perk : m_world.gear().perks_of(character)) {
            for (const std::string& name : perk.perk->abilities) {
                views.push_back(describe_row(name, perks->ability(name), RuleOrigin::perk));
            }
        }
    }
    // Stage B2: the rows that the statuses of the character equip now (EquipAbility).
    for (const PassiveRule& rule : status_rules(character)) {
        views.push_back(describe_row(rule.id, rule.cells, RuleOrigin::status));
    }
    // Stage B4: the threshold abilities of the attributes of the character (also the rows with no combat use).
    for (const PassiveRule& rule : attribute_rules(character, true)) {
        views.push_back(describe_row(rule.id, rule.cells, RuleOrigin::attribute));
    }
    return views;
}

std::vector<RuleView> Passives::describe_table(std::uint32_t table) const {
    std::vector<RuleView> views;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::ProgressionTables* progression = m_world.options().progressionTables;
    if (progression == nullptr) {
        return views;
    }
    for (const gamedata::AbilityNode& node : progression->nodes()) {
        if (node.table != table) {
            continue;
        }
        const gamedata::AbilityRow* row = abilities != nullptr ? abilities->ability_row(node.id) : nullptr;
        views.push_back(describe_row(node.id, row != nullptr ? &row->cells : nullptr, node.active ? RuleOrigin::active : RuleOrigin::node));
        // Stage B2: the helper rows of the node (RequiredEquippedAbilityId).
        if (abilities != nullptr) {
            for (const gamedata::AbilityRow* helper : abilities->helpers_of(node.id)) {
                if (progression->node(helper->id) == nullptr) {
                    views.push_back(describe_row(helper->id, &helper->cells, RuleOrigin::helper));
                }
            }
        }
    }
    return views;
}

} // namespace soulwarden::backend::world

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
                                   "OnHitTakenWhileInvulnerable"};

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
    "DisableCastSpellDurability"};

/** The columns of a value of a rule (a fixed stat, or a part of a hit). */
const std::set<std::string, std::less<>> kValues = {
    "BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage", "HitFromBehindDamage", "PhysicalArmor",
    "ElementalArmor", "MaxHealth", "MaxHealthMod", "BaseDamageReduction", "MODStrength", "MODDexterity", "MODIntelligence",
    "MODFocus", "MODConstitution"};

/** The conditions that the evaluator has (Passives::conditions_pass, counts_pass). */
const std::set<std::string, std::less<>> kConditions = {
    "AttackType", "DamageTableRow", "RemoteDamageTableRow", "TargetStatusEffectCategory", "TargetStatusEffectComparison",
    "TargetHealthPercent", "TargetComparisonType", "MyHealthPercent", "MyComparisonType", "MyStaminaPercent",
    "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType", "HasGritActive", "TargetHasGritActive",
    "AttackerVitalsCategory", "TargetVitalsCategory", "DontHaveStatusEffect", "InSequence", "InAction", "RequireReaction",
    "TrackHitCount", "NumberOfTrackedHits", "NumberOfHitsComparisonType", "NumSuccessfulHits", "NumConsecutiveHits",
    "MaxConsecutiveHits", "ResetConsecutiveOnSuccess", "DamageIsMelee", "DamageIsRanged", "NumAroundMe", "MaxNumAroundMe",
    "NumAroundComparisonType", "EquipLoadCategory"};

/** The effects that the evaluator has (before the damage, after the hit, or a fixed stat through the gear values). */
const std::set<std::string, std::less<>> kEffects = {
    "HealScalingValueMultiplier",
    "BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage", "HitFromBehindDamage", "SelfApplyStatusEffect",
    "OtherApplyStatusEffect", "DmgPctToHealth", "CooldownTimer", "CDRImmediatelyOptions", "PhysicalArmor", "ElementalArmor", "CastSpell",
    "MaxHealth", "MaxHealthMod", "BaseDamageReduction", "MODStrength", "MODDexterity", "MODIntelligence", "MODFocus",
    "MODConstitution",
    // Stage 3.4f-B: a stamina gain of a hit rule (the runeglass rows Runeglass_Just_Stamina_*: OnHit, RequireReaction, 1 or 4 stamina).
    "Stamina"};

/**
 * The conditions that a hit taken has a source for (code review of stage 3.3b-S5, finding F6): the values of the character (its
 * health after the hit, its stamina, its mana, its statuses), grit (none: such a row does not pass), the damage of the hit
 * (RequireReaction), and the hit counts. A rule with OnHitTaken and a different condition (the attacker, the row, the target) is
 * not applied: Passives::describe_row names the column.
 */
const std::set<std::string, std::less<>> kTakenConditions = {
    "MyHealthPercent", "MyComparisonType", "MyStaminaPercent", "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType",
    "DontHaveStatusEffect", "HasGritActive", "RequireReaction", "TrackHitCount", "NumberOfTrackedHits", "NumberOfHitsComparisonType",
    "NumSuccessfulHits", "NumConsecutiveHits", "MaxConsecutiveHits", "ResetConsecutiveOnSuccess"};

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
            }
        }
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

bool Passives::conditions_pass(const PassiveRule& rule, const Entity& attacker, const Entity& target, const HitContext& hit) const {
    const gamedata::CellValues& row = *rule.cells;
    if (!unknown_columns(row).empty()) {
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
    if (const std::string_view categories = row.text("TargetStatusEffectCategory"); !categories.empty()) {
        // The categories of the effects on the creature (EffectCategories of their status rows).
        bool has = false;
        const gamedata::AbilityData* abilities = m_world.options().abilityData;
        const WorldClock::time_point now = m_world.clock().now();
        for (const CreatureEffect& effect : target.creature.effects) {
            const gamedata::StatusRow* status = abilities != nullptr && now < effect.ends ? abilities->status(effect.status) : nullptr;
            if (status == nullptr) {
                continue;
            }
            for (const std::string_view category : parts_of(categories)) {
                has = has || list_has(status->cells.text("EffectCategories"), category);
            }
        }
        const bool wanted = lower(row.text("TargetStatusEffectComparison")) != "notequal";
        if (has != wanted) {
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
    for (const char* column : {"InSequence", "InAction"}) {
        if (const std::string_view sequence = row.text(column);
            !sequence.empty() && (hit.ability.empty() || !list_has(sequence, hit.ability)) && (hit.fragment.empty() || !list_has(sequence, hit.fragment))) {
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
        m_world.for_each_near(character.position, length, [&](Entity& entity) {
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
    m_world.for_each_near(character.position, spell->radius, [&](Entity& entity) {
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
        const bool takes = event == "OnHit" || event == "OnCrit" || (event.empty() && (hit_rule(row) || ability_value_rule(row)))
                           || (event == "OnHeadShot" && hit.head) || (event == "OnHitBehind" && hit.behind);
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
                            || !row.text("CastSpell").empty() || row.number("Stamina") != 0.0F || counted;
        // A rule with OnCrit and only CritChance changed the roll (before_hit): it has no effect after the hit.
        if (!takes || !effect) {
            continue;
        }
        if (!conditions_pass(rule, *own, *hurt, hit)) {
            // NumConsecutiveHits: a hit that the rule does not take ends the sequence of the hits.
            if (row.has("NumConsecutiveHits") && unknown_columns(row).empty()) {
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
    if (const float part = row.number("CooldownTimer"); part < 0.0F) {
        // CDRImmediatelyOptions: ActiveWeapon (each ability of the bar of the weapon in the hand), AbilitySpecific (the
        // ability of CooldownId), AllExcept (each ability of the bar but the one of CooldownId). A part of the time of the data.
        const std::string_view options = row.text("CDRImmediatelyOptions");
        const std::string_view cooldown = row.text("CooldownId");
        if (same_text(options, "ActiveWeapon")) {
            m_world.abilities().reduce_weapon_cooldowns(attacker, -part * rule.potency);
        } else if (same_text(options, "AbilitySpecific")) {
            m_world.abilities().reduce_weapon_cooldowns(attacker, -part * rule.potency, cooldown);
        } else if (same_text(options, "AllExcept")) {
            m_world.abilities().reduce_weapon_cooldowns(attacker, -part * rule.potency, {}, cooldown);
        }
        ++m_counts.cooldownReductions;
        ++m_world.gear().m_counts.cooldownReductions;
    }
    if (const std::string_view statuses = row.text("OtherApplyStatusEffect"); !statuses.empty()) {
        for (const std::string_view status : parts_of(statuses)) {
            if (Entity* hurt = m_world.creatures().find(targetId); hurt != nullptr) {
                if (Entity* own = m_world.find(attackerId)) {
                    target_status(rule, *own, *hurt, status, hit);
                }
            }
        }
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

void Passives::target_status(const PassiveRule& rule, Entity& attacker, Entity& target, std::string_view status, const HitContext& hit) {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::StatusRow* row = abilities != nullptr ? abilities->status(status) : nullptr;
    if (row == nullptr || target.creature.dead) {
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

void Passives::hit_taken(Entity& character, float dealt) {
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
    const bool actionTested = abilities != nullptr && abilities->action_tests(id);
    if (actionTested && !hasEffect) {
        kind("rule of an action");
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
        return false;
    };
    // 2. A node with no cell of an effect: the action data tests it (a rule of an action: Meteoric Crater has the marks OnHit and
    // InAction and no effect), the data of the actions uses it, or it has no effect of its own.
    if (!hasEffect && unknown.empty()) {
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
    }
    return views;
}

} // namespace soulwarden::backend::world

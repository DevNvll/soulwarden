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

/** The events that the backend has a source for (stage 3.3b-S5). */
constexpr const char* kEvents[] = {"OnHit", "OnCrit", "OnKill", "OnHitTaken", "OnHeadShot", "OnHitBehind"};

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
    "IsGlobalAbility", "EquipWhenUnsheathed", "ExcludeFromGameModes", "SupportsSmartcast", "UISpellForManaCheck"};

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
    "NumAroundComparisonType"};

/** The effects that the evaluator has (before the damage, after the hit, or a fixed stat through the gear values). */
const std::set<std::string, std::less<>> kEffects = {
    "BaseDamage", "CritChance", "CritDamage", "ArmorPenetration", "HeadshotDamage", "HitFromBehindDamage", "SelfApplyStatusEffect",
    "OtherApplyStatusEffect", "DmgPctToHealth", "CooldownTimer", "CDRImmediatelyOptions", "PhysicalArmor", "ElementalArmor",
    "MaxHealth", "MaxHealthMod", "BaseDamageReduction", "MODStrength", "MODDexterity", "MODIntelligence", "MODFocus",
    "MODConstitution"};

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
    // InSequence and InAction: the hit is a hit of the cast of an ability of the list (the action of the ability).
    for (const char* column : {"InSequence", "InAction"}) {
        if (const std::string_view sequence = row.text(column); !sequence.empty() && (hit.ability.empty() || !list_has(sequence, hit.ability))) {
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
        if (abilities == nullptr || abilities->tracking_spell(rule.id) == nullptr) {
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
    // A rule that the spell of the area of the hit tracks (Crowded Well: the vortex): the creatures of that area.
    if (hit != nullptr && !hit->spell.empty() && same_text(hit->spell, spell->id)) {
        return hit->areaCount;
    }
    if (!list_has(spell->cells.text("TargetTypes"), "Enemy")) {
        return 0; // A friend in the volume: the backend has no friend relation in a world with one player (a group of one).
    }
    // The creatures that live and are not on their way back, in the radius of the spell (and 3 m up or down).
    std::uint32_t count = 0;
    const float height = spell->height > 0.0F ? spell->height : 3.0F;
    m_world.for_each_near(character.position, spell->radius, [&](Entity& entity) {
        if (entity.kind == EntityKind::creature && !entity.creature.dead && !entity.creature.immune()
            && horizontal_distance(entity.position, character.position) <= spell->radius
            && std::fabs(entity.position.z - character.position.z) <= height) {
            ++count;
        }
    });
    return count;
}

bool Passives::counts_pass(const PassiveRule& rule, const Entity& attacker, const HitContext& hit, bool spend) {
    const gamedata::CellValues& row = *rule.cells;
    CharacterState& state = m_characters[attacker.characterId];
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
        const bool takes = event == "OnHit" || event == "OnCrit" || (event.empty() && hit_rule(row)) || (event == "OnHeadShot" && hit.head)
                           || (event == "OnHitBehind" && hit.behind);
        if (!takes || !conditions_pass(rule, attacker, target, hit)) {
            continue;
        }
        // A rule with OnCrit and CritChance changes the roll (client text of Critical Condition: "increased critical chance"):
        // only its critical chance counts before the hit.
        if (event == "OnCrit") {
            bonus.critChance += row.number("CritChance") * rule.potency;
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
        const bool effect = !row.text("OtherApplyStatusEffect").empty() || !row.text("SelfApplyStatusEffect").empty()
                            || row.number("DmgPctToHealth") > 0.0F || (row.number("CooldownTimer") < 0.0F && !row.text("CDRImmediatelyOptions").empty());
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
        // A heal of a part of the damage of the hit (Insatiable Gravity Well, Prevailing Spirit, Critical Gains), with the heal
        // factor of the equip load (stage 1.3b).
        const float amount = part * rule.potency * hit.dealt * m_world.gear().heal_factor(attacker);
        m_world.health().heal_amount(attacker, amount, rule.id.c_str());
        ++m_counts.heals;
        ++m_world.gear().m_counts.perkHeals;
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

void Passives::hit_taken(Entity& character) {
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
        // The conditions of a hit taken that the backend has: grit (none: the row does not pass) and the hit counts.
        if (row.number("HasGritActive") != 0.0F || row.has("AttackType") || row.has("DamageTableRow")) {
            continue;
        }
        if (!activation_ready(character.characterId, rule, false) || !counts_pass(rule, character, hit, true)) {
            continue;
        }
        (void)activation_ready(character.characterId, rule, true);
        ++m_counts.hitTakenRules;
        m_world.gear().caster_effect(character, row, rule.id, rule.potency);
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
        for (const PassiveRule& rule : rules_of(*character, table)) {
            const gamedata::CellValues& row = *rule.cells;
            if (!event_of(row).empty() || hit_rule(row) || !row.has("NumAroundMe") || row.text("SelfApplyStatusEffect").empty()
                || !unknown_columns(row).empty() || abilities->tracking_spell(rule.id) == nullptr) {
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
        if (!hasEffect || (!anyEvent && !hit_rule(row))) {
            view.applied = true;
            view.note = "the active ability: its plan (stage 3.3a and later) applies its effects";
            return view;
        }
    }
    // 2. A node with no cell of an effect: the action data tests it (a rule of an action: Meteoric Crater has the marks OnHit and
    // InAction and no effect), or it has no effect of its own.
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
            if (!view.applied) {
                view.note = std::string("not applied yet: the action data (") + (lists.empty() ? std::string("?") : lists.front())
                            + (selects ? ") selects a spell of an ability that this backend does not have"
                                       : ") applies it in an action of the client that this backend does not have");
            }
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
    for (const char* column : {"InAction", "InSequence"}) {
        if (const std::string_view list = row.text(column); !list.empty()) {
            const std::vector<std::string_view> names = parts_of(list);
            if (std::none_of(names.begin(), names.end(), [](std::string_view name) { return Abilities::has_plan(name); })) {
                view.note = "not applied yet: its ability " + std::string(list) + " has no plan in this backend";
                return view;
            }
        }
    }
    if (event == "OnHitTaken" && (row.text("SelfApplyStatusEffect").empty() || row.has("AttackType") || row.has("DamageTableRow"))) {
        view.note = "not applied: a hit taken with an effect other than a status on the caster";
        return view;
    }
    if (row.has("NumAroundMe")) {
        const gamedata::SpellRow* spell = abilities != nullptr ? abilities->tracking_spell(id) : nullptr;
        if (spell == nullptr) {
            view.note = "not applied: no tracking spell of the rule in the data";
            return view;
        }
        if (!list_has(spell->cells.text("TargetTypes"), "Enemy") && !(event.empty() && hit_rule(row))) {
            view.note = "not applied: the tracking spell counts friends (no friend relation in the backend)";
            return view;
        }
    }
    // 4. A status with no event and no condition (Unyielding: the fortify of the vortex): the action or the spell of the data gives
    // it; the evaluator does not.
    if (event.empty() && !hit_rule(row) && !row.has("NumAroundMe") && (row.has("SelfApplyStatusEffect") || row.has("OtherApplyStatusEffect"))) {
        view.note = "not applied: a status of the row with no event (the action or the spell of the data gives it)";
        if (actionTested && Abilities::plan_node(id)) {
            view.note = "the plan selects the spell of the node (the action data); " + view.note;
        }
        return view;
    }
    view.applied = true;
    view.note = "applied by the evaluator" + std::string(event.empty() ? (hit_rule(row) ? " (a rule of a hit)" : (row.has("NumAroundMe") ? " (a status while the condition holds)" : " (fixed)")) : " (" + event + ")");
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

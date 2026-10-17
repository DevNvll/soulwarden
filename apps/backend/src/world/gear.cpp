#include "world/gear.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "core/logging/log.h"
#include "world/data_rules.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using data_rules::list_has;
using data_rules::lower;
using data_rules::parts_of;
using data_rules::same_text;

/** The value names of the five attributes in the data (MOD<name>), in the sequence of the attribute numbers. */
constexpr std::array<const char*, attribute::kCount> kAttributeColumns = {"MODConstitution", "MODFocus", "MODStrength",
                                                                          "MODDexterity", "MODIntelligence"};

/** The cells of an ability row that make it a rule of an event, not a passive value. */
bool has_event(const gamedata::CellValues& row) {
    for (const auto& [column, value] : row.numbers) {
        if (column.size() > 2 && column[0] == 'O' && column[1] == 'n' && value != 0.0F) {
            return true;
        }
    }
    return false;
}

/**
 * @return True when a fixed row counts in a load class: no EquipLoadCategory, or the name of the class is in it (the data: Fast, Normal,
 *         Slow, Overburdened; code review of stage 3.4f-B, finding CS-R6).
 */
bool load_allows(const gamedata::CellValues& row, LoadClass load) {
    constexpr std::string_view kNames[] = {"Fast", "Normal", "Slow", "Overburdened"};
    const std::string_view loads = row.text("EquipLoadCategory");
    return loads.empty() || data_rules::list_has(loads, kNames[std::min<std::size_t>(static_cast<std::size_t>(load), 3)]);
}

/** The weapon slot whose perks of the condition OnActive and OnUnsheathed apply: the slot of the weapon in the hand. */
std::optional<std::uint32_t> active_slot(const Entity& character) {
    if (character.combat.selectedWeapon != 0 && character.equipment.item(character.combat.selectedWeapon) != nullptr) {
        return character.combat.selectedWeapon;
    }
    return character.equipment.active_weapon();
}

} // namespace

const gamedata::PerkRow* Gear::swapped(const gamedata::PerkRow& perk, const ItemInstance& item) const {
    const gamedata::PerkData* data = m_world.perk_data();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
    const gamedata::PerkRow* current = &perk;
    if (data == nullptr || definition == nullptr) {
        return current;
    }
    // The swap table of the game (gamedata::PerkData::swap_of). The perks of an instance have their swap from the
    // inventory (Inventory::resolve_perks); the fixed perks of a definition get it here. Three passes, as the client.
    for (unsigned step = 0; step < gear_rules::kSwapChain; ++step) {
        const gamedata::PerkRow* next = data->swap_of(*current, item.itemId, definition->itemClass);
        if (next == nullptr || next == current) {
            break;
        }
        current = next;
    }
    return current;
}

std::vector<GearPerk> Gear::item_perks(const ItemInstance& item, std::uint32_t slot) const {
    std::vector<GearPerk> perks;
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr) {
        return perks;
    }
    std::vector<std::string> names;
    for (const std::string& name : item.perks) {
        if (!name.empty()) {
            names.push_back(name);
        }
    }
    // The fixed perks of the definition: the client shows them with the perks of the instance (live check I2).
    if (const gamedata::PerkLayout* layout = data->layout_of(item.itemId)) {
        for (const std::string& name : layout->fixed) {
            if (!name.empty() && std::none_of(names.begin(), names.end(), [&](const std::string& other) { return same_text(other, name); })) {
                names.push_back(name);
            }
        }
    }
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
    // Each perk counts one time for an item after the swap (code review of stage 1.3b, finding F2): the instance has the
    // swapped id of a fixed perk, and the definition has the old id; the two are one effective perk.
    std::vector<const gamedata::PerkRow*> seen;
    for (const std::string& name : names) {
        const gamedata::PerkRow* perk = data->find(name);
        if (perk == nullptr) {
            continue;
        }
        const gamedata::PerkRow* effective = swapped(*perk, item);
        if (std::find(seen.begin(), seen.end(), effective) != seen.end()) {
            continue;
        }
        seen.push_back(effective);
        // The client removes a perk that the item cannot have (client code at image RVA 0x5CF5750): it has no effect.
        if (definition == nullptr || effective->deprecated || !gamedata::PerkData::allowed(*effective, definition->itemClass)) {
            continue;
        }
        GearPerk entry;
        entry.perk = effective;
        entry.original = perk->id;
        entry.potency = gamedata::PerkData::potency(*effective, item.gearScore, gear_rules::kScalingStart);
        entry.slot = slot;
        entry.itemId = item.itemId;
        perks.push_back(std::move(entry));
    }
    return perks;
}

std::vector<GearPerk> Gear::perks_of(const Entity& character) const {
    std::vector<GearPerk> perks;
    if (m_world.perk_data() == nullptr) {
        return perks;
    }
    const std::optional<std::uint32_t> hand = active_slot(character);
    for (const auto& [slot, place] : character.equipment.slots) {
        if (!place.item || slot >= paperdoll::kSlotCount) {
            continue;
        }
        // The armor, the jewelry, the weapons, and the heart rune. The quick slots, the bags, and the tools have no perk with an effect here.
        if (slot > paperdoll::kRing && !paperdoll::weapon_slot(slot) && slot != paperdoll::kHeartRune) {
            continue;
        }
        for (GearPerk& perk : item_perks(*place.item, slot)) {
            const std::string condition = lower(perk.perk->condition);
            if ((condition == "onactive" || condition == "onunsheathed") && paperdoll::weapon_slot(slot)) {
                // A weapon and its off hand item: only the weapon in the hand (and the off hand item of its slot).
                if (!hand || (slot != *hand && slot != *hand + (paperdoll::kOffHand1 - paperdoll::kMainHand1))) {
                    continue;
                }
            }
            perks.push_back(std::move(perk));
        }
    }
    return perks;
}

GearValues Gear::values(const Entity& character) const {
    GearValues values;
    const gamedata::PerkData* data = m_world.perk_data();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::CombatTables* combat = m_world.combat().tables();
    std::vector<std::string> typeNames;
    if (combat != nullptr) {
        for (const gamedata::DamageType& type : combat->data().damageTypes) {
            typeNames.push_back(type.name);
        }
    }
    const auto add_cells = [&](const gamedata::CellValues& cells, float potency, bool ability) {
        for (unsigned type = 0; type < attribute::kCount; ++type) {
            values.attributes[type] += cells.number(kAttributeColumns[type]) * potency;
        }
        values.maxHealth += (cells.number("MaxHealthMod") + (ability ? cells.number("MaxHealth") : 0.0F)) * potency;
        if (ability) {
            values.baseDamage += cells.number("BaseDamage") * potency;
            values.damageReduction += cells.number("BaseDamageReduction") * potency;
            // The mana parts of a fixed ability row (stage 3.5b.2: Global_Ring_MaxMana, MaxMana 0.1).
            values.maxMana += cells.number("MaxMana") * potency;
            values.manaRate += cells.number("ManaRate") * potency;
        }
        values.physicalArmor += cells.number("PhysicalArmor") * potency;
        values.elementalArmor += cells.number("ElementalArmor") * potency;
        for (const std::string& name : typeNames) {
            if (const float part = cells.number("DMG" + name); part != 0.0F) {
                values.damage[name] += part * potency;
            }
            if (const float part = cells.number("ABS" + name); part != 0.0F) {
                values.absorption[name] += part * potency;
            }
        }
        if (const float tick = cells.number("TickRate"); tick > 0.0F && cells.number("HealthModifierPercent") > 0.0F) {
            values.regeneration += cells.number("HealthModifierPercent") * potency / tick;
        }
    };
    if (data != nullptr) {
        // The equip load: the armor items of the slots 0 to 4, each one in whole tenths (live checks I4 and I12: the
        // client shows 23.25 + 15.5 as 3.8, and the five armor items of the build, 231.57 tenths, as 22.9). It comes before the
        // fixed perks: a fixed perk with EquipLoadCategory counts only in its load classes (code review of stage 3.4f-B, finding CS-R6).
        for (std::uint32_t slot = paperdoll::kHead; slot <= paperdoll::kFeet; ++slot) {
            if (const ItemInstance* item = character.equipment.item(slot)) {
                values.equipLoad += std::floor(data->item_load(item->itemId));
            }
        }
        const gamedata::EquipLoadData& load = data->tables().equipLoad;
        const float percent = load.max > 0.0F ? values.equipLoad / load.max * 100.0F : 0.0F;
        values.load = percent < load.ratioMedium ? LoadClass::light
                      : percent < load.ratioHeavy ? LoadClass::medium
                      : percent <= load.ratioOverburdened ? LoadClass::heavy
                                                         : LoadClass::overburdened;
        values.perks = perks_of(character);
        // The passive ability rows with their stack limit: the largest potencies first.
        std::map<std::string, std::vector<float>> stacked;
        for (const GearPerk& perk : values.perks) {
            for (const std::string& name : perk.perk->affixes) {
                const gamedata::CellValues* affix = data->affix(name);
                if (affix == nullptr) {
                    continue;
                }
                // The status that the affix keeps on its character (Status_Perk_Ward_Amulet_DefSlash) is a status of the
                // status system (Gear::gear_statuses, stage 3.3b-S1): its values come from there.
                add_cells(*affix, perk.potency, false);
            }
            for (const std::string& name : perk.perk->abilities) {
                const gamedata::CellValues* row = data->ability(name);
                // EquipLoadCategory (the names of the load classes of the data: Fast, Normal, Slow, Overburdened): the fixed values of the
                // row count only in its classes (finding CS-R6: GlobalPerk_Artifact_Set2_HeavyLegs, Fast and Normal).
                if (row != nullptr && !has_event(*row) && load_allows(*row, values.load)) {
                    stacked[name].push_back(perk.potency);
                }
            }
        }
        // The fixed stats of the learned nodes of the tree of the weapon in the hand (stage 3.3b-S5, the evaluator of the passive
        // rules): a row with no event and no condition (PhysicalArmor of a node of the sword).
        if (const gamedata::CombatTables* tables = m_world.combat().tables()) {
            const ItemInstance* item = Combat::weapon_in_hand(character);
            const gamedata::WeaponRow* weapon = item != nullptr ? tables->weapon_of(item->itemId) : nullptr;
            const std::uint32_t table = weapon != nullptr && !weapon->mastery.empty() ? gamedata::progression_id(weapon->mastery) : 0;
            for (const PassiveRule& rule : m_world.passives().rules_of(character, table)) {
                if (rule.origin != RuleOrigin::perk && Passives::fixed(*rule.cells)) {
                    add_cells(*rule.cells, 1.0F, true);
                }
            }
        }
        for (auto& [name, potencies] : stacked) {
            const gamedata::CellValues& row = *data->ability(name);
            std::sort(potencies.rbegin(), potencies.rend());
            const float most = row.number("IsStackableMax");
            const std::size_t count = most > 0.0F ? std::min(potencies.size(), static_cast<std::size_t>(most)) : potencies.size();
            for (std::size_t index = 0; index < count; ++index) {
                add_cells(row, potencies[index], true);
            }
        }
    }
    // The statuses of the character (a food, a dust, the status of a heart rune, the status of an affix) are not here:
    // the status system gives their values (Statuses::values), and Gear::refresh adds them one time.
    (void)abilities;
    return values;
}

std::vector<Statuses::GearStatus> Gear::gear_statuses(const Entity& character) const {
    std::vector<Statuses::GearStatus> statuses;
    const gamedata::PerkData* data = m_world.perk_data();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (data == nullptr || abilities == nullptr) {
        return statuses;
    }
    for (const GearPerk& perk : perks_of(character)) {
        for (const std::string& name : perk.perk->affixes) {
            const gamedata::CellValues* affix = data->affix(name);
            const std::string_view status = affix != nullptr ? affix->text("StatusEffect") : std::string_view{};
            if (const gamedata::StatusRow* row = status.empty() ? nullptr : abilities->status(status)) {
                statuses.push_back(Statuses::GearStatus{row, perk.potency, StatusSource{StatusSourceKind::gear, perk.perk->id, perk.slot, character.id}});
            }
        }
    }
    return statuses;
}

void Gear::refresh(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    ++m_counts.refreshes;
    // The statuses of the gear (the affixes of the items) are statuses of the status system: they come or go first.
    m_world.statuses().set_gear(character, gear_statuses(character));
    GearValues values = this->values(character);
    // The values of the statuses (stage 3.3b-S1), with the limits of their categories: each value counts one time.
    const StatusValues status = m_world.statuses().values(character);
    for (unsigned type = 0; type < attribute::kCount; ++type) {
        values.attributes[type] += status.attributes[type];
    }
    values.maxHealth += status.maxHealth;
    values.physicalArmor += status.physicalArmor;
    values.elementalArmor += status.elementalArmor;
    values.damageReduction += status.damageReduction;
    for (const auto& [type, part] : status.damage) {
        values.damage[type] += part;
    }
    for (const auto& [type, part] : status.absorption) {
        values.absorption[type] += part;
    }
    std::array<std::uint32_t, attribute::kCount> bonus{};
    for (unsigned type = 0; type < attribute::kCount; ++type) {
        // The attribute values are whole numbers (the client shows a whole number).
        bonus[type] = values.attributes[type] > 0.0F ? static_cast<std::uint32_t>(std::lround(values.attributes[type])) : 0U;
    }
    const bool bonusChanged = bonus != character.attributes.bonus;
    character.attributes.bonus = bonus;
    character.gear.healthPart = values.maxHealth;
    // The mana parts of the perks and of the statuses (stage 3.5b.2); the progress system adds the parts of the attribute thresholds.
    character.gear.manaPart = values.maxMana + status.maxMana;
    character.gear.manaRatePart = values.manaRate + status.manaRate;
    character.gear.baseDamage = values.baseDamage;
    character.gear.damage = values.damage;
    character.gear.physicalArmor = values.physicalArmor;
    character.gear.elementalArmor = values.elementalArmor;
    character.gear.absorption = values.absorption;
    character.gear.damageReduction = values.damageReduction;
    character.gear.regeneration = values.regeneration;
    character.gear.moveSpeed = status.moveSpeed;
    character.gear.sprintSpeed = status.sprintSpeed;
    character.gear.critChance = status.critChance;
    character.gear.critDamage = status.critDamage;
    // The heal parts of the statuses (stage 3.3b-S6): received (HealMod and the parts of the kinds) and outgoing.
    character.gear.healTaken = status.healTaken;
    character.gear.healConsumable = status.healConsumable;
    character.gear.healNonConsumable = status.healNonConsumable;
    character.gear.healLifeSteal = status.healLifeSteal;
    character.gear.healOutgoing = status.healOutgoing;
    // The regeneration part of the stamina (stage 3.4f-B): the stamina system and the speed table of the owner (the key StaminaRate).
    const bool staminaChanged = character.gear.staminaRate != status.staminaRate;
    character.gear.staminaRate = status.staminaRate;
    // The speed table of the character (stage 3.3b-S2a, S2a-I10): the client runs with these multipliers (live check of
    // status-speed.md: the table of slot 43, not the status map, changes the run), in the limits of the data (StatMultiplierData:
    // the client bounds them itself, getter 0x3FEA370). With no limits in the data (a file of the ability tool from before
    // stage 3.3b-S2a): the limits of the supported build (code review of stage 3.3b-S2a, finding F2): the client bounds the
    // value with them in each case, and the position check must not accept a faster run.
    const auto multiplier = [&](const char* id, float part) {
        float value = 1.0F + part;
        const gamedata::AbilityData* data = m_world.options().abilityData;
        if (const gamedata::StatLimit* limit = data != nullptr ? data->stat_limit(id) : nullptr; limit != nullptr && limit->max > limit->min) {
            value = std::clamp(value, limit->min, limit->max);
        } else {
            value = std::clamp(value, gear_rules::kClientSpeedMin, gear_rules::kClientSpeedMax);
        }
        return std::max(value, 0.0F);
    };
    const float move = multiplier("MoveSpeedMod", status.moveSpeed);
    const float sprint = multiplier("SprintSpeedMod", status.sprintSpeed);
    if (move != character.moveMultiplier || sprint != character.sprintMultiplier || staminaChanged) {
        character.moveMultiplier = move;
        character.sprintMultiplier = sprint;
        m_world.touch(character.id, ComponentKey::statMultipliers);
    }
    const bool loadChanged = character.gear.loadClass != static_cast<std::uint8_t>(values.load);
    character.gear.equipLoad = values.equipLoad;
    character.gear.loadClass = static_cast<std::uint8_t>(values.load);
    if (staminaChanged || loadChanged) {
        // The regeneration of the stamina follows the StaminaRate of the statuses and the factor of the load class (stage 3.4f-B).
        m_world.stamina().refreshed(character, staminaChanged);
    }
    if (bonusChanged) {
        m_world.touch(character.id, ComponentKey::attribute);
    }
    m_world.progress().refresh_vitals(character);
}

std::optional<Conversion> Gear::conversion(const ItemInstance& weapon, std::uint32_t slot) const {
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr) {
        return std::nullopt;
    }
    for (const GearPerk& perk : item_perks(weapon, slot)) {
        for (const std::string& name : perk.perk->affixes) {
            const gamedata::CellValues* affix = data->affix(name);
            if (affix != nullptr && affix->number("DamagePercentage") > 0.0F && !affix->text("DamageType").empty()) {
                return Conversion{std::string(affix->text("DamageType")), std::min(affix->number("DamagePercentage"), 1.0F)};
            }
        }
    }
    return std::nullopt;
}

float Gear::hit_damage(const Entity& attacker, const Entity& target, const HitContext& hit) const {
    if (attacker.kind != EntityKind::player) {
        return 0.0F;
    }
    // The passive BaseDamage (Gear::values), the BaseDamage of the rules of the hit whose conditions pass (the evaluator of
    // the passive rules, stage 3.3b-S5: the perks and the learned nodes), and the part of the equip load class.
    float part = attacker.gear.baseDamage + m_world.passives().before_hit(attacker, target, hit).damage;
    if (const gamedata::PerkData* data = m_world.perk_data()) {
        const auto loadClass = static_cast<std::size_t>(std::min<std::uint8_t>(attacker.gear.loadClass, 2));
        part += data->tables().equipLoad.damage[loadClass];
    }
    return part;
}

float Gear::load_part(const Entity& attacker) const {
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr || attacker.kind != EntityKind::player) {
        return 0.0F;
    }
    const auto loadClass = static_cast<std::size_t>(std::min<std::uint8_t>(attacker.gear.loadClass, 2));
    return data->tables().equipLoad.damage[loadClass];
}

float Gear::heal_factor(const Entity& character) const {
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr) {
        return 1.0F;
    }
    const auto loadClass = static_cast<std::size_t>(std::min<std::uint8_t>(character.gear.loadClass, 2));
    return data->tables().equipLoad.heal[loadClass];
}

void Gear::on_hit(Entity& attacker, Entity& target, const HitContext& hit) {
    // The rules of the perks and of the learned nodes of the weapon tree of the hit: the evaluator of the passive rules
    // (world/passives.h, stage 3.3b-S5) is the one place that reads their cells.
    m_world.passives().after_hit(attacker, target, hit);
}

bool Gear::perk_status_kind(const gamedata::StatusRow& row) {
    return row.cells.number("PhysicalArmor") != 0.0F || row.cells.number("ElementalArmor") != 0.0F
           || (row.cells.number("HealthModifierDamageBased") < 0.0F && !row.cells.text("DamageType").empty())
           || !row.cells.text("CastSpell").empty();
}

void Gear::perk_burst_of(const Entity& character, std::string_view rule) {
    for (const auto& [ability, spell] : gear_rules::kPerkBursts) {
        if (rule == ability) {
            // The burst of the perk around the character, in the next tick (its hits are hits of a different row).
            const std::string characterId = character.characterId;
            const std::string spellId = spell;
            m_world.after(std::chrono::milliseconds(0), [characterId, spellId](World& world) {
                if (Entity* caster = world.find_character(characterId)) {
                    world.abilities().perk_burst(*caster, spellId);
                }
            });
        }
    }
}

void Gear::caster_effect(Entity& attacker, const gamedata::CellValues& row, const std::string& name, float potency) {
    // SelfApplyStatusEffectDurations (code review of stage 3.3b-S1, finding F10): the time of each status of the list, in
    // seconds: one number for each status (a list of the same sequence), or one number for all. No value or 0: the
    // BaseDuration of the status row.
    std::vector<float> durations;
    if (const std::string_view text = row.text("SelfApplyStatusEffectDurations"); !text.empty()) {
        for (const std::string_view part : parts_of(text)) {
            char* end = nullptr;
            const std::string digits(part);
            const float value = std::strtof(digits.c_str(), &end);
            durations.push_back(end == digits.c_str() + digits.size() && std::isfinite(value) ? value : 0.0F);
        }
    } else if (const float value = row.number("SelfApplyStatusEffectDurations"); value > 0.0F) {
        durations.push_back(value);
    }
    std::size_t index = 0;
    for (const std::string_view status : parts_of(row.text("SelfApplyStatusEffect"))) {
        StatusApply how;
        how.seconds = durations.empty() ? 0.0F : durations[std::min(index, durations.size() - 1)];
        ++index;
        how.potency = potency;
        how.source = StatusSource{StatusSourceKind::ability, name, 0, attacker.id};
        const StatusVerdict verdict = m_world.statuses().apply(attacker, status, how);
        if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
            ++m_counts.casterEffects;
        }
    }
}

void Gear::perk_status(Entity& attacker, Entity& target, std::string_view status, float potency, const HitContext& hit) {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::StatusRow* row = abilities != nullptr ? abilities->status(status) : nullptr;
    if (row == nullptr || target.creature.dead) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // The ApplicationCooldown of the status: one application for each character and status in that time.
    const std::string key = attacker.characterId + "/status/" + row->id;
    if (const auto found = m_activations.find(key); found != m_activations.end() && now < found->second) {
        return;
    }
    if (const float wait = row->cells.number("ApplicationCooldown"); wait > 0.0F) {
        m_activations[key] = now + std::chrono::milliseconds(static_cast<long long>(std::lround(wait * 1000.0F)));
    }
    ++m_counts.perkEffects;
    // A rend of the armor of the creature (Sundering Shockwave: PhysicalArmor and ElementalArmor -0.05 times the potency).
    if (row->cells.number("PhysicalArmor") != 0.0F || row->cells.number("ElementalArmor") != 0.0F) {
        CreatureEffect effect;
        effect.status = row->id;
        effect.index = row->index;
        effect.shown = row->nameplate;
        effect.armor = std::min(row->cells.number("PhysicalArmor"), row->cells.number("ElementalArmor")) * potency;
        effect.ends = now + std::chrono::milliseconds(static_cast<long long>(std::lround(row->duration * 1000.0F)));
        effect.source = attacker.id;
        m_world.creatures().apply_effect(target, effect);
    }
    const std::string type(row->cells.text("DamageType"));
    const float based = -row->cells.number("HealthModifierDamageBased");
    // A chain of a spell (ChainLVoidTrigger casts ChainVoidPerk3): the next creatures near the target, one after the other.
    if (const std::string_view spellName = row->cells.text("CastSpell"); !spellName.empty()) {
        const gamedata::SpellRow* spell = abilities->spell(spellName);
        const gamedata::StatusRow* chained = spell != nullptr ? abilities->status(spell->cells.text("StatusEffects")) : nullptr;
        if (spell != nullptr && chained != nullptr) {
            const auto count = std::min<unsigned>(static_cast<unsigned>(spell->cells.number("MaxChainNum")), gear_rules::kChainMost);
            const float part = -chained->cells.number("HealthModifierDamageBased");
            const float charge = chained->cells.number("SourceRuneChargeOnApply");
            const std::string chainType(chained->cells.text("DamageType").empty() ? spell->cells.text("DamageType") : chained->cells.text("DamageType"));
            const auto delay = std::chrono::milliseconds(static_cast<long long>(std::lround(spell->cells.number("ChainDelayDuration") * 1000.0F)));
            std::set<EntityId> hit_;
            hit_.insert(target.id);
            state::WorldPosition from = target.position;
            std::vector<EntityId> sequence;
            for (unsigned link = 0; link < count; ++link) {
                // The nearest creature to the last one, with no wall between them.
                float best = spell->radius;
                const Entity* next = nullptr;
                m_world.for_each_near(from, spell->radius, [&](Entity& entity) {
                    if (entity.kind != EntityKind::creature || entity.creature.dead || entity.creature.immune() || hit_.count(entity.id) != 0) {
                        return;
                    }
                    const float away = horizontal_distance(entity.position, from);
                    if (away <= best && std::fabs(entity.position.z - from.z) <= 3.0F && m_world.navigation().sees(from, entity.position)) {
                        best = away;
                        next = &entity;
                    }
                });
                if (next == nullptr) {
                    break;
                }
                hit_.insert(next->id);
                sequence.push_back(next->id);
                from = next->position;
            }
            const EntityId attackerId = attacker.id;
            for (std::size_t link = 0; link < sequence.size(); ++link) {
                const float amount = part * potency * hit.raw;
                const EntityId targetId = sequence[link];
                m_world.after(delay * static_cast<long long>(link + 1), [this, attackerId, targetId, amount, chainType, charge](World&) {
                    ++m_counts.chainHits;
                    perk_damage(attackerId, targetId, amount, chainType, "the chain of a void perk", charge);
                });
            }
        }
    }
    // A damage over time of the damage of the hit (Abyssal Attunement: 10 percent; the Corrosion of a Runeglass gem).
    if (based > 0.0F && !type.empty()) {
        const float tick = row->cells.number("TickRate");
        const auto ticks = tick > 0.0F ? std::max(1L, std::lround(std::floor(row->duration / tick))) : 1L;
        const bool delayed = row->cells.number("DelayInitialTick") != 0.0F;
        const float amount = based * potency * hit.raw;
        const float onApply = row->cells.number("SourceRuneChargeOnApply");
        const float onTick = row->cells.number("SourceRuneChargeOnTick");
        const EntityId attackerId = attacker.id;
        const EntityId targetId = target.id;
        const auto step = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(tick, 0.0F) * 1000.0F)));
        for (long index = 0; index < ticks; ++index) {
            const auto at = step * (index + (delayed ? 1 : 0));
            const float charge = onTick + (index == 0 ? onApply : 0.0F);
            const auto give = [this, attackerId, targetId, amount, type, charge](World&) {
                ++m_counts.tickHits;
                perk_damage(attackerId, targetId, amount, type, "a damage over time of a perk", charge);
            };
            if (at.count() == 0) {
                give(m_world);
            } else {
                m_world.after(at, give);
            }
        }
    }
}

void Gear::tick_damage(EntityId attacker, EntityId target, float amount, const std::string& type, const char* what, float charge) {
    ++m_counts.tickHits;
    perk_damage(attacker, target, amount, type, what, charge);
}

void Gear::perk_damage(EntityId attackerId, EntityId targetId, float amount, const std::string& type, const char* what, float charge) {
    Entity* attacker = m_world.find(attackerId);
    Entity* target = m_world.creatures().find(targetId);
    // A caster that left the world or died does no damage (the rule of the abilities).
    if (attacker == nullptr || attacker->kind != EntityKind::player || attacker->life.dead || target == nullptr || target->creature.dead) {
        return;
    }
    const HitResult result = m_world.combat().strike_amount(*attacker, *target, amount, type, what);
    if (result.verdict == HitVerdict::accepted && result.damage.dealt > 0.0F && charge > 0.0F) {
        // SourceRuneChargeOnApply and SourceRuneChargeOnTick: only for a change of the health (SourceRuneChargeOnHealthChangeOnly).
        m_world.runes().gain(*attacker, charge);
    }
}

void Gear::on_hit_taken(Entity& character, const gamedata::CreatureContact& contact) {
    m_world.statuses().hit_taken(character);
    if (contact.hitCharge > 0.0F) {
        m_world.runes().gain(character, contact.hitCharge);
    }
}

} // namespace soulwarden::backend::world

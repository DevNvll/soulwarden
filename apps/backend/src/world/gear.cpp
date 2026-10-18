#include "world/gear.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "core/logging/log.h"
#include "gamedata/item_catalog.h"
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
    // The fixed perks of the definition: the client shows them with the perks of the instance (live check I2). A perk of the instance in a
    // place replaces the fixed perk of that place (stage B2: a charm that the player slotted; the Boltcaster of the bow build has Lightning
    // Harnessing II in place 4, where the definition has Slottable_Common_DmgBasic: research bow-build.md section 3.2, the tooltip of the
    // video). Before stage B2 the two counted.
    if (const gamedata::PerkLayout* layout = data->layout_of(item.itemId)) {
        for (std::size_t place = 0; place < layout->fixed.size(); ++place) {
            const std::string& name = layout->fixed[place];
            if (place < item.perks.size() && !item.perks[place].empty()) {
                continue;
            }
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
    const auto add_cells = [&](const gamedata::CellValues& cells, float potency, bool ability, bool mana = true) {
        for (unsigned type = 0; type < attribute::kCount; ++type) {
            values.attributes[type] += cells.number(kAttributeColumns[type]) * potency;
        }
        values.maxHealth += (cells.number("MaxHealthMod") + (ability ? cells.number("MaxHealth") : 0.0F)) * potency;
        if (ability) {
            values.baseDamage += cells.number("BaseDamage") * potency;
            values.damageReduction += cells.number("BaseDamageReduction") * potency;
            // The mana parts of a fixed ability row (stage 3.5b.2: Global_Ring_MaxMana, MaxMana 0.1). The threshold abilities of the attributes
            // have their mana parts in the progress system (Progress::threshold_mana, stage 3.5b.2): `mana` false for them.
            if (mana) {
                values.maxMana += cells.number("MaxMana") * potency;
                values.manaRate += cells.number("ManaRate") * potency;
            }
            // Stage B4: the received heal parts of a fixed ability row of a perk (game data: Global_Amulet_PotionHeal, ConsumableHealMod 0.02
            // x potency, Alchemist Reprieve; GlobalPerk_Artifact_Set1_Earring1, 0.33, The Thirst). One pool with the parts of the statuses
            // (Health::received; the client has one stat multiplier of each kind, enum 0x3FF0810: HealMod 3, ConsumableHealMod 4).
            values.healTaken += cells.number("HealMod") * potency;
            values.healConsumable += cells.number("ConsumableHealMod") * potency;
            values.healNonConsumable += cells.number("NonConsumableHealMod") * potency;
            values.healLifeSteal += cells.number("DmgPctToHealthHealMod") * potency;
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
        // Stage B4: the stat lines of the jewelry (item catalog format 4; client code 0x3636A50: gear score x Scale...PerGS x TrinketBonus x
        // 0.01; the tooltip "+4.53% Max Health" of Gorgon's Amulet at 725, "+9.06% Base Damage" of the Gorgonite Ring at 725, "+5.00% Cooldown
        // Reduction" of Endless Thirst at 800). The max health and the base damage join the parts of the perks (one stat multiplier of each
        // kind; a decision of an agent with live check L1: the cooldown line joins Refreshing in one sum, 19.7 s).
        if (const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog) {
            for (std::uint32_t slot = paperdoll::kHead; slot <= paperdoll::kRing; ++slot) {
                const ItemInstance* item = character.equipment.item(slot);
                const gamedata::TrinketStats* trinket = item != nullptr ? catalog->trinket(item->itemId) : nullptr;
                if (trinket != nullptr) {
                    const std::array<float, 3> parts = trinket->parts(item->gearScore);
                    values.maxHealth += parts[0];
                    values.baseDamage += parts[1];
                    values.cooldownReduction += parts[2];
                }
            }
        }
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
                // A value with a condition of a status of the character (stage B3, Passives::conditional_value) is a value of a hit, not a
                // fixed value (before stage B3 the gear values gave BonusDamageSelfCCPerk1 its BaseDamage with no slow or root).
                if (row != nullptr && !has_event(*row) && load_allows(*row, values.load) && !Passives::conditional_value(*row)) {
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
                    // The potency of the rule: 1 for a node; stage B2: the potency of the status that equips a row. Stage B4: a threshold
                    // ability of an attribute (Con_Bonus_100_1: MaxHealth 0.03) with no mana part (the progress system has it).
                    add_cells(*rule.cells, rule.potency, true, rule.origin != RuleOrigin::attribute);
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
    // Stage B4: the threshold abilities of the attributes are rules of the character (Passives::attribute_rules) with the value of the attribute
    // with its bonus: a new bonus (a food of dexterity, a perk) can add or take a threshold. The values are made again with the new bonus.
    {
        std::array<std::uint32_t, attribute::kCount> next{};
        for (unsigned type = 0; type < attribute::kCount; ++type) {
            next[type] = values.attributes[type] > 0.0F ? static_cast<std::uint32_t>(std::lround(values.attributes[type])) : 0U;
        }
        if (next != character.attributes.bonus) {
            const std::array<std::uint32_t, attribute::kCount> before = character.attributes.bonus;
            character.attributes.bonus = next;
            values = this->values(character);
            for (unsigned type = 0; type < attribute::kCount; ++type) {
                values.attributes[type] += status.attributes[type];
            }
            character.attributes.bonus = before;
        }
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
    // Stage B4: and the received parts of the fixed ability rows of the perks (Alchemist Reprieve, The Thirst).
    character.gear.healTaken = status.healTaken + values.healTaken;
    character.gear.healConsumable = status.healConsumable + values.healConsumable;
    character.gear.healNonConsumable = status.healNonConsumable + values.healNonConsumable;
    character.gear.healLifeSteal = status.healLifeSteal + values.healLifeSteal;
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
    // Stage B4: the cooldown reduction line of the jewelry is a key of the stat multiplier state (CooldownReduction, 0x4F).
    const bool cooldownChanged = character.gear.cooldownReduction != values.cooldownReduction;
    character.gear.cooldownReduction = values.cooldownReduction;
    if (move != character.moveMultiplier || sprint != character.sprintMultiplier || staminaChanged || cooldownChanged) {
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
    // Stage 3.7c: the craft gear score bonus of the statuses (the MaxGS values of a food, of the status of a perk of an item, of a trophy)
    // follows each change of the statuses and of the equipment (field 1 of the Crafting state 205).
    m_world.crafting().refresh_bonus(character);
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
    // The removal window of a running effect of the target (code review of stage B2, finding B2-R4: the rend of a perk came in a window that
    // refused it in Creatures::give_status): the same test before the cooldown, the effect, and a damage that waits.
    if (m_world.creatures().removal_blocks(target, *row)) {
        return;
    }
    // The ApplicationCooldown of the status: one application for each creature and status in that time (stage B2: the key is the target, the
    // rule of a character and of the one status path of a creature, Creatures::application_ready; before: the attacker).
    if (!m_world.creatures().application_ready(target, *row, true)) {
        return;
    }
    ++m_counts.perkEffects;
    // Stage B4: the outgoing time factor of the attacker (Battle Precision, Dex_Bonus_150_1: a DoT; Passives::outgoing_factor), then DurationMax.
    float duration = row->duration * m_world.passives().outgoing_factor(attacker, target, *row);
    if (const float most = row->cells.number("DurationMax"); most > 0.0F && duration > row->duration) {
        duration = std::min(duration, most);
    }
    // A rend of the armor of the creature (Sundering Shockwave: PhysicalArmor and ElementalArmor -0.05 times the potency).
    if (row->cells.number("PhysicalArmor") != 0.0F || row->cells.number("ElementalArmor") != 0.0F) {
        CreatureEffect effect;
        effect.status = row->id;
        effect.index = row->index;
        effect.shown = row->nameplate;
        effect.armor = std::min(row->cells.number("PhysicalArmor"), row->cells.number("ElementalArmor")) * potency;
        effect.ends = now + std::chrono::milliseconds(static_cast<long long>(std::lround(duration * 1000.0F)));
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
        const auto ticks = tick > 0.0F ? std::max(1L, std::lround(std::floor(duration / tick))) : 1L;
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

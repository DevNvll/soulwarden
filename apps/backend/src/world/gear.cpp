#include "world/gear.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& letter : result) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + 32);
        }
    }
    return result;
}

/** @return The parts of a text with "+" or "," between them, with no space around them. */
std::vector<std::string_view> parts_of(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("+,", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            parts.push_back(part);
        }
        start = end + 1;
    }
    return parts;
}

bool same_text(std::string_view left, std::string_view right) {
    return lower(left) == lower(right);
}

bool list_has(std::string_view list, std::string_view value) {
    for (const std::string_view part : parts_of(list)) {
        if (same_text(part, value)) {
            return true;
        }
    }
    return false;
}

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

/** The cells of an OnHit row that are values or marks of the backend, not conditions. */
const std::set<std::string, std::less<>> kBenignColumns = {
    "OnHit", "OnBlockedHit", "RequireReaction", "IsStackableAbility", "IsStackableMax", "IsStackableDiminishingModifier",
    "ActivationCooldown", "CooldownId", "BaseDamage", "OtherApplyStatusEffect", "SelfApplyStatusEffect", "DmgPctToHealth",
    "CooldownTimer", "CDRImmediatelyOptions", "StatusEffectDamageTableIdForRowOverride", "StatusEffectDamageTableRowOverride",
    "DoNotUnequipSelfAppliedSE", "IgnoreDisabledAttackTypes", "OtherApplyStatusEffectDurations", "SelfApplyStatusEffectDurations",
    "UnlockDefault", "CanBeUnapplied"};

/** The conditions of an OnHit row that the backend evaluates (Gear::conditions_pass). */
const std::set<std::string, std::less<>> kKnownConditions = {
    "AttackType", "DamageTableRow", "RemoteDamageTableRow", "TargetStatusEffectCategory", "TargetStatusEffectComparison",
    "TargetHealthPercent", "TargetComparisonType", "MyHealthPercent", "MyComparisonType", "MyStaminaPercent",
    "MyStaminaComparisonType", "MyManaPercent", "MyManaComparisonType", "HasGritActive", "TargetHasGritActive",
    "AttackerVitalsCategory", "TargetVitalsCategory", "DontHaveStatusEffect"};

/** @return The result of a compare of the data ("LessThan", "GreaterThan", "Equal", "NotEqual", ...). Empty: Equal. */
bool compare(float value, float limit, std::string_view kind) {
    const std::string text = lower(kind);
    if (text == "lessthan") {
        return value < limit;
    }
    if (text == "lessthanorequal") {
        return value <= limit;
    }
    if (text == "greaterthan") {
        return value > limit;
    }
    if (text == "greaterthanorequal") {
        return value >= limit;
    }
    if (text == "notequal") {
        return std::fabs(value - limit) > 0.001F;
    }
    return std::fabs(value - limit) <= 0.001F;
}

float percent_of(float value, float maximum) {
    return maximum > 0.0F ? value / maximum * 100.0F : 100.0F;
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
    for (const std::string& name : names) {
        const gamedata::PerkRow* perk = data->find(name);
        if (perk == nullptr) {
            continue;
        }
        const gamedata::PerkRow* effective = swapped(*perk, item);
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
        values.perks = perks_of(character);
        // The passive ability rows with their stack limit: the largest potencies first.
        std::map<std::string, std::vector<float>> stacked;
        for (const GearPerk& perk : values.perks) {
            for (const std::string& name : perk.perk->affixes) {
                const gamedata::CellValues* affix = data->affix(name);
                if (affix == nullptr) {
                    continue;
                }
                add_cells(*affix, perk.potency, false);
                // The status that the affix keeps on its character (Status_Perk_Ward_Amulet_DefSlash).
                const std::string_view status = affix->text("StatusEffect");
                if (!status.empty() && abilities != nullptr) {
                    if (const gamedata::StatusRow* row = abilities->status(status)) {
                        add_cells(row->cells, perk.potency, false);
                    }
                }
            }
            for (const std::string& name : perk.perk->abilities) {
                const gamedata::CellValues* row = data->ability(name);
                if (row != nullptr && !has_event(*row)) {
                    stacked[name].push_back(perk.potency);
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
        // The equip load: the armor items of the slots 0 to 4, each one in whole tenths (live checks I4 and I12: the
        // client shows 23.25 + 15.5 as 3.8, and the five armor items of the build, 231.57 tenths, as 22.9).
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
    }
    // The effects on the character (a food, a dust, the status of a heart rune).
    if (abilities != nullptr) {
        const WorldClock::time_point now = m_world.clock().now();
        for (const PlayerEffect& effect : character.effects) {
            if (now >= effect.ends) {
                continue;
            }
            if (const gamedata::StatusRow* row = abilities->status(effect.status)) {
                gamedata::CellValues cells = row->cells;
                // The heal of an effect with a tick is not a passive regeneration: the consumable system gives it.
                cells.numbers.erase("TickRate");
                add_cells(cells, effect.potency, false);
            }
        }
    }
    return values;
}

void Gear::refresh(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    ++m_counts.refreshes;
    const GearValues values = this->values(character);
    std::array<std::uint32_t, attribute::kCount> bonus{};
    for (unsigned type = 0; type < attribute::kCount; ++type) {
        // The attribute values are whole numbers (the client shows a whole number).
        bonus[type] = values.attributes[type] > 0.0F ? static_cast<std::uint32_t>(std::lround(values.attributes[type])) : 0U;
    }
    const bool bonusChanged = bonus != character.attributes.bonus;
    character.attributes.bonus = bonus;
    character.gear.healthPart = values.maxHealth;
    character.gear.baseDamage = values.baseDamage;
    character.gear.damage = values.damage;
    character.gear.physicalArmor = values.physicalArmor;
    character.gear.elementalArmor = values.elementalArmor;
    character.gear.absorption = values.absorption;
    character.gear.damageReduction = values.damageReduction;
    character.gear.regeneration = values.regeneration;
    character.gear.equipLoad = values.equipLoad;
    character.gear.loadClass = static_cast<std::uint8_t>(values.load);
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

bool Gear::conditions_pass(const gamedata::CellValues& row, const Entity& attacker, const Entity& target, const HitContext& hit) const {
    // A column that is neither a value nor a condition that the backend evaluates: the rule has no effect.
    const auto known = [](const std::string& column) { return kBenignColumns.count(column) != 0 || kKnownConditions.count(column) != 0; };
    for (const auto& [column, value] : row.numbers) {
        if (!known(column)) {
            return false;
        }
    }
    for (const auto& [column, value] : row.texts) {
        if (!known(column)) {
            return false;
        }
    }
    if (!row.has("OnHit")) {
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
    if (row.has("TargetHealthPercent")
        && !compare(percent_of(target.vitals.health, target.vitals.healthMax), row.number("TargetHealthPercent"), row.text("TargetComparisonType"))) {
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
    // The target of this hit is a creature, not a player.
    if (same_text(row.text("TargetVitalsCategory"), "Player")) {
        return false;
    }
    if (const std::string_view category = row.text("AttackerVitalsCategory"); !category.empty() && !same_text(category, "Player")) {
        return false;
    }
    if (const std::string_view status = row.text("DontHaveStatusEffect"); !status.empty()) {
        const WorldClock::time_point now = m_world.clock().now();
        for (const PlayerEffect& effect : attacker.effects) {
            if (now < effect.ends && same_text(effect.status, status)) {
                return false;
            }
        }
    }
    return true;
}

float Gear::hit_damage(const Entity& attacker, const Entity& target, const HitContext& hit) const {
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr || attacker.kind != EntityKind::player) {
        return 0.0F;
    }
    float part = attacker.gear.baseDamage;
    for (const GearPerk& perk : perks_of(attacker)) {
        for (const std::string& name : perk.perk->abilities) {
            const gamedata::CellValues* row = data->ability(name);
            if (row != nullptr && row->number("BaseDamage") != 0.0F && has_event(*row) && conditions_pass(*row, attacker, target, hit)) {
                part += row->number("BaseDamage") * perk.potency;
            }
        }
    }
    const gamedata::EquipLoadData& load = data->tables().equipLoad;
    const auto loadClass = static_cast<std::size_t>(std::min<std::uint8_t>(attacker.gear.loadClass, 2));
    return part + load.damage[loadClass];
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
    const gamedata::PerkData* data = m_world.perk_data();
    if (data == nullptr || attacker.kind != EntityKind::player) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const std::vector<GearPerk> perks = perks_of(attacker);
    const EntityId attackerId = attacker.id;
    const EntityId targetId = target.id;
    for (const GearPerk& perk : perks) {
        for (const std::string& name : perk.perk->abilities) {
            const gamedata::CellValues* row = data->ability(name);
            if (row == nullptr || !has_event(*row)) {
                continue;
            }
            const bool effect = !row->text("OtherApplyStatusEffect").empty() || row->number("DmgPctToHealth") > 0.0F
                                || (row->number("CooldownTimer") < 0.0F && same_text(row->text("CDRImmediatelyOptions"), "ActiveWeapon"));
            Entity* own = m_world.find(attackerId);
            Entity* hurt = m_world.creatures().find(targetId);
            if (!effect || own == nullptr || hurt == nullptr || !conditions_pass(*row, *own, *hurt, hit)) {
                continue;
            }
            // The ActivationCooldown of the row: a value of the character (with its id as the key).
            const std::string key = own->characterId + "/" + name;
            if (const auto found = m_activations.find(key); found != m_activations.end() && now < found->second) {
                continue;
            }
            if (const float wait = row->number("ActivationCooldown"); wait > 0.0F) {
                m_activations[key] = now + std::chrono::milliseconds(static_cast<long long>(std::lround(wait * 1000.0F)));
            }
            ++m_counts.perkHits;
            if (const float part = row->number("DmgPctToHealth"); part > 0.0F && hit.dealt > 0.0F) {
                // A heal of a part of the damage (Insatiable Gravity Well: 15 percent times the potency).
                const float amount = part * perk.potency * hit.dealt * heal_factor(*own);
                m_world.health().heal_amount(*own, amount, name.c_str());
                ++m_counts.perkHeals;
            }
            if (const float part = row->number("CooldownTimer"); part < 0.0F) {
                // A shorter cooldown of each ability of the weapon in the hand (Refreshing Move): a part of the time of the data.
                m_world.abilities().reduce_weapon_cooldowns(*own, -part * perk.potency);
                ++m_counts.cooldownReductions;
            }
            if (const std::string_view status = row->text("OtherApplyStatusEffect"); !status.empty()) {
                perk_status(*own, *hurt, status, perk.potency, hit);
            }
            for (const auto& [ability, spell] : gear_rules::kPerkBursts) {
                if (name == ability) {
                    // The burst of the perk around the character, in the next tick (its hits are hits of a different row).
                    const std::string characterId = own->characterId;
                    const std::string spellId = spell;
                    m_world.after(std::chrono::milliseconds(0), [characterId, spellId](World& world) {
                        if (Entity* caster = world.find_character(characterId)) {
                            world.abilities().perk_burst(*caster, spellId);
                        }
                    });
                }
            }
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
    m_world.consumables().hit_taken(character);
    if (contact.hitCharge > 0.0F) {
        m_world.runes().gain(character, contact.hitCharge);
    }
}

} // namespace soulwarden::backend::world

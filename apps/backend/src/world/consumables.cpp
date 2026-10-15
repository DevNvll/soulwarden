#include "world/consumables.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/item_catalog.h"
#include "gamedata/perk_data.h"
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

bool same_text(std::string_view left, std::string_view right) {
    return lower(left) == lower(right);
}

std::chrono::milliseconds milliseconds_of(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 0.0F) * 1000.0F)));
}

} // namespace

const char* use_verdict_text(UseVerdict verdict) {
    switch (verdict) {
    case UseVerdict::used:
        return "used";
    case UseVerdict::noEntity:
        return "no character in the world";
    case UseVerdict::noData:
        return "no perk data or no ability data";
    case UseVerdict::dead:
        return "the character is dead";
    case UseVerdict::emptySlot:
        return "the slot has no item";
    case UseVerdict::notConsumable:
        return "the item is not a consumable";
    case UseVerdict::cooldown:
        return "the cooldown runs";
    case UseVerdict::notAllowed:
        return "the item is for a siege war only";
    case UseVerdict::inCombat:
        return "the character is in a fight";
    case UseVerdict::storage:
        return "the database did not take the change";
    }
    return "unknown";
}

UseVerdict Consumables::use_quick(SessionId session, unsigned key) {
    const Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return counted(UseVerdict::noEntity);
    }
    if (key == 0 || key > paperdoll::kQuickLast - paperdoll::kQuickFirst + 1) {
        return counted(UseVerdict::emptySlot);
    }
    return use(character->characterId, true, paperdoll::kQuickFirst + key - 1);
}

UseVerdict Consumables::use_bag(SessionId session, std::uint32_t slot) {
    const Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return counted(UseVerdict::noEntity);
    }
    return use(character->characterId, false, slot);
}

UseVerdict Consumables::use(const std::string& characterId, bool paperdoll, std::uint32_t slot) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return counted(UseVerdict::noEntity);
    }
    const gamedata::PerkData* perks = m_world.perk_data();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (perks == nullptr || abilities == nullptr || !abilities->loaded()) {
        return counted(UseVerdict::noData);
    }
    if (character->life.dead) {
        return counted(UseVerdict::dead);
    }
    const WorldClock::time_point now = m_world.clock().now();
    CharacterState& state = m_characters[characterId];
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    // The checks run on the item of the slot in the transaction of the inventory: a use that a check refuses changes nothing.
    UseVerdict verdict = UseVerdict::used;
    const gamedata::ConsumableRow* row = nullptr;
    std::string itemId;
    const auto check = [&](const ItemInstance& item) {
        row = perks->consumable(item.itemId);
        itemId = item.itemId;
        const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
        if (row == nullptr) {
            verdict = UseVerdict::notConsumable;
        } else if (definition != nullptr && gamedata::class_has_any(definition->itemClass, "SiegeWarOnly")) {
            verdict = UseVerdict::notAllowed;
        } else if (row->noCombat && character->life.foughtAt && now - *character->life.foughtAt < Health::kRestTime) {
            verdict = UseVerdict::inCombat;
        } else if (const auto group = state.groups.find(lower(row->cooldown)); !row->cooldown.empty() && group != state.groups.end()
                   && now < (same_text(group->second.itemId, item.itemId) ? group->second.end : group->second.sharedEnd)) {
            verdict = UseVerdict::cooldown;
        }
        return verdict == UseVerdict::used ? ItemError::none : ItemError::notUsable;
    };
    const ItemResult taken = m_world.inventory().take_one(characterId, paperdoll, slot, check);
    if (!taken.ok()) {
        if (taken.error == ItemError::emptySlot || taken.error == ItemError::badSlot || taken.error == ItemError::badEquipSlot) {
            verdict = UseVerdict::emptySlot;
        } else if (taken.error == ItemError::storeFailed) {
            verdict = UseVerdict::storage;
        }
        return counted(verdict == UseVerdict::used ? UseVerdict::storage : verdict);
    }
    character = m_world.find_character(characterId);
    if (character == nullptr || row == nullptr) {
        return counted(UseVerdict::noEntity);
    }
    // The older effects that the item ends (a new food ends the old food).
    end_effects(state, [&](const PlayerEffect& effect) {
        if (std::any_of(row->removeEffects.begin(), row->removeEffects.end(), [&](const std::string& name) { return same_text(name, effect.status); })) {
            return true;
        }
        const gamedata::StatusRow* status = abilities->status(effect.status);
        if (status == nullptr) {
            return false;
        }
        const std::string_view categories = status->cells.text("EffectCategories");
        return std::any_of(row->removeCategories.begin(), row->removeCategories.end(),
                           [&](const std::string& category) { return gamedata::class_has_any(categories, category); });
    });
    for (std::size_t index = 0; index < row->effects.size(); ++index) {
        const gamedata::StatusRow* status = abilities->status(row->effects[index]);
        if (status != nullptr) {
            const float duration = index < row->durations.size() && row->durations[index] > 0.0F ? row->durations[index] : status->duration;
            add_effect(*character, *status, duration);
        }
    }
    // The cooldown, with the perks of the character that shorten it (OnUsedConsumable: an earring).
    if (!row->cooldown.empty() && row->cooldownTime > 0.0F) {
        float factor = 1.0F;
        for (const GearPerk& perk : m_world.gear().perks_of(*character)) {
            for (const std::string& name : perk.perk->abilities) {
                const gamedata::CellValues* cells = perks->ability(name);
                if (cells != nullptr && cells->number("OnUsedConsumable") != 0.0F && cells->number("CooldownTimer") < 0.0F) {
                    factor += cells->number("CooldownTimer") * perk.potency;
                }
            }
        }
        const float seconds = row->cooldownTime * std::max(factor, 0.0F);
        Group& group = state.groups[lower(row->cooldown)];
        group.itemId = itemId;
        group.end = now + milliseconds_of(seconds);
        group.sharedEnd = now + milliseconds_of(seconds * (row->shared ? consumable_rules::kSharedPart : 1.0F));
    }
    {
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        log::write("world: consumable: %s uses %s from slot %u of the %s: %zu effect(s), cooldown %s %.1f s",
                   character->name.c_str(),
                   itemId.c_str(),
                   slot,
                   paperdoll ? "paperdoll" : "bag",
                   row->effects.size(),
                   row->cooldown.empty() ? "none" : row->cooldown.c_str(),
                   static_cast<double>(row->cooldownTime));
    }
    publish(*character, state, true);
    return counted(UseVerdict::used);
}

void Consumables::add_effect(Entity& character, const gamedata::StatusRow& status, float seconds, float potency) {
    if (seconds <= 0.0F) {
        // An effect with no duration: one application now.
        give(character, status, potency);
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    CharacterState& state = m_characters[character.characterId];
    PlayerEffect effect;
    effect.status = status.id;
    effect.potency = potency;
    effect.started = now;
    effect.ends = now + milliseconds_of(seconds);
    effect.hitsLeft = status.stopOnHit;
    const float tick = status.cells.number("TickRate");
    if (tick > 0.0F
        && (status.cells.number("Health") != 0.0F || status.cells.number("HealthModifierPercent") != 0.0F || status.cells.number("Mana") != 0.0F)) {
        effect.nextTick = status.cells.number("DelayInitialTick") != 0.0F ? now + milliseconds_of(tick) : now;
    }
    // StackMax 1: the same effect again has the new times.
    end_effects(state, [&](const PlayerEffect& other) { return same_text(other.status, effect.status); });
    state.effects.push_back(std::move(effect));
    m_active.insert(character.characterId);
    publish(character, state, true);
}

void Consumables::give(Entity& character, const gamedata::StatusRow& row, float potency) {
    const float health = (row.cells.number("Health") + row.cells.number("HealthModifierPercent") * character.vitals.healthMax) * potency;
    if (health > 0.0F && !character.life.dead) {
        m_world.health().heal_amount(character, health * m_world.gear().heal_factor(character), row.id.c_str());
        ++m_counts.heals;
    }
    const float mana = (row.cells.number("Mana") + row.cells.number("ManaModifierPercent") * character.vitals.manaMax) * potency;
    if (mana != 0.0F && !character.life.dead) {
        m_world.abilities().change_mana(character, mana, row.id.c_str());
    }
}

template <class Select>
unsigned Consumables::end_effects(CharacterState& state, Select&& end) {
    const std::size_t before = state.effects.size();
    std::erase_if(state.effects, end);
    const auto ended = static_cast<unsigned>(before - state.effects.size());
    m_counts.ended += ended;
    return ended;
}

void Consumables::publish(Entity& character, CharacterState& state, bool effectsChanged) {
    const WorldClock::time_point now = m_world.clock().now();
    // The countdown of each item of a cooldown id in the quick slots and in the bag (the key of the client is the item id).
    std::vector<ItemCooldown> cooldowns;
    const gamedata::PerkData* perks = m_world.perk_data();
    const auto add = [&](const ItemInstance& item) {
        const gamedata::ConsumableRow* row = perks != nullptr ? perks->consumable(item.itemId) : nullptr;
        if (row == nullptr || row->cooldown.empty()) {
            return;
        }
        const auto group = state.groups.find(lower(row->cooldown));
        if (group == state.groups.end()) {
            return;
        }
        const WorldClock::time_point end = same_text(group->second.itemId, item.itemId) ? group->second.end : group->second.sharedEnd;
        const std::uint32_t key = gamedata::item_id_crc(lower(item.itemId));
        if (end > now && std::none_of(cooldowns.begin(), cooldowns.end(), [&](const ItemCooldown& other) { return other.item == key; })) {
            cooldowns.push_back(ItemCooldown{key, end});
        }
    };
    for (std::uint32_t slot = paperdoll::kQuickFirst; slot <= 19; ++slot) {
        if (const ItemInstance* item = character.equipment.item(slot)) {
            add(*item);
        }
    }
    for (const BagSlot& slot : character.bag.slots) {
        if (slot.item) {
            add(*slot.item);
        }
    }
    std::sort(cooldowns.begin(), cooldowns.end(), [](const ItemCooldown& one, const ItemCooldown& two) { return one.item < two.item; });
    const bool cooldownsChanged = cooldowns.size() != character.itemCooldowns.size()
                                  || !std::equal(cooldowns.begin(), cooldowns.end(), character.itemCooldowns.begin(),
                                                 [](const ItemCooldown& one, const ItemCooldown& two) {
                                                     return one.item == two.item && one.end == two.end;
                                                 });
    if (cooldownsChanged) {
        character.itemCooldowns = std::move(cooldowns);
        m_world.touch(character.id, ComponentKey::cooldowns);
    }
    if (effectsChanged) {
        character.effects = state.effects;
        m_world.gear().refresh(character);
    }
}

void Consumables::entered(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    std::erase_if(found->second.effects, [&](const PlayerEffect& effect) { return effect.ends <= now; });
    if (!found->second.effects.empty()) {
        m_active.insert(character.characterId);
    }
    publish(character, found->second, true);
}

void Consumables::hit_taken(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end() || found->second.effects.empty()) {
        return;
    }
    bool ended = false;
    for (PlayerEffect& effect : found->second.effects) {
        if (effect.hitsLeft > 0) {
            --effect.hitsLeft;
            ended = ended || effect.hitsLeft == 0;
        }
    }
    if (ended) {
        // StopOnHitCount: the effect ends at the hit that the count reaches (a dust).
        end_effects(found->second, [&](const PlayerEffect& effect) {
            const gamedata::StatusRow* status =
                m_world.options().abilityData != nullptr ? m_world.options().abilityData->status(effect.status) : nullptr;
            return status != nullptr && status->stopOnHit > 0 && effect.hitsLeft == 0;
        });
        publish(character, found->second, true);
    }
}

void Consumables::tick() {
    if (m_active.empty()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    std::vector<std::string> idle;
    for (const std::string& characterId : m_active) {
        CharacterState& state = m_characters[characterId];
        Entity* character = m_world.find_character(characterId);
        if (character == nullptr) {
            // The character left the world: its effects stay, and the time runs (the next entry removes the old ones).
            idle.push_back(characterId);
            continue;
        }
        for (PlayerEffect& effect : state.effects) {
            const gamedata::StatusRow* status = abilities != nullptr ? abilities->status(effect.status) : nullptr;
            const float tick = status != nullptr ? status->cells.number("TickRate") : 0.0F;
            while (status != nullptr && effect.nextTick && *effect.nextTick <= now && *effect.nextTick < effect.ends && tick > 0.0F) {
                give(*character, *status, effect.potency);
                ++m_counts.ticks;
                effect.nextTick = *effect.nextTick + milliseconds_of(tick);
            }
        }
        // RemoveOnDeath: a dead character loses the effect.
        const unsigned ended = end_effects(state, [&](const PlayerEffect& effect) {
            if (effect.ends <= now) {
                return true;
            }
            const gamedata::StatusRow* status = abilities != nullptr ? abilities->status(effect.status) : nullptr;
            return character->life.dead && status != nullptr && status->cells.number("RemoveOnDeath") != 0.0F;
        });
        if (ended != 0) {
            publish(*character, state, true);
        }
        if (state.effects.empty()) {
            idle.push_back(characterId);
        }
    }
    for (const std::string& characterId : idle) {
        m_active.erase(characterId);
    }
}

const std::vector<PlayerEffect>* Consumables::effects_of(const std::string& characterId) const {
    const auto found = m_characters.find(characterId);
    return found == m_characters.end() ? nullptr : &found->second.effects;
}

} // namespace soulwarden::backend::world

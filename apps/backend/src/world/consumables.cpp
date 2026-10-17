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
    case UseVerdict::level:
        return "the level of the character is below the RequiredLevel of the item";
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
    const std::uint32_t level = character->progression.level;
    const auto check = [&](const ItemInstance& item) {
        row = perks->consumable(item.itemId);
        itemId = item.itemId;
        const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
        if (row == nullptr) {
            verdict = UseVerdict::notConsumable;
        } else if (definition != nullptr && gamedata::class_has_any(definition->itemClass, "SiegeWarOnly")) {
            verdict = UseVerdict::notAllowed;
        } else if (level < row->level) {
            // RequiredLevel of the item (finding F5): the client sends no key for it, and the server refuses a request.
            verdict = UseVerdict::level;
        } else if (row->noCombat && character->life.foughtAt && now - *character->life.foughtAt < Health::kRestTime) {
            verdict = UseVerdict::inCombat;
        } else if (const auto group = state.groups.find(lower(row->cooldown));
                   !row->cooldown.empty() && group != state.groups.end() && now < group->second.end_of(lower(item.itemId))) {
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
    // The cooldown, with the rules of the character that change it (OnUsedConsumable with CooldownTimer: Refreshing Toast of an earring -0.04;
    // stage B4: a positive value and the CooldownId of the row, The Thirst +0.25 for SurvivalPotion; Passives::consumable_cooldown_factor).
    float seconds = 0.0F;
    if (!row->cooldown.empty() && row->cooldownTime > 0.0F) {
        seconds = row->cooldownTime * m_world.passives().consumable_cooldown_factor(*character, itemId, row->cooldown);
        Group& group = state.groups[lower(row->cooldown)];
        // Each item keeps its own end, and no use makes an end earlier (finding F7).
        WorldClock::time_point& own = group.ends[lower(itemId)];
        own = std::max(own, now + milliseconds_of(seconds));
        group.sharedEnd = std::max(group.sharedEnd, now + milliseconds_of(seconds * (row->shared ? consumable_rules::kSharedPart : 1.0F)));
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
                   static_cast<double>(seconds));
    }
    publish(*character, state);
    // The older statuses that the item ends (a new food ends the old food), then the statuses of the item: the status
    // system of the character (stage 3.3b-S1).
    m_world.statuses().remove_rows(*character, row->removeEffects, row->removeCategories, StatusEnd::replaced);
    for (std::size_t index = 0; index < row->effects.size(); ++index) {
        Entity* own = m_world.find_character(characterId);
        const gamedata::StatusRow* status = abilities->status(row->effects[index]);
        if (own == nullptr || status == nullptr) {
            continue;
        }
        StatusApply how;
        how.seconds = index < row->durations.size() && row->durations[index] > 0.0F ? row->durations[index] : 0.0F;
        how.source = StatusSource{StatusSourceKind::consumable, itemId, paperdoll ? slot : 0, own->id};
        m_world.statuses().apply(*own, *status, how);
    }
    // Stage B4: the rules of the use of a consumable (OnUsedConsumable with a status: the Toast perks of the earring, Passives::consumable_used).
    if (Entity* own = m_world.find_character(characterId)) {
        m_world.passives().consumable_used(*own, itemId, row->cooldown);
    }
    return counted(UseVerdict::used);
}

void Consumables::publish(Entity& character, const CharacterState& state) {
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
        const WorldClock::time_point end = group->second.end_of(lower(item.itemId));
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
}

void Consumables::entered(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found != m_characters.end()) {
        publish(character, found->second);
    }
}

void Consumables::items_changed(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found != m_characters.end()) {
        publish(character, found->second);
    }
}

} // namespace soulwarden::backend::world

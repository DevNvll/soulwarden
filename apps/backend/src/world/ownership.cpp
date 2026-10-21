#include "world/ownership.h"

#include <algorithm>
#include <set>

#include "core/logging/log.h"
#include "world/crafting.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The reward type of an entitlement whose reward is an item of the bag (the amulet of a DLC pack). */
constexpr const char* kRewardInventoryItem = "InventoryItem";

/** @return The texts of `extra` that `list` does not have, after the texts of `list`. */
std::vector<std::string> joined(std::vector<std::string> list, const std::vector<std::string>& extra) {
    for (const std::string& text : extra) {
        if (std::find(list.begin(), list.end(), text) == list.end()) {
            list.push_back(text);
        }
    }
    return list;
}

} // namespace

Ownership::Ownership(World& world, state::Store& store, const gamedata::ProgressionTables* tables)
    : m_world(world), m_store(store), m_source(tables) {}

std::vector<std::string> Ownership::policy_aliases() const {
    std::vector<std::string> aliases;
    if (tables() != nullptr) {
        for (const gamedata::EntitlementRow& row : tables()->entitlements()) {
            aliases.push_back(row.alias);
        }
    }
    return aliases;
}

std::vector<std::string> Ownership::achievements_of(const std::vector<std::string>& aliases) const {
    std::vector<std::string> achievements;
    if (tables() == nullptr) {
        return achievements;
    }
    for (const std::string& alias : aliases) {
        const gamedata::EntitlementRow* row = tables()->entitlement(alias);
        if (row != nullptr && !row->achievement.empty()
            && std::find(achievements.begin(), achievements.end(), row->achievement) == achievements.end()) {
            achievements.push_back(row->achievement);
        }
    }
    return achievements;
}

std::vector<std::string> Ownership::account_entitlements(const std::string& personaId) {
    return joined(m_store.list_entitlements(personaId), policy_aliases());
}

std::optional<std::uint32_t> Ownership::achievement_bit(const std::string& id) const {
    if (tables() != nullptr) {
        if (const gamedata::AchievementRow* row = tables()->achievement(id)) {
            return row->index;
        }
    }
    const gamedata::InteractionData* interactions = m_world.interactions().data();
    if (interactions != nullptr) {
        if (const std::optional<std::uint32_t> bit = interactions->achievement_index(id)) {
            return bit;
        }
    }
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (catalog != nullptr) {
        if (const std::optional<std::uint32_t> bit = catalog->objective_achievement_index(id)) {
            return bit;
        }
    }
    // Stage 3.7b: the first-craft achievement of a recipe (the crafting data: FirstCraftAchievementId and its AchievementIndex).
    const gamedata::CraftingData* crafting = m_world.crafting().data();
    if (crafting != nullptr) {
        if (const std::optional<std::uint32_t> bit = crafting->achievement_index(id)) {
            return bit;
        }
    }
    // Stage 1.8a: the whole achievement table of the game (the quest data): the actions of the quest tasks.
    const gamedata::QuestData* quests = m_world.quests().data();
    return quests != nullptr ? quests->achievement_index(id) : std::nullopt;
}

void Ownership::quest_achievements(Entity& entity, const std::vector<std::string>& unlocked, const std::vector<std::string>& locked) {
    std::set<std::uint32_t> bits(entity.achievements.begin(), entity.achievements.end());
    for (const std::string& id : unlocked) {
        if (const std::optional<std::uint32_t> bit = achievement_bit(id)) {
            bits.insert(*bit);
        }
    }
    for (const std::string& id : locked) {
        if (const std::optional<std::uint32_t> bit = achievement_bit(id)) {
            bits.erase(*bit);
        }
    }
    const std::vector<std::uint32_t> list(bits.begin(), bits.end());
    if (list == entity.achievements) {
        return;
    }
    const bool hadBits = !entity.achievements.empty();
    entity.achievements = list;
    const Revision revision = m_world.touch(entity.id, ComponentKey::achievements);
    if (hadBits && entity.achievements.empty()) {
        entity.achievementsEmptied = revision;
    }
    for (const std::string& id : unlocked) {
        log::write("ownership: quest action: character %s gets the achievement %s", entity.characterId.c_str(), id.c_str());
    }
    for (const std::string& id : locked) {
        log::write("ownership: quest action: character %s loses the achievement %s", entity.characterId.c_str(), id.c_str());
    }
}

bool Ownership::write_achievements(const std::string& characterId, const std::vector<std::string>& ids) {
    std::vector<std::string> added;
    if (!m_store.grant_achievements(characterId, ids, &added)) {
        log::write("ownership: the database did not take %zu achievement(s) of character %s", ids.size(), characterId.c_str());
        return false;
    }
    for (const std::string& id : added) {
        log::write("ownership: character %s gets the achievement %s", characterId.c_str(), id.c_str());
    }
    return true;
}

void Ownership::achievements_written(Entity& entity, const std::vector<std::string>& ids) {
    bool changed = false;
    for (const std::string& id : ids) {
        const std::optional<std::uint32_t> index = achievement_bit(id);
        if (!index) {
            continue; // The row stays, with no bit on the wire.
        }
        const auto place = std::lower_bound(entity.achievements.begin(), entity.achievements.end(), *index);
        if (place == entity.achievements.end() || *place != *index) {
            entity.achievements.insert(place, *index);
            changed = true;
        }
    }
    if (changed) {
        m_world.touch(entity.id, ComponentKey::achievements);
        // Stage 1.8a: the change event (a build preset, a first craft).
        m_world.quests().achievements_changed(entity);
    }
}

void Ownership::set_bits(Entity& entity, const std::vector<std::string>& aliases, const std::vector<std::string>& achievements) {
    // A row that the game data does not have stays in the store, with no bit on the wire.
    std::set<std::uint32_t> entitlementBits;
    std::set<std::uint32_t> achievementBits;
    if (tables() != nullptr) {
        for (const std::string& alias : aliases) {
            if (const gamedata::EntitlementRow* row = tables()->entitlement(alias)) {
                entitlementBits.insert(row->index);
            }
        }
    }
    for (const std::string& id : achievements) {
        if (const std::optional<std::uint32_t> bit = achievement_bit(id)) {
            achievementBits.insert(*bit);
        }
    }
    const std::vector<std::uint32_t> entitlementList(entitlementBits.begin(), entitlementBits.end());
    const std::vector<std::uint32_t> achievementList(achievementBits.begin(), achievementBits.end());
    if (entitlementList != entity.entitlements) {
        entity.entitlements = entitlementList;
        m_world.touch(entity.id, ComponentKey::entitlements);
    }
    if (achievementList != entity.achievements) {
        entity.achievements = achievementList;
        m_world.touch(entity.id, ComponentKey::achievements);
        // Stage 1.8a: the change event (the DLC of an account; at the world entry the quests of the character are not loaded yet).
        m_world.quests().achievements_changed(entity);
    }
}

bool Ownership::grant_account(const std::string& personaId, const char* source) {
    const std::vector<std::string> policy = policy_aliases();
    if (policy.empty()) {
        return true; // No expansion data: nothing to give (a file of format 1, or no tables).
    }
    std::vector<std::string> added;
    const bool stored = m_store.grant_entitlements(personaId, policy, source, &added);
    if (!stored) {
        log::write("ownership: the database did not accept the entitlements of account %s (%s): the grant comes again "
                   "at the next login or world entry",
                   personaId.c_str(),
                   source);
    } else if (!added.empty()) {
        log::write("ownership: account %s got %zu entitlement(s) of the DLC (%s), the first one %s",
                   personaId.c_str(),
                   added.size(),
                   source,
                   added.front().c_str());
    }
    // The entities of the account in the world get the new bits (a character of the account can be in the world while
    // its client makes the sync again).
    for (Entity* entity : m_world.characters_of_account(personaId)) {
        const std::vector<std::string> aliases = joined(m_store.list_entitlements(personaId), policy);
        set_bits(*entity, aliases, joined(m_store.list_achievements(entity->characterId), achievements_of(aliases)));
    }
    return stored;
}

void Ownership::entered(Entity& entity) {
    if (tables() == nullptr) {
        // No expansion data: no grant, but the achievements that the character earned in play (a lore page, stage 2.5a)
        // come from their rows.
        set_bits(entity, {}, m_store.list_achievements(entity.characterId));
        return;
    }
    const std::vector<std::string> policy = policy_aliases();
    (void)grant_account(entity.personaId, "entry");
    const std::vector<std::string> aliases = joined(m_store.list_entitlements(entity.personaId), policy);
    const std::vector<std::string> granted = achievements_of(aliases);
    std::vector<std::string> added;
    if (!granted.empty()) {
        if (!m_store.grant_achievements(entity.characterId, granted, &added)) {
            log::write("ownership: the database did not accept the achievements of character %s: the entity has them, and "
                       "the grant comes again at the next world entry",
                       entity.characterId.c_str());
        } else if (!added.empty()) {
            log::write("ownership: character %s got %zu achievement(s) of the DLC, the first one %s",
                       entity.characterId.c_str(),
                       added.size(),
                       added.front().c_str());
        }
    }
    const std::vector<std::string> achievements = joined(m_store.list_achievements(entity.characterId), granted);
    set_bits(entity, aliases, achievements);
    give_rewards(entity, aliases);
    const gamedata::ExpansionRow* expansion = tables() != nullptr ? tables()->owned_expansion() : nullptr;
    log::write("ownership: character %s has %zu entitlement(s) and %zu achievement(s) on the wire; expansion %s (largest "
               "level %u)",
               entity.characterId.c_str(),
               entity.entitlements.size(),
               entity.achievements.size(),
               expansion != nullptr ? expansion->id.c_str() : "none",
               tables() != nullptr ? tables()->level_cap() : 0U);
}

void Ownership::give_rewards(const Entity& entity, const std::vector<std::string>& aliases) {
    if (tables() == nullptr) {
        return;
    }
    const std::vector<std::string> got = m_store.list_entitlement_rewards(entity.characterId);
    for (const std::string& alias : aliases) {
        const gamedata::EntitlementRow* row = tables()->entitlement(alias);
        if (row == nullptr || row->rewardType != kRewardInventoryItem || row->rewards.empty()
            || std::find(got.begin(), got.end(), alias) != got.end()) {
            continue;
        }
        // One item (the table gives no quantity: a rule of this project). The record and the item rows are one
        // transaction: with no commit the character gets neither, and the next entry tries again.
        Entity* character = m_world.find_character(entity.characterId);
        const Bag bagBefore = character != nullptr ? character->bag : Bag{};
        state::Store::Batch batch(m_store);
        const char* cause = "the database did not accept it";
        ItemResult given;
        given.error = ItemError::storeFailed;
        if (batch.open() && m_store.record_entitlement_reward(entity.characterId, alias)) {
            given = m_world.inventory().give(entity.characterId, row->rewards, 1);
            if (!given.ok()) {
                cause = item_error_text(given.error);
            } else if (batch.commit()) {
                log::write("ownership: character %s got the reward of %s: 1 %s", entity.characterId.c_str(), alias.c_str(),
                           row->rewards.c_str());
                continue;
            }
        }
        if (character != nullptr && given.ok()) {
            // The item went into the entity, and the transaction did not commit: the entity gets its bag of before.
            character->bag = bagBefore;
            m_world.touch(character->id, ComponentKey::container);
        }
        log::write("ownership: no reward of %s for character %s (%s): the next world entry tries again", alias.c_str(),
                   entity.characterId.c_str(), cause);
    }
}

bool Ownership::unlock_achievement(Entity& entity, const std::string& id, std::uint32_t index) {
    std::vector<std::string> added;
    if (!m_store.grant_achievements(entity.characterId, {id}, &added)) {
        log::write("ownership: the database did not take the achievement %s of character %s: no change", id.c_str(),
                   entity.characterId.c_str());
        return false;
    }
    const auto place = std::lower_bound(entity.achievements.begin(), entity.achievements.end(), index);
    if (place == entity.achievements.end() || *place != index) {
        entity.achievements.insert(place, index);
        m_world.touch(entity.id, ComponentKey::achievements);
        // Stage 1.8a: the change event (the read of a lore page: a TaskAchievement of a lore achievement).
        m_world.quests().achievements_changed(entity);
    }
    if (!added.empty()) {
        log::write("ownership: character %s got the achievement %s (bit %u)", entity.characterId.c_str(), id.c_str(), index);
    }
    return true;
}

bool Ownership::lock_achievement(Entity& entity, const std::string& id) {
    if (!m_store.remove_achievements(entity.characterId, {id})) {
        log::write("ownership: the database did not take the lock of the achievement %s of character %s: no change", id.c_str(),
                   entity.characterId.c_str());
        return false;
    }
    const std::vector<std::uint32_t> before = entity.achievements;
    quest_achievements(entity, {}, {id});
    if (entity.achievements != before) {
        m_world.quests().achievements_changed(entity);
    }
    return true;
}

std::optional<OwnershipView> Ownership::view(const std::string& characterId) {
    const auto character = m_store.find_character(characterId);
    if (!character) {
        return std::nullopt;
    }
    OwnershipView view;
    view.entitlements = account_entitlements(character->personaId);
    view.achievements = joined(m_store.list_achievements(characterId), achievements_of(view.entitlements));
    view.rewards = m_store.list_entitlement_rewards(characterId);
    view.expansion = tables() != nullptr ? tables()->owned_expansion() : nullptr;
    view.inWorld = m_world.find_character(characterId) != nullptr;
    return view;
}

} // namespace soulwarden::backend::world

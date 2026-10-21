#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/entity.h"

// The ownership system (stage 1.1b): the entitlements of each account and the achievements of each character that the
// DLC gives.
//
// Decision of the user (2026-10-15): "i want the full game with dlc. for any player, auto unlock the dlc". The grant
// policy of this backend, with no purchase and no setting:
// - Each account owns each entitlement of the list of the game data (ProgressionTables::entitlements): the expansion
//   Rise of the Angry Earth (Ent_Xpack_RiseOfTheAngryEarth_Owner) and the DLC packs of Steam with the entitlements that
//   they give (research dlc-entitlements.md, Q1).
// - Each character owns the achievement of each entitlement of its account that names one (the column AchievementId of
//   the entitlement table: Expansion_2023_Unlock). The expansion check of the client reads this achievement, not the
//   entitlement of the account (image RVA 0x684B340, live check L1 of stage 1.1b).
// - The reward of an entitlement of the type InventoryItem (the amulet of a DLC pack) comes one time to each character,
//   through the inventory system: the row of `entitlement_rewards` and the item rows are one transaction.
// The system writes the rows of the grant at the login of an account (the token route), at the entitlement sync of
// the client, and before the first record of a character (its entity). A grant again changes nothing. The bits of a
// grant are in the entity also when the database did not accept the rows (the policy has no condition).
// The level, the experience, the points, and the skills of a character do not change: the expansion changes only the
// limits (ProgressionTables::level_cap, rank_cap).
//
// Stage 2.5a: the system also keeps the achievements that a character earns in play (the read of a lore page,
// unlock_achievement): their rows are in the same table, and the next world entry gives the entity their bits again.
//
// Stage 1.8a: each change of the bits of a character that the quest system did not make is a change event for the quest system
// (Quests::achievements_changed: a TaskAchievement of the character can complete), and the lock of a quest task removes a bit.
//
// Each function names an account or a character by its id (roadmap, rule MP-9). World thread only: a route gives the
// world a task (World::call).

namespace soulwarden::backend::world {

class World;

/** The ownership of a character, for a reader (a command). */
struct OwnershipView {
    /** The entitlement aliases of the account of the character (the rows, and the policy). */
    std::vector<std::string> entitlements;
    /** The achievement ids of the character. */
    std::vector<std::string> achievements;
    /** The entitlements whose reward the character got. */
    std::vector<std::string> rewards;
    /** The expansion that the character owns, or null (no expansion data). */
    const gamedata::ExpansionRow* expansion{nullptr};
    /** True: the values of the entity of the character. False: of the rows. */
    bool inWorld{false};
};

class Ownership {
public:
    /** @param tables The progression tables (format 2 has the expansions and the entitlements), or null. */
    Ownership(World& world, state::Store& store, const gamedata::ProgressionTables* tables);

    /**
     * Gives an account each entitlement of the policy that it does not have (rows of the store) and gives each entity of
     * the account in the world the new bits.
     * @param source A text for the rows ("login", "sync", "entry").
     * @return False when the database did not accept the rows (the grant comes again at the next login or entry).
     */
    bool grant_account(const std::string& personaId, const char* source);

    /**
     * The entity of a character comes into the world: the grant of its account and of the character, the reward items
     * that the character did not get, and the bits of the entity. Before its first record.
     */
    void entered(Entity& entity);

    /**
     * Gives a character an achievement of the game data that it earned in play (stage 2.5a: the read of a lore page; the
     * achievement row of the page: javelindata_loreitems AchievementId, javelindata_achievements AchievementIndex). The row of
     * `achievements` first, then the bit of the entity and the mark of its state: a database that did not take the row changes
     * nothing. A second call changes nothing (the row and the bit are there).
     * @return False when the database did not take the row.
     */
    [[nodiscard]] bool unlock_achievement(Entity& entity, const std::string& id, std::uint32_t index);
    /**
     * Stage 1.8a (a development command; the quest tasks use quest_achievements): removes an achievement of a character. The row first,
     * then the bit and the mark of the state; a lock of the last bit marks the empty list. Then the change event of the quest system.
     * @return False when the database did not take the change.
     */
    [[nodiscard]] bool lock_achievement(Entity& entity, const std::string& id);

    /**
     * Writes the rows of achievements that a character gets with a change of more than one system (stage B0: the objective of each
     * artifact of a build preset, world/build_presets.h), in one write (in a batch: a savepoint of its transaction). No change of an
     * entity: `achievements_written` gives the bits after the commit.
     * @return False when the database did not take the rows.
     */
    [[nodiscard]] bool write_achievements(const std::string& characterId, const std::vector<std::string>& ids);
    /** Gives the entity of a character the bits of written achievements (each id with a bit of the game data) and marks its state. */
    void achievements_written(Entity& entity, const std::vector<std::string>& ids);

    /**
     * Stage 1.8a: the achievement actions of a quest plan after its commit (world/quests.h: the rows are in the database). Sets the bit of
     * each unlocked id and clears the bit of each locked id (the lock and the temporary unlock of a task), and marks the state. A lock that
     * removes the last bit marks the empty list (Entity::achievementsEmptied): the owner then gets an empty list fragment. No change event:
     * the quest system ran its rules on the same change.
     */
    void quest_achievements(Entity& entity, const std::vector<std::string>& unlocked, const std::vector<std::string>& locked);

    /** @return The entitlement aliases of an account: its rows and the entitlements of the policy. */
    [[nodiscard]] std::vector<std::string> account_entitlements(const std::string& personaId);

    /** @return The ownership of a character, or no value when the store does not have the character. */
    [[nodiscard]] std::optional<OwnershipView> view(const std::string& characterId);

    /**
     * @return The bit of an achievement id: the progression tables (the achievements of the entitlements), then the interaction
     *         data (the lore pages, stage 2.5a), then the item catalog (the objectives of the artifacts, stage B0), then the crafting
     *         data (the first-craft achievements of the recipes, stage 3.7b), then the quest data (the whole achievement table of the
     *         game, stage 1.8a). No value: the game data has no row of it (the row stays,
     *         with no bit). Code review of stage 3.7b, finding CR7B-R3: the crafting system checks a first craft with this resolver too
     *         (one bit for one fact: the check and the bit that the commit sets).
     */
    [[nodiscard]] std::optional<std::uint32_t> achievement_bit(const std::string& id) const;

private:
    /** @return The entitlement aliases of the policy (each one of the game data), or none with no tables. */
    [[nodiscard]] std::vector<std::string> policy_aliases() const;
    /** @return The achievements that these entitlements give. */
    [[nodiscard]] std::vector<std::string> achievements_of(const std::vector<std::string>& aliases) const;
    /** Sets the bits of an entity from the aliases and the achievement ids; marks a state that changed. */
    void set_bits(Entity& entity, const std::vector<std::string>& aliases, const std::vector<std::string>& achievements);
    /** Gives a character the reward items of its entitlements that it did not get (one time each). */
    void give_rewards(const Entity& entity, const std::vector<std::string>& aliases);

    [[nodiscard]] const gamedata::ProgressionTables* tables() const {
        return m_source != nullptr && m_source->loaded() ? m_source : nullptr;
    }

    World& m_world;
    state::Store& m_store;
    const gamedata::ProgressionTables* m_source;
};

} // namespace soulwarden::backend::world

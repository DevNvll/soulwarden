#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "gamedata/creature_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/loot_tables.h"
#include "world/entity.h"
#include "world/load_meter.h"
#include "world/world_clock.h"

// The rewards of a kill: the experience, the weapon mastery experience, the loot, and the coin
// (protocol notes 4.21). A part of the creature system: Creatures::hurt calls it for each hit of a
// character and for a death by a hit.
//
// The game data has the values (gamedata/loot_tables.h); the rules are in this one place:
//
// - The credit (a rule of this project with the credit data of the game, Event Credit Data). A
//   character gets the rewards of a kill when its hits did damage of `healthShare` or more of the
//   maximum health of the creature (0.01), its last hit was `timeLimit` or less before the death
//   (120 s), and at the death it is in the world, lives, is not ignored by the creatures, and is
//   `range` or less from the creature (200 m). `receivers` characters at most (10), the most
//   damage first. The credit modifiers of the game for that number of characters apply: 1
//   character gets the full experience and coin. A death that no hit of a character caused (the
//   health command, the kill command) gives nothing. The credit of a creature goes away when it is
//   at its anchor again with its full health (the reset).
// - The experience (the calculation of the client, image RVA 0x5C4C1D0): the creature level of the
//   reward is at most the level of the character plus MaxRewardLevelDelta of the level difference
//   (the creature level minus the level of the character; a difference outside the table has the
//   row at the end of the table). The experience is KillExperience of that level, times the credit
//   modifier, times the experience factor of the creature type, times 1 + KillExpModifier of the
//   difference; the whole part goes to the progress system (Progress::add_experience). NoExpNoCoin:
//   no experience and no coin.
// - The weapon mastery (a rule of this project: the factor of the game, ProgressionCurrencyAmount,
//   is an empty cell in the supported build): the mastery of the weapon in the hand of the
//   character (WeaponMasteryCategoryId of its weapon row, the combat tables) gets the experience of
//   the kill, through the skill system (Skills::add_experience).
// - The coin: with the coin chance of the creature type, or of the kill event when the type has
//   none (0.05), KillCurrency of the level times the coin factor of the type times the credit
//   modifier, the whole part.
// - The loot: CanDropLoot and a loot table; the outer drop chance is LootDropChance times the loot
//   factor of the creature type (not with IgnoreLootDropModifier) times the credit modifier. The
//   loot table is rolled with the rules of the roller of the client (image RVA 0x368B070, loot.md
//   1.3 and 1.4) and the generator of the world (World::random_unit). The items and the coin go to
//   the character in one transaction of the inventory system (Inventory::grant), with the cause
//   "ItemGathered" (1): the client shows its loot notice.
// - A development switch (the debug message DebugSetForceDropAILoot of the client): for a
//   character with the switch on, the outer drop chance and the coin chance pass.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

/** Rules of this project for the rewards (protocol notes 4.21). */
namespace reward_rules {
/** The tables and the buckets of one roll go this deep at most (a guard: the client has one too). */
inline constexpr unsigned kLargestDepth = 32;
/** One roll gives this many items at most. */
inline constexpr std::size_t kLargestDrops = 64;
/** The numeric tags of a roll (the levels) and their bonus. */
inline constexpr const char* kLevelTag = "level";
inline constexpr const char* kEnemyLevelTag = "enemylevel";
inline constexpr const char* kContentLevelTag = "mincontlevel";
inline constexpr const char* kEnemyContentLevelTag = "minenemycontlevel";
inline constexpr const char* kGlobalModTag = "globalmod";
} // namespace reward_rules

/** One item of a roll. */
struct LootDrop {
    std::string itemId;
    std::uint32_t quantity{0};
    /** The gear score of a new instance. No value: the item has no gear score. */
    std::optional<std::uint32_t> gearScore;
};

/** What a roll knows: the levels and the tags. */
struct LootContext {
    std::uint32_t characterLevel{1};
    std::uint32_t creatureLevel{1};
    /** The loot tags of the creature (its vitals row), in lower case. */
    std::vector<std::string> tags;
};

/** The reward of one character for one kill. */
struct KillReward {
    std::string characterId;
    EntityId character{0};
    std::uint32_t experience{0};
    /** The id text of the weapon mastery that got the experience. Empty: none (no weapon, or no mastery). */
    std::string mastery;
    std::uint64_t coin{0};
    std::vector<LootDrop> items;
    /** Items of the roll that the inventory did not give: the bag had no free slot, or the catalog has no such item. */
    std::uint32_t itemsLost{0};
    bool lootGate{false};
};

/** Counts of the rewards, for the log and for the tests. */
struct RewardCounts {
    std::uint64_t kills{0};
    /** Kills with no character that has the credit. */
    std::uint64_t killsWithNoCredit{0};
    std::uint64_t credited{0};
    std::uint64_t experience{0};
    std::uint64_t masteryExperience{0};
    std::uint64_t coin{0};
    std::uint64_t coinRewards{0};
    std::uint64_t lootGates{0};
    std::uint64_t rolls{0};
    std::uint64_t tableVisits{0};
    std::uint64_t items{0};
    std::uint64_t itemsLost{0};
    /** A table that a roll visited a second time in its own path (the guard of the client). */
    std::uint64_t guards{0};
};

class Rewards {
public:
    Rewards(World& world, const gamedata::LootTables* tables);

    [[nodiscard]] bool has_data() const {
        return m_tables != nullptr && m_tables->loaded();
    }
    [[nodiscard]] const gamedata::LootTables* tables() const {
        return has_data() ? m_tables : nullptr;
    }

    /** A character did damage to a creature (Creatures::hurt): its credit entry. */
    void hit(Entity& creature, const Entity& character, float damage);
    /** The creature is at its anchor with its full health: no character has its credit now. */
    void restored(Entity& creature) {
        creature.creature.credit.clear();
    }
    /**
     * A creature died by a hit of a character (Creatures::hurt): the characters with the credit get
     * their rewards.
     * @return The reward of each character with the credit.
     */
    std::vector<KillReward> killed(Entity& creature);

    /**
     * Rolls the loot table of a vitals row (a kill, and the development route that counts the items
     * of many rolls).
     * @param forced True: the outer drop chance passes.
     * @param creditLoot The loot factor of the credit.
     */
    std::vector<LootDrop> roll(const gamedata::LootVitals& vitals, const LootContext& context, bool forced,
                               float creditLoot = 1.0F);

    /** Development switch: the outer drop chance and the coin chance of the kills of a character pass. */
    void set_forced(const std::string& characterId, bool forced);
    [[nodiscard]] bool forced(const std::string& characterId) const {
        return m_forced.count(characterId) != 0;
    }

    [[nodiscard]] const RewardCounts& counts() const {
        return m_counts;
    }
    /** @return The real time of the rewards of the kills (a measurement). */
    [[nodiscard]] const LoadMeter& load() const {
        return m_load;
    }

private:
    /** The values that a table gives to the entries below it. */
    struct Inherited {
        bool useLevelGearScore{false};
        std::int32_t gearScoreBonus{0};
        std::uint32_t gearScoreMin{0};
        std::uint32_t gearScoreMax{0};
    };
    void roll_table(std::uint32_t index, const LootContext& context, Inherited inherited, std::vector<std::uint32_t>& path,
                    std::vector<LootDrop>& drops);
    void visit(const gamedata::LootEntry& entry, const LootContext& context, const Inherited& inherited,
               std::vector<std::uint32_t>& path, std::vector<LootDrop>& drops);
    void roll_bucket(std::uint32_t index, const LootContext& context, const Inherited& inherited, std::vector<LootDrop>& drops);
    void add_item(const std::string& itemId, std::uint32_t quantity, const LootContext& context, const Inherited& inherited,
                  std::vector<LootDrop>& drops);
    /** @return The value of a numeric tag of the roll (the levels), or no value for a tag that is not numeric. */
    [[nodiscard]] std::optional<std::uint32_t> numeric(const std::string& tag, const LootContext& context) const;
    [[nodiscard]] bool has_tag(const std::string& tag, const LootContext& context) const;
    /** @return A whole number from `low` to `high` (both included), from the generator of the world. */
    [[nodiscard]] std::uint32_t uniform(std::uint32_t low, std::uint32_t high);

    World& m_world;
    const gamedata::LootTables* m_tables;
    std::unordered_set<std::string> m_forced;
    RewardCounts m_counts;
    LoadMeter m_load;
};

/**
 * @return The respawn time of a spawn point (a rule of this project with the two respawn values of
 *         its spawner, protocol notes 4.21): a time from the smallest to the largest value with the
 *         generator of the world when the largest value is larger, and the smallest value when it is
 *         not (a largest value of 0 is no value: the version converter of the client, image RVA
 *         0x3BF6720, gives an old spawner only the smallest value); `kShortestRespawn` for a time of 0.
 * @param unit A random number from 0 to 1 (World::random_unit).
 */
[[nodiscard]] std::chrono::milliseconds respawn_time(const gamedata::SpawnPoint& point, float unit);

/** The respawn time of a spawner whose time is 0 (its two values are 0: 4 spawn points of the supported build). */
inline constexpr std::chrono::seconds kShortestRespawn{30};

} // namespace soulwarden::backend::world

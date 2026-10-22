#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// The loot tables and the kill rewards: what the rewards of a kill need from the game files
// (protocol notes 4.21). The experience and the coin of a kill for each creature level, the reward
// modifiers of each creature type, the level difference rows, the credit values of the game, the
// loot data of each vitals row, the loot tables and the loot buckets that a creature can reach, and
// the gear score ranges of the levels.
//
// The tables have the cells of the game files and no rule: the rules of a kill are in one place of
// the world (world/rewards.h).
//
// The tables are game data: the tool tools/gamedata/loot.sh makes the file from the game copy of
// the user, and the file stays outside the repository. Without the file the backend starts and
// works, and a kill gives nothing.

namespace soulwarden::backend::gamedata {

/** File name of the tables in the game data directory of the backend. */
inline constexpr const char* kLootTablesFile = "loot-tables.json";

/** The tiers of an item that have a gear score range of a level (GSLimitT1 to GSLimitT5). */
inline constexpr std::size_t kGearScoreTiers = 5;

/** The reward of a kill of a creature of one level (table Rewards of the game). */
struct KillReward {
    std::uint32_t experience{0};
    /** Coin, in hundredths of a coin (the wallet unit). */
    std::uint32_t coin{0};
    std::uint32_t progression{0};
};

/** The reward factors of one creature type: Solo, Solo+, Elite, ... (table RewardModifiers). */
struct RewardModifier {
    float experience{1.0F};
    float coin{1.0F};
    /** The chance of the coin. 0: the chance of the kill event applies. */
    float coinChance{0.0F};
    float lootDrop{1.0F};
    /** ProgressionCurrencyAmount. No value: the cell is empty (each row of the supported build). */
    std::optional<float> progression;
};

/** One level difference (the creature level minus the level of the character, table AILevelDisparity). */
struct LevelDisparity {
    std::int32_t difference{0};
    /** The part of the experience that the difference adds (-0.4: 40 percent less). */
    float experience{0.0F};
    /** The creature level of the reward is at most the level of the character plus this. 0: no limit. */
    std::uint32_t rewardLevelDelta{0};
};

/** The credit data of the game (Event Credit Data of playerbaseattributes.pbadb). */
struct CreditData {
    /** A hit gives credit for this time, in seconds. */
    float timeLimit{0.0F};
    /** A character needs damage of this part of the maximum health of the creature. */
    float healthShare{0.0F};
    /** A character with credit is this near to the creature at its death, in meters. */
    float range{0.0F};
    /** Largest number of characters with credit (Max AI Loot Receiver Count). */
    std::uint32_t receivers{0};
    struct Modifier {
        float experience{1.0F};
        float loot{1.0F};
        float coin{1.0F};
    };
    /** Entry K: the factors for K + 1 characters with credit. */
    std::vector<Modifier> modifiers;
};

/** The loot data of one base vitals row. */
struct LootVitals {
    std::string id;
    std::string creatureType;
    bool canDropLoot{false};
    /** The outer drop chance of a kill (LootDropChance). */
    float dropChance{0.0F};
    bool ignoreDropModifier{false};
    /** Index into LootData::tables. No value: the row names no table. */
    std::optional<std::uint32_t> table;
    /** The loot tags of the row ("Beast", "Common"): the tags of the roll. */
    std::vector<std::string> tags;
    /** NoExpNoCoin: a kill gives no experience and no coin. */
    bool noExperienceNoCoin{false};
    /** KillEventIdOverride in lower case. Empty: the kill event of the game (KillEvent). */
    std::string event;
    /**
     * Stage 1.8c: VitalsCategories of the base row (each in lower case, no repeat; client 0x825E7C0: the categories of a kill task). Empty:
     * a file of the tool of before (the kill uses the vitals row and the category row of the kind).
     */
    std::vector<std::string> categories;
};

enum class LootEntryKind : std::uint8_t {
    table,
    bucket,
    item,
};

/** One entry of a loot table. */
struct LootEntry {
    LootEntryKind kind{LootEntryKind::item};
    /** The id of the table, of the bucket, or of the item (the ItemID text). */
    std::string id;
    /** Index into LootData::tables or LootData::buckets. */
    std::uint32_t index{0};
    /** The roll reaches the entry when it is this value or more. */
    std::uint32_t threshold{0};
    /** The quantity range: the count of an item, the visits of a table, the selections of a bucket. */
    std::uint32_t quantityMin{1};
    std::uint32_t quantityMax{1};
    /** The gear score range of the entry. 0 and 0: none. */
    std::uint32_t gearScoreMin{0};
    std::uint32_t gearScoreMax{0};
};

/** The roll bonus setting of a table (RollBonusSetting). */
enum class RollBonus : std::uint8_t {
    addToRoll,
    clampMax,
    ignoreBonus,
};

struct LootTable {
    std::string id;
    /** True: AND (each entry that the roll reaches). False: OR (one entry with the highest reached threshold). */
    bool all{false};
    RollBonus bonus{RollBonus::addToRoll};
    std::uint32_t maxRoll{0};
    /** The conditions: a numeric tag of the roll gives a bonus; a tag must be a tag of the roll (one of them). */
    std::vector<std::string> conditions;
    /** True: the bonus of the numeric conditions is the roll. */
    bool conditionOverridesRoll{false};
    bool useLevelGearScore{false};
    std::int32_t gearScoreBonus{0};
    /** The entries, in the sequence of their thresholds. */
    std::vector<LootEntry> entries;
};

/** One tag of a candidate of a bucket: a name, or a name with a number range ("Level:5-18"). */
struct LootTag {
    std::string name;
    bool numeric{false};
    std::uint32_t low{0};
    std::uint32_t high{0};
};

struct BucketCandidate {
    std::string item;
    std::uint32_t quantityMin{1};
    std::uint32_t quantityMax{1};
    /** The chance of the candidate in a selection (OddsN; 1 when the cell is empty). */
    float odds{1.0F};
    /** True: one tag that matches is enough. False: each tag must match. */
    bool matchOne{false};
    std::vector<LootTag> tags;
};

struct LootBucket {
    std::string id;
    std::vector<BucketCandidate> candidates;
};

/** The content of the file: the data of a load and of a test. */
struct LootData {
    /** Entry K: the creature level K + 1. */
    std::vector<KillReward> rewards;
    /** Creature type to its factors. */
    std::unordered_map<std::string, RewardModifier> modifiers;
    std::vector<LevelDisparity> disparity;
    /** Kill event id in lower case to its coin chance. */
    std::unordered_map<std::string, float> events;
    CreditData credit;
    /** Entry K: the level K + 1, and for each tier the gear score range [min, max]. */
    std::vector<std::array<std::pair<std::uint32_t, std::uint32_t>, kGearScoreTiers>> gearScore;
    /** Creature level to the largest gear score of a loot item of a creature (AILootGSCeiling). */
    std::unordered_map<std::uint32_t, std::uint32_t> lootCeiling;
    /**
     * Stage 3.5b.1 (format 2): a level to the smallest gear score of a loot item of a loot container (ContainerLootGSFloor), the loot
     * roots of the chests (the loot container variation table: each one is a table of `tables`), and the loot limits of the game
     * (exported, not used: the counter rules are not proved; docs/systems/chests.md). A file of format 1 has none of them.
     */
    std::unordered_map<std::uint32_t, std::uint32_t> containerFloor;
    std::vector<std::string> chestRoots;
    std::size_t limits{0};
    /**
     * Stage 1.8d (`containerRoots`, an optional member of format 2): the loot table of each loot container item (the item id in lower case
     * to the table id; RepairRecipe "[LTID]NAME" of the item table, client 0x2E57210, 0x3687F10). A file of before has none.
     */
    std::unordered_map<std::string, std::string> containerRoots;
    std::vector<LootVitals> bases;
    /** Vitals row id in lower case (a base row, or a level variant row) to the index of its base row. */
    std::unordered_map<std::string, std::uint32_t> vitals;
    std::vector<LootTable> tables;
    std::vector<LootBucket> buckets;
};

/** The kill event of the game: its coin chance applies when the creature type has none. */
inline constexpr const char* kKillEvent = "killevent";

class LootTables {
public:
    /**
     * Reads the file of the tool. A file that is not complete or whose parts do not fit together
     * is not used, as a whole: a member that is not there or has a different form, a number that
     * is not finite or not in its range, a reward table whose levels do not start at 1 and go up by
     * 1, a creature type, a level difference, or a vitals row id two times, a vitals row that names
     * a table that the file does not have, an entry of a table that names a table or a bucket that
     * the file does not have, a range whose end is below its start, credit data with no modifier,
     * and a file with no kill event.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the tables with no file (tests). The references of the entries are resolved by their ids. @return False, with no change, when the data is not complete. */
    [[nodiscard]] bool set(LootData data);

    [[nodiscard]] bool loaded() const {
        return !m_data.rewards.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const LootData& data() const {
        return m_data;
    }

    /** @return The reward of a creature level, or null for a level that the table does not have. */
    [[nodiscard]] const KillReward* reward(std::uint32_t level) const;
    /** @return The factors of a creature type, or null. */
    [[nodiscard]] const RewardModifier* modifier(std::string_view creatureType) const;
    /** @return The row of a level difference, or null for a difference with no row. */
    [[nodiscard]] const LevelDisparity* disparity(std::int64_t difference) const;
    /** @return The smallest and the largest level difference of the table (0 and 0 with no row). */
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> disparity_range() const;
    /** @return The coin chance of a kill event (an empty id: the kill event of the game). */
    [[nodiscard]] float coin_chance(std::string_view event) const;
    /** @return The loot data of a vitals row (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const LootVitals* vitals_of(std::string_view vitalsId) const;
    /** @return The gear score range of an item tier at a level, or no value (no row, or a tier outside 1 to 5). */
    [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> gear_score(std::uint32_t level, std::uint32_t tier) const;
    /** @return The largest gear score of a loot item of a creature of a level, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> loot_ceiling(std::uint32_t level) const;
    /** @return The smallest gear score of a loot item of a loot container of a level (stage 3.5b.1), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> container_floor(std::uint32_t level) const;
    /** @return The index of a table (the compare ignores the case of the letters A to Z), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> table_index(std::string_view id) const;
    /** Stage 1.8d: @return The loot table of a loot container item (any case), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> container_table(std::string_view itemId) const;
    /** @return The table with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const LootTable* table(std::string_view id) const;
    [[nodiscard]] const LootTable& table(std::uint32_t index) const {
        return m_data.tables[index];
    }
    [[nodiscard]] const LootBucket& bucket(std::uint32_t index) const {
        return m_data.buckets[index];
    }

private:
    [[nodiscard]] static bool check(LootData& data, std::string& error);
    void build_keys();

    LootData m_data;
    std::string m_error;
    std::unordered_map<std::string, std::uint32_t> m_tableKeys;
};

/** @return The text with the letters A to Z in lower case. */
[[nodiscard]] std::string lower_text(std::string_view text);

} // namespace soulwarden::backend::gamedata

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The progression tables: the rules of the level, of the attributes, of the weapon mastery,
// and of the trade skills of a character (protocol notes 4.12 and 4.13).
//
// The tables are game data: the tool tools/gamedata/progression.sh makes the file from the
// game copy of the user, and the file stays outside the repository. Without the file the
// backend starts and works: it sends the stored values of a character, and it refuses a
// command or a request that needs a rule of a table.
//
// An id of a weapon mastery, of a trade skill, of a tree node, and of an ability table on the
// wire is the CRC-32 of the lower-case id text of the game table.

namespace soulwarden::backend::gamedata {

/** File name of the tables in the game data directory of the backend. */
inline constexpr const char* kProgressionTablesFile = "progression-tables.json";

/** Number of the attributes of a character. */
inline constexpr std::size_t kAttributeCount = 5;
/** Smallest value of an attribute when the backend has no tables (the first row of each attribute table). */
inline constexpr std::uint32_t kDefaultAttributeMinimum = 5;

/**
 * The ability tables of the client in the sequence of its ability data manager (protocol
 * notes 4.13): the index of a table in this list is the "table index" of a client request,
 * and the position of the bar of a weapon in the ability state. The client makes the list at
 * its start from the tables of the type AbilityData, in a sequence that the game files do not
 * give: these are the values of the live client of the supported build (two runs). 16 of the
 * tables are weapon trees; the other ones are tables of creatures, perks, and items.
 */
inline constexpr std::array<std::uint32_t, 29> kAbilityTableOrder{
    0x2915FB5D, 0x6020B33F, 0x6E4FD87A, 0xAF42A7F2, 0x5553E699, 0x7C2BB2C5, 0x180B9A9F, 0x7A04F0CE,
    0xB8C4187D, 0x4A366E22, 0x2EE6A42E, 0x05E2FC0C, 0xAE40FE40, 0xF94C464F, 0x0C20C4DE, 0xAC457988,
    0x572562B8, 0x9E461EEF, 0x1C79C0C7, 0xE7AAC3FB, 0x81575CBB, 0x32733382, 0x2A11B501, 0x1C6AEA69,
    0x630D9982, 0xC7EA8FF8, 0x359CB911, 0xE8EC5716, 0xFF59AEF3,
};

/** @return The id on the wire of an id text: the CRC-32 of the text with the letters A to Z in lower case. */
[[nodiscard]] std::uint32_t progression_id(std::string_view text);

/** One level of a character. */
struct LevelRow {
    /** Total experience at which the level starts. */
    std::uint32_t experience{0};
    /** Attribute points that a character gets when it comes to the level. */
    std::uint32_t attributePoints{0};
    /** Health that the level adds to the health of a player. */
    std::uint32_t health{0};
    /** Cost of an attribute reset at this level, in hundredths of a coin. */
    std::uint32_t resetCost{0};
};

enum class ProgressionKind {
    weapon,
    tradeSkill,
};

/** The ranks of one weapon mastery or trade skill. */
struct RankTable {
    /** Entry R: the experience that takes a character from the rank R to the next rank. */
    std::vector<std::uint32_t> experience;
    /** Weapons: the cost of a tree reset at the rank R, in the currency "Azoth". */
    std::vector<std::uint32_t> resetCost;
};

/** One weapon mastery or one trade skill. */
struct ProgressionRow {
    /** The id text of the game table, for example "SwordAbilityTable" or "Mining". */
    std::string id;
    /** The id on the wire. For a weapon it is also the id of its point pool and of its ability table. */
    std::uint32_t wireId{0};
    ProgressionKind kind{ProgressionKind::tradeSkill};
    /** Largest rank of a player with no expansion (MaxLevel). ProgressionTables::rank_cap gives the rank of a player. */
    std::uint32_t maxRank{0};
    RankTable ranks;
    /** Largest rank with the expansion (Expansion01MaxLevel; stage 1.1b). 0: the row has none, and maxRank applies. */
    std::uint32_t expansionMaxRank{0};
};

/**
 * Stage 1.8d: the standing of one territory (a categorical progression row with the rank table Territory_Standing; format 3 of the file).
 * The points of a rank go to MaximumInfluence of the rank; AutoRankUp gives the next rank with the rest (client 0x3CAACF0, 0x3CB7500).
 */
struct StandingRow {
    /** The territory id; the id text of the row is its decimal text ("6" is Windsward), the id on the wire its CRC-32. */
    std::uint16_t territory{0};
    std::string id;
    std::uint32_t wireId{0};
    /** MaxLevel. */
    std::uint32_t maxRank{0};
    /** Entry R: MaximumInfluence of the rank R (the experience of the RankTable) and XpReward of the rank R. */
    RankTable ranks;
    std::vector<std::uint32_t> xpReward;
    /** ProgressionPointPool (TerritoryRankBonus_6): a rank-up gives one token of it (0x3CB7500). */
    std::string pool;
    bool autoRankUp{false};
};

/** One row of the expansion table of the game (Expansions): the limits of a player who owns the expansion (stage 1.1b). */
struct ExpansionRow {
    /** "None" (no expansion) or "Expansion2023" (Rise of the Angry Earth). */
    std::string id;
    /** The largest level of the screen (MaxDisplayLevel). */
    std::uint32_t maxLevel{0};
    /** The largest gear score of a craft (MaxCraftGS) and of an equipped item (MaxEquipGS). */
    std::uint32_t maxCraftGearScore{0};
    std::uint32_t maxEquipGearScore{0};
    /** The largest trade skill level (MaxTradeskillLevel). */
    std::uint32_t maxTradeSkillLevel{0};
    /** The entitlement that gives the expansion (EntitlementId). Empty: the row of no expansion. */
    std::string entitlement;
};

/** One entitlement that the backend gives each account (stage 1.1b): the expansion and the DLC packs of Steam. */
struct EntitlementRow {
    /** The alias (UniqueTagID), for example "Ent_Xpack_RiseOfTheAngryEarth_Owner". */
    std::string alias;
    /** The bit of the entitlement in the entitlement state of the client (EntitlementIndex). */
    std::uint32_t index{0};
    /** RewardType and Reward(s) of the game table. */
    std::string rewardType;
    std::string rewards;
    /** The achievement that the entitlement gives each character of the account (AchievementId). Empty: none. */
    std::string achievement;
};

/** One achievement that an entitlement gives: its bit in the achievement state of the client (AchievementIndex). */
struct AchievementRow {
    std::string id;
    std::uint32_t index{0};
};

/** The client reads 575 bytes of the entitlement vector at most (image RVA 0x3050A50): a bit index is below 4,600. */
inline constexpr std::uint32_t kEntitlementBitLimit = 575 * 8;
/** The client reads 0x2000000 bytes of the achievement vector at most (image RVA 0x279E550). */
inline constexpr std::uint32_t kAchievementBitLimit = 0x2000000U * 8;

/** One node of a weapon tree: an ability or a passive that a mastery point unlocks. */
struct AbilityNode {
    /** The id text of the game table, for example "Ability_Sword_WhirlingBlade". */
    std::string id;
    std::uint32_t wireId{0};
    /** The weapon: the id on the wire of its ability table and of its point pool. */
    std::uint32_t table{0};
    /** The tree of the weapon (0 or 1), and the place of the node in the tree. */
    std::uint32_t tree{0};
    std::uint32_t row{0};
    std::uint32_t column{0};
    /** True: an ability that the player starts with a key, and that has a place on the ability bar. */
    bool active{false};
    /** The node that this node needs (the id on the wire), or 0. */
    std::uint32_t required{0};
};

/** Largest rank of a weapon mastery or of a trade skill: the rank has 16 bits on the wire. */
inline constexpr std::uint32_t kLargestRank = 0xFFFF;

/**
 * @return True when the ranks of a weapon mastery or of a trade skill are ranks that the
 *         backend can use: the largest rank has 16 bits, the rank table has the experience of
 *         each rank below the largest one, each such experience is larger than 0 (a rank with
 *         the experience 0 has no end), and a weapon has the cost of a tree reset for each of
 *         its ranks. The loader refuses a file with a row that is not valid.
 */
[[nodiscard]] bool valid_ranks(const ProgressionRow& row);
/** @return True when the ranks of a row are valid for each largest rank that it has (maxRank, and expansionMaxRank when it is not 0). */
[[nodiscard]] bool valid_rank_caps(const ProgressionRow& row);

/** One threshold of an attribute (stage 3.5b.2): a character with the attribute at `value` or above has these abilities (EquipAbilities). */
struct AttributeThreshold {
    std::uint32_t value{0};
    std::vector<std::string> abilities;
};

/** The content of the tables, for a load and for a test. */
struct ProgressionData {
    /** The largest level of a player with no expansion (the row "None"), as the screen shows it. */
    std::uint32_t levelCap{0};
    /** Entry K: the level K + 1 of the screen. */
    std::vector<LevelRow> levels;
    std::uint32_t attributeMinimum{kDefaultAttributeMinimum};
    /** Entry K: the health that the constitution value `attributeMinimum` + K gives. */
    std::vector<std::uint32_t> constitutionHealth;
    /** Health, stamina, and mana of a player with no level health and no attribute health. */
    std::uint32_t baseHealth{0};
    std::uint32_t baseStamina{0};
    std::uint32_t baseMana{0};
    /**
     * The mana regeneration of a player (stage 1.3b, the row Player of the base vitals): the mana in one second, and the
     * delay after a spend and after a spend to 0 (seconds). A file from before stage 1.3b: no regeneration.
     */
    float manaRegenRate{0.0F};
    float manaRegenDelay{0.0F};
    float manaRegenDelayOnEmpty{0.0F};
    /**
     * The threshold abilities of each attribute (stage 3.5b.2, `attributes.abilities` of the file; the names "strength", "dexterity",
     * "intelligence", "focus", "constitution"), in the sequence of their values. Empty in an older file: no threshold ability.
     */
    std::map<std::string, std::vector<AttributeThreshold>> attributeAbilities;
    std::vector<ProgressionRow> progressions;
    std::vector<AbilityNode> nodes;
    /**
     * Stage 1.1b (format 2): the rows of the expansion table, the entitlements that each account gets, and the
     * achievements that they give. Empty in a file of format 1: the players have the limits of no expansion.
     */
    std::vector<ExpansionRow> expansions;
    std::vector<EntitlementRow> entitlements;
    std::vector<AchievementRow> achievements;
    /** Stage 1.8d (format 3): the territory standings. Empty in an older file: a quest reward of standing is refused. */
    std::vector<StandingRow> standings;
};

class ProgressionTables {
public:
    /**
     * Reads the file of the tool (format 1, or format 2 with the expansions of stage 1.1b).
     * @return False when the file is not there, has a different form, or has a table that is
     *         not valid: a level table whose experience does not start at 0 or does not go up,
     *         or a rank table that is not valid (valid_ranks, also for the largest rank with the
     *         expansion), an expansion with a level that the level table does not have or an
     *         entitlement that the list does not have, an entitlement with a bit index outside
     *         the range of the client or an achievement that the list does not have, two rows
     *         with one alias, one id, or one bit index. The tables then stay as they were, and
     *         the backend works with no tables.
     */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the tables with no file (tests). The ids on the wire are computed from the id texts. */
    void set(ProgressionData data);

    [[nodiscard]] bool loaded() const {
        return !m_data.levels.empty();
    }
    [[nodiscard]] const ProgressionData& data() const {
        return m_data;
    }

    /**
     * @return The largest level of a player (a level of the screen: 1 is the first level): the level of the owned
     *         expansion, or the level of no expansion.
     */
    [[nodiscard]] std::uint32_t level_cap() const {
        return owned_expansion() != nullptr ? owned_expansion()->maxLevel : m_data.levelCap;
    }
    /**
     * @return The expansion that each player of this backend owns (stage 1.1b), or null (a file of format 1). The backend
     *         gives each account each entitlement of the list (world/ownership.h, a decision of the user of 2026-10-15), and
     *         the client takes the row of the expansion whose entitlement gives an achievement that the character has
     *         (image RVA 0x684B340): thus it is the last row whose entitlement is in the list and names an achievement.
     */
    [[nodiscard]] const ExpansionRow* owned_expansion() const {
        return m_owned < m_data.expansions.size() ? &m_data.expansions[m_owned] : nullptr;
    }
    /** @return The row of no expansion ("None"), or null. */
    [[nodiscard]] const ExpansionRow* base_expansion() const;
    /** @return The largest gear score of an equipped item of a player: MaxEquipGS of the owned expansion, or of no expansion. 0: no limit (no rows). */
    [[nodiscard]] std::uint32_t max_equip_gear_score() const;
    /**
     * @return The largest gear score of a craft of a player (stage 3.7c; client 0x2E57A70 with the setting
     *         javelin.max-craft-gear-score-override 0): MaxCraftGS of the owned expansion, or of no expansion. 0: no limit (no rows).
     */
    [[nodiscard]] std::uint32_t max_craft_gear_score() const;
    /** @return The entitlements that each account gets, the expansion first. */
    [[nodiscard]] std::span<const EntitlementRow> entitlements() const {
        return m_data.entitlements;
    }
    /** @return The entitlement with this alias, or null. */
    [[nodiscard]] const EntitlementRow* entitlement(std::string_view alias) const;
    /** @return The achievement with this id, or null. */
    [[nodiscard]] const AchievementRow* achievement(std::string_view id) const;
    /** @return The row of a level of the screen, or null. */
    [[nodiscard]] const LevelRow* level(std::uint32_t level) const;
    /** @return The attribute points that a character of this level has in total (spent and free). */
    [[nodiscard]] std::uint32_t attribute_points(std::uint32_t level) const;
    [[nodiscard]] std::uint32_t attribute_minimum() const {
        return m_data.attributeMinimum;
    }
    /** @return The largest value of an attribute. */
    [[nodiscard]] std::uint32_t attribute_maximum() const;
    /** @return The health that a value of the constitution gives. 0 for a value that the table does not have. */
    [[nodiscard]] std::uint32_t constitution_health(std::uint32_t value) const;

    /**
     * @return The largest rank of a player of this backend in a weapon mastery or a trade skill: the largest rank with the
     *         owned expansion when the row has one (not 0), else the largest rank with no expansion (the rule of the client,
     *         image RVA 0x3CB38E0 and 0x3CB6330; stage 1.1b).
     */
    [[nodiscard]] std::uint32_t rank_cap(const ProgressionRow& row) const {
        return owned_expansion() != nullptr && row.expansionMaxRank != 0 ? row.expansionMaxRank : row.maxRank;
    }
    /** @return The weapon mastery or the trade skill with this id on the wire, or null. */
    [[nodiscard]] const ProgressionRow* progression(std::uint32_t wireId) const;
    /** @return The weapon mastery or the trade skill with this id text (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const ProgressionRow* progression(std::string_view id) const;
    [[nodiscard]] std::span<const ProgressionRow> progressions() const {
        return m_data.progressions;
    }

    /** Stage 1.8d: @return The standing of a territory, or null. */
    [[nodiscard]] const StandingRow* standing(std::uint16_t territory) const;
    /** Stage 1.8d: @return The standing with this id on the wire, or null. */
    [[nodiscard]] const StandingRow* standing_of(std::uint32_t wireId) const;

    /** @return The node with this id on the wire, or null. */
    [[nodiscard]] const AbilityNode* node(std::uint32_t wireId) const;
    [[nodiscard]] const AbilityNode* node(std::string_view id) const;
    [[nodiscard]] std::span<const AbilityNode> nodes() const {
        return m_data.nodes;
    }
    /** @return The last row of a tree of a weapon. */
    [[nodiscard]] std::uint32_t last_row(std::uint32_t table, std::uint32_t tree) const;

private:
    void build_keys();

    ProgressionData m_data;
    std::unordered_map<std::uint32_t, std::size_t> m_standingKeys;
    /** The index of the owned expansion in m_data.expansions; no row: the size of the list or more. */
    std::size_t m_owned{static_cast<std::size_t>(-1)};
    std::unordered_map<std::uint32_t, std::size_t> m_progressionKeys;
    std::unordered_map<std::uint32_t, std::size_t> m_nodeKeys;
    /** (table, tree) to the last row of the tree. */
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> m_lastRows;
};

} // namespace soulwarden::backend::gamedata

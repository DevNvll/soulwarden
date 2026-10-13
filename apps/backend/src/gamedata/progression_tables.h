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
    /** Largest rank of a player with no expansion. */
    std::uint32_t maxRank{0};
    RankTable ranks;
};

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

/** The content of the tables, for a load and for a test. */
struct ProgressionData {
    /** The largest level of a player, as the screen shows it. */
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
    std::vector<ProgressionRow> progressions;
    std::vector<AbilityNode> nodes;
};

class ProgressionTables {
public:
    /**
     * Reads the file of the tool.
     * @return False when the file is not there, has a different form, or has a table that is
     *         not valid: a level table whose experience does not start at 0 or does not go up,
     *         or a rank table that is not valid (valid_ranks). The tables then stay as they
     *         were, and the backend works with no tables.
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

    /** @return The largest level of a player (a level of the screen: 1 is the first level). */
    [[nodiscard]] std::uint32_t level_cap() const {
        return m_data.levelCap;
    }
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

    /** @return The weapon mastery or the trade skill with this id on the wire, or null. */
    [[nodiscard]] const ProgressionRow* progression(std::uint32_t wireId) const;
    /** @return The weapon mastery or the trade skill with this id text (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const ProgressionRow* progression(std::string_view id) const;
    [[nodiscard]] std::span<const ProgressionRow> progressions() const {
        return m_data.progressions;
    }

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
    std::unordered_map<std::uint32_t, std::size_t> m_progressionKeys;
    std::unordered_map<std::uint32_t, std::size_t> m_nodeKeys;
    /** (table, tree) to the last row of the tree. */
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> m_lastRows;
};

} // namespace soulwarden::backend::gamedata

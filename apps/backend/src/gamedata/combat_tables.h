#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The combat tables: what the damage of an attack needs from the game files (protocol notes
// 4.15). The weapon of an item with its base damage, its attribute factors, and the damage
// rows of its light and of its heavy attack; the damage rows; the damage types; the damage
// that each value of an attribute and each level add; the rule of the gear score; and the
// defense values of each creature (the vitals rows).
//
// The tables have the cells of the game files and no rule: the damage rule is in one place
// of the world (world/combat.h).
//
// The tables are game data: the tool tools/gamedata/combat.sh makes the file from the game
// copy of the user, and the file stays outside the repository. Without the file the backend
// starts and works, and an attack of a player on a creature has no effect.

namespace soulwarden::backend::gamedata {

/** File name of the tables in the game data directory of the backend. */
inline constexpr const char* kCombatTablesFile = "combat-tables.json";

/** Number of the attributes of a character (world/entity.h, namespace attribute). */
inline constexpr std::size_t kCombatAttributes = 5;

enum class DamageCategory {
    /** No defense value applies ("True"). */
    direct,
    physical,
    elemental,
};

struct DamageType {
    /** Name of the type in the game tables ("Slash"). */
    std::string name;
    /** Number of the type on the wire (the damage number message). */
    std::uint8_t wire{0};
    DamageCategory category{DamageCategory::direct};
};

/** One row of the damage table of the weapons of a player: one attack. */
struct DamageRow {
    std::string id;
    /** "Light", "Heavy", "Ability", ... */
    std::string attackType;
    /** Index into CombatTables::damage_types. */
    std::uint32_t type{0};
    /** The part of the weapon damage that the attack does, and the damage that it adds. */
    float factor{1.0F};
    float added{0.0F};
};

/** One row of the weapon table. */
struct WeaponRow {
    std::string id;
    /** True for the use "Combat Melee". */
    bool melee{false};
    /** Tags of the weapon ("1H_Melee,Sword"). For a log line only. */
    std::string tags;
    float baseDamage{0.0F};
    /** The damage factor of each attribute, with the number of the attribute as the index. 0: the attribute adds nothing. */
    std::array<float, kCombatAttributes> scaling{};
    /** The damage rows of the light and of the heavy attack: indexes into CombatTables::damage_rows. */
    std::optional<std::uint32_t> lightRow;
    std::optional<std::uint32_t> heavyRow;
    /** The gear score rate of this weapon. 0: the rate of the constants. */
    float gearScoreRate{0.0F};
};

/** The defense values of one creature kind (its base vitals row). */
struct DefenseRow {
    /** The part of a physical and of an elemental damage that the creature does not take. */
    float physical{0.0F};
    float elemental{0.0F};
    /** For each damage type (the index of CombatTables::damage_types): the weakness and the absorption. */
    std::vector<float> weakness;
    std::vector<float> absorption;
};

/** The values of the damage rule that the game has in its base attributes file. */
struct CombatConstants {
    /** The part of the base damage that one level of the attacker adds. */
    float levelDamage{0.0F};
    /**
     * The gear score of a weapon: the base damage grows by `gearScoreRate` for each
     * `gearScoreInterval` points above `gearScoreMinimum`. Above `gearScoreLimit` the rate is
     * `gearScoreRate` times `gearScoreSmallerRate`.
     */
    float gearScoreRate{0.0F};
    float gearScoreSmallerRate{1.0F};
    std::uint32_t gearScoreInterval{5};
    std::uint32_t gearScoreMinimum{100};
    std::uint32_t gearScoreLimit{500};
};

/** The tables as plain data: the content of the file, and what a test sets. */
struct CombatData {
    CombatConstants constants;
    std::vector<DamageType> damageTypes;
    /** The value of an attribute for entry 0 of each list of `attributeDamage`. */
    std::uint32_t attributeMinimum{5};
    /** For each attribute (its number as the index): the damage value of each value of the attribute. */
    std::array<std::vector<float>, kCombatAttributes> attributeDamage;
    std::vector<DamageRow> damageRows;
    std::vector<WeaponRow> weapons;
    /** Item id in lower case to the index of its weapon. */
    std::unordered_map<std::string, std::uint32_t> items;
    std::vector<DefenseRow> defenses;
    /** Vitals row id in lower case to the index of its defense values. */
    std::unordered_map<std::string, std::uint32_t> vitals;
};

class CombatTables {
public:
    /**
     * Reads the file of the tool. A file that is not complete (a member that is not there, an
     * index outside its list, a number that is not finite) is not used.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the tables with no file (tests). @return False, with no change, when the data is not complete. */
    [[nodiscard]] bool set(CombatData data);

    [[nodiscard]] bool loaded() const {
        return !m_data.weapons.empty();
    }
    /** @return Why the last load gave no tables. Empty after a load that gave tables. */
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const CombatData& data() const {
        return m_data;
    }
    [[nodiscard]] const CombatConstants& constants() const {
        return m_data.constants;
    }

    /** @return The weapon of an item (the compare ignores the case of the letters A to Z), or null for an item with no weapon. */
    [[nodiscard]] const WeaponRow* weapon_of(std::string_view itemId) const;
    /** @return The damage row with this index. The index must come from a weapon of these tables. */
    [[nodiscard]] const DamageRow& damage_row(std::uint32_t index) const {
        return m_data.damageRows[index];
    }
    [[nodiscard]] const DamageType& damage_type(std::uint32_t index) const {
        return m_data.damageTypes[index];
    }
    /** @return The defense values of a vitals row (the compare ignores the case), or null when the tables have no such row. */
    [[nodiscard]] const DefenseRow* defense_of(std::string_view vitalsId) const;
    /**
     * @return The damage value of one value of an attribute (the column ModifierValueSum of the
     *         attribute table): 0 for a value below the first row, and the last row for a value
     *         above it.
     */
    [[nodiscard]] float attribute_damage(std::size_t attribute, std::uint32_t value) const;

private:
    [[nodiscard]] static bool check(const CombatData& data, std::string& error);

    CombatData m_data;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

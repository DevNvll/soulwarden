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

/**
 * The impact of a hit of a damage row (stage 3.3b-S4, protocol notes 4.26): how far the hit moves its target. The
 * columns ImpactDistanceX, Y, Z and ImpactDecayRate of the row, and the same columns with "_AI" for a target that is a
 * creature (the client takes the "_AI" value when the row has one: client code at image RVA 0x683D910).
 */
struct DamageImpact {
    /** X to the side, Y away from the source of the hit (a negative Y pulls to it), Z up; meters. */
    std::array<float, 3> distance{};
    /**
     * The decay rate of the travel (client code at image RVA 0x48A0D30): the travel at the time t of the reaction is
     * |distance| * (1 - decay^(30 t)). 0 or less: a constant speed of |distance| meters in each second to the source
     * point (or away from it), not past it (client code at image RVA 0x488B8F0).
     */
    float decay{0.0F};
    /** The same values for a target that is a creature. All distances 0: the row has none (the values above count). */
    std::array<float, 3> creatureDistance{};
    float creatureDecay{0.0F};
    /** UseAttackerPosForReaction: the direction comes from the place of the attacker, not from the source of the damage. */
    bool fromAttacker{false};
};

/**
 * The reaction of a hit of a damage row (user test 2, the member `reactions` of the tool): the columns NoReaction, PowerLevel, HitStun, and
 * the two columns with "_AI" for a target that is a creature (-1: a blank cell, the target takes the normal value).
 */
struct DamageReaction {
    bool none{false};
    std::int32_t power{0};
    std::int32_t hitStun{0};
    std::int32_t creaturePower{-1};
    std::int32_t creatureHitStun{-1};

    /** @return The power level of a hit on a creature: the "_AI" value when the row has one (the rule of the "_AI" impact values). */
    [[nodiscard]] std::int32_t power_on_creature() const {
        return creaturePower >= 0 ? creaturePower : power;
    }
    /** @return The hit stun of a hit on a creature, in frames of 1/30 s (the "_AI" value when the row has one). */
    [[nodiscard]] std::int32_t stun_on_creature() const {
        return creatureHitStun >= 0 ? creatureHitStun : hitStun;
    }
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
    /**
     * The heart rune charge of a hit of this row (stage 1.3b, columns AttackRuneCharge and HitRuneCharge): the
     * charge that the attacker gets, and the charge that a player gets who takes the hit. 0: no charge.
     */
    float attackCharge{0.0F};
    float hitCharge{0.0F};
    /** The impact of a hit (stage 3.3b-S4). No value: the hit moves no target (also a file of a tool from before the stage). */
    std::optional<DamageImpact> impact{};
    /**
     * The cells of the row for a critical hit, a headshot, and a hit from behind (stage 3.3b-S5, the member `hitRules` of the
     * tool): CanCrit (the flag CanCrit of the damage number), NoHeadshot, NoBackstab, DmgCoefHead, and DmgCoefCrit (0: blank).
     */
    bool canCrit{false};
    /**
     * An explicit CanCrit 0 of the data (code review of stage 3.3b-S5, finding F2; 42 rows: the burst of Gravity Well, the trap of
     * the musket): the row is never critical. A blank cell (an ordinary attack row) is false here. A file of format 1 has none.
     */
    bool noCrit{false};
    bool noHeadshot{false};
    bool noBackstab{false};
    float headFactor{0.0F};
    float critFactor{0.0F};
    /**
     * The status effects of a hit of the row (the column StatusEffect; format 3, stage 3.3b-M2: the root of Gravity on the pull rows _RP2),
     * and EffectOnlyOnDamage (only after a hit with damage above 0). Empty with a file of format 1 or 2.
     */
    std::vector<std::string> effects{};
    bool effectOnlyOnDamage{false};
    /** The reaction of a hit (user test 2). No value: a file of a tool from before user test 2 (the reaction of stage 3.2a). */
    std::optional<DamageReaction> reaction{};
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
    /**
     * The weapon mastery of the weapon: the id text of its progression (WeaponMasteryCategoryId,
     * "SwordAbilityTable"). Empty: none (a weapon of a creature, a tool), or a file of a tool from
     * before stage 3.5a.
     */
    std::string mastery;
    /**
     * The critical values of the weapon (stage 3.3b-S5, the member `criticals` of the tool): CritChance and
     * CritDamageMultiplier. `critical` false: no values (a file of a tool from before the stage), and no critical hit.
     */
    bool critical{false};
    float critChance{0.0F};
    float critMultiplier{1.0F};
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

/** The armor values of one armor item (its row of the armor table of the game). */
struct ArmorRow {
    /** The factor of the item for a physical and for an elemental damage. */
    float physical{0.0F};
    float elemental{0.0F};
    /** The part of the armor of a full set that this item gives (a chest gives more than a glove). */
    float rating{0.0F};
};

/** The values of the armor rule that the game has in its base attributes file. */
struct ArmorConstants {
    /** The power of the gear score of an armor item in its armor. */
    float ratingExponent{1.2F};
    /** The power of the gear score of an attacker against the armor of its target. */
    float mitigationExponent{1.2F};
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
    /**
     * The armor of a character against an attack of a creature (stage 3.4b). A file of a tool
     * from before that stage has none of these values: a character then has no armor in the
     * damage rule, and the level difference changes no damage.
     */
    ArmorConstants armorConstants;
    /** Item id in lower case to the armor values of the item. */
    std::unordered_map<std::string, ArmorRow> armor;
    /**
     * The level difference of an attacker and its target (the level of the attacker minus the
     * level of the target) to the part of the damage that the difference adds.
     */
    std::unordered_map<std::int32_t, float> levelDamage;
    /** True for a file of format 3 (stage 3.3b-M2): the damage rows have their status effects (DamageRow::effects). */
    bool rowEffects{false};
};

class CombatTables {
public:
    /**
     * Reads the file of the tool. A file that is not complete (a member that is not there, an
     * index outside its list, a number that is not finite) is not used. The members "armor"
     * and "levelDamage" are members of a file of stage 3.4b: a file with no such member is
     * correct, and a member with a wrong form is not.
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
    /** @return True when the damage rows have their status effects (format 3, stage 3.3b-M2). */
    [[nodiscard]] bool has_row_effects() const {
        return m_data.rowEffects;
    }

    /** @return The weapon of an item (the compare ignores the case of the letters A to Z), or null for an item with no weapon. */
    [[nodiscard]] const WeaponRow* weapon_of(std::string_view itemId) const;
    /**
     * @return The index of the damage row with this id (the compare ignores the case of the
     *         letters A to Z): the row of an ability (stage 3.3a). No value: no such row.
     */
    [[nodiscard]] std::optional<std::uint32_t> row_index(std::string_view id) const;
    /** @return The damage row with this index. The index must come from a weapon of these tables. */
    [[nodiscard]] const DamageRow& damage_row(std::uint32_t index) const {
        return m_data.damageRows[index];
    }
    [[nodiscard]] const DamageType& damage_type(std::uint32_t index) const {
        return m_data.damageTypes[index];
    }
    /** @return The defense values of a vitals row (the compare ignores the case), or null when the tables have no such row. */
    [[nodiscard]] const DefenseRow* defense_of(std::string_view vitalsId) const;
    [[nodiscard]] const ArmorConstants& armor_constants() const {
        return m_data.armorConstants;
    }
    /** @return The armor values of an item (the compare ignores the case of the letters A to Z), or null for an item with no armor. */
    [[nodiscard]] const ArmorRow* armor_of(std::string_view itemId) const;
    /**
     * @return The part of the damage that a level difference adds (the table of the game has a
     *         value for some differences). 0 for a difference with no value.
     */
    [[nodiscard]] float level_damage(std::int64_t difference) const;
    /**
     * @return The damage value of one value of an attribute (the column ModifierValueSum of the
     *         attribute table): 0 for a value below the first row, and the last row for a value
     *         above it.
     */
    [[nodiscard]] float attribute_damage(std::size_t attribute, std::uint32_t value) const;

private:
    [[nodiscard]] static bool check(const CombatData& data, std::string& error);

    CombatData m_data;
    /** Lower-case id of each damage row to its index. */
    std::unordered_map<std::string, std::uint32_t> m_rows;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

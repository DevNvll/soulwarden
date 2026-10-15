#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/cell_values.h"

// The ability data: what the abilities of a player need from the game files (stage 3.3a,
// protocol notes 4.22). The cooldown of each ability (the cooldown table of the players), the
// status effects (the status tables: the values of an effect on a creature, and the number of
// each effect on the wire), and the spells (the spell tables: the size of an area and the
// samples of a path).
//
// The data has the cells of the game files and no rule: the rules of the abilities are in the
// world (world/abilities.h). The damage rows of the abilities are in the combat tables.
//
// The data is game data: the tool tools/gamedata/abilities.sh makes the file from the game copy
// of the user, and the file stays outside the repository. Without the file the backend starts
// and works, and the backend refuses each ability press (no data).

namespace soulwarden::backend::gamedata {

/** File name of the ability data in the game data directory of the backend. */
inline constexpr const char* kAbilityDataFile = "ability-data.json";

/** The cooldown of one ability (the table CooldownData of the players). */
struct CooldownRow {
    std::string id;
    /** The time of the cooldown, and the shortest time that a change of the cooldown leaves (seconds). */
    float time{0.0F};
    float minTime{0.0F};
};

/** One status effect (StatusEffectData). */
struct StatusRow {
    std::string id;
    /**
     * The number of the effect on the wire: the position of the CRC-32 of its lower-case id in
     * the list of the CRC values of all rows of the status tables, in the unsigned order (the
     * list of the client, live proof of stage 3.3a: 4,517 rows with the duplicate).
     */
    std::uint16_t index{0};
    /** BaseDuration (seconds). */
    float duration{0.0F};
    /** MoveSpeedMod: the part of the speed that the effect adds (-0.2: 20 percent slower). */
    float moveSpeed{0.0F};
    /** Stunned: no movement and no attack. NoNav: no movement (a root). */
    bool stunned{false};
    bool root{false};
    /** StopOnHitCount: the effect ends at this hit that the target takes after it. 0: none. */
    std::uint32_t stopOnHit{0};
    /** ShowInNameplates: the client shows the icon of the effect at the name plate. */
    bool nameplate{false};
    /** DMGStandard: the part of the damage of the target that the effect adds (-0.1: a weaken). */
    float damage{0.0F};
    /**
     * The other cells of the row that are not empty (stage 1.3b, file format 2): Health,
     * HealthModifierPercent, TickRate, MODConstitution, PhysicalArmor, ABS<type>, DMG<type>,
     * EffectCategories, OnEndStatusEffect, CastSpell, SourceRuneChargeOnApply, ... Empty for a file of format 1.
     */
    CellValues cells;
};

/** One spell (SpellData): the shape of an area or of a path. */
struct SpellRow {
    std::string id;
    /** Radius and Height of the shape (meters). */
    float radius{0.0F};
    float height{0.0F};
    /** CastDistance: the distance of a cast (a projectile, the step of a path; meters). */
    float castDistance{0.0F};
    /** NumToSpawn and SpawnRate: the samples of a path and the time between two of them (milliseconds). */
    std::uint32_t count{0};
    float spawnRate{0.0F};
    /** Duration (milliseconds). */
    float duration{0.0F};
    /** The other cells of the row that are not empty (stage 1.3b): DamageTableRow, ManaCost, MaxChainNum, ... */
    CellValues cells;
};

/** One row of a cost table of the players (ManaData or StaminaData, stage 1.3b): the cost of a cost id. */
struct CostRow {
    std::string id;
    float cost{0.0F};
};

/**
 * The cost columns of one ability row (stage 1.3b): an active ability with a ManaCostMult changes the mana
 * cost of each cost id of its ManaCostList ("All": each id), and one with a StaminaCostFlatMod adds to the
 * stamina cost of each id of its StaminaCostList (client code at image RVA 0x67EAED0 and 0x67EB010).
 */
struct AbilityCostRow {
    std::string id;
    std::vector<std::string> mana;
    float manaMultiplier{0.0F};
    std::vector<std::string> stamina;
    float staminaFlat{0.0F};
};

/**
 * One row of an ability table of the players that puts a status on its caster (stage 3.3b-S1, file format 3): its
 * cells (SelfApplyStatusEffect, the event OnHit or OnHitTaken, the conditions AttackType, DamageTableRow, InSequence,
 * ...). The rule that evaluates the cells is in the world (world/gear.h, the caster effects of the learned nodes).
 */
struct AbilityRow {
    std::string id;
    CellValues cells;
};

/**
 * One status category (StatusEffectCategoryData, stage 3.3b-S1, file format 3): the ValueLimits of the category. The
 * sum of a value (MoveSpeedMod, PhysicalArmor, ABSSlash, ...) over the effects of the category is not above a positive
 * limit and not below a negative limit (client code 0x40242C0 sums the speed values of the effects by category).
 */
struct StatusCategory {
    std::string id;
    std::unordered_map<std::string, float> limits;
};

/** The limits of one stat multiplier (StatMultiplierData, stage 3.3b-S2a): the client bounds the value of its table to them. */
struct StatLimit {
    std::string id;
    float min{0.0F};
    float max{0.0F};
};

/** The data as plain values: the content of the file, and what a test sets. */
struct AbilityTables {
    std::vector<CooldownRow> cooldowns;
    std::vector<StatusRow> statusEffects;
    std::vector<SpellRow> spells;
    /** The cost tables and the cost columns of the ability rows (stage 1.3b, file format 2). */
    std::vector<CostRow> manaCosts;
    std::vector<CostRow> staminaCosts;
    std::vector<AbilityCostRow> abilityCosts;
    /** The caster effect rows and the status categories (stage 3.3b-S1, file format 3; empty for a file of format 1 or 2). */
    std::vector<AbilityRow> abilityRows;
    std::vector<StatusCategory> statusCategories;
    /** The limits of the stat multipliers (stage 3.3b-S2a; empty for an older file): MoveSpeedMod and SprintSpeedMod 0 to 1.5. */
    std::vector<StatLimit> statMultipliers;
    /**
     * The number of the rows of the status tables of the game, with each duplicate: the length of the
     * status list of the client. Each status number is below it (code review of stage 3.3a, finding F10).
     * 0: not known (a test with no file), and only the sequence of the numbers is checked. A file must have the
     * number, and each status number of the file is below it (code review of stage 1.3b, finding F10).
     */
    std::uint32_t statusRows{0};
};

class AbilityData {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a row
     * with a member of a wrong form, a number that is not finite, two rows with one id, and a
     * member "counts" whose number is not the size of its list.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. @return False, with no data, when it is not correct. */
    [[nodiscard]] bool set(AbilityTables tables);

    [[nodiscard]] bool loaded() const {
        return !m_tables.cooldowns.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const AbilityTables& tables() const {
        return m_tables;
    }
    /** @return The row with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const CooldownRow* cooldown(std::string_view id) const;
    [[nodiscard]] const StatusRow* status(std::string_view id) const;
    [[nodiscard]] const SpellRow* spell(std::string_view id) const;
    /** @return The cost of a cost id of a cost table (stage 1.3b), or no value: the id has no row, thus no cost. */
    [[nodiscard]] std::optional<float> mana_cost(std::string_view id) const;
    [[nodiscard]] std::optional<float> stamina_cost(std::string_view id) const;
    /** @return The cost columns of an ability row, or null for a row with none. */
    [[nodiscard]] const AbilityCostRow* ability_cost(std::string_view id) const;
    /** @return The caster effect row of an ability (stage 3.3b-S1), or null. */
    [[nodiscard]] const AbilityRow* ability_row(std::string_view id) const;
    /** @return A status category with its limits (stage 3.3b-S1), or null. */
    [[nodiscard]] const StatusCategory* category(std::string_view id) const;
    /** @return The limits of a stat multiplier (stage 3.3b-S2a), or null. */
    [[nodiscard]] const StatLimit* stat_limit(std::string_view id) const;

private:
    AbilityTables m_tables;
    std::unordered_map<std::string, std::size_t> m_cooldowns;
    std::unordered_map<std::string, std::size_t> m_status;
    std::unordered_map<std::string, std::size_t> m_spells;
    std::unordered_map<std::string, std::size_t> m_manaCosts;
    std::unordered_map<std::string, std::size_t> m_staminaCosts;
    std::unordered_map<std::string, std::size_t> m_abilityCosts;
    std::unordered_map<std::string, std::size_t> m_abilityRows;
    std::unordered_map<std::string, std::size_t> m_categories;
    std::unordered_map<std::string, std::size_t> m_statLimits;
    std::string m_error;
    /** The check of `set`, with `fromFile` true for a file: the file must name the number of the status rows (finding F10). */
    [[nodiscard]] bool accept(AbilityTables tables, bool fromFile);
};

} // namespace soulwarden::backend::gamedata

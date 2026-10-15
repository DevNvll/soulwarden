#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
};

/** The data as plain values: the content of the file, and what a test sets. */
struct AbilityTables {
    std::vector<CooldownRow> cooldowns;
    std::vector<StatusRow> statusEffects;
    std::vector<SpellRow> spells;
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

private:
    AbilityTables m_tables;
    std::unordered_map<std::string, std::size_t> m_cooldowns;
    std::unordered_map<std::string, std::size_t> m_status;
    std::unordered_map<std::string, std::size_t> m_spells;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

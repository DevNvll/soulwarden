#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "world/entity.h"
#include "world/world_clock.h"

// The status system of a character (stage 3.3b-S1): the one owner of the status effects of a player. World thread only.
//
// A status is a row of the status tables of the game (gamedata::StatusRow) on a character: a food, a potion, a dust
// (world/consumables.h), the status of a heart rune (world/runes.h), the status that an affix of an equipped item keeps
// (world/gear.h), a caster effect of an ability row (SelfApplyStatusEffect; world/gear.h), and the end status of a status
// (OnEndStatusEffect). The statuses are values of the character with the character id as their key (roadmap, rule
// MP-1): a new session and a new entity of the character keep them. Only this system changes them. They are not in the
// database: a restart of the backend ends them (a rule of this project, before this stage too).
//
// The rules (the cells of the status row; the source of each rule is in docs/systems/status-effects.md):
//   - One status of a row for each character (the maps of the client have one entry for each status number: client
//     code 0x4038E80). A new application of a row that the character has: one more stack, to StackMax (a value below 1
//     is 1: client loader 0x44B65F5); the end: RefreshDuration: now + the duration; a DurationMax above the duration:
//     the old end + the duration, to now + DurationMax; else the later end. The larger potency stays. At StackMax with
//     DontReplaceStack the application changes nothing.
//   - The duration: the duration of the source (DurationOverrides of a consumable), else BaseDuration; not more than
//     DurationMax. A row with no duration is one application now (a heal, a mana change, CastSpell). The gear gives a
//     permanent status.
//   - ApplicationCooldown: one application of the row for each character in that time.
//   - A TickRate with Health, HealthModifierPercent, Mana, or ManaModifierPercent: a heal (times the heal factor of the
//     equip load) and a mana change at each tick, the first tick at the application (DelayInitialTick: one tick later).
//   - StopOnHitCount: the status ends at that hit that the character takes. RemoveOnDeath: it ends at the death.
//   - The end at its time: the OnEndStatusEffect of the row comes on the character. A removal before the end (a death,
//     a new food, the leave of a heart rune activation, a command): the end status comes too, but not for a row with
//     DontApplyOnEndEffectOnRemove.
//   - CastSpell of a row: the spell of the rune explosion goes to the rune system (Runes::status_spell). Other spells:
//     not in this stage (a count).
//   - The values (StatusValues): MOD<attribute>, MaxHealthMod, PhysicalArmor, ElementalArmor, DMG<type> (and
//     DMGStandard), ABS<type>, BaseDamageReduction, MoveSpeedMod, SprintSpeedMod, each times the potency and the stacks;
//     the sum of a value over the statuses of a category is not above the positive ValueLimits of the category, and not
//     below the negative one (StatusEffectCategoryData; client code 0x40242C0 for the speed). The gear system adds them
//     to the gear summary of the character (world/gear.h): a value counts one time.
//
// The cost: the expiry and the ticks look only at the due list (one entry for each end and each tick that is due),
// not at each character, creature, or spawn point (test case ST11 has a time limit).

namespace soulwarden::backend::world {

class World;

/** Why a status ended. */
enum class StatusEnd : std::uint8_t {
    /** Its end on the world clock. */
    time,
    /** A death of the character (RemoveOnDeath). */
    death,
    /** StopOnHitCount. */
    hits,
    /** A newer status of the source removed it (RemoveStatusEffects, RemoveStatusEffectCategories of a consumable). */
    replaced,
    /** Its source went away (an item that the character took off). */
    source,
    /** The character left the world (a status of the gear, the activation of a heart rune). */
    leave,
    /** A development command. */
    command,
};
inline constexpr std::size_t kStatusEndCount = 7;

[[nodiscard]] const char* status_end_text(StatusEnd cause);

enum class StatusVerdict : std::uint8_t {
    /** A new status on the character. */
    applied,
    /** The character had the status: a new stack or a new end. */
    renewed,
    /** A row with no duration: one application now (a heal, a mana change, a spell). */
    instant,
    /** The data has no such row, or the backend has no ability data. */
    unknown,
    /** The character is dead. */
    dead,
    /** ApplicationCooldown: the row had an application on the character in that time. */
    cooldown,
    /** DontReplaceStack at StackMax. */
    full,
};
inline constexpr std::size_t kStatusVerdictCount = 7;

[[nodiscard]] const char* status_verdict_text(StatusVerdict verdict);

/** How a status comes on a character. */
struct StatusApply {
    /** The duration in seconds. 0 or less: the BaseDuration of the row. */
    float seconds{0.0F};
    /** True: no end (the status of an affix of an equipped item). */
    bool permanent{false};
    float potency{1.0F};
    StatusSource source;
};

/** The values of the statuses of a character, with the limits of the categories (Statuses::values). Parts: 0.2 is 20 percent. */
struct StatusValues {
    std::array<float, attribute::kCount> attributes{};
    float maxHealth{0.0F};
    float physicalArmor{0.0F};
    float elementalArmor{0.0F};
    float damageReduction{0.0F};
    /** DMG<type> and ABS<type> by the name of the damage type of the combat tables. */
    std::unordered_map<std::string, float> damage;
    std::unordered_map<std::string, float> absorption;
    float moveSpeed{0.0F};
    float sprintSpeed{0.0F};
};

/** Counts of the status system, for the log, `tools/world list`, and the tests. */
struct StatusCounts {
    std::array<std::uint64_t, kStatusVerdictCount> applications{};
    std::array<std::uint64_t, kStatusEndCount> ended{};
    std::uint64_t ticks{0};
    std::uint64_t heals{0};
    /** The entries of the due list that the tick took (each one an end or a tick that was due, or an old entry). */
    std::uint64_t dueTaken{0};
    /** The spells of a status that the backend does not have (CastSpell of a row with no rule here). */
    std::uint64_t spellsNotDone{0};
};

class Statuses {
public:
    explicit Statuses(World& world) : m_world(world) {}

    /** Puts a status row on a character in the world. */
    StatusVerdict apply(Entity& character, const gamedata::StatusRow& row, const StatusApply& how);
    /** The same with the id of the row ("FoodCONT5"). */
    StatusVerdict apply(Entity& character, std::string_view status, const StatusApply& how);

    /** Ends each status of a character that `select` selects. @return The number of statuses that ended. */
    unsigned remove(Entity& character, const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause);
    /** Ends the statuses with these ids or with a category of the list (a new food ends the old food). */
    unsigned remove_rows(Entity& character, const std::vector<std::string>& ids, const std::vector<std::string>& categories,
                         StatusEnd cause);
    /** Ends the statuses of one source. */
    unsigned remove_source(Entity& character, const StatusSource& source, StatusEnd cause);

    /** One status of the gear: a status that an affix of an equipped item keeps (world/gear.h). */
    struct GearStatus {
        const gamedata::StatusRow* row{nullptr};
        float potency{1.0F};
        StatusSource source;
    };
    /**
     * The gear system gives the statuses of the equipped items: each status of the gear that is not in the list ends,
     * each one of the list is on the character (permanent, with the larger potency). It does not refresh the gear
     * values (the gear system calls it in its refresh).
     */
    void set_gear(Entity& character, const std::vector<GearStatus>& wanted);

    /** The character died: the statuses with RemoveOnDeath end. */
    void died(Entity& character);
    /** The character took a hit of a creature: a status with StopOnHitCount counts it. */
    void hit_taken(Entity& character);
    /** The world made the entity of a character: it gets the statuses of the character. */
    void entered(Entity& character);
    /** The character leaves the world: the statuses of its gear end (its next entry gives them again from its items). */
    void left(Entity& character);
    /** The world calls it in each tick: the ends and the ticks that are due. */
    void tick();

    /** @return The values of the statuses of a character now, with the limits of the categories. */
    [[nodiscard]] StatusValues values(const Entity& character) const;
    /** @return The statuses of a character (the tests and the development route), or null. */
    [[nodiscard]] const std::vector<ActiveStatus>* statuses_of(const std::string& characterId) const;
    /** @return True when the character has a status of this row. */
    [[nodiscard]] bool has(const Entity& character, std::string_view status) const;
    [[nodiscard]] const StatusCounts& counts() const {
        return m_counts;
    }
    /** @return The number of entries of the due list (the tests: the list does not grow with the world). */
    [[nodiscard]] std::size_t due_size() const {
        return m_due.size();
    }

private:
    struct CharacterState {
        std::vector<ActiveStatus> statuses;
        /** The end of the ApplicationCooldown of each row (the lower-case id). */
        std::unordered_map<std::string, WorldClock::time_point> applied;
    };
    /** One end or one tick on the due list. An entry whose status is not there with the same number is old. */
    struct Due {
        std::string characterId;
        std::uint64_t serial{0};
        bool end{false};
    };

    StatusVerdict counted(StatusVerdict verdict) {
        ++m_counts.applications[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** Puts the statuses into the entity, marks the status state, and refreshes the gear values (`refresh`). */
    void publish(Entity& character, CharacterState& state, bool refresh = true);
    /** The heal and the mana of one application or one tick of a row. */
    void give(Entity& character, const gamedata::StatusRow& row, float amount);
    /** The due entries of a status: its end and its next tick. */
    void schedule(const std::string& characterId, const ActiveStatus& status);
    /** Ends the statuses of the state that `select` selects, with the end statuses. @return The number that ended. */
    unsigned end_statuses(Entity* character, const std::string& characterId, CharacterState& state,
                          const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause);
    /** The CastSpell of a row (the rune explosion). */
    void cast(Entity& character, const gamedata::StatusRow& row, const StatusSource& source);
    [[nodiscard]] const gamedata::StatusRow* row_of(std::string_view status) const;

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::uint64_t m_nextSerial{1};
    StatusCounts m_counts;
};

} // namespace soulwarden::backend::world

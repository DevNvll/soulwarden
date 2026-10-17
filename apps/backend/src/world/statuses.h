#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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
//   - The duration: the duration of the source (DurationOverrides of a consumable), else BaseDuration, times the time factor of the
//     character (stage B3: 1 plus the StatusEffectDurationReduction of its rules for the status, Passives::duration_factor); not more
//     than DurationMax. A row with no duration is one application now (a heal, a mana change, CastSpell). The gear gives a
//     permanent status.
//   - OnStackStatusEffect (stage B3): after an application, the stacks of the status against AddOnStackSize with
//     AddOnStackSizeComparison: the named statuses come on the character (the timer of Archer's Speed at the first stack of its haste).
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
    /**
     * Stage B3: the weapon with a tag left the hand (a tag removal action of the data: Bow_Remove_StatusEffects_Sheathe ends the haste of
     * Dodge and Weave when the bow leaves the hand).
     */
    weapon,
};
inline constexpr std::size_t kStatusEndCount = 8;

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
    /**
     * Stage B3: true: `seconds` is the time of a development command, and the time factors of the character (StatusEffectDurationReduction
     * of its rules: Speedy, Alacrity) do not change it. False: the time of the source or of the row times the time factor of the character.
     */
    bool fixedTime{false};
    float potency{1.0F};
    StatusSource source;
    /**
     * Stage 3.3b-S6: the source of a heal of the row (HealthModifierDamageBased with UseHealScalingValue: the healer and its heal base at
     * the cast). A status of a row with no such heal ignores it.
     */
    HealSource heal;
    /**
     * Stage 3.4f-C: the source of the damage of a row with a negative health term (a status area of a creature: the creature and its core
     * damage at the application). A row with no such term ignores it.
     */
    DamageSource damage;
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
    /** The critical chance and the critical damage parts (CritChanceModifier, CritDamageModifier; stage 3.3b-S5). */
    float critChance{0.0F};
    float critDamage{0.0F};
    /**
     * The heal parts (stage 3.3b-S6): HealMod, ConsumableHealMod, NonConsumableHealMod, DmgPctToHealthHealMod (received), and
     * HealScalingValueMultiplier (outgoing), each with the limits of its categories (Disease: HealMod -0.5).
     */
    float healTaken{0.0F};
    float healConsumable{0.0F};
    float healNonConsumable{0.0F};
    float healLifeSteal{0.0F};
    float healOutgoing{0.0F};
    /** The StaminaRate part (stage 3.4f-B): the regeneration of the stamina is (1 + this part) times its rate. */
    float staminaRate{0.0F};
    /**
     * The mana parts (stage 3.5b.2, the mana of the client): MaxMana (the maximum is (1 + the parts) times the base) and ManaRate (the
     * regeneration is (1 + the parts) times the base rate).
     */
    float maxMana{0.0F};
    float manaRate{0.0F};
};

/** Counts of the status system, for the log, `tools/world list`, and the tests. */
struct StatusCounts {
    std::array<std::uint64_t, kStatusVerdictCount> applications{};
    std::array<std::uint64_t, kStatusEndCount> ended{};
    std::uint64_t ticks{0};
    std::uint64_t heals{0};
    /**
     * The heals of a row with HealthModifierDamageBased and UseHealScalingValue (stage 3.3b-S6: the heal base of the healer; Divine
     * Embrace, the pulses of the Beacon and of Sacred Ground).
     */
    std::uint64_t abilityHeals{0};
    /**
     * The ticks of a heal that is a part of the damage of the source with no heal base (stage 3.3b-S2b, and from stage 3.3b-S6 only: a
     * status with no healer, a command; a row with HealthModifierDamageBased and no UseHealScalingValue, whose base is the core damage
     * of the source: not in this stage): the tick comes at the TickRate of the row, and it gives no health.
     */
    std::uint64_t pulsesWithNoHeal{0};
    /** The entries of the due list that the tick took (each one an end or a tick that was due, or an old entry). */
    std::uint64_t dueTaken{0};
    /** The spells of a status that the backend does not have (CastSpell of a row with no rule here). */
    std::uint64_t spellsNotDone{0};
    /** Stage 3.4f-C: the ticks of a row with a negative health term that hurt their character (a poison of a creature area). */
    std::uint64_t damageTicks{0};
    /** Stage B3: the applications whose time a time factor of the character changed (Speedy, Alacrity), and the OnStackStatusEffect statuses. */
    std::uint64_t scaledTimes{0};
    std::uint64_t onStack{0};
};

class Statuses {
public:
    explicit Statuses(World& world) : m_world(world) {}

    /** Puts a status row on a character in the world. */
    StatusVerdict apply(Entity& character, const gamedata::StatusRow& row, const StatusApply& how);
    /**
     * @return The end of a status of a row at a new application that keeps it (the one rule of a character and, from stage B2, of a
     *         creature): RefreshDuration: now plus the time, not before the old end; a DurationMax above the duration: the old end plus
     *         the time, not after now plus DurationMax; else the later end of the two. A reading of the names of the columns (a decision of
     *         an agent: the add code of the server is not in the client; the loader 0x44B4430 has no reader of RefreshDuration).
     */
    [[nodiscard]] static WorldClock::time_point renewed_end(const gamedata::StatusRow& row, WorldClock::time_point old, WorldClock::time_point now,
                                                            std::chrono::milliseconds lasting);
    /** The same with the id of the row ("FoodCONT5"). */
    StatusVerdict apply(Entity& character, std::string_view status, const StatusApply& how);

    /** Ends each status of a character that `select` selects. @return The number of statuses that ended. */
    unsigned remove(Entity& character, const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause);
    /** Ends the statuses with these ids or with a category of the list (a new food ends the old food). */
    unsigned remove_rows(Entity& character, const std::vector<std::string>& ids, const std::vector<std::string>& categories,
                         StatusEnd cause);
    /** Ends the statuses of one source. */
    unsigned remove_source(Entity& character, const StatusSource& source, StatusEnd cause);
    /**
     * Ends the status of one row and one source of a character, also of a character whose entity left the world (code review
     * of stage 3.3b-S2b, finding F6: an aura takes its status back from a friend that left). @return The number that ended.
     */
    unsigned remove_row_of(const std::string& characterId, std::string_view status, const StatusSource& source, StatusEnd cause);
    /**
     * Gives the status of one row of a character a different source with no new application (finding F4: the status of two
     * Beacons stays when the Beacon of its source ends while the other one still gives it). The wire has no source: no new
     * fragment. @return False: the character has no status of the row with the source `from`.
     */
    bool change_source(const std::string& characterId, std::string_view status, const StatusSource& from, const StatusSource& to,
                       const HealSource* heal = nullptr);

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
    /**
     * The heal and the mana of one application or one tick of a row (stage 3.3b-S6: the one heal path of the health system, with the
     * kind of the source and the heal source of the status). `tick` false: the application. `at`: the due time of a tick (the dodge decides
     * its damage at this time: code review of stage 3.4f-C, finding AR-R1); no value: now.
     */
    void give(Entity& character, const gamedata::StatusRow& row, float amount, const StatusSource& source, const HealSource& heal, bool tick,
              const DamageSource& damage = {}, std::optional<WorldClock::time_point> at = std::nullopt);
    /** @return The factor of the potency of a row on a character from the EffectPotencyMods of its other statuses ("SacredGroundCat=-0.1"). */
    [[nodiscard]] float potency_factor(const Entity& character, const gamedata::StatusRow& row) const;
    /** The due entries of a status: its end and its next tick. */
    void schedule(const std::string& characterId, const ActiveStatus& status);
    /** Ends the statuses of the state that `select` selects, with the end statuses. @return The number that ended. */
    unsigned end_statuses(Entity* character, const std::string& characterId, CharacterState& state,
                          const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause);
    /** The CastSpell of a row (the rune explosion). */
    void cast(Entity& character, const gamedata::StatusRow& row, const StatusSource& source);
    /**
     * Stage B3: OnStackStatusEffect of a row after an application that left the status with `stacks` stacks: the stacks against AddOnStackSize
     * with AddOnStackSizeComparison (no size: each application), and then the named statuses on the character (source: the status).
     */
    void on_stack(Entity& character, const gamedata::StatusRow& row, unsigned stacks, const StatusSource& source);
    [[nodiscard]] const gamedata::StatusRow* row_of(std::string_view status) const;

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::uint64_t m_nextSerial{1};
    /** Stage B3: the depth of the OnStackStatusEffect applications (a bound: a row of the data that names itself does not loop). */
    unsigned m_onStackDepth{0};
    StatusCounts m_counts;
};

} // namespace soulwarden::backend::world

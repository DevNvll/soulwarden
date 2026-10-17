#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/stamina_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The stamina of a player and the dodge (stage 3.4f-B, docs/systems/stamina-and-dodge.md; research creature-ranged.md task D; live
// checks LS1 to LS9 of the stage). World thread only.
//
// The stamina of a character is a value of the character in the world (Entity::vitals, rule MP-1). Only this system changes it, through
// one path (Stamina::change): a payment of an action (a dodge at its mark, a stamina cost of a fragment of an attack), a gain of a status
// (the cell Stamina of a status row: Stamina Recovery, the stamina of the node Mauler's Stamina), and the regeneration of the tick. The
// values come from the game data (stamina-data.json, tools/gamedata/stamina.sh):
//
//   maximum         m_initMax of the Stamina component of the player slice (100), times the MaxStamina multiplier (1 with no source)
//   regeneration    m_regenRate (33.33 in one second) times the regeneration factor of the load class and 1 + the StaminaRate of the
//                   statuses; it starts when the delay ends and no dodge stops it
//   delay           a payment sets the delay StaminaRegenDelay (1 s; StaminaRegenDelayOnEmpty at 0) of the base vitals; the dodge stops the
//                   regeneration from its start to its mark CanTransition and sets the delay of its exit (SetStaminaRegenDelay, 1 s) there
//   empty           a payment stops at 0 (no debt: live check LS6); at 0 the character is winded for m_maxWindedTime (2 s) and exhausted
//                   until its stamina is full again (the action Exhausted of the global action list: no dodge in that time)
//
// The client of the owner predicts the same values (live checks LS5 to LS7: the memory of its Stamina component) and it does not take
// the value of the Stamina state (LS2, LS3): the server keeps its own value with the rules of the client, and its value decides a dodge.
//
// The dodge. The client sends the dodge input (bit 322) also for a press that it refuses itself (LS9): the input is a request. The server
// accepts a dodge (Stamina::dodge) when the character lives, its input is not locked (a travel), it is not overburdened, it is not
// exhausted, its stamina is above 0 (the condition IfStaminaValue > 0 of the action Dodge), and no dodge plays, or the dodge that plays is
// in a window CanDodgeCancel of its fragment (a buffered window: the next dodge starts at the end of the window; an instant window: at
// once). The basic attack sequence decides a dodge in the fragment of an attack (world/attack_sequence.h). An accepted dodge:
//
//   the fragment     the dodge class of the load class (light: Dodge_Roll_Fast, medium: Dodge_Roll, heavy: Dodge_Roll_Slow)
//   the payment      the cost of the class (45, 45, 50) times the cost factor of the load class, at the mark PayStaminaCost (0.1 s),
//                    through the stamina path; a dodge that a death, a travel, or a leave ended before the mark pays nothing
//   immunity         from the start of the fragment to the end of its Invulnerability clip (0.5 s), the end not included: a creature hit
//                    in this window does not hurt the character (Health::creature_hit); the passive rules with
//                    OnHitTakenWhileInvulnerable get the hit (world/passives.h)
//
// The time of a dodge is the world time at which the session takes the input step (a rule of this project: no rewind, research task D).

namespace soulwarden::backend::world {

class World;

/** The constants of the stamina system that the game data does not give (rules of this project, each with its source). */
namespace stamina_rules {
/**
 * The immune window of an earlier dodge stays this long after its end (code review of stage 3.4f-B, finding CS-R1, a decision of an
 * agent): the world tests a contact of a flight at its time in the segment of the tick, at most one tick after that time; 1 s is far more
 * than one tick.
 */
inline constexpr std::chrono::milliseconds kPastWindowKeep{1000};
/** The most kept windows of one character (a bound of the memory; a dodge needs 0.267 s at least before the next one starts). */
inline constexpr std::size_t kPastWindows = 8;
} // namespace stamina_rules

/** The answer to a dodge input. */
enum class DodgeVerdict : std::uint8_t {
    /** The dodge starts now. */
    accepted,
    /** A dodge plays and the press is in a buffered window CanDodgeCancel: the next dodge starts at the end of the window. */
    buffered,
    /** The backend has no stamina data: the dodge of the client has no rule of the server (no payment, no immunity). */
    noData,
    /** The character is dead or not in the world. */
    dead,
    /** The input of the character is locked (a travel that the client did not confirm). */
    locked,
    /** The equip load of the character is overburdened (the action Dodge of the client refuses it). */
    overburdened,
    /** The character is exhausted (its stamina was 0 and is not full again). */
    exhausted,
    /** The stamina of the character is 0. */
    noStamina,
    /** A dodge plays and the press is not in a window CanDodgeCancel of its fragment. */
    dodging,
    /**
     * Stage U3 (research Q4; game data: Not IfEncumbered of the actions Dodge, Dodge_Stunlock and of CanDodgeCancel): the bag of the
     * character is over its weight limit (Inventory::encumbrance). The client plays no dodge ("Encumbered - Cannot run, dodge, or jump").
     */
    encumbered,
};
inline constexpr std::size_t kDodgeVerdictCount = 10;

/** @return A short text for a log line, for example "the character is exhausted". */
[[nodiscard]] const char* dodge_verdict_text(DodgeVerdict verdict);

/** The counts of the stamina system, for the log, the development routes, and the tests. */
struct StaminaCounts {
    std::array<std::uint64_t, kDodgeVerdictCount> dodges{};
    /** Payments that changed the stamina, and payments of a dodge that a death, a travel, or a leave cancelled before the mark. */
    std::uint64_t payments{0};
    std::uint64_t paymentsCancelled{0};
    /** Gains of a status (the cell Stamina), and regeneration steps that changed the stamina. */
    std::uint64_t gains{0};
    std::uint64_t regenerationSteps{0};
    /** Times that the stamina of a character came to 0 (the exhausted state started), and creature hits that a dodge avoided. */
    std::uint64_t emptied{0};
    std::uint64_t evaded{0};
    /** Code review of stage B3, finding B3-R1: dodge actions that an accepted attack from the mark CanAttackCancel ended. */
    std::uint64_t attackCancels{0};
};

class Stamina {
public:
    explicit Stamina(World& world) : m_world(world) {}

    /** @return True when the backend has the stamina data (stamina-data.json). */
    [[nodiscard]] bool has_data() const;
    /**
     * The one stamina path. A cost (amount below 0) stops at 0 and sets the delay of the regeneration; at 0 the character is winded and
     * exhausted. A gain stops at the maximum. A dead character and a creature get no change. The owner gets the Stamina state.
     * @return The change of the stamina (after the limits).
     */
    float change(Entity& character, float amount, const char* what);
    /** A payment of an action at its mark (a stamina cost of a fragment of an attack): the cost id of the stamina costs, times `multiplier`. */
    float pay_cost(Entity& character, std::string_view costId, float multiplier, const char* what);
    /** @return The maximum of the stamina of a character (the data, or the value of its vitals with no data). */
    [[nodiscard]] float maximum(const Entity& character) const;
    /** @return The regeneration of a character in one second (0 with no data). */
    [[nodiscard]] float regeneration(const Entity& character) const;
    /** @return The multiplier of the regeneration (the stat key StaminaRate of the speed table: 1 + the StaminaRate of the statuses). */
    [[nodiscard]] static float regeneration_multiplier(const Entity& character);
    /** @return True while the character is exhausted (its stamina came to 0 and is not full again). */
    [[nodiscard]] bool exhausted(const Entity& character) const;

    /**
     * A dodge input of the character at the world time `at` (the basic attack sequence permitted it, or no attack plays). `locked`: the
     * input of its session is locked (a travel). The checks and the start are in the header of this file.
     */
    DodgeVerdict dodge(Entity& character, WorldClock::time_point at, bool locked);
    /**
     * Code review of stage B3, finding B3-R1: an attack of a character was accepted at `at` (a basic shot, the start of a melee attack). When its
     * dodge action plays, the time is at or after the mark CanAttackCancel of the fragment of the last dodge (stamina data format 2; rule 96 of
     * player.grid: Dodge to Attack, Shoot, Offhand), and no buffered next dodge waits, the attack replaces the dodge action: it ends now, with
     * its exit effects (Passives::dodge_ended: Dodge and Weave, Evasive Tactics), and its mark CanTransition gives them no second time.
     */
    void attack_started(Entity& character, WorldClock::time_point at);
    /**
     * @return True when a dodge input of the character can start a dodge now (the checks of a start: life, lock, load, exhausted, stamina
     *         above 0). With no stamina data: true (the client plays its dodge, and the server has no rule for it). The attack sequence asks
     *         it before a dodge ends an attack (code review of stage 3.4f-B, finding CS-R2).
     */
    [[nodiscard]] bool can_dodge(const Entity& character, bool locked) const;
    /**
     * @return True when a dodge of the character makes it immune at `at` (the Invulnerability clip of its fragment, the end not included).
     *         The window of an earlier dodge of the character counts too (code review of stage 3.4f-B, finding CS-R1): a buffered next
     *         dodge that started before the world tested an earlier contact does not end the window of that contact.
     */
    [[nodiscard]] bool immune(const Entity& character, WorldClock::time_point at) const;
    /** @return The start of the dodge whose immune window has `at`, or no value. */
    [[nodiscard]] std::optional<WorldClock::time_point> immune_since(const Entity& character, WorldClock::time_point at) const;
    /**
     * A creature hit at `at` did not hurt the character: its dodge took it. The counts, the log line, and the passive rules. `at`: the
     * time of the contact (no value: now).
     */
    void evaded(Entity& character, const Entity& creature, float wouldDeal, const char* what,
                std::optional<WorldClock::time_point> at = std::nullopt);
    /** @return True while a dodge of the character plays (from its start to its mark CanTransition). */
    [[nodiscard]] bool dodging(const Entity& character, WorldClock::time_point at) const;

    /** The world made the entity of a character (a world entry): its stamina is full, and no dodge plays. */
    void entered(Entity& character);
    /** The character died, travelled, respawned, or left the world: its dodge ends (a payment before its mark is cancelled). */
    void interrupted(const std::string& characterId, const char* why);
    /** The character lives again (a respawn, a revive): its stamina is full and it is not exhausted. */
    void refilled(Entity& character);
    /**
     * The status values or the gear of a character changed: the regeneration and the maximum follow. `rateChanged`: the StaminaRate part
     * changed (the owner gets the Stamina state with the new multiplier).
     */
    void refreshed(Entity& character, bool rateChanged);

    /** The regeneration of the characters, the payments of the dodges at their marks, the marks CanTransition, and the buffered dodges. */
    void tick();

    [[nodiscard]] const StaminaCounts& counts() const {
        return m_counts;
    }

    /** The dodge of one character, for the development routes and the tests. */
    struct DodgeView {
        std::uint32_t generation{0};
        WorldClock::time_point start{};
        WorldClock::time_point immuneEnd{};
        WorldClock::time_point transitionAt{};
        bool paid{false};
        std::string fragment;
        float cost{0.0F};
    };
    [[nodiscard]] std::optional<DodgeView> dodge_of(const std::string& characterId) const;

private:
    struct DodgeState {
        std::uint32_t generation{0};
        EntityId entity{0};
        const gamedata::DodgeFragment* fragment{nullptr};
        std::string costId;
        float cost{0.0F};
        WorldClock::time_point start{};
        WorldClock::time_point payAt{};
        WorldClock::time_point immuneEnd{};
        WorldClock::time_point transitionAt{};
        bool paid{false};
        bool transitioned{false};
        /**
         * Code review of stage B3, finding B3-R1: an accepted attack from the mark CanAttackCancel on ended the dodge action (its exit effects
         * came then); the mark CanTransition gives them no second time.
         */
        bool ended{false};
        /** A buffered second dodge: its start (the end of the buffered window). */
        std::optional<WorldClock::time_point> next;
        bool nextLocked{false};
    };

    DodgeVerdict counted(DodgeVerdict verdict) {
        ++m_counts.dodges[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** The checks of a dodge start that do not depend on a dodge that plays. */
    DodgeVerdict start_checks(const Entity& character, bool locked) const;
    /**
     * Starts an accepted dodge at `at`. `chained` (stage B3): the buffered next dodge of the dodge that plays: it continues the dodge action of
     * the client (no new action, no end of the action).
     */
    void start(Entity& character, WorldClock::time_point at, bool chained = false);
    /** The work of one dodge that is due now (its payment, its mark CanTransition, its buffered next dodge). */
    void step_dodge(const std::string& characterId, WorldClock::time_point now);
    /** The regeneration of one character to `now`. @return False when it has no more regeneration to do (full). */
    bool regenerate(Entity& character, WorldClock::time_point now);
    void schedule(const std::string& characterId, WorldClock::time_point at);
    void watch(Entity& character);

    /** The immune window of a dodge that a later dodge replaced or that an interruption ended (finding CS-R1). */
    struct PastWindow {
        EntityId entity{0};
        WorldClock::time_point start{};
        WorldClock::time_point end{};
    };
    /** Keeps the immune window of the dodge of a character that ends now, and forgets the windows older than kPastWindowKeep. */
    void keep_window(const std::string& characterId, const DodgeState& state, WorldClock::time_point end);

    World& m_world;
    StaminaCounts m_counts;
    std::map<std::string, DodgeState> m_dodges;
    /** The immune windows of the earlier dodges of each character (at most kPastWindows, kPastWindowKeep after their end). */
    std::map<std::string, std::vector<PastWindow>> m_pastWindows;
    /** The characters whose stamina is not full, and the time of their last regeneration step. */
    std::map<std::string, WorldClock::time_point> m_regenerating;
    /** The due work of the dodges: the time and the character. */
    std::multimap<WorldClock::time_point, std::string> m_due;
    std::uint32_t m_generation{0};
};

} // namespace soulwarden::backend::world

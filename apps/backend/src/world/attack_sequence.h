#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gamedata/weapon_actions.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The sequence of the basic attacks of a character (stage 3.3b-M1, protocol notes 4.32): the server follows the attack graph of
// the weapon in the hand (gamedata::WeaponActions) with the input steps of the client, as the client plays it, and finds the
// fragment and the strike of each melee contact.
//
// The rules of the client (the data of the action lists and the animation databases, and the live check I1 of the stage):
//
//   - The clock of a fragment that starts at the input step s is (k - s + 1) / 30 seconds at the step k.
//   - From rest, the attack input (bit 555) starts the first fragment of the graph (Attack_Start_Charge).
//   - An instant transition starts its next fragment at the first step in its window whose input fits: the attack input or its
//     hold for CanAttack_Heavy, the attack input for CanAttack_Primary_Rapier, neither for Attack_Heavy_NotHeld, the release for
//     Attack_Primary_Released, each step for a transition with no condition (the full charge). The new fragment can take a
//     transition in the same step (Attack_Start_Charge_C2, then the light swing at the release step).
//   - A buffered transition takes a press (not a hold) in its window: its next fragment starts at the end of the window. A press
//     before the window is lost.
//   - A fragment ends at its length (the end of its animation). A press after the end, or after the last transition window of the
//     fragment, starts a new attack from rest.
//   - A dodge input (bit 322) in a dodge window of the fragment ends the attack: at once in a CanDodgeCancel window that starts after
//     the start of the fragment, at the end of a CanDodgeBuffer window. A dodge in a cancel window that starts with the fragment is no
//     end (live check I1: the client played no dodge there).
//
// A contact of the client is a hit of a strike of a fragment visit when the clock of the visit at the input step of the contact
// is in the window of the strike, with a tolerance (attack_rules). A strike hits each target one time: the ledger has the visit,
// the strike, and the target. The rules of the tolerance, of the band at the end of a fragment, and of the history are decisions
// of an agent (protocol notes 4.32).
//
// The sequence is a value of the character: the combat system keeps it with the character id as its key (rule MP-1). An input
// number is a value of one client: a step of a different session or of a new input sequence of the client ends the sequence (a
// reconnect), as a change of the weapon, an accepted ability, a death, a travel, and a dodge do. World thread only.

namespace soulwarden::backend::world {

namespace attack_rules {
/** Input steps of the client in one second (the simulation of the client: 30 steps). */
inline constexpr float kStepsInSecond = 30.0F;
/**
 * The tolerance of the window of a strike, in input steps: a contact can come this number of steps before the start of the
 * window and after its end (live check I1: each contact of 7 weapons was in its window, some at its end; a decision of an agent).
 */
inline constexpr float kEarlySteps = 1.0F;
inline constexpr float kLateSteps = 2.0F;
/** The sequence keeps this number of visits for the contacts that come after the next visit started. */
inline constexpr std::size_t kVisitHistory = 4;
/**
 * A press in this band (seconds) around the length of the fragment before it can be the next step of the chain or a new attack:
 * the visit has the other fragment too, and the first contact that fits only the other one selects it (the length of a fragment
 * with two clips of one animation is not fully known; a decision of an agent).
 */
inline constexpr float kLengthBand = 0.25F;
/** A tolerance of the clock comparisons (the client adds 1/30 s in floats). */
inline constexpr float kClockEpsilon = 0.0005F;
/** The most transitions in one step (a guard against a loop of the data). */
inline constexpr int kTransitionsInStep = 6;
/**
 * The window of a visit on the world clock: a contact that comes more than this before the time of its strike on the world
 * clock is not a hit (the input numbers are values of the client; the same rule as ability_rules::kPhaseEarly).
 */
inline constexpr std::chrono::milliseconds kWorldEarly{250};
} // namespace attack_rules

/** The attack bits of one input step of the client. */
struct AttackBits {
    bool press{false};
    bool hold{false};
    bool release{false};
    bool dodge{false};
};

/** Why a sequence ended (for the counts and the log). */
enum class SequenceEnd : std::uint8_t { session, restart, weapon, ability, dodge, death, travel, command };
inline constexpr std::size_t kSequenceEndCount = 8;
[[nodiscard]] const char* sequence_end_text(SequenceEnd end);

/** One visit of a fragment. */
struct AttackVisit {
    /** Count of the visits of the character, from 1: the identity of the attack of a strike. */
    std::uint32_t number{0};
    std::uint32_t fragment{0};
    /** The other fragment of a press in the band of the length (attack_rules::kLengthBand), or -1. */
    std::int32_t alternate{-1};
    /** The input number of the start, and the time of the world clock at it. */
    std::uint16_t start{0};
    WorldClock::time_point startedAt{};
    /** True after the next visit started (`interrupted`: an end of the sequence, not a transition) at the input `endInput`. */
    bool ended{false};
    bool interrupted{false};
    std::uint16_t endInput{0};
    /** A buffered transition that took a press: the next fragment and the time of the clock at which it starts. -1: none. */
    std::int32_t pending{-1};
    float pendingAt{0.0F};
    /** The end of a buffered dodge (seconds of the clock), or below 0. */
    float dodgeAt{-1.0F};
    /** The payments of the fragment that the visit passed. */
    std::size_t paid{0};
};

/** The fragment and the strike of a contact. */
struct StrikeMatch {
    enum class Verdict : std::uint8_t {
        /** A strike of a visit fits the contact, and the target took no hit of that strike. */
        hit,
        /** The sequence has no visit for the input of the contact (no attack, an other session or input sequence). */
        noVisit,
        /** A visit has the input of the contact, but no strike window of it fits. */
        noStrike,
        /** The strike of the contact hit the target already. */
        sameStrike,
    };
    Verdict verdict{Verdict::noVisit};
    std::uint32_t visit{0};
    const gamedata::AttackFragment* fragment{nullptr};
    std::uint32_t strike{0};
    const gamedata::AttackStrike* strikeData{nullptr};
};

/** The counts of the sequences of the world (tools/world list). */
struct SequenceCounts {
    std::uint64_t visits{0};
    std::uint64_t strikes{0};
    std::uint64_t alternates{0};
    std::uint64_t staminaPayments{0};
    std::uint64_t manaPayments{0};
    std::uint64_t paymentsWithCondition{0};
    std::uint64_t ends[kSequenceEndCount]{};
};

class AttackSequences {
public:
    /** The state of one character. */
    struct State {
        SessionId session{0};
        std::uint32_t inputSequence{0};
        /** The weapon in the hand (Combat::weapon_key) and its graph. */
        std::string weapon;
        const gamedata::WeaponActionSet* actions{nullptr};
        bool hasInput{false};
        std::uint16_t lastInput{0};
        /** The visits, the newest last. The newest one plays while it is not `ended`. */
        std::deque<AttackVisit> visits;
        std::uint32_t visitCount{0};
        /** The strike ledger: the visit number, the strike, and the target of each hit (only of the visits of `visits`). */
        std::set<std::tuple<std::uint32_t, std::uint32_t, EntityId>> hits;
        /** The count of the ends of the sequence. */
        std::uint32_t generation{0};
    };

    /**
     * One input step of the client of a session for its character (in the sequence of the input numbers; the session gives each
     * step one time). A step of a different session or input sequence, or with a different weapon in the hand, ends the old
     * sequence first. A weapon with no graph (`actions` null) has no sequence.
     * @return The payments that the visits passed in this step (the world pays them).
     */
    std::vector<const gamedata::AttackPayment*> step(const std::string& characterId, SessionId session, std::uint32_t inputSequence,
                                                     std::uint16_t input, const AttackBits& bits, const std::string& weapon,
                                                     const gamedata::WeaponActionSet* actions, WorldClock::time_point now);

    /**
     * Finds the visit and the strike of a contact at an input number. `usable` says if a strike has a row for the character (the
     * learned nodes of the clip conditions): a strike with no row for it is not a match (the great axe has two strikes in one
     * window, one with the pull node and one without it).
     */
    [[nodiscard]] StrikeMatch match(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                    EntityId target, WorldClock::time_point now,
                                    const std::function<bool(const gamedata::AttackStrike&)>& usable);
    /** Notes the hit of a strike on a target (StrikeMatch::visit, strike). */
    void note_hit(const std::string& characterId, std::uint32_t visit, std::uint32_t strike, EntityId target);

    /** Ends the sequence of a character (a weapon change, an accepted ability, a death, a travel). */
    void end(const std::string& characterId, SequenceEnd why);
    /** Removes the state of a character that left the world (its entity is gone). */
    void forget(const std::string& characterId);
    /** @return The number of the targets that a strike of a visit hit. */
    [[nodiscard]] std::size_t strike_hits(const std::string& characterId, std::uint32_t visit, std::uint32_t strike) const;
    /** Counts a payment of a fragment: 0 stamina, 1 mana, 2 a payment with a condition that the world does not model. */
    void count_payment(int kind);

    [[nodiscard]] const State* state_of(const std::string& characterId) const;
    [[nodiscard]] const SequenceCounts& counts() const {
        return m_counts;
    }
    /** @return The number of characters with a state (the test of the load). */
    [[nodiscard]] std::size_t size() const {
        return m_states.size();
    }

private:
    void reset(State& state, SequenceEnd why, std::uint16_t input);
    void start_visit(State& state, std::uint32_t fragment, std::int32_t alternate, std::uint16_t input, WorldClock::time_point now);
    void advance(State& state, std::uint16_t input, const AttackBits& bits, WorldClock::time_point now,
                 std::vector<const gamedata::AttackPayment*>& payments);
    [[nodiscard]] bool strike_fits(const State& state, const AttackVisit& visit, std::uint32_t fragment, std::uint16_t input,
                                   WorldClock::time_point now, const std::function<bool(const gamedata::AttackStrike&)>& usable,
                                   std::uint32_t& strike) const;

    std::unordered_map<std::string, State> m_states;
    SequenceCounts m_counts;
};

} // namespace soulwarden::backend::world

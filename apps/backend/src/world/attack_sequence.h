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
/**
 * The steps of a gap of the input (reports that did not come: a client sends each entry in 5 reports) that the sequence fills with
 * the last known hold and no press, no release, and no dodge (code review of stage 3.3b-M1, finding M1-R4: a buffered transition
 * starts at its due step, not at the first step after the gap). A larger gap is not filled (a bound of the work; a decision of an
 * agent: 3 s).
 */
inline constexpr unsigned kGapFill = 90;
/**
 * A press of a key of the bar stays valid for this number of input steps (the action lists of the player: IfActiveAbilityMoveName with
 * BackTicks 20 and UseBacktickInputs): the ability starts at the first step of them at which its action can start (stage 3.3b-M2).
 */
inline constexpr unsigned kAbilityBackSteps = 20;
} // namespace attack_rules

/** The attack bits of one input step of the client. */
struct AttackBits {
    bool press{false};
    bool hold{false};
    bool release{false};
    bool dodge{false};
    /** The block press (block_press_virtual, bit 484; stage 3.3b-M2: the swing of Charge with Unpredictable Strike). */
    bool block{false};
    /** The input of the session is locked (a travel that the client did not confirm; stage 3.4f-B: only the dodge reads it). */
    bool locked{false};
};

/**
 * The facts of a character for the conditions of a transition inside an ability (stage 3.3b-M2): the learned nodes and the ability
 * rows of its perks. The combat system makes them from the entity.
 */
struct AttackFacts {
    std::function<bool(std::uint32_t node)> learned;
    std::function<bool(std::uint32_t perkAbility)> perk;
};

/** Something that a fragment visit passed in a step (stage 3.3b-M2; a payment from stage 3.3b-M1). */
struct SequenceEvent {
    enum class Kind : std::uint8_t {
        /** A payment of the fragment (PayStaminaCost, CAGE-PayManaCost). */
        payment,
        /** The start of a strike of an ability (the time of its window): the pull of Reap, which the client does not report. */
        strike,
        /** A spell cast of the fragment (CAGE-CastSpell: the ground of Mighty Gavel). */
        cast,
        /** A named mark of the fragment (the cast of a spell of the action: CastGavityWellProjectile). */
        marker,
        /**
         * The start of the fragment of an ability that waited for its step (a buffered window of the fragment that played): the spells
         * of the action at its start (code review of stage 3.3b-M2, finding M2-R5: a cancelled start gives no event).
         */
        start,
    };
    Kind kind{Kind::payment};
    /** The cast of the visit (0: a basic attack), the visit number, the fragment, and the index of the item in its list. */
    std::uint64_t cast{0};
    std::uint32_t visit{0};
    const gamedata::AttackFragment* fragment{nullptr};
    std::uint32_t index{0};
    /**
     * The time of the world clock at which the item is due (code review of stage 3.3b-M2, finding M2-R1): the due time of the visit
     * (AttackVisit::dueAt) and the time of the item in its fragment. The input number of a step can reach the item earlier (a client
     * that sends its numbers faster than 30 in a second, or the gap fill): the item then waits for this time (Combat::step).
     */
    WorldClock::time_point due{};
    /**
     * The count of the ends of the sequence when the item came (State::generation; code review of stage 3.3b-M2b, finding M2b-R2): an
     * item that waits for its due time runs only when no end of the sequence came after it (a travel, a change of the weapon, a death),
     * also when its visit had ended before at the end of its fragment.
     */
    std::uint32_t generation{0};
};

/** Where the fragment of an ability starts after its press (AttackSequences::start_ability). */
struct AbilityStart {
    enum class Verdict : std::uint8_t {
        /** At the step of the press. */
        now,
        /** At a later step (the end of a buffered window, the start of an instant window, the mark CanTransition, the end). */
        later,
        /** The fragment that plays takes no ability in the next steps: the client does not play the press. */
        refused,
    };
    Verdict verdict{Verdict::now};
    /** The input step of the start, and its steps after the press. */
    std::uint16_t input{0};
    unsigned delaySteps{0};
    /**
     * A start at the step of the press (not a dry run): the visit of the fragment, its due time on the world clock, and the count of the
     * ends of the sequence (code review of stage 3.3b-M2b, finding M2b-R1: the spells of the action start at the due time of the visit).
     * 0: no visit.
     */
    std::uint32_t visit{0};
    WorldClock::time_point due{};
    std::uint32_t generation{0};
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
    /** The input number of the start, and the time of the world clock at which the session got it. */
    std::uint16_t start{0};
    WorldClock::time_point startedAt{};
    /**
     * The time of the world clock at which the start is due on the clock of the client (code review of stage 3.3b-M1, finding M1-R1):
     * the time at which the session got the press that started the attack from rest, plus 1/30 s for each input step from that press
     * to this start. A client that sends its input numbers faster than 30 in a second does not reach a later fragment (a full
     * charge) or a strike before its time.
     */
    WorldClock::time_point dueAt{};
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
    /**
     * Stage 3.3b-M2: the cast of an ability whose fragment this visit plays (Abilities; 0: a basic attack), and the strikes, the
     * spell casts, and the marks of the fragment that the visit passed (a bit for each one, in the order of the fragment).
     */
    std::uint64_t cast{0};
    std::uint64_t strikesPassed{0};
    std::uint32_t castsPassed{0};
    std::uint32_t markersPassed{0};
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
    /** The index of the fragment of the match: the fragment of the visit, or its other one (the band at the end of a fragment). */
    std::uint32_t fragmentIndex{0};
    std::uint32_t strike{0};
    /** The cast of the visit (an ability; 0: a basic attack). */
    std::uint64_t cast{0};
    /**
     * A transition that the client takes at a collision (Charge: character_collided), which the server does not see: the contact
     * shows it. The visit takes it at the hit (note_hit): its fragment starts at `lazyStart`. -1: none.
     */
    std::int32_t lazyFragment{-1};
    std::uint16_t lazyStart{0};
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
    /** The items of a fragment of an ability that waited for their due time on the world clock (finding M2-R1). */
    std::uint64_t delayedEvents{0};
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
        /** The hold of the attack input at the last step (the hold or the press bit, with no release): the steps of a gap get it (M1-R4). */
        bool holding{false};
        /** The strike ledger: the visit number, the strike, and the target of each hit (only of the visits of `visits`). */
        std::set<std::tuple<std::uint32_t, std::uint32_t, EntityId>> hits;
        /** The hits of each cast of an ability (IfAbilityNumHits; stage 3.3b-M2): the cast and its accepted hits. */
        std::uint64_t hitsCast{0};
        std::uint32_t castHits{0};
        /** A press of an ability that starts later (a buffered window): its fragment, its cast, and its input step. */
        std::int32_t pendingAbility{-1};
        std::uint64_t pendingCast{0};
        std::uint16_t pendingInput{0};
        WorldClock::time_point pendingDue{};
        /** The count of the ends of the sequence. */
        std::uint32_t generation{0};
        /**
         * Stage 3.4f-B: the step permitted a dodge (a dodge input at rest, at once in a cancel window, or the end of a buffered window
         * of the fragment that played). The combat system takes it (take_dodge) and gives it to the stamina system.
         */
        bool dodged{false};
    };

    /**
     * One input step of the client of a session for its character (in the sequence of the input numbers; the session gives each
     * step one time). A step of a different session or input sequence, or with a different weapon in the hand, ends the old
     * sequence first. A weapon with no graph (`actions` null) has no sequence.
     * @return The payments that the visits passed in this step (the world pays them).
     */
    std::vector<SequenceEvent> step(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                    const AttackBits& bits, const std::string& weapon, const gamedata::WeaponActionSet* actions,
                                    WorldClock::time_point now, const AttackFacts& facts = {});

    /**
     * The press of a key of the bar of a character was accepted (stage 3.3b-M2): the fragment of the ability (the default fragment of
     * its action or a selection) starts as a visit of the sequence with the cast. At rest it starts at the press. In a fragment that
     * plays it starts at the first of the next attack_rules::kAbilityBackSteps steps at which the action of the ability can start: in
     * the window of a transition of the fragment to the ability (an instant window at once, a buffered window at its end), at the mark
     * CanTransition, or at the end of the fragment. With none, the start is refused (the client does not play the press), and nothing
     * changes.
     * @param dryRun True: only the verdict (the checks of a press before its cost and its cooldown).
     */
    AbilityStart start_ability(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                               const std::string& weapon, const gamedata::WeaponActionSet* actions, std::uint32_t ability,
                               std::uint32_t fragment, std::uint64_t cast, WorldClock::time_point now, bool dryRun = false);

    /**
     * Finds the visit and the strike of a contact at an input number. `usable` says if a strike has a row for the character (the
     * learned nodes of the clip conditions): a strike with no row for it is not a match (the great axe has two strikes in one
     * window, one with the pull node and one without it).
     */
    [[nodiscard]] StrikeMatch match(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                    EntityId target, WorldClock::time_point now,
                                    const std::function<bool(const gamedata::AttackStrike&)>& usable, const AttackFacts& facts = {});
    /**
     * Notes the hit of a strike on a target (StrikeMatch::visit, fragmentIndex, strike). The first hit of a visit commits its fragment
     * (code review of stage 3.3b-M1, finding M1-R3): the other fragment of the band goes, and a later contact cannot hit with it. The
     * combat system calls it after the other checks of the claim, thus a refused contact changes nothing.
     */
    void note_hit(const std::string& characterId, std::uint32_t visit, std::uint32_t fragment, std::uint32_t strike, EntityId target);
    /**
     * Notes the hit of a match (note_hit), and first takes its collision transition (StrikeMatch::lazyFragment): the fragment starts at
     * the step of the match. @return The visit number of the hit.
     */
    std::uint32_t note_match(const std::string& characterId, const StrikeMatch& match, EntityId target, WorldClock::time_point now);
    /** @return The accepted hits of a cast of an ability (the condition IfAbilityNumHits). */
    [[nodiscard]] std::uint32_t cast_hits(const std::string& characterId, std::uint64_t cast) const;
    /** @return The newest visit of a character, or null. */
    [[nodiscard]] const AttackVisit* newest(const std::string& characterId) const;

    /**
     * Ends the sequence of a character (a weapon change, an accepted ability, a death, a travel). Also at rest (no visit plays): each
     * item of an earlier visit that waits for its due time is cancelled (the count of the ends goes up; finding M2b-R2).
     */
    void end(const std::string& characterId, SequenceEnd why);
    /** @return The count of the ends of the sequence of a character (State::generation), or no value with no state. */
    [[nodiscard]] std::optional<std::uint32_t> generation_of(const std::string& characterId) const;
    /** Removes the state of a character that left the world (its entity is gone). */
    void forget(const std::string& characterId);
    /** @return The number of the targets that a strike of a visit hit. */
    [[nodiscard]] std::size_t strike_hits(const std::string& characterId, std::uint32_t visit, std::uint32_t strike) const;
    /**
     * @return True when the last step of the character permitted a dodge (stage 3.4f-B): the dodge input at rest, in a cancel window of
     *         the fragment that plays, or at the end of a buffered dodge window. The value is cleared.
     */
    bool take_dodge(const std::string& characterId);
    /** Counts a payment of a fragment: 0 stamina, 1 mana, 2 a payment with a condition that the world does not model. */
    void count_payment(int kind);
    /** Counts an item of a fragment that waits for its due time (Combat::step). */
    void count_delayed() {
        ++m_counts.delayedEvents;
    }

    [[nodiscard]] const State* state_of(const std::string& characterId) const;
    /**
     * @return True when the character has the visit and no end of the sequence interrupted it (a death, a travel, a change of the
     *         weapon, a dodge, a new session): a fragment event that waited for its time still belongs to an attack that plays
     *         (finding M2-R1). A visit that ended at the end of its fragment or at a transition stays valid.
     */
    [[nodiscard]] bool live_visit(const std::string& characterId, std::uint32_t visit) const;
    [[nodiscard]] const SequenceCounts& counts() const {
        return m_counts;
    }
    /** @return The number of characters with a state (the test of the load). */
    [[nodiscard]] std::size_t size() const {
        return m_states.size();
    }

private:
    void reset(State& state, SequenceEnd why, std::uint16_t input);
    void start_visit(State& state, std::uint32_t fragment, std::int32_t alternate, std::uint16_t input, WorldClock::time_point now,
                     WorldClock::time_point due, std::uint64_t cast = 0);
    void advance(State& state, std::uint16_t input, const AttackBits& bits, WorldClock::time_point now, std::vector<SequenceEvent>& events,
                 const AttackFacts& facts);
    /** @return True when the condition of a transition holds at a step (format 2: the terms with the facts, the hits of the cast). */
    [[nodiscard]] bool holds(const State& state, const AttackVisit& visit, const gamedata::AttackTransition& transition, const AttackBits& bits,
                             bool instant, const AttackFacts& facts) const;
    /** @return The state of a character for a step of a session (made, or reset for a new session, input sequence, or weapon). */
    State* state_for(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                     const std::string& weapon, const gamedata::WeaponActionSet* actions);
    /**
     * @return True when a strike of a fragment fits a contact: the strike of its time window (the window that holds the step of the
     *         contact; else the nearest window in the tolerance; at a tie the strike that started last). The ledger does not choose
     *         the strike (code review of stage 3.3b-M2, finding M2-R2): a strike that hit the target already is a repeat, and the
     *         contact does not become a hit of the next strike.
     */
    [[nodiscard]] bool strike_fits(const State& state, const AttackVisit& visit, std::uint32_t fragment, std::uint16_t input,
                                   WorldClock::time_point now, const std::function<bool(const gamedata::AttackStrike&)>& usable,
                                   std::uint32_t& strike) const;

    std::unordered_map<std::string, State> m_states;
    SequenceCounts m_counts;
};

} // namespace soulwarden::backend::world

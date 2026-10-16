#include "world/attack_sequence.h"

#include <algorithm>
#include <cmath>

namespace soulwarden::backend::world {
namespace {

using attack_rules::kClockEpsilon;

/** @return The steps from one input number to a later one, or a value above 0x7FFF for an earlier one (16 bits, with wrap). */
unsigned steps_between(std::uint16_t from, std::uint16_t to) {
    return static_cast<std::uint16_t>(to - from);
}

/** @return The clock of a visit (seconds) at an input step: (k - s + 1) / 30. */
float clock_of(const AttackVisit& visit, std::uint16_t input) {
    return static_cast<float>(steps_between(visit.start, input) + 1) / attack_rules::kStepsInSecond;
}

bool in_window(float t, float start, float end) {
    return t >= start - kClockEpsilon && (end < 0.0F || t < end - kClockEpsilon);
}

/** @return True when the input of a step fits the input of a transition (an instant one, or a buffered window that takes a press). */
bool fits(gamedata::AttackInput wanted, const AttackBits& bits, bool instant) {
    switch (wanted) {
    case gamedata::AttackInput::always:
        return true;
    case gamedata::AttackInput::virtualInput:
        // The hold counts at an instant transition (the repeat of a long hold), not in a buffered window (live check I1: the
        // second charged swing of the war hammer did not repeat when the button went up in its buffered window).
        return bits.press || (instant && bits.hold);
    case gamedata::AttackInput::press:
        return bits.press;
    case gamedata::AttackInput::notHeld:
        return !bits.press && !bits.hold;
    case gamedata::AttackInput::released:
        return bits.release;
    }
    return false;
}

bool takes_press(gamedata::AttackInput input) {
    return input == gamedata::AttackInput::virtualInput || input == gamedata::AttackInput::press;
}

/** @return The target of the first transition of a fragment with this input whose window has the clock, or -1. */
std::int32_t follow(const gamedata::AttackFragment& fragment, gamedata::AttackInput input, float t) {
    for (const gamedata::AttackTransition& transition : fragment.transitions) {
        if (transition.input == input && in_window(t, transition.start, transition.end)) {
            return static_cast<std::int32_t>(transition.to);
        }
    }
    return -1;
}

/** @return The target of an instant transition of a fragment that a press takes at the clock (or after the clock), or -1. */
std::int32_t press_target(const gamedata::AttackFragment& fragment, float t) {
    for (const gamedata::AttackTransition& transition : fragment.transitions) {
        if (transition.kind == gamedata::TransitionKind::instant && takes_press(transition.input)
            && (transition.end < 0.0F || t < transition.end - kClockEpsilon)) {
            return static_cast<std::int32_t>(transition.to);
        }
    }
    return -1;
}

} // namespace

const char* sequence_end_text(SequenceEnd end) {
    switch (end) {
    case SequenceEnd::session:
        return "a different session";
    case SequenceEnd::restart:
        return "the input numbers of the client started again";
    case SequenceEnd::weapon:
        return "a change of the weapon";
    case SequenceEnd::ability:
        return "an accepted ability";
    case SequenceEnd::dodge:
        return "a dodge";
    case SequenceEnd::death:
        return "the death of the character";
    case SequenceEnd::travel:
        return "a travel";
    case SequenceEnd::command:
        return "a command";
    }
    return "unknown";
}

void AttackSequences::reset(State& state, SequenceEnd why, std::uint16_t input) {
    if (!state.visits.empty() && !state.visits.back().ended) {
        state.visits.back().ended = true;
        state.visits.back().interrupted = true;
        state.visits.back().endInput = input;
    }
    if (why == SequenceEnd::session || why == SequenceEnd::restart) {
        // The input numbers of the old sequence cannot be compared with the new ones: no contact of it fits any more.
        state.visits.clear();
        state.hits.clear();
    }
    ++state.generation;
    ++m_counts.ends[static_cast<std::size_t>(why)];
}

void AttackSequences::start_visit(State& state, std::uint32_t fragment, std::int32_t alternate, std::uint16_t input,
                                  WorldClock::time_point now) {
    if (!state.visits.empty() && !state.visits.back().ended) {
        state.visits.back().ended = true;
        state.visits.back().endInput = input;
    }
    AttackVisit visit;
    visit.number = ++state.visitCount;
    visit.fragment = fragment;
    visit.alternate = alternate == static_cast<std::int32_t>(fragment) ? -1 : alternate;
    visit.start = input;
    visit.startedAt = now;
    state.visits.push_back(visit);
    ++m_counts.visits;
    m_counts.alternates += visit.alternate >= 0 ? 1 : 0;
    // The ledger keeps only the hits of the visits of the history: its size has a limit.
    while (state.visits.size() > attack_rules::kVisitHistory) {
        const std::uint32_t gone = state.visits.front().number;
        state.visits.pop_front();
        for (auto hit = state.hits.begin(); hit != state.hits.end();) {
            hit = std::get<0>(*hit) == gone ? state.hits.erase(hit) : std::next(hit);
        }
    }
}

void AttackSequences::advance(State& state, std::uint16_t input, const AttackBits& bits, WorldClock::time_point now,
                              std::vector<const gamedata::AttackPayment*>& payments) {
    const gamedata::WeaponActionSet& actions = *state.actions;
    // A press starts one new attack at most (from rest, or in the tail of a fragment); the conditions of the transitions still
    // see the input of the step.
    bool pressUsed = false;
    for (int loop = 0; loop < attack_rules::kTransitionsInStep; ++loop) {
        AttackVisit* visit = !state.visits.empty() && !state.visits.back().ended ? &state.visits.back() : nullptr;
        if (visit == nullptr) {
            if (!bits.press || pressUsed) {
                return;
            }
            pressUsed = true;
            // From rest: the first fragment of the graph. A press just after the end of a fragment can be the next step of its
            // chain (the band of the length): the visit gets that fragment as its other one.
            std::int32_t alternate = -1;
            if (!state.visits.empty() && !state.visits.back().interrupted) {
                const AttackVisit& before = state.visits.back();
                const gamedata::AttackFragment& last = actions.fragments[before.fragment];
                const float t = clock_of(before, input);
                if (last.length > 0.0F && std::fabs(t - last.length) < attack_rules::kLengthBand) {
                    alternate = press_target(last, t);
                }
            }
            start_visit(state, actions.start, alternate, input, now);
            continue;
        }
        const gamedata::AttackFragment& fragment = actions.fragments[visit->fragment];
        const float t = clock_of(*visit, input);
        while (visit->paid < fragment.payments.size() && fragment.payments[visit->paid].time <= t + kClockEpsilon) {
            payments.push_back(&fragment.payments[visit->paid]);
            ++visit->paid;
        }
        // The end of the animation of the fragment: the character is at rest (a press of this step starts a new attack).
        if (fragment.length > 0.0F && t > fragment.length + kClockEpsilon && visit->pending < 0) {
            visit->ended = true;
            visit->endInput = input;
            continue;
        }
        // A dodge: at once in a cancel window, at the end of a buffer window.
        if (visit->dodgeAt >= 0.0F && t >= visit->dodgeAt - kClockEpsilon) {
            reset(state, SequenceEnd::dodge, input);
            return;
        }
        if (bits.dodge) {
            for (const gamedata::DodgeWindow& window : fragment.dodge) {
                if (!in_window(t, window.start, window.end)) {
                    continue;
                }
                // A dodge in a cancel window that starts with the fragment is no end: the client plays no dodge there (live check of
                // stage 3.3b-M1: left Shift at 0.03 s to 0.15 s of the first swing of the great axe, its window CanDodgeCancel 0 to
                // 0.167 s, three tries: the swing went on to its end; at 0.6 s the dodge started at once, and at 0.35 s, in the window
                // CanDodgeBuffer to 0.533 s, at 0.567 s).
                if (!window.buffered && window.start <= kClockEpsilon) {
                    break;
                }
                if (!window.buffered || window.end < 0.0F) {
                    reset(state, SequenceEnd::dodge, input);
                    return;
                }
                if (visit->dodgeAt < 0.0F) {
                    visit->dodgeAt = window.end;
                }
                break;
            }
        }
        // A buffered transition that took a press: its fragment starts at the end of its window.
        if (visit->pending >= 0 && t >= visit->pendingAt - kClockEpsilon) {
            const auto next = static_cast<std::uint32_t>(visit->pending);
            std::int32_t alternate = -1;
            if (fragment.length > 0.0F && std::fabs(t - fragment.length) < attack_rules::kLengthBand && next != actions.start) {
                alternate = static_cast<std::int32_t>(actions.start);
            }
            start_visit(state, next, alternate, input, now);
            continue;
        }
        const gamedata::AttackTransition* taken = nullptr;
        bool laterWindow = false;
        for (const gamedata::AttackTransition& transition : fragment.transitions) {
            if (takes_press(transition.input) && (transition.end < 0.0F || transition.end > t + kClockEpsilon)) {
                laterWindow = true;
            }
            if (!in_window(t, transition.start, transition.end)) {
                continue;
            }
            if (transition.kind == gamedata::TransitionKind::instant && fits(transition.input, bits, true)) {
                taken = &transition;
                break;
            }
            if (transition.kind == gamedata::TransitionKind::buffered && visit->pending < 0 && fits(transition.input, bits, false)) {
                visit->pending = static_cast<std::int32_t>(transition.to);
                visit->pendingAt = transition.end;
            }
        }
        if (taken != nullptr) {
            std::int32_t alternate = -1;
            if (takes_press(taken->input)) {
                // A press near the end of the fragment: the client can be at rest already (a new attack).
                if (fragment.length > 0.0F && std::fabs(t - fragment.length) < attack_rules::kLengthBand && taken->to != actions.start) {
                    alternate = static_cast<std::int32_t>(actions.start);
                }
            } else if (visit->alternate >= 0) {
                // The same transition of the other fragment of the visit (the charge of the other branch).
                alternate = follow(actions.fragments[static_cast<std::size_t>(visit->alternate)], taken->input, t);
            }
            pressUsed = pressUsed || takes_press(taken->input);
            start_visit(state, taken->to, alternate, input, now);
            continue;
        }
        // A press that no window of the fragment takes, now or later: the tail of the fragment; a new attack from rest.
        if (bits.press && !pressUsed && !laterWindow && visit->pending < 0) {
            visit->ended = true;
            visit->endInput = input;
            continue;
        }
        return;
    }
}

std::vector<const gamedata::AttackPayment*> AttackSequences::step(const std::string& characterId, SessionId session,
                                                                  std::uint32_t inputSequence, std::uint16_t input,
                                                                  const AttackBits& bits, const std::string& weapon,
                                                                  const gamedata::WeaponActionSet* actions, WorldClock::time_point now) {
    std::vector<const gamedata::AttackPayment*> payments;
    auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        if (actions == nullptr) {
            return payments;
        }
        found = m_states.emplace(characterId, State{}).first;
        found->second.session = session;
        found->second.inputSequence = inputSequence;
    }
    State& state = found->second;
    if (state.session != session) {
        reset(state, SequenceEnd::session, input);
        state.session = session;
        state.inputSequence = inputSequence;
        state.hasInput = false;
    } else if (state.inputSequence != inputSequence) {
        reset(state, SequenceEnd::restart, input);
        state.inputSequence = inputSequence;
        state.hasInput = false;
    }
    if (state.weapon != weapon || state.actions != actions) {
        if (!state.visits.empty() && !state.visits.back().ended) {
            reset(state, SequenceEnd::weapon, input);
        }
        // The visits name fragments of the graph of the old weapon: they go (no contact of the old weapon fits the new one).
        state.visits.clear();
        state.hits.clear();
        state.weapon = weapon;
        state.actions = actions;
    }
    state.hasInput = true;
    state.lastInput = input;
    if (state.actions == nullptr) {
        return payments;
    }
    advance(state, input, bits, now, payments);
    return payments;
}

bool AttackSequences::strike_fits(const State& state, const AttackVisit& visit, std::uint32_t fragment, std::uint16_t input,
                                  WorldClock::time_point now, const std::function<bool(const gamedata::AttackStrike&)>& usable,
                                  std::uint32_t& strike) const {
    const gamedata::AttackFragment& data = state.actions->fragments[fragment];
    const float t = clock_of(visit, input);
    const float early = attack_rules::kEarlySteps / attack_rules::kStepsInSecond;
    const float late = attack_rules::kLateSteps / attack_rules::kStepsInSecond;
    for (std::uint32_t index = 0; index < data.strikes.size(); ++index) {
        const gamedata::AttackStrike& candidate = data.strikes[index];
        if (t < candidate.start - early - kClockEpsilon || t > candidate.end + late + kClockEpsilon || !usable(candidate)) {
            continue;
        }
        // The world clock: a client that sends its input numbers faster than 30 in a second does not reach a later strike before its
        // time, and an old input number does not reach a strike long after its time.
        const auto earliest = visit.startedAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(candidate.start - early))
                              - attack_rules::kWorldEarly;
        const auto latest = visit.startedAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(candidate.end + late))
                            + std::chrono::milliseconds(500);
        if (now < earliest || now > latest) {
            continue;
        }
        strike = index;
        return true;
    }
    return false;
}

StrikeMatch AttackSequences::match(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                   EntityId target, WorldClock::time_point now,
                                   const std::function<bool(const gamedata::AttackStrike&)>& usable) {
    StrikeMatch result;
    const auto found = m_states.find(characterId);
    if (found == m_states.end() || found->second.actions == nullptr || found->second.session != session
        || found->second.inputSequence != inputSequence) {
        return result;
    }
    State& state = found->second;
    for (auto visit = state.visits.rbegin(); visit != state.visits.rend(); ++visit) {
        if (steps_between(visit->start, input) > 0x7FFF) {
            continue; // The contact is before the visit.
        }
        // A visit that a dodge, a weapon change, or an ability interrupted has no contact at or after its end.
        if (visit->interrupted && steps_between(visit->endInput, input) <= 0x7FFF) {
            continue;
        }
        result.verdict = StrikeMatch::Verdict::noStrike;
        std::uint32_t strike = 0;
        bool alternate = false;
        if (!strike_fits(state, *visit, visit->fragment, input, now, usable, strike)) {
            if (visit->alternate < 0 || !strike_fits(state, *visit, static_cast<std::uint32_t>(visit->alternate), input, now, usable, strike)) {
                continue;
            }
            alternate = true;
        }
        if (alternate) {
            // The contact fits only the other fragment: the client played it (the band at the end of a fragment).
            const auto other = static_cast<std::uint32_t>(visit->alternate);
            visit->alternate = static_cast<std::int32_t>(visit->fragment);
            visit->fragment = other;
        }
        result.visit = visit->number;
        result.fragment = &state.actions->fragments[visit->fragment];
        result.strike = strike;
        result.strikeData = &result.fragment->strikes[strike];
        result.verdict = state.hits.count({visit->number, strike, target}) != 0 ? StrikeMatch::Verdict::sameStrike : StrikeMatch::Verdict::hit;
        return result;
    }
    return result;
}

void AttackSequences::note_hit(const std::string& characterId, std::uint32_t visit, std::uint32_t strike, EntityId target) {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return;
    }
    found->second.hits.insert({visit, strike, target});
    ++m_counts.strikes;
}

void AttackSequences::end(const std::string& characterId, SequenceEnd why) {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return;
    }
    if (!found->second.visits.empty() && !found->second.visits.back().ended) {
        reset(found->second, why, found->second.lastInput);
    }
}

void AttackSequences::forget(const std::string& characterId) {
    m_states.erase(characterId);
}

std::size_t AttackSequences::strike_hits(const std::string& characterId, std::uint32_t visit, std::uint32_t strike) const {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& [hitVisit, hitStrike, target] : found->second.hits) {
        count += hitVisit == visit && hitStrike == strike ? 1 : 0;
    }
    return count;
}

void AttackSequences::count_payment(int kind) {
    if (kind == 0) {
        ++m_counts.staminaPayments;
    } else if (kind == 1) {
        ++m_counts.manaPayments;
    } else {
        ++m_counts.paymentsWithCondition;
    }
}

const AttackSequences::State* AttackSequences::state_of(const std::string& characterId) const {
    const auto found = m_states.find(characterId);
    return found == m_states.end() ? nullptr : &found->second;
}

} // namespace soulwarden::backend::world

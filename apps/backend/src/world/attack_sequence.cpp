#include "world/attack_sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace soulwarden::backend::world {
namespace {

using attack_rules::kClockEpsilon;

/** @return The steps from one input number to a later one, or a value above 0x7FFF for an earlier one (16 bits, with wrap). */
unsigned steps_between(std::uint16_t from, std::uint16_t to) {
    return static_cast<std::uint16_t>(to - from);
}

/** @return The time of a number of input steps of the client on the world clock (1/30 s each). */
std::chrono::steady_clock::duration steps_time(unsigned steps) {
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(static_cast<double>(steps) / attack_rules::kStepsInSecond));
}

/** @return A time of a fragment (seconds) as a duration of the world clock. */
std::chrono::steady_clock::duration seconds_time(float seconds) {
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::max(seconds, 0.0F)));
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
        // A step with neither the attack input nor its hold is a release too (code review of stage 3.3b-M1, finding M1-R2): a client
        // that stops its hold with no release bit makes the partial heavy attack, not a full charge.
        return bits.release || (!bits.press && !bits.hold);
    case gamedata::AttackInput::ability:
    case gamedata::AttackInput::terms:
        return false; // The start of an ability is a press (start_ability); the terms are a condition of the data (holds).
    }
    return false;
}

/** @return The input step at which the clock of a visit that started at `start` has this value (the step k with (k - s + 1) / 30 = clock). */
std::uint16_t step_at(std::uint16_t start, float clock) {
    const long steps = std::max(std::lround(clock * attack_rules::kStepsInSecond) - 1L, 0L);
    return static_cast<std::uint16_t>(start + static_cast<unsigned>(steps));
}

bool takes_press(gamedata::AttackInput input) {
    return input == gamedata::AttackInput::virtualInput || input == gamedata::AttackInput::press;
}

/** @return True for a fragment of a charge: it has a transition at the release (Attack_Start_Charge of each weapon). */
bool charges(const gamedata::AttackFragment& fragment) {
    return std::any_of(fragment.transitions.begin(), fragment.transitions.end(),
                       [](const gamedata::AttackTransition& transition) { return transition.input == gamedata::AttackInput::released; });
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

/** Adds an item of a fragment with the count of the ends of the sequence now (code review of stage 3.3b-M2b, finding M2b-R2). */
void push_event(const AttackSequences::State& state, std::vector<SequenceEvent>& events, SequenceEvent event) {
    event.generation = state.generation;
    events.push_back(event);
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
    if (why == SequenceEnd::dodge) {
        state.dodged = true;
    }
    state.pendingAbility = -1;
    state.pendingCast = 0;
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
                                  WorldClock::time_point now, WorldClock::time_point due, std::uint64_t cast) {
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
    visit.dueAt = due;
    // The fragments of one ability keep its cast (a transition inside the ability: the exit of Whirlwind, the uppercut of Charge).
    if (cast == 0 && !state.visits.empty() && state.visits.back().cast != 0 && state.actions != nullptr
        && fragment < state.actions->fragments.size() && state.actions->fragments[fragment].name.rfind("Attack_", 0) != 0) {
        cast = state.visits.back().cast;
    }
    visit.cast = cast;
    if (cast != 0 && state.hitsCast != cast) {
        state.hitsCast = cast;
        state.castHits = 0;
    }
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

bool AttackSequences::holds(const State& state, const AttackVisit& visit, const gamedata::AttackTransition& transition, const AttackBits& bits,
                            bool instant, const AttackFacts& facts) const {
    if (transition.input != gamedata::AttackInput::terms) {
        return fits(transition.input, bits, instant);
    }
    const std::uint32_t hits = visit.cast != 0 && state.hitsCast == visit.cast ? state.castHits : 0;
    for (const std::vector<gamedata::ConditionLiteral>& term : transition.terms) {
        bool all = true;
        for (const gamedata::ConditionLiteral& literal : term) {
            bool value = false;
            switch (literal.kind) {
            case gamedata::LiteralKind::node:
                value = facts.learned && facts.learned(literal.id);
                break;
            case gamedata::LiteralKind::perk:
                value = facts.perk && facts.perk(literal.id);
                break;
            case gamedata::LiteralKind::attack:
                value = bits.press || (instant && bits.hold);
                break;
            case gamedata::LiteralKind::block:
                value = bits.block;
                break;
            case gamedata::LiteralKind::collided:
                value = false; // The server does not see a collision of the client: a contact shows it (match).
                break;
            case gamedata::LiteralKind::hits:
                value = hits < literal.count;
                break;
            case gamedata::LiteralKind::playing:
                value = state.actions->fragments[visit.fragment].name == literal.text;
                break;
            }
            if (value == literal.negated) {
                all = false;
                break;
            }
        }
        if (all) {
            return true;
        }
    }
    return false;
}

void AttackSequences::advance(State& state, std::uint16_t input, const AttackBits& bits, WorldClock::time_point now,
                              std::vector<SequenceEvent>& events, const AttackFacts& facts) {
    const gamedata::WeaponActionSet& actions = *state.actions;
    // A press starts one new attack at most (from rest, or in the tail of a fragment); the conditions of the transitions still
    // see the input of the step.
    bool pressUsed = false;
    for (int loop = 0; loop < attack_rules::kTransitionsInStep; ++loop) {
        // A press of an ability that waited for its step (start_ability): its fragment starts, with its cast (stage 3.3b-M2).
        if (state.pendingAbility >= 0 && steps_between(state.pendingInput, input) <= 0x7FFF) {
            const auto fragment = static_cast<std::uint32_t>(state.pendingAbility);
            const std::uint16_t at = state.pendingInput;
            const std::uint64_t cast = state.pendingCast;
            const WorldClock::time_point due = state.pendingDue;
            state.pendingAbility = -1;
            state.pendingCast = 0;
            start_visit(state, fragment, -1, at, now, due, cast);
            push_event(state, events, SequenceEvent{SequenceEvent::Kind::start, cast, state.visits.back().number, &actions.fragments[fragment], 0, due});
            continue;
        }
        AttackVisit* visit = !state.visits.empty() && !state.visits.back().ended ? &state.visits.back() : nullptr;
        if (visit == nullptr) {
            // A dodge input at rest (stage 3.4f-B): no fragment of an attack decides it.
            if (bits.dodge) {
                state.dodged = true;
            }
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
            // The press starts the clock of the attack: each later fragment is due 1/30 s after it for each input step (finding M1-R1).
            start_visit(state, actions.start, alternate, input, now, now);
            continue;
        }
        const gamedata::AttackFragment& fragment = actions.fragments[visit->fragment];
        const float t = clock_of(*visit, input);
        // Each item has its due time on the world clock: the due time of the visit and the time of the item (finding M2-R1).
        const auto due = [visit](float time) { return visit->dueAt + seconds_time(time); };
        while (visit->paid < fragment.payments.size() && fragment.payments[visit->paid].time <= t + kClockEpsilon) {
            push_event(state, events, SequenceEvent{SequenceEvent::Kind::payment, visit->cast, visit->number, &fragment,
                                           static_cast<std::uint32_t>(visit->paid), due(fragment.payments[visit->paid].time)});
            ++visit->paid;
        }
        // The strikes, the spell casts, and the marks of the fragment of an ability that the clock passed (stage 3.3b-M2).
        if (visit->cast != 0) {
            for (std::uint32_t index = 0; index < fragment.strikes.size() && index < 64; ++index) {
                if ((visit->strikesPassed & (1ULL << index)) == 0 && fragment.strikes[index].start <= t + kClockEpsilon) {
                    visit->strikesPassed |= 1ULL << index;
                    push_event(state, events, SequenceEvent{SequenceEvent::Kind::strike, visit->cast, visit->number, &fragment, index,
                                                   due(fragment.strikes[index].start)});
                }
            }
            for (std::uint32_t index = 0; index < fragment.casts.size() && index < 32; ++index) {
                if ((visit->castsPassed & (1U << index)) == 0 && fragment.casts[index].time <= t + kClockEpsilon) {
                    visit->castsPassed |= 1U << index;
                    push_event(state, events, SequenceEvent{SequenceEvent::Kind::cast, visit->cast, visit->number, &fragment, index,
                                                   due(fragment.casts[index].time)});
                }
            }
            for (std::uint32_t index = 0; index < fragment.markers.size() && index < 32; ++index) {
                if ((visit->markersPassed & (1U << index)) == 0 && fragment.markers[index].time <= t + kClockEpsilon) {
                    visit->markersPassed |= 1U << index;
                    push_event(state, events, SequenceEvent{SequenceEvent::Kind::marker, visit->cast, visit->number, &fragment, index,
                                                   due(fragment.markers[index].time)});
                }
            }
        }
        // The end of the animation of the fragment: the character is at rest (a press of this step starts a new attack).
        if (fragment.length > 0.0F && t > fragment.length + kClockEpsilon && visit->pending < 0) {
            visit->ended = true;
            visit->endInput = input;
            continue;
        }
        // A dodge: at once in a cancel window, at the end of a buffer window. The stamina system must be able to start it at that time
        // (finding CS-R2): a refused dodge leaves the attack as it is.
        if (visit->dodgeAt >= 0.0F && t >= visit->dodgeAt - kClockEpsilon) {
            if (bits.dodgeAllowed) {
                reset(state, SequenceEnd::dodge, input);
                return;
            }
            visit->dodgeAt = -1.0F;
        }
        if (bits.dodge && bits.dodgeAllowed) {
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
        // A buffered transition that took a press: its fragment starts at the end of its window, at its due input step also when the
        // step of the report came later (code review of stage 3.3b-M1, finding M1-R4: lost reports do not move the clock of the next
        // fragment). The due step k has the clock (k - s + 1) / 30 = the end of the window.
        if (visit->pending >= 0 && t >= visit->pendingAt - kClockEpsilon) {
            const auto next = static_cast<std::uint32_t>(visit->pending);
            const long dueSteps = std::max(std::lround(visit->pendingAt * attack_rules::kStepsInSecond) - 1L, 0L);
            const unsigned atSteps = std::min(static_cast<unsigned>(dueSteps), steps_between(visit->start, input));
            const auto due = static_cast<std::uint16_t>(visit->start + atSteps);
            const float dueClock = clock_of(*visit, due);
            std::int32_t alternate = -1;
            if (fragment.length > 0.0F && std::fabs(dueClock - fragment.length) < attack_rules::kLengthBand && next != actions.start) {
                alternate = static_cast<std::int32_t>(actions.start);
            }
            start_visit(state, next, alternate, due, now, visit->dueAt + steps_time(atSteps));
            continue;
        }
        const gamedata::AttackTransition* taken = nullptr;
        bool laterWindow = false;
        for (const gamedata::AttackTransition& transition : fragment.transitions) {
            // The start of an ability is a press of its key (start_ability), not an input of a step.
            if (transition.input == gamedata::AttackInput::ability) {
                continue;
            }
            if (takes_press(transition.input) && (transition.end < 0.0F || transition.end > t + kClockEpsilon)) {
                laterWindow = true;
            }
            if (!in_window(t, transition.start, transition.end)) {
                continue;
            }
            // The full charge of a charge fragment needs the hold at its step (finding M1-R2): a step with no hold took the release
            // transition before (fits), and a charge with no hold at its end is no full charge.
            if (transition.input == gamedata::AttackInput::always && charges(fragment) && !bits.press && !bits.hold) {
                continue;
            }
            if (transition.kind == gamedata::TransitionKind::instant && holds(state, *visit, transition, bits, true, facts)) {
                taken = &transition;
                break;
            }
            if (transition.kind == gamedata::TransitionKind::buffered && visit->pending < 0 && holds(state, *visit, transition, bits, false, facts)) {
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
            } else if (visit->alternate >= 0 && taken->input != gamedata::AttackInput::terms) {
                // The same transition of the other fragment of the visit (the charge of the other branch).
                alternate = follow(actions.fragments[static_cast<std::size_t>(visit->alternate)], taken->input, t);
            }
            pressUsed = pressUsed || takes_press(taken->input);
            start_visit(state, taken->to, alternate, input, now, visit->dueAt + steps_time(steps_between(visit->start, input)));
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

AttackSequences::State* AttackSequences::state_for(const std::string& characterId, SessionId session, std::uint32_t inputSequence,
                                                   std::uint16_t input, const std::string& weapon, const gamedata::WeaponActionSet* actions) {
    auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        if (actions == nullptr) {
            return nullptr;
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
        state.pendingAbility = -1;
        state.weapon = weapon;
        state.actions = actions;
    }
    return &state;
}

std::vector<SequenceEvent> AttackSequences::step(const std::string& characterId, SessionId session, std::uint32_t inputSequence,
                                                 std::uint16_t input, const AttackBits& bits, const std::string& weapon,
                                                 const gamedata::WeaponActionSet* actions, WorldClock::time_point now, const AttackFacts& facts) {
    std::vector<SequenceEvent> events;
    State* found = state_for(characterId, session, inputSequence, input, weapon, actions);
    if (found == nullptr) {
        return events;
    }
    State& state = *found;
    const bool hadInput = state.hasInput;
    const std::uint16_t lastInput = state.lastInput;
    state.hasInput = true;
    state.lastInput = input;
    if (state.actions == nullptr) {
        return events;
    }
    // The steps of a gap (reports that did not come): each one with the last known hold and no press, release, or dodge (code review of
    // stage 3.3b-M1, finding M1-R4). A missing entry gets no invented press.
    if (hadInput) {
        const unsigned gap = steps_between(lastInput, input);
        if (gap > 1 && gap <= attack_rules::kGapFill) {
            AttackBits filled;
            filled.hold = state.holding;
            for (unsigned missing = 1; missing < gap; ++missing) {
                advance(state, static_cast<std::uint16_t>(lastInput + missing), filled, now, events, facts);
            }
        }
    }
    advance(state, input, bits, now, events, facts);
    state.holding = (bits.press || bits.hold) && !bits.release;
    return events;
}

bool AttackSequences::take_dodge(const std::string& characterId) {
    const auto found = m_states.find(characterId);
    if (found == m_states.end() || !found->second.dodged) {
        return false;
    }
    found->second.dodged = false;
    return true;
}

AbilityStart AttackSequences::start_ability(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                            const std::string& weapon, const gamedata::WeaponActionSet* actions, std::uint32_t ability,
                                            std::uint32_t fragment, std::uint64_t cast, WorldClock::time_point now, bool dryRun) {
    AbilityStart result;
    result.input = input;
    if (actions == nullptr || fragment >= actions->fragments.size()) {
        return result;
    }
    // The state as a step of this session would have it: a different session, input sequence, or weapon is a state at rest.
    const auto existing = m_states.find(characterId);
    const State* current = existing != m_states.end() && existing->second.session == session && existing->second.inputSequence == inputSequence
                                   && existing->second.weapon == weapon && existing->second.actions == actions
                               ? &existing->second
                               : nullptr;
    const AttackVisit* playing = current != nullptr && !current->visits.empty() && !current->visits.back().ended ? &current->visits.back() : nullptr;
    std::optional<std::uint16_t> at;
    if (playing == nullptr) {
        at = input;
    } else {
        // The step at which the action of the ability starts in the fragment that plays (the action lists of the player: the windows
        // CanAttack_<ability>, the mark CanTransition, the end of the fragment), from the step of the press only (user test 2, UT2-1). The
        // conditions CanAttack_<ability> of the great axe and the war hammer read the press of the input step with no BackTicks (client
        // code 0x2BEBDA0, 0x2A22F90): an instant window that holds the press step starts the ability at the press, a buffered window that
        // holds it starts the ability at its end (the clip latches the press: 0x257D2A0, 0x257D5B0; live check of user test 2: a press at
        // the last step of a buffered window played at the end of the window, 6 of 6 tries), and the mark CanTransition and the end of the
        // fragment start it at the press when the press step is at or after them. A press before a window, the mark, or the end does not
        // play (live check of user test 2: Wrecking Ball at 0.4 s of Shockwave, whose window starts at 0.8 s; before the correction the
        // server started it 1033 ms later and ended the fragment that the client played).
        const gamedata::AttackFragment& data = actions->fragments[playing->fragment];
        const float t = clock_of(*playing, input);
        const auto consider = [&](std::uint16_t step) {
            if (!at || steps_between(step, *at) <= 0x7FFF) {
                at = step;
            }
        };
        for (const gamedata::AttackTransition& transition : data.transitions) {
            if (transition.input != gamedata::AttackInput::ability || transition.abilityId != ability
                || !in_window(t, transition.start, transition.end)) {
                continue;
            }
            if (transition.kind == gamedata::TransitionKind::buffered && transition.end >= 0.0F) {
                consider(step_at(playing->start, transition.end)); // The press waits to the end of the window.
            } else {
                consider(input);
            }
        }
        if (data.canTransition >= 0.0F && t >= data.canTransition - kClockEpsilon) {
            consider(input);
        }
        if (data.length > 0.0F && t > data.length + kClockEpsilon) {
            consider(input);
        }
    }
    if (!at) {
        result.verdict = AbilityStart::Verdict::refused;
        return result;
    }
    result.input = *at;
    result.delaySteps = steps_between(input, *at);
    result.verdict = result.delaySteps == 0 ? AbilityStart::Verdict::now : AbilityStart::Verdict::later;
    if (dryRun) {
        return result;
    }
    State* state = state_for(characterId, session, inputSequence, input, weapon, actions);
    if (state == nullptr) {
        return result;
    }
    // The due time of the fragment: on the clock of the visit that plays (finding M1-R1), else the press.
    const WorldClock::time_point due = playing != nullptr ? playing->dueAt + steps_time(steps_between(playing->start, *at)) : now;
    if (result.delaySteps == 0) {
        state->pendingAbility = -1;
        if (!state->visits.empty() && !state->visits.back().ended) {
            // The ability ends the fragment that plays (as a transition of it: its contacts before the start stay).
            state->visits.back().ended = true;
            state->visits.back().endInput = input;
            ++m_counts.ends[static_cast<std::size_t>(SequenceEnd::ability)];
        }
        start_visit(*state, fragment, -1, input, now, due, cast);
        state->hasInput = true;
        state->lastInput = input;
        result.visit = state->visits.back().number;
        result.due = due;
        result.generation = state->generation;
        return result;
    }
    state->pendingAbility = static_cast<std::int32_t>(fragment);
    state->pendingCast = cast;
    state->pendingInput = *at;
    state->pendingDue = due;
    return result;
}

bool AttackSequences::strike_fits(const State& state, const AttackVisit& visit, std::uint32_t fragment, std::uint16_t input,
                                  WorldClock::time_point now, const std::function<bool(const gamedata::AttackStrike&)>& usable,
                                  std::uint32_t& strike) const {
    const gamedata::AttackFragment& data = state.actions->fragments[fragment];
    const float t = clock_of(visit, input);
    const float early = attack_rules::kEarlySteps / attack_rules::kStepsInSecond;
    const float late = attack_rules::kLateSteps / attack_rules::kStepsInSecond;
    // Two strikes can fit one step (the late tolerance of a spin of Whirlwind and the start of the next spin; stage 3.3b-M2): the strike
    // of the time window of the step, else the nearest window; at a tie the strike that started last (finding M2-R2: the ledger does not
    // choose, thus a second contact of one step is a repeat of the same strike).
    std::optional<std::uint32_t> best;
    float bestDistance = 0.0F;
    float bestStart = 0.0F;
    for (std::uint32_t index = 0; index < data.strikes.size(); ++index) {
        const gamedata::AttackStrike& candidate = data.strikes[index];
        if (t < candidate.start - early - kClockEpsilon || t > candidate.end + late + kClockEpsilon || !usable(candidate)) {
            continue;
        }
        // The world clock: a client that sends its input numbers faster than 30 in a second does not reach a later strike before its
        // time (the due time of the visit: the press of the attack and 1/30 s for each step since it, finding M1-R1), and an old input
        // number does not reach a strike long after its time.
        const auto earliest = visit.dueAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(candidate.start - early))
                              - attack_rules::kWorldEarly;
        const auto latest = visit.startedAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(candidate.end + late))
                            + std::chrono::milliseconds(500);
        if (now < earliest || now > latest) {
            continue;
        }
        const float distance = t < candidate.start ? candidate.start - t : (t > candidate.end ? t - candidate.end : 0.0F);
        if (!best || distance < bestDistance - kClockEpsilon
            || (std::fabs(distance - bestDistance) <= kClockEpsilon && candidate.start > bestStart)) {
            best = index;
            bestDistance = distance;
            bestStart = candidate.start;
        }
    }
    if (!best) {
        return false;
    }
    strike = *best;
    return true;
}

StrikeMatch AttackSequences::match(const std::string& characterId, SessionId session, std::uint32_t inputSequence, std::uint16_t input,
                                   EntityId target, WorldClock::time_point now,
                                   const std::function<bool(const gamedata::AttackStrike&)>& usable, const AttackFacts& facts) {
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
        // A visit that a dodge, a weapon change, or an ability interrupted has no contact at or after its end. A visit that ended (its
        // length, or the start of the next fragment) has no contact more than the late tolerance after its end (code review of stage
        // 3.3b-M1, finding M1-R5).
        if (visit->interrupted && steps_between(visit->endInput, input) <= 0x7FFF) {
            continue;
        }
        if (visit->ended && steps_between(visit->endInput, input) <= 0x7FFF
            && static_cast<float>(steps_between(visit->endInput, input)) > attack_rules::kLateSteps) {
            continue;
        }
        result.verdict = StrikeMatch::Verdict::noStrike;
        std::uint32_t strike = 0;
        bool alternate = false;
        if (!strike_fits(state, *visit, visit->fragment, input, now, usable, strike)) {
            if (visit->alternate < 0
                || !strike_fits(state, *visit, static_cast<std::uint32_t>(visit->alternate), input, now, usable, strike)) {
                continue;
            }
            alternate = true;
        }
        // The contact fits only the other fragment: the client played it (the band at the end of a fragment). The visit takes it only
        // at the hit (note_hit, finding M1-R3).
        const std::uint32_t chosen = alternate ? static_cast<std::uint32_t>(visit->alternate) : visit->fragment;
        result.visit = visit->number;
        result.fragmentIndex = chosen;
        result.fragment = &state.actions->fragments[chosen];
        result.strike = strike;
        result.strikeData = &result.fragment->strikes[strike];
        result.cast = visit->cast;
        result.verdict = state.hits.count({visit->number, strike, target}) != 0 ? StrikeMatch::Verdict::sameStrike : StrikeMatch::Verdict::hit;
        return result;
    }
    // A transition of the ability that plays at a collision of the client (Charge: character_collided), which the server does not see
    // (stage 3.3b-M2): a contact that fits a strike of its next fragment, started at a step in the window of the transition, shows it.
    // The latest such step is the one of the client (the strike of the uppercut comes 1 step after its start).
    if (!state.visits.empty() && !state.visits.back().ended && state.visits.back().cast != 0
        && steps_between(state.visits.back().start, input) <= 0x7FFF) {
        const AttackVisit& visit = state.visits.back();
        const gamedata::AttackFragment& data = state.actions->fragments[visit.fragment];
        for (const gamedata::AttackTransition& transition : data.transitions) {
            if (transition.input != gamedata::AttackInput::terms) {
                continue;
            }
            // A term with the collision, whose other literals hold with no input of the step.
            bool collision = false;
            for (const std::vector<gamedata::ConditionLiteral>& term : transition.terms) {
                const bool has = std::any_of(term.begin(), term.end(), [](const gamedata::ConditionLiteral& literal) {
                    return literal.kind == gamedata::LiteralKind::collided && !literal.negated;
                });
                if (!has) {
                    continue;
                }
                gamedata::AttackTransition rest = transition;
                rest.terms = {{}};
                for (const gamedata::ConditionLiteral& literal : term) {
                    if (literal.kind != gamedata::LiteralKind::collided) {
                        rest.terms.front().push_back(literal);
                    }
                }
                collision = collision || holds(state, visit, rest, AttackBits{}, true, facts);
            }
            if (!collision) {
                continue;
            }
            const gamedata::AttackFragment& next = state.actions->fragments[transition.to];
            const unsigned back = std::min(steps_between(visit.start, input), 15U);
            for (unsigned offset = 0; offset <= back; ++offset) {
                const auto at = static_cast<std::uint16_t>(input - offset);
                if (!in_window(clock_of(visit, at), transition.start, transition.end)) {
                    continue;
                }
                AttackVisit trial;
                trial.fragment = transition.to;
                trial.start = at;
                trial.startedAt = now;
                trial.dueAt = visit.dueAt + steps_time(steps_between(visit.start, at));
                std::uint32_t strike = 0;
                if (!strike_fits(state, trial, transition.to, input, now, usable, strike)) {
                    continue;
                }
                result.verdict = StrikeMatch::Verdict::hit;
                result.visit = visit.number;
                result.fragmentIndex = transition.to;
                result.fragment = &next;
                result.strike = strike;
                result.strikeData = &next.strikes[strike];
                result.cast = visit.cast;
                result.lazyFragment = static_cast<std::int32_t>(transition.to);
                result.lazyStart = at;
                return result;
            }
        }
    }
    return result;
}

std::uint32_t AttackSequences::note_match(const std::string& characterId, const StrikeMatch& match, EntityId target, WorldClock::time_point now) {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return match.visit;
    }
    State& state = found->second;
    std::uint32_t visit = match.visit;
    if (match.lazyFragment >= 0 && !state.visits.empty() && state.visits.back().number == match.visit && !state.visits.back().ended) {
        // The transition at the collision: the next fragment starts at the step of the match, with the cast.
        const AttackVisit& before = state.visits.back();
        const WorldClock::time_point due = before.dueAt + steps_time(steps_between(before.start, match.lazyStart));
        const std::uint64_t cast = before.cast;
        start_visit(state, static_cast<std::uint32_t>(match.lazyFragment), -1, match.lazyStart, now, due, cast);
        visit = state.visits.back().number;
    }
    note_hit(characterId, visit, match.fragmentIndex, match.strike, target);
    return visit;
}

std::uint32_t AttackSequences::cast_hits(const std::string& characterId, std::uint64_t cast) const {
    const auto found = m_states.find(characterId);
    return found != m_states.end() && cast != 0 && found->second.hitsCast == cast ? found->second.castHits : 0;
}

bool AttackSequences::live_visit(const std::string& characterId, std::uint32_t visit) const {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return false;
    }
    return std::any_of(found->second.visits.begin(), found->second.visits.end(),
                       [visit](const AttackVisit& played) { return played.number == visit && !played.interrupted; });
}

const AttackVisit* AttackSequences::newest(const std::string& characterId) const {
    const auto found = m_states.find(characterId);
    return found == m_states.end() || found->second.visits.empty() ? nullptr : &found->second.visits.back();
}

void AttackSequences::note_hit(const std::string& characterId, std::uint32_t visit, std::uint32_t fragment, std::uint32_t strike,
                               EntityId target) {
    const auto found = m_states.find(characterId);
    if (found == m_states.end()) {
        return;
    }
    // The first hit commits the fragment of the visit (finding M1-R3): the other fragment of the band goes. A hit of a cast of an ability
    // counts for the condition IfAbilityNumHits (the exit of Whirlwind).
    for (AttackVisit& played : found->second.visits) {
        if (played.number == visit) {
            if (played.alternate >= 0 && static_cast<std::uint32_t>(played.alternate) == fragment) {
                played.fragment = fragment;
            }
            played.alternate = -1;
            if (played.cast != 0) {
                if (found->second.hitsCast != played.cast) {
                    found->second.hitsCast = played.cast;
                    found->second.castHits = 0;
                }
                ++found->second.castHits;
            }
            break;
        }
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
        return;
    }
    // At rest: no visit plays, but an item of a visit that ended at the end of its fragment can wait for its due time (the pull of Reap,
    // the ground of Mighty Gavel). The end cancels it (code review of stage 3.3b-M2b, finding M2b-R2), and a press that waits too.
    found->second.pendingAbility = -1;
    found->second.pendingCast = 0;
    ++found->second.generation;
}

std::optional<std::uint32_t> AttackSequences::generation_of(const std::string& characterId) const {
    const auto found = m_states.find(characterId);
    return found == m_states.end() ? std::nullopt : std::optional<std::uint32_t>(found->second.generation);
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

std::string AttackSequences::describe(const std::string& characterId, std::uint16_t input, WorldClock::time_point now) const {
    const auto found = m_states.find(characterId);
    if (found == m_states.end() || found->second.actions == nullptr) {
        return "no sequence";
    }
    const State& state = found->second;
    std::string text = "visits:";
    char part[320];
    for (const AttackVisit& visit : state.visits) {
        const gamedata::AttackFragment& data = state.actions->fragments[visit.fragment];
        const bool after = steps_between(visit.start, input) <= 0x7FFF;
        const float t = after ? clock_of(visit, input) : -1.0F;
        // The nearest strike window of the fragment to the clock of the input.
        const gamedata::AttackStrike* nearest = nullptr;
        float distance = 0.0F;
        for (const gamedata::AttackStrike& strike : data.strikes) {
            const float away = t < strike.start ? strike.start - t : (t > strike.end ? t - strike.end : 0.0F);
            if (nearest == nullptr || away < distance) {
                nearest = &strike;
                distance = away;
            }
        }
        std::snprintf(part, sizeof(part), " [#%u %s%s, start %u, %s %u, clock %.3f s, nearest strike %s%.3f to %.3f s, due %+.0f ms]",
                      visit.number, data.name.c_str(), visit.cast != 0 ? " (ability)" : "", static_cast<unsigned>(visit.start),
                      !visit.ended ? "plays, no end," : (visit.interrupted ? "interrupted at" : "ended at"),
                      static_cast<unsigned>(visit.ended ? visit.endInput : input), static_cast<double>(t), nearest == nullptr ? "none " : "",
                      nearest != nullptr ? static_cast<double>(nearest->start) : 0.0, nearest != nullptr ? static_cast<double>(nearest->end) : 0.0,
                      std::chrono::duration<double, std::milli>(now - visit.dueAt).count());
        text += part;
    }
    if (state.visits.empty()) {
        text += " none";
    }
    if (state.pendingAbility >= 0) {
        std::snprintf(part, sizeof(part), "; an ability %s waits for input %u", state.actions->fragments[static_cast<std::size_t>(state.pendingAbility)].name.c_str(),
                      static_cast<unsigned>(state.pendingInput));
        text += part;
    }
    std::snprintf(part, sizeof(part), "; the last input %u, %u end(s) of the sequence", static_cast<unsigned>(state.lastInput), state.generation);
    text += part;
    return text;
}

const AttackSequences::State* AttackSequences::state_of(const std::string& characterId) const {
    const auto found = m_states.find(characterId);
    return found == m_states.end() ? nullptr : &found->second;
}

} // namespace soulwarden::backend::world

// User test 2: the corrections of the faults that the user found in play (the diagnosis user-test-2.md, UT2-1 to UT2-13). Each case
// names its finding, and each one fails on the tree of the release before the stage (commit ff668bf). All ids, names, and values are
// made up: no file of the game and no value of a capture is here.
//
//   UX1  UT2-1   an ability starts only from the step of its press: a press before a window of the fragment that plays is refused
//                (no cast, no cooldown); a press in a window starts at its end; two characters have their own sequences
//   UX2  UT2-2   the end of an action, of a walk, and of a stun gives the idle record of the slice to each observer; a later state
//                keeps it (no scope fields); a slice with no idle action keeps the rule from before
//   UX3  UT2-3   each movement state of a reaction with a push has the record of the reaction again (the same start count, the time
//                of the state), for two sessions; the end gives the idle record; a walk keeps one record
//   UX4  UT2-4   the push lasts the reaction of the hit: the length of the reaction fragment of its power; a decay travel ends near
//                its end; a row with NoReaction pushes for 0.4 s (Pushable); a hit with no reaction pushes nothing
//   UX5  UT2-4   after a push the creature goes on with its path (no new search) and steps at once
//   UX6  UT2-9   the reaction rule of the action lists: no reaction for a tick of a damage over time, the fragment of the power of
//                the row, the creature types, the chain limit, the hit stun, two sessions
//   UX7  UT2-13  each refused claim with no strike and each press refused with "casting" has a log line with the visits (32 at most)
//   UX8  UT2-7   the loaders: the reaction lengths of a slice, its idle action, the reaction cells of a damage row
//   UX9  UT2-7   the check of the creature tool (check_creature_tool.py): the cases length-* (the reaction lengths and the attack
//                lengths from the animation files)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <fstream>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"
#include "support/idle_fixture.h"
#include "support/motion_fixture.h"
#include "support/test_navigation.h"
#include "world/behavior.h"
#include "world/navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace motion;
using nlohmann::json;

using namespace idle;

/** The scope fields of one movement state of a creature. */
struct ScopeSeen {
    /** -2: no scope fields; -1: the fields with no record; else the action index of the record. */
    int action{-2};
    std::uint16_t startCount{0};
    /** The time of scope 0 in ticks of 1/30 s (fields 28, 38, 49, 50), when the state has it. */
    std::optional<std::uint32_t> ticks;
    /** Seconds from the start of the case part to the end of the step in which the client got the state. */
    double at{0.0};
};

ScopeSeen scope_seen(const Fragment& movement, double at) {
    ScopeSeen seen;
    seen.at = at;
    const auto blob = movement.fields.find({0, 33});
    if (blob == movement.fields.end()) {
        return seen;
    }
    server::hub::Reader reader(blob->second);
    if (reader.size() == 0) {
        seen.action = -1;
        return seen;
    }
    seen.startCount = reader.u16();
    (void)reader.size(); // The fragment index + 1.
    seen.action = static_cast<int>(reader.size()) - 1;
    const auto byte_of = [&](unsigned field) -> std::optional<std::uint32_t> {
        const auto found = movement.fields.find({0, field});
        if (found == movement.fields.end() || found->second.size() != 1) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(found->second[0]);
    };
    const auto b0 = byte_of(28);
    const auto b1 = byte_of(38);
    const auto b2 = byte_of(49);
    const auto b3 = byte_of(50);
    if (b0 && b1 && b2 && b3) {
        seen.ticks = *b0 | (*b1 << 8) | (*b2 << 16) | (*b3 << 24);
    }
    return seen;
}

/** Appends the movement states of one interest number that a client got from the message `from` on. */
void collect(const FakeClient& client, std::size_t from, std::uint32_t interest, double at, std::vector<ScopeSeen>& out) {
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == shop::kWolfSlots[0] && fragment.type == 11) {
                    out.push_back(scope_seen(fragment, at));
                }
            }
        }
    }
}

std::string describe(const std::vector<ScopeSeen>& states) {
    std::string text;
    for (const ScopeSeen& state : states) {
        char part[64];
        std::snprintf(part, sizeof(part), "%s%.2f:%d", text.empty() ? "" : " ", state.at, state.action);
        text += part;
        if (text.size() > 600) {
            text += " ...";
            break;
        }
    }
    return text;
}

/** The two sessions of a case and their states of one creature. */
struct Watch {
    IdleShop& shop;
    world::EntityId id;
    std::uint32_t interestA{0};
    std::uint32_t interestB{0};
    state::WorldPosition betaPlace;
    std::uint16_t betaInput{400};
    world::WorldClock::time_point origin;
    std::vector<ScopeSeen> a;
    std::vector<ScopeSeen> b;

    Watch(IdleShop& owner, world::EntityId creature) : shop(owner), id(creature) {
        const auto& knownA = shop.host.session(1).observer().known();
        const auto& knownB = shop.host.session(2).observer().known();
        interestA = knownA.count(id) != 0 ? knownA.at(id).interest : 0;
        interestB = knownB.count(id) != 0 ? knownB.at(id).interest : 0;
        betaPlace = shop.world().find_character(kCharacterB)->position;
        origin = shop.host.clock.now();
    }
    [[nodiscard]] bool ready() const {
        return interestA != 0 && interestB != 0;
    }
    /** A new part of the case: the states that wait in the two sessions go first, then the times start again. */
    void restart() {
        steps(2);
        a.clear();
        b.clear();
        origin = shop.host.clock.now();
    }
    /** One step: a report of B and one tick, then a report of A and one tick (2 ticks of 1/30 s; the pattern of case FM7). */
    void step() {
        const std::size_t fromA = shop.a.got.size();
        const std::size_t fromB = shop.b.got.size();
        shop.host.clock.advance(kTick);
        shop.host.datagram(shop.b.peer, shop.b.report_at(betaInput++, betaPlace.x, betaPlace.y, betaPlace.z));
        shop.host.tick();
        shop.b.exchange(shop.host);
        shop.walker.send(shop.walker.entry());
        shop.b.exchange(shop.host);
        const double at = std::chrono::duration<double>(shop.host.clock.now() - origin).count();
        collect(shop.a, fromA, interestA, at, a);
        collect(shop.b, fromB, interestB, at, b);
    }
    void steps(int count) {
        for (int index = 0; index < count; ++index) {
            step();
        }
    }
};

/** @return The states with a scope record or the fields with no record (not the states with no scope fields). */
std::vector<ScopeSeen> scoped(const std::vector<ScopeSeen>& states) {
    std::vector<ScopeSeen> out;
    std::copy_if(states.begin(), states.end(), std::back_inserter(out), [](const ScopeSeen& state) { return state.action != -2; });
    return out;
}

bool same_scopes(const std::vector<ScopeSeen>& a, const std::vector<ScopeSeen>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (a[index].action != b[index].action || a[index].startCount != b[index].startCount) {
            return false;
        }
    }
    return true;
}

// ---- UX1: UT2-1, the press of an ability -------------------------------------------------------------------------------------

/**
 * UX1 (UT2-1): an ability press before the windows of the fragment that plays (Execute at 0.133 s of Reap; the windows of Execute in
 * Reap start at 0.3 s) is refused with "casting": the client reads the press only at its input step (live check of user test 2:
 * Wrecking Ball at 0.4 s of Shockwave did not play; the server of the release started it 1033 ms later). The refused press starts no
 * cooldown: a press in the window (0.5 s of Reap, the buffered window [0.3, 0.9)) is accepted and starts at the end of the window. A
 * second character (B) has its own sequence: its press at rest is accepted at its step (rule MP-10).
 */
void press_step_rule(const Options&) {
    gawh::GawhShop shop;
    shop.enter_b();
    shop.hold("TestAxe");
    shop.hold("TestAxe", kCharacterB);
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Executioner", nullptr});
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Executioner", nullptr}, kCharacterB);
    shop.wolf(-1.5F);
    shop.walker.idle(20);
    const std::uint16_t reap = shop.press(0);
    check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_JudgementsReach", "setup: Reap plays from its press");
    const std::uint64_t delayed = shop.abilities().counts().delayedStarts;
    const std::uint64_t casting = shop.refused(world::CastVerdict::casting);
    const std::uint64_t accepted = shop.accepted();
    const std::uint16_t early = shop.press(1);
    check(early == static_cast<std::uint16_t>(reap + 3), "setup: the press of Execute is at 0.133 s of Reap (its windows start at 0.3 s)");
    check(shop.refused(world::CastVerdict::casting) == casting + 1 && shop.abilities().counts().delayedStarts == delayed
              && shop.accepted() == accepted && shop.visits().back() == "Ability_GreatAxe_JudgementsReach",
          "a press before the window of the fragment that plays is refused (casting): no cast and no later start; Reap plays on");
    const auto* cooldowns = shop.abilities().cooldowns_of(kCharacterA);
    check(cooldowns != nullptr && cooldowns->count(gamedata::progression_id("Ability_Greataxe_Executioner")) == 0,
          "the refused press starts no cooldown of Execute");
    shop.until(static_cast<std::uint16_t>(reap + 14));
    const std::uint16_t inside = shop.press(1);
    shop.until(static_cast<std::uint16_t>(reap + 28));
    const world::AttackVisit* execute = shop.host.world.combat().sequences().newest(kCharacterA);
    check(inside == static_cast<std::uint16_t>(reap + 14) && shop.accepted() == accepted + 1 && execute != nullptr
              && shop.visits().back() == "Ability_GreatAxe_Executioner" && execute->start == static_cast<std::uint16_t>(reap + 26),
          "a press inside the buffered window [0.3, 0.9) of Reap (at 0.5 s) starts Execute at the end of the window");
    // B: its own press at rest, while A has its casts (one sequence for each character).
    world::Entity* b = shop.world().find_character(kCharacterB);
    Walker bWalker{shop.host, shop.b, b->position};
    bWalker.input = 300;
    bWalker.idle(2);
    bWalker.bits({kAbilityBits[0], kVirtualBit});
    const std::uint16_t bPressed = bWalker.input;
    bWalker.bits({kHoldBit});
    bWalker.bits({kReleaseBit});
    const world::AttackVisit* bVisit = shop.host.world.combat().sequences().newest(kCharacterB);
    check(shop.accepted() == accepted + 2 && bVisit != nullptr && bVisit->start == bPressed,
          "B presses Reap at rest: it starts at the step of its press (the sequences of A and B are separate, rule MP-10)");
}

// ---- UX2: UT2-2, the idle record -------------------------------------------------------------------------------------------------

/**
 * UX2 (UT2-2): the client does not end an action of an action list by itself (NoAutoBlendOut), and it ends its own idle at each tick
 * while the states have the scope fields with no record. The end of an action of the server, of a walk, and of a stun gives the idle
 * record of the slice (a new start count), the same for each observer (rule MP-2), and a later state of the creature (a turn) keeps it
 * (no scope fields). A slice with no action Idle keeps the rule from before user test 2 (no state at the end of an action).
 */
void idle_record(const Options&) {
    IdleShop shop;
    shop.enter_b();
    world::Creatures& creatures = shop.world().creatures();
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    const world::EntityId id = wolf->id;
    shop.run(1.0F);
    Watch watch(shop, id);
    check(watch.ready(), "setup: the two sessions observe the wolf");
    if (!watch.ready()) {
        return;
    }
    check(idle_action_of(shop.host.creatures.slices()[0]) == static_cast<std::uint32_t>(kIdleIndex) && !idle_action_of(shop.host.creatures.slices()[1]),
          "the loader finds the action Idle of the slice (and none in the slice with no such action)");
    // An action of the server with an end (an attack of 1.4 s).
    watch.restart();
    check(creatures.play(id, "Attack_Bite", 1.4F).error == world::CreatureError::none, "setup: the attack starts");
    watch.steps(30); // 2.0 s
    wolf = shop.creature(id);
    const std::vector<ScopeSeen> endA = scoped(watch.a);
    const std::vector<ScopeSeen> endB = scoped(watch.b);
    note("A: " + describe(watch.a));
    note("B: " + describe(watch.b));
    check(!wolf->creature.action && idle_of(*wolf) && idle_of(*wolf)->action == static_cast<std::uint32_t>(kIdleIndex),
          "after the end of the attack the wolf has no action of the server and has its idle record");
    check(endA.size() == 2 && endA[0].action == kBiteIndex && endA[1].action == kIdleIndex && endA[1].at >= 1.4 && endA[1].at < 1.55
              && endA[1].startCount == idle_of(*wolf)->generation && endA[1].ticks && *endA[1].ticks <= 3,
          "A gets the attack, then at its end (1.4 s) the idle record with a new start count and the time 0 (not the fields with no record)");
    check(same_scopes(endA, endB), "B gets the same records in the same sequence (rule MP-2)");
    // A turn: no scope fields (the idle stays in the client).
    watch.restart();
    creatures.moved(*wolf);
    watch.steps(3);
    check(!watch.a.empty() && !watch.b.empty() && scoped(watch.a).empty() && scoped(watch.b).empty(),
          "a later state (a turn) has no scope fields for each session: the client keeps its idle record");
    // A walk and its end.
    watch.restart();
    for (int index = 1; index <= 5; ++index) {
        wolf = shop.creature(id);
        creatures.move(*wolf, {wolf->position.x + 0.4F, wolf->position.y, wolf->position.z}, world::Gait::run);
        watch.step();
    }
    creatures.stop(*shop.creature(id));
    watch.steps(2);
    const std::vector<ScopeSeen> walkA = scoped(watch.a);
    wolf = shop.creature(id);
    check(walkA.size() == 2 && walkA[0].action == kMoveIndex && walkA[1].action == kIdleIndex && idle_of(*wolf)
              && walkA[1].startCount == idle_of(*wolf)->generation && same_scopes(walkA, scoped(watch.b)),
          "a walk has one record of Move_Combat, and its end the idle record with a new start count, for A and B");
    // A stun of a slice with no action Stun: the idle record.
    watch.restart();
    world::CreatureEffect stun;
    stun.status = "Test_Stun";
    stun.stun = true;
    stun.ends = shop.host.clock.now() + std::chrono::milliseconds(600);
    check(creatures.play(id, "Attack_Bite", 1.4F).error == world::CreatureError::none && creatures.apply_effect(*shop.creature(id), stun),
          "setup: a stun stops an attack");
    watch.steps(15);
    const std::vector<ScopeSeen> stunA = scoped(watch.a);
    note("the stun: A: " + describe(watch.a));
    check(!stunA.empty() && stunA.back().action == kIdleIndex && idle_of(*shop.creature(id))
              && stunA.back().startCount == idle_of(*shop.creature(id))->generation && same_scopes(stunA, scoped(watch.b)),
          "a stun that stops the attack (a slice with no action Stun) gives the idle record, for A and B");
    // A new observer gets the idle record in its first record.
    {
        const auto first = decode_movement(server::rep::encode_creature_movement_state(*shop.creature(id), shop.host.clock, 0));
        const ScopeSeen seen = scope_seen(first, 0.0);
        check(seen.action == kIdleIndex, "the first record of a session that observes the wolf now has its idle record");
    }
    // A slice with no action Idle: the rule from before (no state at the end of an action).
    world::Entity* plain = shop.spawn("Test_Plain", -4.0F);
    const world::EntityId plainId = plain->id;
    shop.run(1.0F);
    Watch other(shop, plainId);
    other.restart();
    check(creatures.play(plainId, "Attack_Bite", 1.4F).error == world::CreatureError::none, "setup: the attack of the plain wolf starts");
    other.steps(30);
    const std::vector<ScopeSeen> plainA = scoped(other.a);
    check(other.ready() && plainA.size() == 1 && plainA[0].action == kBiteIndex && !idle_of(*shop.creature(plainId)),
          "a slice with no action Idle: no state at the end of the action (the rule from before user test 2)");
}

// ---- UX3: UT2-3, the record of a timed action in each state ---------------------------------------------------------------------

/**
 * UX3 (UT2-3): while a one-shot action of the server with an end plays, each movement state has its record again (the same start
 * count, the time of the state): the client requeues a one-shot record whose time is more than half its length behind its own time
 * (client code 0x2A2BC20). A reaction with the push of a heavy arrow (no decay, a state each 50 ms) for two sessions; at its end the
 * idle record; a walk keeps one record (the rule of user test 1).
 */
void reaction_record_in_push(const Options&) {
    IdleShop shop;
    shop.enter_b();
    world::Creatures& creatures = shop.world().creatures();
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    const world::EntityId id = wolf->id;
    shop.run(1.0F);
    Watch watch(shop, id);
    check(watch.ready(), "setup: the two sessions observe the wolf");
    if (!watch.ready()) {
        return;
    }
    watch.restart();
    const state::WorldPosition before = wolf->position;
    const world::HitResult hit = shop.hit(*wolf, "Test_BowHeavy");
    wolf = shop.creature(id);
    check(hit.verdict == world::HitVerdict::accepted && wolf->creature.action && wolf->creature.action->action == kReactionIndex
              && wolf->creature.forced,
          "setup: the heavy hit starts the reaction (power 3) and the push");
    if (!wolf->creature.action) {
        return;
    }
    const std::uint16_t generation = wolf->creature.action->generation;
    const double length = kReactionLengths[3];
    watch.steps(25); // 1.67 s
    wolf = shop.creature(id);
    const float moved = std::hypot(wolf->position.x - before.x, wolf->position.y - before.y);
    note("the push moved the wolf " + std::to_string(moved) + " m");
    for (const auto* states : {&watch.a, &watch.b}) {
        const char* who = states == &watch.a ? "A" : "B";
        note(std::string(who) + ": " + describe(*states));
        std::size_t during = 0;
        std::size_t good = 0;
        for (const ScopeSeen& state : *states) {
            if (state.at < length - 0.07) {
                ++during;
                good += state.action == kReactionIndex && state.startCount == generation && state.ticks
                                && std::fabs(static_cast<double>(*state.ticks) - state.at * 30.0) <= 3.0
                            ? 1
                            : 0;
            }
        }
        check(during >= 8 && good == during,
              std::string(who) + ": each movement state while the reaction plays has its record again (the same start count, the time of the state)");
        const auto end = std::find_if(states->begin(), states->end(), [&](const ScopeSeen& state) { return state.at >= length; });
        check(end != states->end() && end->action == kIdleIndex, std::string(who) + ": the first state after the end of the reaction has the idle record");
    }
    check(same_scopes(watch.a, watch.b), "A and B get the same scope values in the same sequence (MP-2: one encoder for each observer)");
    check(moved > 0.85F && moved < 0.95F, "the push of the heavy row (1 m/s, no decay) lasts the reaction of power 3 of the slice: 0.9 m in 0.9 s");
    // The guard of the rule of user test 1: a walk has one record of its locomotion.
    watch.restart();
    for (int index = 1; index <= 8; ++index) {
        wolf = shop.creature(id);
        creatures.move(*wolf, {wolf->position.x + 0.4F, wolf->position.y, wolf->position.z}, world::Gait::run);
        watch.step();
    }
    const std::vector<ScopeSeen> walk = scoped(watch.a);
    check(watch.a.size() >= 8 && walk.size() == 1 && walk[0].action == kMoveIndex,
          "a walk has one record of Move_Combat and no scope fields in its later steps (the rule of user test 1)");
}

// ---- UX4: UT2-4, the length of a push ------------------------------------------------------------------------------------------

/**
 * UX4 (UT2-4, UT2-7): the push of a hit lasts the reaction that the hit started (the client pushes in the reaction to the end of its
 * fragment, protocol notes 4.26): the light row of power 2 (0.5 m/s, no decay) 1.2 s, 0.6 m. A decay travel (1 m, decay 0.85) ends
 * when less than 2 cm is left, at its full distance, long before 1.5 s. A row with NoReaction pushes for 0.4 s (the action Pushable). A
 * hit that starts no reaction (a light hit on an Elite) pushes nothing. The observer B gets the states of the push too.
 */
void push_length(const Options&) {
    IdleShop shop;
    shop.enter_b();
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    const world::EntityId id = wolf->id;
    shop.run(1.0F);
    Watch watch(shop, id);
    const auto travel = [&](const char* row, float limit) {
        const state::WorldPosition before = shop.creature(id)->position;
        const world::WorldClock::time_point start = shop.host.clock.now();
        const world::HitResult hit = shop.hit(*shop.creature(id), row);
        float seconds = 0.0F;
        bool moving = shop.creature(id)->creature.forced.has_value();
        while (shop.creature(id)->creature.forced && seconds < limit) {
            watch.step();
            seconds = std::chrono::duration<float>(shop.host.clock.now() - start).count();
        }
        const world::Entity* after = shop.creature(id);
        const float moved = std::hypot(after->position.x - before.x, after->position.y - before.y);
        note(std::string(row) + ": moved " + std::to_string(moved) + " m in " + std::to_string(seconds) + " s");
        watch.steps(30); // The reaction ends (2 s), and the wolf stands for the next row.
        return std::make_tuple(hit.verdict == world::HitVerdict::accepted && moving, moved, seconds);
    };
    watch.restart();
    const auto [lightHit, lightMoved, lightTime] = travel("Test_BowLight", 3.0F);
    check(lightHit && near(lightMoved, 0.6F, 0.05F) && lightTime > 1.1F && lightTime < 1.35F,
          "the light row of power 2 (0.5 m/s, no decay) pushes for the length of r_R2_F of the slice: 0.6 m in 1.2 s");
    check(!scoped(watch.b).empty() && watch.b.size() >= 8, "the observer B gets the states of the push");
    const auto [decayHit, decayMoved, decayTime] = travel("Test_Decay", 3.0F);
    check(decayHit && near(decayMoved, 1.0F, 0.01F) && decayTime < 0.9F,
          "a decay travel (1 m, decay 0.85) ends when less than 2 cm is left (0.80 s by the rule of the client), at its full distance "
          "(before: 1.47 s)");
    const std::uint64_t reactions = reactions_of(shop.world().creatures());
    const auto [pushHit, pushMoved, pushTime] = travel("Test_NoReaction", 3.0F);
    check(pushHit && near(pushMoved, 0.4F, 0.05F) && pushTime < 0.5F && reactions_of(shop.world().creatures()) == reactions,
          "a row with NoReaction plays no reaction and pushes for 0.4 s (the action Pushable: 1 m/s, 0.4 m)");
    world::Entity* elite = shop.spawn("Test_Elite", -4.0F);
    const state::WorldPosition elitePlace = elite->position;
    const world::HitResult eliteHit = shop.hit(*elite, "Test_BowLight");
    shop.run(1.5F);
    elite = shop.creature(elite->id);
    check(eliteHit.verdict == world::HitVerdict::accepted && !elite->creature.forced && !elite->creature.action
              && std::hypot(elite->position.x - elitePlace.x, elite->position.y - elitePlace.y) < 0.01F && !pushed(eliteHit),
          "a light hit on an Elite starts no reaction (the creature types of the action Reaction) and pushes nothing");
}

// ---- UX5: UT2-4, the path after a push -------------------------------------------------------------------------------------------

constexpr float kGround = 50.0F;
constexpr int kTile = 15;

state::WorldPosition place(float x, float y, float z = kGround) {
    return state::WorldPosition{x, y, z};
}

struct NoSink final : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

/**
 * UX5 (UT2-4): a wolf in a fight walks around a wall on a path of a search. A push of 0.3 m (decay 0.85) moves it. At the end of the
 * push it goes on with its path: no new search, and its first step comes in the next step of the behavior (the release forgot the path
 * at the start and at the end of each push, and the first step waited for a new search).
 */
void path_after_push(const Options& options) {
    ServerHost host;
    if (!host.creatures.set_full({idle_wolf_slice()}, {kind_of("Test_Wolf", "Solo", 0x21112222)}, {}, {{1, shop::kWolfHealth, 100.0F, 50.0F}})) {
        check(false, "setup: the creature data");
        return;
    }
    const std::filesystem::path folder = options.workDirectory / "ux5-path-after-push";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    // A wall along y = 1000 from x = 985 to x = 1008: the path from (1000, 990) to (1000, 1010) goes around its east end.
    region.tiles[{kTile, kTile}] = [](int cellX, int cellY, std::vector<TestFloor>& floors) {
        const float x = static_cast<float>(kTile) * 64.0F + (static_cast<float>(cellX) + 0.5F) * 0.25F;
        const float y = static_cast<float>(kTile) * 64.0F + (static_cast<float>(cellY) + 0.5F) * 0.25F;
        if (y > 999.5F && y < 1000.5F && x > 985.0F && x < 1008.0F) {
            floors.clear();
            floors.push_back({kGround + 3.0F, gamedata::NavFloor::kBlocked});
        }
    };
    region.write(folder);
    check(host.navigation.set_directory(folder.string()), "setup: the navigation data of the wall loads");
    NoSink sink;
    const Player player = add_player(host.store, "1", "", "ux5-character", "", place(1000.0F, 1010.0F));
    world::Entity* character = host.world.enter(1, sink, player.characterId, player.account.personaId).entity;
    (void)host.world.travel(character->characterId, place(1000.0F, 1010.0F), world::TravelScreen::off);
    const auto ticks = [&](int count) {
        for (int index = 0; index < count; ++index) {
            host.clock.advance(std::chrono::milliseconds(10));
            host.world.tick();
        }
    };
    ticks(1200); // The shelter of the arrival ends.
    character->vitals.healthMax = character->vitals.health = 100000.0F;
    world::Entity* wolf = host.world.creatures().spawn("Test_Wolf", place(1000.0F, 990.0F), 0.0F).entity;
    const world::EntityId id = wolf->id;
    host.world.creatures().behavior().hurt_by(*wolf, character->id);
    ticks(100); // 1 s on the path around the wall.
    wolf = host.world.creatures().find(id);
    const std::uint64_t searches = host.world.navigation().counts().searches;
    check(wolf->creature.mind.mode == world::CreatureMode::fight && searches >= 1 && wolf->position.x > 1000.5F,
          "setup: the wolf fights the character and walks on a searched path around the wall");
    const state::WorldPosition atPush = wolf->position;
    check(push_for(host.world.creatures(), *wolf, impact_of(0.3F, 0.85F), place(atPush.x - 1.0F, atPush.y), std::chrono::milliseconds(800)),
          "setup: the push moves the wolf");
    int afterEnd = -1;
    state::WorldPosition atEnd{};
    for (int tick = 0; tick < 200; ++tick) {
        ticks(1);
        wolf = host.world.creatures().find(id);
        if (afterEnd < 0 && !wolf->creature.forced) {
            afterEnd = 0;
            atEnd = wolf->position;
        } else if (afterEnd >= 0) {
            ++afterEnd;
            if (world::horizontal_distance(wolf->position, atEnd) > 0.05F) {
                break;
            }
        }
    }
    note("the first step after the push came " + std::to_string(afterEnd * 10) + " ms after its end; searches " + std::to_string(searches)
         + " to " + std::to_string(host.world.navigation().counts().searches));
    check(host.world.navigation().counts().searches == searches, "the wolf goes on with its path after the push: no new search");
    check(afterEnd >= 0 && afterEnd <= 21, "its first step comes in the next step of the behavior (200 ms at most after the end of the push)");
}

// ---- UX6: UT2-9, the reaction rule ---------------------------------------------------------------------------------------------

/**
 * UX6 (UT2-9): the reaction of a hit follows its damage row and the rule of the action Reaction of the action lists (game data): a tick
 * of a damage over time plays none; a light row of PowerLevel_AI 2 plays r_R2_F for its length with a hit stun of HitStun_AI frames
 * (the behavior starts no attack in it); an ability of power 2 plays on an Elite, a light row does not, a Boss plays none; the third
 * chained reaction of a Solo creature starts the limit (no reaction of a light row for the reaction and 2 s after it). The two sessions
 * get the same reaction record.
 */
void reaction_rule(const Options&) {
    IdleShop shop;
    shop.enter_b();
    world::Creatures& creatures = shop.world().creatures();
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    const world::EntityId id = wolf->id;
    shop.run(1.0F);
    Watch watch(shop, id);
    // A tick of a damage over time: no reaction.
    const std::uint64_t reactions = reactions_of(creatures);
    const world::HitResult tick = shop.world().combat().strike_amount(*shop.character(), *wolf, 20.0F, "Slash", "a test tick");
    check(tick.verdict == world::HitVerdict::accepted && !shop.creature(id)->creature.action && reactions_of(creatures) == reactions,
          "a tick of a damage over time hurts the wolf and starts no reaction (it has no damage row)");
    // A light row of power 2: the fragment r_R2_F of the slice, its length 1.2 s, the hit stun 18 frames (0.6 s).
    watch.restart();
    const world::HitResult light = shop.hit(*shop.creature(id), "Test_BowLight");
    wolf = shop.creature(id);
    const gamedata::CreatureSlice& body = *wolf->creature.body;
    check(light.verdict == world::HitVerdict::accepted && wolf->creature.action && wolf->creature.action->action == kReactionIndex
              && wolf->creature.action->fragment == static_cast<std::uint32_t>(body.reactions[2]) && wolf->creature.action->ends
              && near(std::chrono::duration<float>(*wolf->creature.action->ends - shop.host.clock.now()).count(), 1.2F, 0.02F)
              && stun_of(*wolf)
              && near(std::chrono::duration<float>(*stun_of(*wolf) - shop.host.clock.now()).count(), 0.6F, 0.02F),
          "a light row of PowerLevel_AI 2 plays r_R2_F of the slice for 1.2 s (its length) with a hit stun of 18 frames (0.6 s)");
    watch.steps(3);
    const std::vector<ScopeSeen> recordA = scoped(watch.a);
    check(!recordA.empty() && recordA[0].action == kReactionIndex && same_scopes(recordA, scoped(watch.b)),
          "A and B get the same reaction record");
    // The creature types.
    world::Entity* elite = shop.spawn("Test_Elite", 0.0F, 4.0F);
    world::Entity* boss = shop.spawn("Test_Boss", 0.0F, -4.0F);
    (void)shop.hit(*elite, "Test_BowLight");
    check(!shop.creature(elite->id)->creature.action, "a light row plays no reaction on an Elite");
    (void)shop.hit(*shop.creature(elite->id), "Test_Power2");
    check(shop.creature(elite->id)->creature.action && shop.creature(elite->id)->creature.action->action == kReactionIndex,
          "an ability of power 2 plays a reaction on an Elite");
    (void)shop.hit(*boss, "Test_Power2");
    check(!shop.creature(boss->id)->creature.action, "a Boss plays no reaction");
    // The chain limit: three chained reactions of light rows on a Solo wolf, then none in the limit, then one again after it.
    watch.steps(40);
    const std::uint64_t limits = limits_of(creatures);
    std::vector<int> started;
    std::optional<std::uint32_t> firstFragment;
    for (int hit = 0; hit < 5; ++hit) {
        const std::uint64_t before = reactions_of(creatures);
        (void)shop.hit(*shop.creature(id), "Test_Power0");
        started.push_back(reactions_of(creatures) > before ? 1 : 0);
        if (hit == 0 && shop.creature(id)->creature.action) {
            firstFragment = shop.creature(id)->creature.action->fragment;
        }
        watch.steps(4); // 0.27 s: the reaction of power 0 (0.5 s) still plays at the next hit
    }
    note("the reactions of five chained hits: " + std::to_string(started[0]) + std::to_string(started[1]) + std::to_string(started[2])
         + std::to_string(started[3]) + std::to_string(started[4]));
    check(firstFragment == static_cast<std::uint32_t>(body.reactions[1]),
          "a row of power 0 plays r_R1_F (the reaction table of the action lists: power 0 and 1 play r_R1_F; r_R0_F is additive)");
    check(started == std::vector<int>{1, 1, 1, 0, 0} && limits_of(creatures) == limits + 1,
          "the third chained reaction starts the limit: the fourth and the fifth hit play no reaction");
    watch.steps(45); // 3 s: the end of the reaction and the 2 s of the limit.
    const std::uint64_t again = reactions_of(creatures);
    (void)shop.hit(*shop.creature(id), "Test_Power0");
    check(reactions_of(creatures) == again + 1, "after the limit a hit plays its reaction again");
    // The hit stun holds the behavior: a wolf in its reach of A attacks (1.4 s) and waits its pause (1.5 s); a hit of 60 frames of hit
    // stun (2 s) 0.1 s after the end of its attack holds its next attack to the end of the stun (3.5 s after the start of the attack, not
    // 2.9 s).
    watch.steps(30);
    creatures.remove(id);
    creatures.remove(elite->id);
    creatures.remove(boss->id);
    world::Entity* fighter = shop.spawn("Test_Wolf", 1.5F, 0.0F);
    const world::EntityId fighterId = fighter->id;
    shop.world().set_creature_behavior(true);
    creatures.behavior().hurt_by(*fighter, shop.character()->id);
    const auto attacking = [&]() {
        const world::Entity* own = shop.creature(fighterId);
        return own->creature.action && own->creature.action->action == static_cast<std::uint32_t>(kBiteIndex);
    };
    int first = 0;
    for (; first < 60 && !attacking(); ++first) {
        watch.step();
    }
    const world::WorldClock::time_point attackStart = shop.host.clock.now();
    const std::uint16_t firstAttack = shop.creature(fighterId)->creature.action ? shop.creature(fighterId)->creature.action->generation : 0;
    // The end of the attack: its action ends, and the next step of the behavior ends the attack of the mind (its pause starts).
    while ((attacking() || shop.creature(fighterId)->creature.mind.attack) && shop.host.clock.now() - attackStart < std::chrono::seconds(3)) {
        watch.step();
    }
    (void)shop.hit(*shop.creature(fighterId), "Test_Stun");
    const world::WorldClock::time_point stunned = shop.host.clock.now();
    std::optional<double> next;
    for (int index = 0; index < 60 && !next; ++index) {
        watch.step();
        if (attacking() && shop.creature(fighterId)->creature.action->generation != firstAttack) {
            next = std::chrono::duration<double>(shop.host.clock.now() - attackStart).count();
        }
    }
    note("the first attack after " + std::to_string(first) + " steps; the hit stun " +
         std::to_string(std::chrono::duration<double>(stunned - attackStart).count()) + " s after its start; the next attack " +
         (next ? std::to_string(*next) : std::string("none")) + " s after its start");
    const double stunAt = std::chrono::duration<double>(stunned - attackStart).count();
    check(next && *next >= stunAt + 2.0 - 0.05 && *next < stunAt + 2.3,
          "the next attack comes at the end of the hit stun (2 s after the hit), not at the end of the pause (2.9 s after the start)");
    shop.world().set_creature_behavior(false);
    creatures.remove(fighterId);
    watch.steps(40);
}

// ---- UX7: UT2-13, the log of the refusals --------------------------------------------------------------------------------------

std::size_t lines_with(const std::string& text) {
    return static_cast<std::size_t>(
        std::count_if(log_lines().begin(), log_lines().end(), [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

/**
 * UX7 (UT2-13): each claim refused with "no strike" has its own log line with the visits of the sequence (the fragment, the start, the
 * end and its cause, the clock of the contact, the nearest strike window), and each press refused with "casting" the same; at most 32
 * lines of each in a session. The release had one line for the first refusal of each cause.
 */
void refusal_log(const Options&) {
    gawh::GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Executioner", nullptr});
    world::Entity* wolf = shop.wolf(-1.5F);
    shop.walker.idle(20);
    const std::size_t pressLines = lines_with("rep: ability press refused (a different cast plays)");
    const std::size_t claimLines = lines_with("rep: hit claim refused (no strike of the attack fits the contact)");
    // Two presses before the window, in two casts of Reap.
    for (int cast = 0; cast < 2; ++cast) {
        const std::uint16_t reap = shop.press(0);
        shop.until(static_cast<std::uint16_t>(reap + 3));
        shop.press(1);
        shop.until(static_cast<std::uint16_t>(reap + 80));
        shop.abilities().reset_cooldowns(kCharacterA, true);
    }
    note("log lines: " + std::to_string(log_lines().size()) + ", with \"ability press\": " + std::to_string(lines_with("ability press"))
         + ", with \"world: ability\": " + std::to_string(lines_with("world: ability")) + ", refused casting "
         + std::to_string(shop.refused(world::CastVerdict::casting)));
    check(lines_with("rep: ability press refused (a different cast plays)") == pressLines + 2,
          "each press refused with \"a different cast plays\" (casting) has its log line (the release: only the first one of the session)");
    bool visits = false;
    for (const std::string& line : log_lines()) {
        visits = visits || (line.find("rep: ability press refused (a different cast plays)") != std::string::npos
                            && line.find("Ability_GreatAxe_JudgementsReach (ability), start") != std::string::npos);
    }
    check(visits, "the line names the visit that plays (the fragment of Reap, its start, its clock, its nearest strike)");
    // Three contacts with no strike: one swing of the axe, contacts before its strike window.
    for (int swing = 0; swing < 3; ++swing) {
        shop.step({kActionAttack});
        const std::uint16_t released = shop.step({kActionAttackRelease});
        shop.until(static_cast<std::uint16_t>(released + 2));
        shop.step({}, {gawh::touch(*wolf)});
        shop.walker.idle(60);
    }
    check(lines_with("rep: hit claim refused (no strike of the attack fits the contact)") == claimLines + 3,
          "each claim with no strike has its log line with the visits (the release: only the first one of the session)");
    for (int more = 0; more < 40; ++more) {
        shop.step({kActionAttack});
        const std::uint16_t released = shop.step({kActionAttackRelease});
        shop.until(static_cast<std::uint16_t>(released + 2));
        shop.step({}, {gawh::touch(*wolf)});
        shop.walker.idle(60);
    }
    check(lines_with("rep: hit claim refused (no strike of the attack fits the contact)") == claimLines + 32,
          "at most 32 such lines in a session");
}

// ---- UX8: UT2-7, the loaders ---------------------------------------------------------------------------------------------------

/** UX8 (UT2-7, UT2-9): the loader of the creature data takes the reaction lengths and the action Idle of a slice. */
void loaders(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "ux8-loaders";
    std::filesystem::create_directories(folder);
    const auto document = [](const json& lengths) {
        json slice = {{"path", "slices/test/wolf.dynamicslice"},
                      {"asset", "11111111-1111-1111-1111-111111111111"},
                      {"sub", 2},
                      {"root", "1000000000000011"},
                      {"interestRadius", 100.0},
                      {"movement", 5},
                      {"vitals", 9},
                      {"marker", 15},
                      {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                      {"components", json::array()},
                      {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Idle", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}}},
                      {"reactions", {1, 2, 5, 3, 4}}};
        if (!lengths.is_null()) {
            slice["reactionLengths"] = lengths;
        }
        return json{{"format", 1},
                    {"counts", {{"spawnPoints", 0}, {"kinds", 1}, {"slices", 1}}},
                    {"levels", {{18, 1690.875977}}},
                    {"slices", {slice}},
                    {"kinds",
                     {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
                       {"categoryId", 0x33334444}, {"name", "Test Wolf"}, {"vitalsName", "Test Wolf"}, {"family", "Wildlife"},
                       {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})},
                       {"points", 0}}}},
                    {"spawnPoints", json::array()}};
    };
    int number = 0;
    const auto load = [&](gamedata::CreatureData& data, const json& file) {
        const std::filesystem::path path = folder / ("creatures-" + std::to_string(number++) + ".json");
        std::ofstream(path) << file.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    const bool loaded = load(data, document({0.5, 0.7, 1.2, -1.0, 2.0}));
    check(loaded && reaction_length_of(data.slices()[0], 2) == 1.2F && reaction_length_of(data.slices()[0], 3) == -1.0F
              && idle_action_of(data.slices()[0]) == 1U,
          "the creature data has the reaction lengths (-1: not known) and the action Idle of a slice: " + data.error());
    gamedata::CreatureData old;
    check(load(old, document(nullptr)) && reaction_length_of(old.slices()[0], 2) < 0.0F,
          "a file of a tool from before user test 2 (no member reactionLengths) loads, with no length");
    for (const json& bad : {json({0.5, 0.7}), json({0.5, 0.7, 0.0, 1.0, 1.0}), json({0.5, 0.7, 13.0, 1.0, 1.0}), json({0.5, "x", 1.0, 1.0, 1.0})}) {
        gamedata::CreatureData refused;
        check(!load(refused, document(bad)), "the creature data refuses the reaction lengths " + bad.dump());
    }
}

/** UX9 (UT2-7): the cases length-* of the check of the creature tool (made-up animation databases and CAF files). */
void creature_tool_lengths(const Options& options) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    check(std::filesystem::is_regular_file(tool), "the check of the creature tool is in the tree");
    const std::filesystem::path work = options.workDirectory / "check-creature-lengths";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "check-creature-lengths.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok    length-", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
            note(line.substr(0, 160));
        } else if (line.rfind("FAIL", 0) == 0) {
            note(line.substr(0, 200));
        }
    }
    check(status == 0, "each case of the check of the creature tool passes");
    for (const char* name : {"length-reactions", "length-attack-animation", "length-attack-end-blend", "length-attack-guess", "length-caf-cut"}) {
        check(passed.contains(name), std::string("the case ") + name + " passes");
    }
}

} // namespace

std::vector<Case> user_test_2_cases() {
    return {
        {"UX1", "user test 2, UT2-1: an ability starts only from the step of its press; a press before a window is refused (two characters)",
         press_step_rule},
        {"UX2", "user test 2, UT2-2: the end of an action, a walk, and a stun give the idle record of the slice (two sessions)", idle_record},
        {"UX3", "user test 2, UT2-3: each state of a reaction with a push has its record again; the end gives the idle record (two sessions)",
         reaction_record_in_push},
        {"UX4", "user test 2, UT2-4: the push lasts the reaction of the hit; a decay travel ends near its end; Pushable; no reaction, no push",
         push_length},
        {"UX5", "user test 2, UT2-4: after a push the creature goes on with its path and steps at once", path_after_push},
        {"UX6", "user test 2, UT2-9: the reaction rule of the data: the damage over time, the power, the creature types, the limit, the hit stun",
         reaction_rule},
        {"UX7", "user test 2, UT2-13: each refused claim with no strike and each refused press has a log line with the visits (32 at most)",
         refusal_log},
        {"UX8", "user test 2, UT2-7: the loaders of the reaction lengths and the idle action", loaders},
        {"UX9", "user test 2, UT2-7: the cases length-* of the check of the creature tool (runs check_creature_tool.py)", creature_tool_lengths},
    };
}

} // namespace soulwarden::backend::tests

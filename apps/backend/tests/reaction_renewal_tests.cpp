// The cases of stage QG9c, part A (docs/systems/creatures.md, "The death of a slice with no reaction action"): the client of a player that
// hits a creature predicts a reaction of it and keeps that reaction pending when the slice has no action with the ClearDamaged activity
// (Medusa, the Corrupted Ogre); its repair of the action history in the death fragment stops the client (0x4871DB6, 0x4871FDA). At the
// death of such a creature after a hit of a character, each session that observes it makes it again: the stop of its interest, then, 250 ms
// later, a first record with a new interest number and the death action from its start. The accounts, characters, kinds, and places are
// made up.
//
//   QG9C1  a brute (no Reaction, no Reaction_Additive, no Pushable) that A hit dies: the session of A (the hitter) and the session of B (an
//          observer) each stop its interest and make it again with a new number and the death action from its start; the stop comes
//          before the new record, not in the same tick; controls: a wolf with the action Reaction dies after a hit of A (no renewal), and
//          a brute that no character hit dies by the command (no renewal)

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/range_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace range;

constexpr const char* kBrute = "Test_Brute";

/** The brute: the wolf slice with no action that acknowledges a reaction (the form of the Medusa slice: Death, no Reaction). */
gamedata::CreatureSlice brute_slice() {
    gamedata::CreatureSlice slice = wolf_slice();
    slice.path = "slices/test/brute.dynamicslice";
    slice.asset.fill(0x41);
    slice.root = 0x1000000000000041ULL;
    slice.actions = {{"Idle_No_Task", "Idle", 0, false},
                     {"Reaction_Breakout", "", -1, false},
                     {"Death", "r_Death", 4, true},
                     {"Attack_Bite", "Attack_Bite", 7, false},
                     {"Move_Combat", "Move_Combat", 11, false}};
    return slice;
}

gamedata::CreatureKind brute_kind() {
    gamedata::CreatureKind kind = wolf_kind();
    kind.id = kBrute;
    kind.vitals = kind.id;
    kind.category = "Test_Brute_Category";
    kind.vitalsId = 0x41112222;
    kind.categoryId = 0x41112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.bodies = {{1, 100}};
    return kind;
}

/** The movement state of the first record of an interest number from the message `from` on. */
std::optional<Fragment> first_movement(const FakeClient& client, std::uint32_t interest, std::size_t from) {
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == kWolfSlots[0] && fragment.type == 11) {
                    return fragment;
                }
            }
        }
    }
    return std::nullopt;
}

/** @return The index of the first message from `from` on with a stop of the interest number, or the count of the messages. */
std::size_t stop_message(const FakeClient& client, std::uint32_t interest, std::size_t from) {
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == 8) {
            const Bundle bundle = read_bundle(client.got[index].body);
            if (std::find(bundle.stops.begin(), bundle.stops.end(), interest) != bundle.stops.end()) {
                return index;
            }
        }
    }
    return client.got.size();
}

/** @return The time of scope 0 of a movement state in ticks (the four time fields), or -1 with no time. */
long long scope_ticks(const Fragment& fragment) {
    long long ticks = 0;
    const unsigned fields[] = {28, 38, 49, 50};
    for (unsigned at = 0; at < 4; ++at) {
        const auto found = fragment.fields.find({0, fields[at]});
        if (found == fragment.fields.end() || found->second.empty()) {
            return -1;
        }
        ticks |= static_cast<long long>(found->second[0]) << (8 * at);
    }
    return ticks;
}

void renewal_at_death(const Options&) {
    Range range(true, true);
    if (!range.host.creatures.set_full({wolf_slice(), brute_slice()}, {wolf_kind(), brute_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}})) {
        throw std::runtime_error("QG9C1: the creature data is not valid: " + range.host.creatures.error());
    }
    range.host.world.set_creature_behavior(false);
    range.enter_both();
    check(range.host.creatures.slices()[1].deathAction.has_value() && !range.host.creatures.slices()[1].hitAction.has_value(),
          "setup: the brute slice has a death action and no action Reaction");

    world::Entity* brute = range.host.world.creatures().spawn(kBrute, at(kPlace.x + 2.0F, kPlace.y), 90.0F).entity;
    check(brute != nullptr, "setup: the brute is in the world");
    if (brute == nullptr) {
        return;
    }
    const world::EntityId id = brute->id;
    check(range.until([&] { return interest_in(range.host, 1, id) != 0 && interest_in(range.host, 2, id) != 0; }),
          "setup: the sessions of A and B observe the brute");
    const std::uint32_t oldA = interest_in(range.host, 1, id);
    const std::uint32_t oldB = interest_in(range.host, 2, id);
    const world::HitResult hit = range.strike(*brute, "Test_Light");
    check(hit.verdict == world::HitVerdict::accepted, "setup: A hits the brute");
    const std::size_t fromA = range.a.got.size();
    const std::size_t fromB = range.b.got.size();
    // The kill after the hit: the death of the brute.
    check(range.host.world.creatures().set_health(id, 0.0F).error == world::CreatureError::none && brute->creature.dead, "setup: the brute dies");
    range.step(2);
    const auto sessions = {std::tuple{1U, &range.a, fromA, oldA}, std::tuple{2U, &range.b, fromB, oldB}};
    for (const auto& [session, client, from, old] : sessions) {
        const std::string who = session == 1 ? "A (the hitter)" : "B (an observer)";
        check(stop_message(*client, old, from) < client->got.size(), who + ": the stop of the old interest number of the brute");
        check(interest_in(range.host, session, id) == 0, who + ": no record of the brute in the tick of its stop");
    }
    check(range.until([&] { return interest_in(range.host, 1, id) != 0 && interest_in(range.host, 2, id) != 0; }, 30),
          "the brute comes in again in both sessions");
    for (const auto& [session, client, from, old] : sessions) {
        const std::string who = session == 1 ? "A (the hitter)" : "B (an observer)";
        const std::size_t stop = stop_message(*client, old, from);
        const std::uint32_t renewed = interest_in(range.host, session, id);
        check(renewed != 0 && renewed != old, who + ": a new interest number");
        const std::optional<Fragment> movement = first_movement(*client, renewed, stop);
        check(movement.has_value() && scope_ticks(*movement) == 0, who + ": its first record has the death action from its start");
        note("QG9C1 " + who + " old=" + std::to_string(old) + " new=" + std::to_string(renewed)
             + " ticks=" + std::to_string(movement ? scope_ticks(*movement) : -2));
    }

    // Control: a wolf (the action Reaction) dies after a hit of A: no renewal.
    world::Entity* wolf = range.host.world.creatures().spawn("Test_Wolf", at(kPlace.x - 2.0F, kPlace.y), 90.0F).entity;
    check(wolf != nullptr && range.until([&] { return interest_in(range.host, 1, wolf->id) != 0; }), "setup: A observes the wolf");
    if (wolf != nullptr) {
        const std::uint32_t wolfInterest = interest_in(range.host, 1, wolf->id);
        (void)range.strike(*wolf, "Test_Light");
        const std::size_t from = range.a.got.size();
        (void)range.host.world.creatures().set_health(wolf->id, 0.0F);
        range.step(15);
        check(stop_message(range.a, wolfInterest, from) == range.a.got.size() && interest_in(range.host, 1, wolf->id) == wolfInterest,
              "control: the dead wolf keeps its interest number (no renewal)");
    }
    // Control: a brute that no character hit dies by the command: no renewal.
    world::Entity* other = range.host.world.creatures().spawn(kBrute, at(kPlace.x, kPlace.y + 2.0F), 90.0F).entity;
    check(other != nullptr && range.until([&] { return interest_in(range.host, 1, other->id) != 0; }), "setup: A observes the second brute");
    if (other != nullptr) {
        const std::uint32_t otherInterest = interest_in(range.host, 1, other->id);
        const std::size_t from = range.a.got.size();
        (void)range.host.world.creatures().set_health(other->id, 0.0F);
        range.step(15);
        check(stop_message(range.a, otherInterest, from) == range.a.got.size() && interest_in(range.host, 1, other->id) == otherInterest,
              "control: a brute that no character hit keeps its interest number (no renewal)");
    }
}

} // namespace

std::vector<Case> reaction_renewal_cases() {
    return {{"QG9C1", "stage QG9c: the death of a slice with no reaction action after a hit: each session makes the body again", renewal_at_death}};
}

} // namespace soulwarden::backend::tests

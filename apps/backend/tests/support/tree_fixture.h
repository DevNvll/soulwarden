#pragma once

// The world of the cases of the behavior trees (stage BT1: tests/behavior_tree_tests.cpp; stage BT2: tests/behavior_binding_tests.cpp and
// tests/review_bt1_tests.cpp): the support world (A with a client, B) with eleven slices of each rule of the behavior. The kinds, the
// slices, the rows, and the places are made up.

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/support_fixture.h"

namespace soulwarden::backend::tests::trees {

using namespace support;

/** A melee slice with light attacks and a heavy attack (the cadence of the node melee): the archer slice with three attacks and no shot. */
inline gamedata::CreatureSlice fighter_slice() {
    gamedata::CreatureSlice slice = range::archer_slice(true);
    slice.path = "slices/test/fighter.dynamicslice";
    slice.asset.fill(0x51);
    slice.root = 0x1000000000000051ULL;
    slice.ranged.clear();
    const auto attack = [](float factor, float reach) {
        gamedata::CreatureContact contact;
        contact.time = 0.5F;
        contact.reach = reach;
        contact.factor = factor;
        contact.type = "Slash";
        contact.wire = 4;
        contact.category = gamedata::DamageCategory::physical;
        contact.row = "Test_Fighter";
        return gamedata::CreatureAttack{range::kBash, 1.4F, {contact}};
    };
    slice.attacks = {attack(0.2F, 2.2F), attack(0.7F, 2.6F), attack(0.25F, 2.0F)};
    slice.behavior->sightAngle = 180.0F;
    slice.behavior->runSpeed = 6.0F;
    return slice;
}

/** A caster: the archer slice whose basic shot is the cast Cast_One_Projectile (stage 3.4f-B: the band of a caster). */
inline gamedata::CreatureSlice caster_slice() {
    gamedata::CreatureSlice slice = range::archer_slice(true);
    slice.path = "slices/test/caster.dynamicslice";
    slice.asset.fill(0x52);
    slice.root = 0x1000000000000052ULL;
    slice.actions[range::kFireBow] = {"Cast_One_Projectile", "Cast_One_Projectile", 36, false};
    slice.behavior->sightAngle = 180.0F;
    return slice;
}

/** A creature that does not fight and wanders near its home. */
inline gamedata::CreatureSlice wanderer_slice() {
    gamedata::CreatureSlice slice = range::archer_slice(true);
    slice.path = "slices/test/wanderer.dynamicslice";
    slice.asset.fill(0x53);
    slice.root = 0x1000000000000053ULL;
    slice.behavior->sightDistance = 0.0F;
    slice.behavior->wander = true;
    slice.behavior->wanderNearest = 2.0F;
    slice.behavior->wanderFarthest = 6.0F;
    slice.behavior->ambientSpeed = 1.5F;
    return slice;
}

/** The world of the cases: the support world (A with a client, B) with eleven slices of each rule of the behavior. */
struct Arena : Support {
    /** A change of the slices and the kinds before the world gets them (stage BT2: the families, the creature types, the server trees). */
    using Change = std::function<void(std::vector<gamedata::CreatureSlice>&, std::vector<gamedata::CreatureKind>&)>;
    explicit Arena(bool observe = true, const Change& change = {}) : Support(observe) {
        std::vector<gamedata::CreatureSlice> slices = {fighter_slice(),      range::archer_slice(true), caster_slice(),
                                                       area::shaman_slice(), area::striker_slice(),     area::thrower_slice()};
        for (gamedata::CreatureSlice& slice : support_slices()) {
            slices.push_back(std::move(slice));
        }
        slices.push_back(wanderer_slice());
        const char* names[] = {"Test_Fighter", "Test_Archer", "Test_Caster", "Test_Shaman", "Test_Striker", "Test_Thrower",
                               "Test_Cleric",  "Test_Hound",  "Test_Ghost",  "Test_Knight", "Test_Wanderer"};
        std::vector<gamedata::CreatureKind> kinds;
        for (std::uint32_t index = 0; index < 11; ++index) {
            kinds.push_back(area::area_kind(names[index], index, 0x61112222 + 0x10 * index));
        }
        if (change) {
            change(slices, kinds);
        }
        if (!host.creatures.set_full(std::move(slices), std::move(kinds), {}, range::archer_levels(), {}, {range::arrow_slice()},
                                     gamedata::CreatureCapsule{range::kPlayerRadius, range::kPlayerSegment, {0.0F, 0.0F, 1.0F},
                                                               {0.0F, 0.0F, 0.0F}})) {
            throw std::runtime_error("tree test: the creature data is not valid: " + host.creatures.error());
        }
        hardy(*character());
    }
    static void hardy(world::Entity& character) {
        character.vitals.healthMax = 1.0e7F;
        character.vitals.health = 1.0e7F;
    }
    world::Entity* creature(const char* kind, float east, float north = 0.0F, float yaw = 90.0F) {
        return make(kind, east, north, yaw);
    }
    /** One step of A (33 ms) with a walk of (dx, dy) meters. */
    void move(float dx, float dy) {
        walker.position.x += dx;
        walker.position.y += dy;
        step();
    }
    /** A goes to a place at once (a command of the world), and its client reports that place. */
    void jump(const state::WorldPosition& place) {
        host.world.set_position(character()->id, place, false);
        walker.position = place;
        step();
    }
};

/**
 * @return The number of the records of the movement state (the slot of the action list) of a creature that a session decoded from the message
 *         `from` on (stage BT2: the cases of two sessions; code review of stage BT1, finding R8: a count of packets does not prove a record).
 */
inline std::size_t movement_records(Arena& arena, const FakeClient& client, std::uint32_t session, const world::Entity& creature,
                                    std::size_t from) {
    const std::uint32_t interest = range::interest_in(arena.host, session, creature.id);
    if (interest == 0) {
        return 0;
    }
    std::size_t count = 0;
    for (const Fragment& fragment : range::fragments_for(client, interest, from)) {
        count += fragment.slot == creature.creature.body->movementSlot ? 1 : 0;
    }
    return count;
}

/**
 * @return The number of the records of the movement state of a creature that a session decoded from the message `from` on whose scope record
 * has this action (code review of stage BT2, finding BTR2-R4: a movement fragment with no action record passed the cases of two sessions).
 * The wire has the index of the action plus 1 in the scope info (field 33 of the movement state).
 */
inline std::size_t action_records(Arena& arena, const FakeClient& client, std::uint32_t session, const world::Entity& creature, std::size_t from,
                                  std::uint32_t action) {
    constexpr unsigned kFieldScopeInfo = 33;
    const std::uint32_t interest = range::interest_in(arena.host, session, creature.id);
    if (interest == 0) {
        return 0;
    }
    std::size_t count = 0;
    for (const Fragment& fragment : range::fragments_for(client, interest, from)) {
        if (fragment.slot != creature.creature.body->movementSlot || fragment.fields.count({0, kFieldScopeInfo}) == 0) {
            continue;
        }
        server::hub::Reader reader(fragment.fields.at({0, kFieldScopeInfo}));
        if (reader.size() == 0) {
            continue; // The scope fields with no record.
        }
        reader.u16();
        reader.size();
        count += reader.size() == action + 1 ? 1 : 0;
    }
    return count;
}

} // namespace soulwarden::backend::tests::trees

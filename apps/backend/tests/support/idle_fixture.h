#pragma once

// The world of the cases of user test 2 (user_test_2_tests.cpp: UX2 to UX6) and of the corrections of its code review
// (review_user_test_2_tests.cpp: RT1 to RT6): the wolf slice with the action Idle and the reaction lengths, the creature types, the damage
// rows with the reaction cells, and the shop. All ids, names, and values are made up.

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "support/check.h"
#include "support/motion_fixture.h"
#include "support/state_decoder.h"

namespace soulwarden::backend::tests::idle {

using namespace motion;

// The members of the stage that a tree before it does not have. The proof that a case fails on the tree of the release (ff668bf) builds
// this file there with SOULWARDEN_U2_BASELINE: each such member counts as absent, and the checks of the behavior stay the same.
#ifndef SOULWARDEN_U2_BASELINE
inline std::optional<world::CreatureAction> idle_of(const world::Entity& creature) {
    return creature.creature.idle;
}
inline std::optional<world::WorldClock::time_point> stun_of(const world::Entity& creature) {
    return creature.creature.hitStunUntil;
}
inline std::optional<std::uint32_t> idle_action_of(const gamedata::CreatureSlice& slice) {
    return slice.idleAction;
}
inline float reaction_length_of(const gamedata::CreatureSlice& slice, std::size_t power) {
    return slice.reactionLengths[power];
}
inline std::uint64_t reactions_of(world::Creatures& creatures) {
    return creatures.counts().reactions;
}
inline std::uint64_t limits_of(world::Creatures& creatures) {
    return creatures.counts().reactionLimits;
}
inline bool pushed(const world::HitResult& hit) {
    return hit.push.has_value();
}
inline bool push_for(world::Creatures& creatures, world::Entity& creature, const gamedata::DamageImpact& impact, const state::WorldPosition& source,
              std::chrono::milliseconds length) {
    return creatures.impact(creature, impact, source, source, "Test_Push", length);
}
#else
inline std::optional<world::CreatureAction> idle_of(const world::Entity&) {
    return std::nullopt;
}
inline std::optional<world::WorldClock::time_point> stun_of(const world::Entity&) {
    return std::nullopt;
}
inline std::optional<std::uint32_t> idle_action_of(const gamedata::CreatureSlice&) {
    return std::nullopt;
}
inline float reaction_length_of(const gamedata::CreatureSlice&, std::size_t) {
    return -1.0F;
}
inline std::uint64_t reactions_of(world::Creatures&) {
    return 0;
}
inline std::uint64_t limits_of(world::Creatures&) {
    return 0;
}
inline bool pushed(const world::HitResult&) {
    return true;
}
inline bool push_for(world::Creatures& creatures, world::Entity& creature, const gamedata::DamageImpact& impact, const state::WorldPosition& source,
              std::chrono::milliseconds) {
    return creatures.impact(creature, impact, source, source, "Test_Push");
}
#endif

inline Fragment decode_movement(const Bytes& bytes) {
    Fragment part;
    part.type = 11;
    std::size_t consumed = 0;
    if (!detail::read_state(bytes, consumed, part) || consumed != bytes.size()) {
        throw std::runtime_error("user test 2: a movement state did not decode");
    }
    return part;
}

// ---- the world of the cases UX2 to UX6 ----------------------------------------------------------------------------------------

/** The actions of the wolf slice of the shop (shop_fixture.h) and the action Idle of these cases. */
inline constexpr int kReactionIndex = 1;
inline constexpr int kBiteIndex = 3;
inline constexpr int kMoveIndex = 4;
inline constexpr int kIdleIndex = 5;
/** The made-up lengths of the reaction fragments r_R0_F to r_R4_F of the slice (seconds). */
inline constexpr float kReactionLengths[5] = {0.5F, 0.7F, 1.2F, 0.9F, 2.0F};

/** The wolf slice of the shop with the action Idle and the reaction lengths (the members of the tool of user test 2). */
inline gamedata::CreatureSlice idle_wolf_slice(bool withIdle = true) {
    gamedata::CreatureSlice slice = shop::wolf_slice();
    if (withIdle) {
        slice.actions.push_back({"Idle", "Idle", 0, false});
    }
#ifndef SOULWARDEN_U2_BASELINE
    for (std::size_t power = 0; power < 5; ++power) {
        slice.reactionLengths[power] = kReactionLengths[power];
    }
#endif
    return slice;
}

inline gamedata::CreatureKind kind_of(const char* id, const char* type, std::uint32_t vitalsId, std::uint32_t body = 0) {
    gamedata::CreatureKind kind = shop::wolf_kind();
    kind.id = id;
    kind.vitals = id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = vitalsId + 1;
    kind.name = id;
    kind.vitalsName = id;
    kind.creatureType = type;
    kind.bodies = {{body, 100}};
    return kind;
}

/** A damage row of the cases with an impact and the reaction cells of the tool of user test 2. */
inline gamedata::DamageRow reaction_row(const char* id, const char* attack, std::optional<gamedata::DamageImpact> impact, bool none,
                                 std::int32_t power, std::int32_t stun) {
    gamedata::DamageRow row{id, attack, 1, 0.1F, 0.0F};
    row.impact = impact;
#ifndef SOULWARDEN_U2_BASELINE
    gamedata::DamageReaction reaction;
    reaction.none = none;
    reaction.power = 0;
    reaction.hitStun = 15;
    reaction.creaturePower = power;
    reaction.creatureHitStun = stun;
    row.reaction = reaction;
#else
    (void)none;
    (void)power;
    (void)stun;
#endif
    return row;
}

/** The shop of the motion cases with the idle slice, three creature types, and rows with reaction cells. */
struct IdleShop : MotionShop {
    explicit IdleShop(bool withIdle = true) {
        // The kinds: a Solo wolf, an Elite and a Boss of the same slice, and a Solo wolf of a slice with no idle action.
        if (!host.creatures.set_full({idle_wolf_slice(withIdle), idle_wolf_slice(false)},
                                     {kind_of("Test_Wolf", "Solo", 0x21112222), kind_of("Test_Elite", "Elite", 0x31112222),
                                      kind_of("Test_Boss", "Boss", 0x41112222), kind_of("Test_Plain", "Solo", 0x51112222, 1)},
                                     {}, {{1, shop::kWolfHealth, 100.0F, 50.0F}})) {
            throw std::runtime_error("user test 2: the creature data is not valid: " + host.creatures.error());
        }
        gamedata::CombatData data = motion_combat();
        data.damageRows.push_back(reaction_row("Test_BowLight", "Light", impact_of(0.0F, 0.0F, 0.5F), false, 2, 18));
        data.damageRows.push_back(reaction_row("Test_BowHeavy", "Heavy", impact_of(0.0F, 0.0F, 1.0F), false, 3, 22));
        data.damageRows.push_back(reaction_row("Test_Decay", "Light", impact_of(0.5F, 0.85F, 1.0F), false, 2, 0));
        data.damageRows.push_back(reaction_row("Test_NoReaction", "Ability", impact_of(0.0F, 0.0F, 1.0F), true, 2, 0));
        data.damageRows.push_back(reaction_row("Test_Power2", "Ability", std::nullopt, false, 2, 0));
        data.damageRows.push_back(reaction_row("Test_Power0", "Ability", std::nullopt, false, 0, 0));
        data.damageRows.push_back(reaction_row("Test_Stun", "Light", std::nullopt, false, 2, 60));
        if (!host.combat.set(data)) {
            throw std::runtime_error("user test 2: the combat tables are not valid: " + host.combat.error());
        }
        host.world.set_creature_behavior(false);
        hold("TestAxe");
    }
    world::Entity* spawn(const char* kind, float east, float north = 0.0F) {
        return host.world.creatures().spawn(kind, at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
    }
    /** A hit of A with a row of the cases through the combat system (Combat::strike). */
    world::HitResult hit(world::Entity& target, const char* row) {
        const gamedata::WeaponRow* axe = host.combat.weapon_of("TestAxe");
        const world::HitWeapon use{"TestAxe", world::paperdoll::kMainHand1, axe, 100, {}};
        return world().combat().strike(*character(), target, use, *host.combat.row_index(row), 1.0F, "a test hit");
    }
};


} // namespace soulwarden::backend::tests::idle

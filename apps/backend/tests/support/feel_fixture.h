#pragma once

// The world of the cases of stage U3 (combat_feel_tests.cpp: CF1 to CF11): the wolf slice of user test 2 with a reaction table (a fragment
// for each side and power, a HitStun clip, a turn), an additive reaction in its own scope, and a Homing clip on its bite; the kinds Solo,
// Solo+, and a promoted elite Solo+; the damage rows of a light, a heavy, and a pull hit. All ids, names, and values are made up.

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/check.h"
#include "support/idle_fixture.h"
#include "support/motion_fixture.h"

namespace soulwarden::backend::tests::feel {

using namespace idle;

/** The action Reaction_Additive of the slice (after the action Idle). */
inline constexpr int kAdditiveIndex = 6;
/** The scope of the additive fragment (the wolf of the data: 4, "Additive"). */
inline constexpr std::uint32_t kAdditiveScope = 4;
/** The fragments of the rows of power 2 and 3 (front, back, left, right), and of the additive table. */
inline constexpr std::uint32_t kFront2 = 15;
inline constexpr std::uint32_t kBack2 = 16;
inline constexpr std::uint32_t kLeft2 = 17;
inline constexpr std::uint32_t kRight2 = 18;
inline constexpr std::uint32_t kFront3 = 26;
inline constexpr std::uint32_t kBack3 = 27;
inline constexpr std::uint32_t kLeft3 = 28;
inline constexpr std::uint32_t kRight3 = 29;
inline constexpr std::uint32_t kAddFront = 35;
inline constexpr std::uint32_t kAddRight = 38;
/** The length of the rows of power 2 (no HitStun clip) and of power 3 (a HitStun clip at 0.183 s: the Inquisitor Cleric of the data). */
inline constexpr float kLength2 = 0.9F;
inline constexpr float kLength3 = 1.333F;
inline constexpr float kClip3 = 0.183333F;
/** The length of the additive fragment (its action ends 0.2 s before the end: ExitOffsetSeconds). */
inline constexpr float kAdditiveLength = 0.9F;
/** The Homing clip of the bite: from 0.2 s, to 0.75 m from the target in 0.3 s, at most 6 m (the wolf lunge of the data, made shorter). */
inline constexpr float kHomingStart = 0.2F;
inline constexpr float kHomingDistance = 0.75F;
inline constexpr float kHomingDuration = 0.3F;

#ifndef SOULWARDEN_U3_BASELINE
inline gamedata::ReactionRow row_of(std::int32_t power, std::vector<gamedata::AngleRange> angles, std::uint32_t fragment, float length,
                                    float clip = -1.0F, bool align = false, float offset = 0.0F) {
    gamedata::ReactionRow row;
    row.power = power;
    row.angles = std::move(angles);
    row.fragment = static_cast<std::int32_t>(fragment);
    row.length = length;
    row.hitStun = clip;
    row.align = align;
    row.alignOffset = offset;
    return row;
}

/** The side conditions of the reaction tables of the data (damaged_front, _back, _left, _right; tolerance 0.01). */
inline std::vector<gamedata::AngleRange> front() {
    return {{0.0F, 45.0F, 0.01F}, {-45.0F, 0.0F, 0.01F}};
}
inline std::vector<gamedata::AngleRange> back() {
    return {{135.0F, 180.0F, 0.01F}, {-180.0F, -135.0F, 0.01F}};
}
inline std::vector<gamedata::AngleRange> left() {
    return {{-135.0F, -45.0F, 0.01F}};
}
inline std::vector<gamedata::AngleRange> right() {
    return {{45.0F, 135.0F, 0.01F}};
}

/** The members of stage U3 of the slice. */
inline void add_feel(gamedata::CreatureSlice& slice) {
    gamedata::ReactionTable table;
    table.rows = {row_of(2, front(), kFront2, kLength2), row_of(2, back(), kBack2, kLength2), row_of(2, left(), kLeft2, kLength2),
                  row_of(2, right(), kRight2, kLength2), row_of(3, front(), kFront3, kLength3, kClip3),
                  row_of(3, back(), kBack3, kLength3, kClip3), row_of(3, left(), kLeft3, kLength3, kClip3),
                  row_of(3, right(), kRight3, kLength3, kClip3, true, 45.0F),
                  // Power 4: a back row whose name the slice does not have (the tool gives the fragment of the action Reaction).
                  row_of(4, front(), 79, 2.0F), row_of(4, back(), 15, 1.2F)};
    table.motion = {{"Default", 1.0F, 1.0F}, {"PromotedElite", 1.0F, 0.15F}};
    table.exit = 0.0F;
    table.largestPower = 4;
    slice.reactionTable = table;
    gamedata::AdditiveReaction additive;
    additive.rows = {row_of(-1, front(), kAddFront, kAdditiveLength), row_of(-1, back(), 36, kAdditiveLength),
                     row_of(-1, left(), 37, kAdditiveLength), row_of(-1, right(), kAddRight, kAdditiveLength)};
    additive.motion = {{"Default", 1.0F, 1.0F}, {"PromotedElite", 1.0F, 0.0F}, {"ReactionLimiter_Active", 1.0F, 0.0F}};
    additive.exit = 0.2F;
    additive.largestPower = -1;
    additive.action = kAdditiveIndex;
    additive.scope = kAdditiveScope;
    slice.additive = additive;
    gamedata::HomingClip clip;
    clip.start = kHomingStart;
    clip.duration = 0.4F;
    clip.moveToTarget = true;
    clip.moveToDistance = kHomingDistance;
    clip.moveToDuration = kHomingDuration;
    clip.maxMoveDistance = 6.0F;
    clip.turnRate = 600.0F;
    clip.radius = 10.0F;
    clip.maxAngle = 90.0F;
    clip.height = 5.0F;
    clip.useRadius = true;
    slice.attacks.front().homing = {clip};
}
inline void promote(gamedata::CreatureKind& kind) {
    kind.promotedElite = true;
}
/** @return The scope and the fragment of the additive reaction that plays, or no value. */
inline std::optional<std::pair<std::uint32_t, std::uint32_t>> additive_of(const world::Entity& creature) {
    if (!creature.creature.additive) {
        return std::nullopt;
    }
    return std::make_pair(creature.creature.additive->scope, creature.creature.additive->record.fragment);
}
inline std::uint64_t additive_count(world::Creatures& creatures) {
    return creatures.counts().additiveReactions;
}
/** @return True when the forced movement of the creature is the push of an additive reaction (it does not hold the behavior). */
inline bool additive_push(const world::Entity& creature) {
    return creature.creature.forced && creature.creature.forced->additive;
}
/** @return The pushes of an additive reaction that did not start because the push of a full reaction runs. */
inline std::uint64_t kept_full(world::Creatures& creatures) {
    return creatures.forced().counts().keptFull;
}
#else
inline void add_feel(gamedata::CreatureSlice&) {}
inline void promote(gamedata::CreatureKind&) {}
inline std::optional<std::pair<std::uint32_t, std::uint32_t>> additive_of(const world::Entity&) {
    return std::nullopt;
}
inline std::uint64_t additive_count(world::Creatures&) {
    return 0;
}
inline bool additive_push(const world::Entity&) {
    return false;
}
inline std::uint64_t kept_full(world::Creatures&) {
    return 0;
}
#endif

/** The wolf slice of user test 2 (idle_wolf_slice) with the members of stage U3, the action Reaction_Additive, and a bite of reach 1.5 m. */
inline gamedata::CreatureSlice feel_slice() {
    gamedata::CreatureSlice slice = idle_wolf_slice(true);
    slice.actions.push_back({"Reaction_Additive", "r_R0_F", static_cast<std::int32_t>(kAddFront), true});
    slice.attacks.front().contacts.front().reach = 1.5F;
    add_feel(slice);
    return slice;
}

/** The world of the cases: a Solo wolf, a Solo+ wolf, and a promoted elite Solo+ wolf of the U3 slice; light, heavy, and pull rows. */
struct FeelShop : MotionShop {
    explicit FeelShop(bool observe = false) : MotionShop(observe) {
        gamedata::CreatureKind promoted = kind_of("Test_Promoted", "Solo+", 0x61112222);
        promote(promoted);
        if (!host.creatures.set_full({feel_slice()},
                                     {kind_of("Test_Wolf", "Solo", 0x21112222), kind_of("Test_Plus", "Solo+", 0x31112222), promoted},
                                     {}, {{1, shop::kWolfHealth * 100.0F, 100.0F, 50.0F}})) {
            throw std::runtime_error("stage U3: the creature data is not valid: " + host.creatures.error());
        }
        gamedata::CombatData data = motion_combat();
        data.damageRows.push_back(reaction_row("Test_FeelLight", "Light", impact_of(0.0F, 0.0F, 0.5F), false, 2, 18));
        data.damageRows.push_back(reaction_row("Test_FeelHeavy", "Heavy", impact_of(0.0F, 0.0F, 1.0F), false, 3, 22));
        data.damageRows.push_back(reaction_row("Test_FeelShort", "Heavy", impact_of(0.0F, 0.0F, 1.0F), false, 3, 5));
        data.damageRows.push_back(reaction_row("Test_FeelPull", "Heavy", impact_of(-2.0F, 0.8F, -2.0F), false, 3, 22));
        data.damageRows.push_back(reaction_row("Test_FeelPower5", "Ability", std::nullopt, false, 5, 0));
        data.damageRows.push_back(reaction_row("Test_FeelPower4", "Ability", std::nullopt, false, 4, 0));
        if (!host.combat.set(data)) {
            throw std::runtime_error("stage U3: the combat tables are not valid: " + host.combat.error());
        }
        host.world.set_creature_behavior(false);
        hold("TestAxe");
    }
    /** A creature of a kind at a place (east, north of A) with a direction (degrees, 0 north, 90 west). */
    world::Entity* spawn(const char* kind, float east, float north, float yaw) {
        return host.world.creatures().spawn(kind, at(kPlace.x + east, kPlace.y + north), yaw).entity;
    }
    /** A hit of A with a row through the combat system (Combat::strike), A at a place first (the source of the hit). */
    world::HitResult hit_from(world::Entity& target, const char* row, float east, float north) {
        world::Entity* own = character();
        host.world.set_position(own->id, at(kPlace.x + east, kPlace.y + north), false);
        const gamedata::WeaponRow* axe = host.combat.weapon_of("TestAxe");
        const world::HitWeapon use{"TestAxe", world::paperdoll::kMainHand1, axe, 100, {}};
        return world().combat().strike(*own, target, use, *host.combat.row_index(row), 1.0F, "a test hit");
    }
    /** Ticks of the world for some seconds (no input). */
    void wait(float seconds) {
        const int ticks = static_cast<int>(std::lround(seconds * 30.0F));
        for (int tick = 0; tick < ticks; ++tick) {
            host.clock.advance(kTick);
            host.tick();
        }
    }
};

} // namespace soulwarden::backend::tests::feel

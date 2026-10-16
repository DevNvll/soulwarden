#pragma once

// The world of the cases of the creature shots (stage 3.4f-A, tests/ranged_tests.cpp; stage 3.4f-B, tests/ranged_b_tests.cpp and
// tests/review_ranged_tests.cpp): the test archer (a bash and the basic shot FireBow), its arrow, the player capsule, the world with the
// behavior on, and the helpers of the records. The accounts, the characters, the creature kinds, the rows, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/progression_tables.h"
#include "support/shop_fixture.h"
#include "world/behavior.h"
#include "world/projectiles.h"

namespace soulwarden::backend::tests::range {

using namespace shop;

/** The actions of the test archer: the melee bash (3) and the basic shot FireBow (5). */
inline constexpr std::uint32_t kBash = 3;
inline constexpr std::uint32_t kFireBow = 5;
inline constexpr float kRelease = 1.33333F;
inline constexpr float kShotLength = 2.46667F;
inline constexpr float kArrowSpeed = 50.0F;
inline constexpr float kArrowLife = 1.5F;
inline constexpr std::uint32_t kArrowSlot = 1;
inline constexpr float kPlayerRadius = 0.41F;
inline constexpr float kPlayerSegment = 0.8F;
inline constexpr std::uint32_t kAttackIndex = 7;
inline constexpr float kArcherDamage = 100.0F;

inline std::uint32_t crc_of(const char* text) {
    return gamedata::progression_id(text);
}

inline gamedata::RangedRelease arrow_release() {
    gamedata::RangedRelease release;
    release.time = kRelease;
    release.profile = "AI_Bow";
    release.attack = "Basic";
    release.attackIndex = kAttackIndex;
    release.ammo = "Arrow_AI";
    release.ammoKey = crc_of("Arrow_AI");
    release.projectile = 0;
    release.table = "TestBowDamageTable";
    release.tableKey = crc_of("TestBowDamageTable");
    release.rowIndex = 0;
    release.damage.row = "Test_Bow_Shot";
    release.damage.factor = 0.35F;
    release.damage.type = "Slash";
    release.damage.wire = 4;
    release.damage.category = gamedata::DamageCategory::physical;
    release.lead = true;
    release.aimSpeed = 50.0F;
    release.leadAngle = 10.0F;
    release.aimAngle = 45.0F;
    release.missMin = 1.5F;
    release.missMax = 3.0F;
    release.range = 100.0F;
    release.radius = 0.0F;
    release.accelDelay = 0.0F;
    return release;
}

inline gamedata::ProjectileSliceData arrow_slice() {
    gamedata::ProjectileSliceData arrow;
    arrow.path = "slices/test/arrow.dynamicslice";
    arrow.asset.fill(0x61);
    arrow.subId = 2;
    arrow.root = 0x6C1BE29704877324ULL;
    arrow.slot = kArrowSlot;
    arrow.speed = kArrowSpeed;
    arrow.extraSpeed = 10.0F;
    arrow.timeForMaxSpeed = 1.5F;
    arrow.accel = {0.0F, 0.0F, -2.0F};
    arrow.flyingLifetime = kArrowLife;
    arrow.stuckLifetime = 8.0F;
    return arrow;
}

/** The archer of the cases: the wolf slice of the shop with a bash and the basic shot (the values of the Evil Knight archer). */
inline gamedata::CreatureSlice archer_slice(bool melee = true) {
    gamedata::CreatureSlice slice = wolf_slice();
    slice.path = "slices/test/archer.dynamicslice";
    slice.asset.fill(0x31);
    slice.root = 0x1000000000000031ULL;
    slice.actions = {{"Idle_No_Task", "Idle", 0, false},       {"Reaction", "r_R2_F", 15, true}, {"Death", "r_Death", 4, true},
                     {"Attack_Bash", "Charged_Melee", 7, false}, {"Move_Combat", "Move_Combat", 11, false},
                     {"FireBow", "Fire_Bow", 35, false}};
    slice.behavior->sightAngle = 75.0F;
    slice.behavior->sightDistance = 12.0F;
    slice.behavior->runSpeed = 5.5F;
    slice.behavior->walkSpeed = 5.5F;
    slice.attacks.clear();
    if (melee) {
        gamedata::CreatureContact bash;
        bash.time = 0.5F;
        bash.reach = 2.2F;
        bash.factor = 0.4F;
        bash.type = "Strike";
        bash.wire = 6;
        bash.category = gamedata::DamageCategory::physical;
        bash.row = "Test_Bash";
        slice.attacks = {gamedata::CreatureAttack{kBash, 1.4F, {bash}}};
    }
    slice.ranged = {gamedata::CreatureRangedAttack{kFireBow, kShotLength, {arrow_release()}}};
    return slice;
}

inline gamedata::CreatureKind archer_kind() {
    gamedata::CreatureKind kind = wolf_kind();
    kind.id = "Test_Archer";
    kind.vitals = kind.id;
    kind.category = "Test_Archer_Category";
    kind.vitalsId = 0x31112222;
    kind.categoryId = 0x31112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Human";
    kind.bodies = {{0, 100}};
    return kind;
}

inline std::vector<gamedata::CreatureLevel> archer_levels() {
    return {{1, kWolfHealth, kArcherDamage, 50.0F}};
}

/** The world of a case: the shop with the archer data, the behavior on, A with a client, B (enter_b) next to A. */
struct Range : Shop {
    explicit Range(bool observe = true, bool melee = true, std::vector<gamedata::SpawnPoint> points = {}) : Shop(false, false, observe) {
        if (!host.creatures.set_full({archer_slice(melee)}, {archer_kind()}, std::move(points), archer_levels(), {}, {arrow_slice()},
                                     gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}})) {
            throw std::runtime_error("ranged test: the creature data is not valid: " + host.creatures.error());
        }
        host.world.set_creature_behavior(true);
        walker.idle(5);
    }
    world::Behavior& behavior() {
        return host.world.creatures().behavior();
    }
    world::Projectiles& projectiles() {
        return host.world.projectiles();
    }
    const world::CreatureShotCounts& shots() {
        return projectiles().creature_counts();
    }
    std::uint64_t ends(world::CreatureShotEnd end) {
        return shots().ends[static_cast<std::size_t>(end)];
    }
    /** An archer `east` m east of A (and `north` m to the north), its face to the west. */
    world::Entity* archer(float east, float north = 0.0F) {
        world::Entity* entity = host.world.creatures().spawn("Test_Archer", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
        return entity;
    }
    /** True after enter_b: each step also gives the datagrams of the host to the client of B. */
    bool withB{false};
    void enter_both() {
        enter_b();
        withB = true;
    }
    /** One step of A (33 ms), and the exchange of the client of B. */
    void step(int count = 1) {
        for (int index = 0; index < count; ++index) {
            walker.idle(1);
            if (withB) {
                b.exchange(host);
            }
        }
    }
    /** Steps until the condition holds, at most `limit` steps. @return True when it holds. */
    bool until(const std::function<bool()>& condition, int limit = 300) {
        for (int index = 0; index < limit; ++index) {
            if (condition()) {
                return true;
            }
            step();
        }
        return condition();
    }
    /** Steps to the time `time` of the world clock. */
    void to(world::WorldClock::time_point time) {
        while (host.clock.now() < time) {
            step();
        }
    }
    world::WorldClock::time_point now() {
        return host.clock.now();
    }
    std::size_t projectile_entities() {
        return static_cast<std::size_t>(std::count_if(host.world.entities().begin(), host.world.entities().end(), [](const auto& entry) {
            return entry.second.kind == world::EntityKind::projectile;
        }));
    }
};

inline std::chrono::microseconds seconds(float value) {
    return std::chrono::microseconds(static_cast<long long>(std::lround(value * 1.0e6F)));
}

/** @return The interest number of an entity in a session of the host, or 0. */
inline std::uint32_t interest_in(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** @return The records of an interest number from the message `from` on: the fragments of each record, in their sequence. */
inline std::vector<Fragment> fragments_for(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest == interest) {
                found.insert(found.end(), record.fragments.begin(), record.fragments.end());
            }
        }
    }
    return found;
}

/** @return The number of the stops of an interest number from the message `from` on. */
inline std::size_t stops_of(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == 8) {
            const Bundle bundle = read_bundle(client.got[index].body);
            count += static_cast<std::size_t>(std::count(bundle.stops.begin(), bundle.stops.end(), interest));
        }
    }
    return count;
}

/** @return The Projectile states (slot 1, type 16) of the records of an interest number. */
inline std::vector<Fragment> projectile_states(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (const Fragment& fragment : fragments_for(client, interest, from)) {
        if (fragment.slot == kArrowSlot && fragment.type == 16) {
            found.push_back(fragment);
        }
    }
    return found;
}

inline std::uint64_t field_u64(const Fragment& fragment, unsigned field) {
    server::hub::Reader reader(fragment.fields.at({0, field}));
    return reader.u64();
}

/** The projectile entity of the newest flight of the system, or 0. */
inline world::EntityId newest_projectile(Range& range) {
    world::EntityId id = 0;
    std::uint64_t number = 0;
    for (const auto& [key, entity] : range.host.world.entities()) {
        if (entity.kind == world::EntityKind::projectile && entity.shot.number >= number) {
            number = entity.shot.number;
            id = key;
        }
    }
    return id;
}

/**
 * The test caster (stage 3.4f-B): the values of the Skeleton Mage of the retail data (the action Cast_One_Projectile with one release
 * at 0.5 s of an action of 1.4 s, the attack AI_Mage_Sphere/Basic of the index 125, no ammunition, the slice mage_projectile_slow of
 * 20 m/s with no fall and a flight of 3 s, the row 0 of SkeletonMageDamageTable with the factor 0.35, Arcane), with made-up assets.
 */
inline constexpr std::uint32_t kCast = 6;
inline constexpr float kCastRelease = 0.5F;
inline constexpr float kCastLength = 1.4F;
inline constexpr float kSlowSpeed = 20.0F;
inline constexpr std::uint32_t kMageIndex = 125;
inline constexpr std::uint32_t kSlowSlot = 1;

inline gamedata::RangedRelease mage_release() {
    gamedata::RangedRelease release;
    release.time = kCastRelease;
    release.profile = "AI_Mage_Sphere";
    release.attack = "Basic";
    release.attackIndex = kMageIndex;
    release.ammo = "";
    release.ammoKey = 0;
    release.projectile = 1;
    release.sliceOverride = true;
    release.table = "TestMageDamageTable";
    release.tableKey = crc_of("TestMageDamageTable");
    release.rowIndex = 0;
    release.damage.row = "Test_Mage_CastOne_Projectile";
    release.damage.factor = 0.35F;
    release.damage.type = "Arcane";
    release.damage.wire = 8;
    release.damage.category = gamedata::DamageCategory::elemental;
    release.lead = true;
    release.aimSpeed = kSlowSpeed;
    release.leadAngle = 10.0F;
    release.aimAngle = 45.0F;
    release.missMin = 1.5F;
    release.missMax = 4.0F;
    release.range = 100.0F;
    release.radius = 0.0F;
    release.accelDelay = 0.0F;
    return release;
}

inline gamedata::ProjectileSliceData slow_slice() {
    gamedata::ProjectileSliceData ball;
    ball.path = "slices/test/mage_projectile_slow.dynamicslice";
    ball.asset.fill(0x62);
    ball.subId = 2;
    ball.root = 0x000005CDF2A38B9CULL;
    ball.slot = kSlowSlot;
    ball.speed = kSlowSpeed;
    ball.accel = {0.0F, 0.0F, 0.0F};
    ball.flyingLifetime = 3.0F;
    ball.stuckLifetime = 0.0F;
    return ball;
}

/** The caster of the cases: the slice of the archer with the cast in the place of FireBow and no melee attack (a mage). */
inline gamedata::CreatureSlice mage_slice() {
    gamedata::CreatureSlice slice = archer_slice(false);
    slice.path = "slices/test/mage.dynamicslice";
    slice.asset.fill(0x32);
    slice.root = 0x1000000000000032ULL;
    slice.actions.push_back({"Cast_One_Projectile", "PRIEST_Cast_One_Projectile", 102, false});
    slice.behavior->runSpeed = 1.25F;
    slice.behavior->walkSpeed = 1.25F;
    slice.ranged = {gamedata::CreatureRangedAttack{kCast, kCastLength, {mage_release()}}};
    return slice;
}

inline gamedata::CreatureKind mage_kind() {
    gamedata::CreatureKind kind = archer_kind();
    kind.id = "Test_Mage";
    kind.vitals = kind.id;
    kind.category = "Test_Mage_Category";
    kind.vitalsId = 0x32112222;
    kind.categoryId = 0x32112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.bodies = {{1, 100}};
    return kind;
}

/** Gives a host the archer (slice 0) and the mage (slice 1), their arrow and ball, and the capsule of the player. */
template <class Host>
void set_battle(Host& host, std::vector<gamedata::SpawnPoint> points = {}) {
    if (!host.creatures.set_full({archer_slice(), mage_slice()}, {archer_kind(), mage_kind()}, std::move(points), archer_levels(), {},
                                 {arrow_slice(), slow_slice()},
                                 gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}})) {
        throw std::runtime_error("ranged test: the creature data with the mage is not valid: " + host.creatures.error());
    }
}

} // namespace soulwarden::backend::tests::range

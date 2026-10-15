// Cases of stage 3.3b-S3: the projectiles of the players (world/projectiles.h): the shot item of a client, the launch and its
// rules, the ledger (one shot for each press), the flight on the world clock, the hit on the capsule of a creature, a wall, a
// miss, the hit near the muzzle, the fire staff (the spell row and the one mana path), two sessions, and the load. The data
// has made-up profiles, slices, and rows with the values of the rules of the game data (the bow: range 100 m, radius 0.15 m,
// near range 2 m, no acceleration in the first 5 m, speed 100 m/s, gravity 9.8; the fire ball: range 50 m, life time 0.5 s,
// no near range). All ids, names, and values are made up.
//
//   PJ1   the projectile data and the capsule of the creature data: the form of the tool, the refusals, the lookups
//   PJ2   the capsule of a creature (its axis, its direction, its feet) and the sweep of a segment with a radius
//   PJ3   a bow shot of a session: the launch, the flight, one later hit with the row of the shot and the ammunition factor
//   PJ4   a miss (the range), the life time, a wall of the navigation data (no damage behind it), the caster leaves
//   PJ5   the hit near the muzzle: accepted at the launch with no flight; a receiver that is not a creature with this
//         reference, that the session does not observe, or that is not near, and a profile with no near range: refused
//   PJ6   the rules of a launch: each refusal changes nothing (no projectile, no cost, no damage)
//   PJ7   the ledger: one shot for each press, 40 presses (more than the 32 local projectiles of the client), two fast shots
//         at one creature give two hits, a projectile hits one creature one time
//   PJ8   the fire staff: the row of the spell for a magic shot (row 0), the heavy hold, the life time, the one mana path
//   PJ9   two sessions (MP-10): the damage belongs to the caster; the observer gets the health of the creature and no number;
//         its resources do not change; its own shot is refused by its own rules
//   PJ10  the load: the flight looks only at the creatures near its segments; 4,000 far creatures add no look
//   PJ11  the projectile data tool refuses a source that is not complete (check_projectile_tool.py)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/projectile_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"
#include "world/projectiles.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using namespace projectile;
using nlohmann::json;
using std::chrono::milliseconds;

/** PJ1: the projectile data and the capsule of the creature data. */
void projectile_data(const Options& options) {
    gamedata::ProjectileData data;
    check(data.set(projectile_tables()) && data.loaded(), "the data of the cases is valid");
    const gamedata::RangedWeapon* bow = data.weapon("testbowrow");
    check(bow != nullptr && bow->profile == "TestBowProfile" && bow->ammoType == "Arrow", "the profile of a weapon (the case of the letters is ignored)");
    const gamedata::RangedAttack* basic = data.attack("TestBowProfile", crc("basic"));
    check(basic != nullptr && basic->maxRange == 100.0F && basic->nearRange == 2.0F && data.attack("TestBowProfile", crc("nothing")) == nullptr,
          "an attack by the CRC-32 of its lower-case name (the shot item of the client)");
    check(data.shots("TestBowProfile", crc("basic")).size() == 2 && data.ammo(crc("defaultarrow")) != nullptr
              && data.ammo(crc("defaultarrow"))->damageModifier == 1.2F && data.slice(0) != nullptr && data.slice(-1) == nullptr,
          "the shots of an attack, the ammunition by its key, and the slices");
    const auto refused = [](const char* what, const std::function<void(gamedata::ProjectileTables&)>& change) {
        gamedata::ProjectileTables tables = projectile_tables();
        change(tables);
        gamedata::ProjectileData wrong;
        const bool set = wrong.set(tables);
        if (set) {
            note(std::string("accepted: ") + what);
        }
        return !set && !wrong.loaded() && !wrong.error().empty();
    };
    bool all = true;
    all = refused("a shot of an attack that the profile does not have", [](auto& t) { t.shots[0].attack = "Unknown"; }) && all;
    all = refused("a shot with a slice outside the list", [](auto& t) { t.shots[5].slice = 9; }) && all;
    all = refused("a range that is not finite", [](auto& t) { t.attacks[0].maxRange = std::nanf(""); }) && all;
    all = refused("two attacks with one name in one profile", [](auto& t) { t.attacks[1].name = "basic"; }) && all;
    all = refused("a slice with a negative speed", [](auto& t) { t.slices[0].speed = -1.0F; }) && all;
    all = refused("an ammunition row with a slice outside the list", [](auto& t) { t.ammo[0].slice = 5; }) && all;
    check(all, "the data refuses a shot, a slice, a number, an attack, or an ammunition row that is not correct (as a whole)");
    // The file of the tool: a member with a wrong form, and a count that is not the size of its list.
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "projectile-data.json";
    json document{{"format", 1},
                  {"counts", {{"attacks", 1}, {"slices", 1}, {"ammo", 1}, {"weapons", 1}, {"shots", 1}}},
                  {"attacks", {{"P", "Basic", false, 100.0, 0.15, 2.0, 0.2, 5.0, 30.0, 80.0, 1.0, 0, true, true, {0.0, 0.0, -9.8}, 0}}},
                  {"slices", {{"slices/test/arrow.dynamicslice", 100.0, 25.0, 1.0, {0.0, 0.0, -9.8}, 10.0, 8.0}}},
                  {"ammo", {{"DefaultArrow", "Arrow", 1.2, 0}}},
                  {"weapons", {{"W", "P", "Arrow"}}},
                  {"shots", {{"P", "Basic", "Row", 0.0, -1, "", "", false, 12, true}}}};
    const auto load = [&path](const json& content, gamedata::ProjectileData& out) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return out.load(path.string());
    };
    gamedata::ProjectileData file;
    check(load(document, file) && file.tables().shots.size() == 1, "a file of the form of the tool loads");
    json wrongCount = document;
    wrongCount["counts"]["shots"] = 2;
    json wrongForm = document;
    wrongForm["attacks"][0][3] = "100";
    gamedata::ProjectileData other;
    check(!load(wrongCount, other) && !load(wrongForm, other) && !other.load((options.workDirectory / "no-such-file.json").string())
              && !other.loaded(),
          "a count that is not the size of its list, a number that is a text, and no file give no data (the backend starts with none)");
    // The capsule of a creature slice in the creature data file (format 3 with the member of stage 3.3b-S3).
    const json slice{{"path", "slices/test/wolf.dynamicslice"}, {"asset", "11111111-1111-1111-1111-111111111111"}, {"sub", 2},
                     {"root", "1000000000000011"}, {"interestRadius", 60.0}, {"movement", 5}, {"vitals", 9}, {"marker", 15},
                     {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                     {"components", json::array({json::array({"1000000000000011", "0000000000000A01", "Root/Vitals"})})},
                     {"capsule", {0.7, 0.9, {0.0, 1.0, 0.0}, {0.0, 0.1, 0.0}}}};
    json creatures{{"format", 3},
                   {"levels", {{18, 1690.875977, 616.41, 180.0}}},
                   {"slices", {slice}},
                   {"kinds", {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 1},
                               {"categoryId", 2}, {"name", "Test_Wolf"}, {"vitalsName", "Test_Wolf"}, {"family", "Wildlife"},
                               {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})}}}},
                   {"spawnPoints", json::array()},
                   {"routes", json::array()}};
    const auto path2 = options.workDirectory / "projectile-creatures.json";
    const auto creature_load = [&path2](const json& content, gamedata::CreatureData& out) {
        std::ofstream(path2, std::ios::binary | std::ios::trunc) << content.dump();
        return out.load(path2.string());
    };
    gamedata::CreatureData withCapsule;
    check(creature_load(creatures, withCapsule) && withCapsule.slices()[0].capsule && withCapsule.slices()[0].capsule->radius == 0.7F
              && withCapsule.slices()[0].capsule->axis[1] == 1.0F && near(withCapsule.slices()[0].capsule->offset[1], 0.1F),
          "a creature slice has the capsule of its character");
    json old = creatures;
    old["slices"][0].erase("capsule");
    json bad = creatures;
    bad["slices"][0]["capsule"][0] = 0.0;
    gamedata::CreatureData oldData;
    gamedata::CreatureData badData;
    check(creature_load(old, oldData) && !oldData.slices()[0].capsule && !creature_load(bad, badData),
          "a file of an older tool has no capsule (the default capsule), and a capsule with no radius is refused");
}

/** PJ2: the capsule of a creature and the sweep. */
void capsule_sweep(const Options&) {
    ProjectileShop shop;
    world::Entity* wolf = shop.wolf_east(10.0F);
    // The wolf looks west (yaw 90): the axis of its body is east to west; its lowest point is at its feet.
    world::Entity copy = *wolf;
    copy.creature.yaw = 90.0F;
    const world::WorldCapsule west = world::capsule_of(copy);
    check(near(west.a.z, kHeight + kWolfRadius) && near(west.b.z, kHeight + kWolfRadius) && near(std::fabs(west.a.x - west.b.x), kWolfLength)
              && near(west.a.y, west.b.y) && near(west.radius, kWolfRadius),
          "a body axis turned to the west: the segment is east to west, 0.5 m above the feet (its radius)");
    copy.creature.yaw = 0.0F;
    const world::WorldCapsule north = world::capsule_of(copy);
    check(near(std::fabs(north.a.y - north.b.y), kWolfLength) && near(north.a.x, north.b.x), "a body axis to the north: the segment is north to south");
    const state::WorldPosition from{kPlace.x, kPlace.y, kHeight + 0.6F};
    const auto hit = world::sweep(from, {kPlace.x + 20.0F, kPlace.y, kHeight + 0.6F}, west, 0.15F);
    check(hit && near(*hit * 20.0F, 10.0F - 0.5F - 0.5F - 0.15F, 0.02F), "a line to the east touches the end of the capsule (its half sphere) with the radius of the projectile");
    check(!world::sweep(from, {kPlace.x + 20.0F, kPlace.y, kHeight + 3.0F}, west, 0.15F), "a line that rises over the wolf does not touch it");
    check(!world::sweep(from, {kPlace.x + 5.0F, kPlace.y, kHeight + 0.6F}, west, 0.15F), "a segment that ends before the wolf does not touch it");
    const state::WorldPosition inside = west.a;
    check(world::sweep(inside, {inside.x + 1.0F, inside.y, inside.z}, west, 0.0F) == 0.0F, "a segment that starts in the capsule touches it at its start");
    world::WorldCapsule sphere{at(kPlace.x + 5.0F, kPlace.y, kHeight + 0.6F), at(kPlace.x + 5.0F, kPlace.y, kHeight + 0.6F), 0.5F};
    const auto ball = world::sweep(from, {kPlace.x + 10.0F, kPlace.y, kHeight + 0.6F}, sphere, 0.0F);
    check(ball && near(*ball * 10.0F, 4.5F, 0.01F), "a capsule with no axis is a sphere");
    const auto side = world::sweep({kPlace.x + 9.5F, kPlace.y - 3.0F, kHeight + 0.5F}, {kPlace.x + 9.5F, kPlace.y + 3.0F, kHeight + 0.5F}, west, 0.0F);
    check(side && near(*side * 6.0F, 2.5F, 0.02F), "a line across the body touches the cylinder of the capsule");
}

/** PJ3: a bow shot of the session of A: the launch, the flight, one later hit. */
void bow_hit(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(20.0F);
    const world::EntityId id = wolf->id;
    world::Entity* other = shop.wolf_east(3.0F, 10.0F);
    const world::EntityId otherId = other->id;
    const float health = wolf->vitals.health;
    const std::size_t from = shop.a.got.size();
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    check(shop.verdicts(world::ShotVerdict::accepted) == 1 && shop.host.world.projectiles().active().size() == 1,
          "the shot is accepted: one projectile in the air");
    check(shop.creature(id)->vitals.health == health, "no damage at the launch (the creature is 20 m away)");
    shop.walker.idle(3);
    check(shop.creature(id)->vitals.health == health, "no damage 0.1 s after the launch (the projectile flies 100 m/s)");
    shop.walker.idle(6);
    const float dealt = health - shop.creature(id)->vitals.health;
    check(dealt > 0.0F && shop.ends(world::ProjectileEnd::hit) == 1 && shop.host.world.projectiles().active().empty(),
          "about 0.2 s after the launch the projectile hits the wolf and ends");
    shop.walker.idle(30);
    check(health - shop.creature(id)->vitals.health == dealt && shop.counts().damage == 1, "one hit: the wolf takes no second damage");
    // The damage: the row of the shot times the damage modifier of the ammunition (1.2), through Combat::strike.
    const float before = shop.creature(otherId)->vitals.health;
    shop.strike(*shop.creature(otherId), "TestBowLight", "TestBow");
    const float plain = before - shop.creature(otherId)->vitals.health;
    note("the shot did " + std::to_string(dealt) + ", the row with no ammunition " + std::to_string(plain));
    check(near(dealt, plain * 1.2F, 0.05F), "the damage is the row of the shot times the ammunition modifier 1.2");
    const auto numbers = damage_entries(shop.a, from);
    check(numbers.size() == 1 && near(numbers[0][0].second, dealt, 0.05F), "the client of A gets one damage number");
}

/** PJ4: a miss, the life time, a wall, the caster leaves. */
void miss_and_wall(const Options& options) {
    {
        ProjectileShop shop;
        shop.hold("TestBow");
        // Up into the sky: no creature, no wall; the range of the profile ends it (100 m at 100 m/s: 1 s).
        shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", {0.0F, 0.0F, 1.0F}));
        shop.walker.idle(20);
        check(shop.host.world.projectiles().active().size() == 1, "0.7 s after the launch the projectile is in the air");
        shop.walker.idle(20);
        check(shop.ends(world::ProjectileEnd::range) == 1 && shop.counts().damage == 0, "it ends at its range: a miss with no damage");
        // A shot, then the caster leaves the world: the projectile ends with no hit.
        world::Entity* wolf = shop.wolf_east(60.0F);
        const float health = wolf->vitals.health;
        const world::EntityId id = wolf->id;
        shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
        check(shop.world().leave(shop.character()->owner), "setup: the caster leaves the world");
        shop.host.clock.advance(milliseconds(1000));
        shop.host.world.tick();
        check(shop.ends(world::ProjectileEnd::caster) == 1 && shop.creature(id)->vitals.health == health,
              "the projectile of a caster that left ends with no hit");
    }
    {
        ProjectileShop shop;
        shop.hold("TestStaff");
        // The fire ball: 0.5 s of life at 100 m/s (50 m), the same as its range; a wolf at 70 m takes nothing.
        world::Entity* wolf = shop.wolf_east(70.0F);
        const float health = wolf->vitals.health;
        shop.shoot(ProjectileShop::item("basic", nullptr, 0.0F, nullptr));
        shop.walker.idle(30);
        check(shop.ends(world::ProjectileEnd::range) + shop.ends(world::ProjectileEnd::lifetime) == 1 && wolf->vitals.health == health,
              "the fire ball ends after 50 m (its life time and its range): the wolf at 70 m takes nothing");
    }
    {
        // A wall of the navigation data 10 m east of A (cells with a solid 3 m high), a wolf behind it at 20 m.
        ProjectileShop shop;
        TestNavRegion region;
        region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
            const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
            if (px >= 2010.0F && px < 2010.5F) {
                floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
            }
        };
        const auto folder = options.workDirectory / "projectile-wall";
        region.write(folder);
        check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation data with the wall loads");
        shop.walker.idle(20);
        shop.hold("TestBow");
        world::Entity* wolf = shop.wolf_east(20.0F);
        const world::EntityId id = wolf->id;
        const float health = wolf->vitals.health;
        shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
        shop.walker.idle(20);
        check(shop.ends(world::ProjectileEnd::wall) == 1 && shop.creature(id)->vitals.health == health && shop.counts().damage == 0,
              "the wall stops the projectile: the wolf behind it takes no damage");
        check(shop.world().navigation().line_stop({kPlace.x, kPlace.y, kHeight + 1.0F}, {kPlace.x + 12.0F, kPlace.y, kHeight + 1.0F}).has_value(),
              "the line test of the navigation finds the wall");
    }
}

/** PJ5: the hit near the muzzle. */
void near_hit(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(1.8F);
    const world::EntityId id = wolf->id;
    const float health = wolf->vitals.health;
    const auto with_receiver = [&](world::Entity& target, std::uint64_t facet) {
        InputShotItem shot = ProjectileShop::item("basic", "TestBowLight");
        shot.receiverActor = target.actor.bytes;
        shot.receiver = target.id;
        shot.facet = facet;
        shot.point = {target.position.x - 0.6F, target.position.y, kHeight + 0.6F};
        return shot;
    };
    const std::uint64_t facet = wolf->creature.body->target(wolf->id, wolf->creature.body->components[1]);
    shop.shoot(with_receiver(*wolf, facet));
    const float dealt = health - shop.creature(id)->vitals.health;
    check(shop.verdicts(world::ShotVerdict::accepted) == 1 && dealt > 0.0F && shop.ends(world::ProjectileEnd::nearHit) == 1
              && shop.host.world.projectiles().active().empty(),
          "a receiver near the muzzle takes the hit at the launch, and no projectile flies");
    shop.walker.idle(30);
    check(health - shop.creature(id)->vitals.health == dealt, "one hit");
    // Refusals: a wrong facet target, a creature that is not near (6 m), a creature that the session does not observe.
    shop.shoot(with_receiver(*shop.creature(id), facet + 1));
    world::Entity* far = shop.wolf_east(6.0F, 3.0F);
    const std::uint64_t farFacet = far->creature.body->target(far->id, far->creature.body->components[1]);
    shop.shoot(with_receiver(*far, farFacet));
    world::Entity* hidden = shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 1.5F, kPlace.y + 400.0F), 90.0F).entity;
    const std::uint64_t hiddenFacet = hidden->creature.body->target(hidden->id, hidden->creature.body->components[1]);
    shop.shoot(with_receiver(*hidden, hiddenFacet));
    note("refusals: bad reference " + std::to_string(shop.verdicts(world::ShotVerdict::badReference)) + ", not near "
         + std::to_string(shop.verdicts(world::ShotVerdict::nearTooFar)) + ", not observed " + std::to_string(shop.verdicts(world::ShotVerdict::notObserved)));
    check(shop.verdicts(world::ShotVerdict::badReference) == 1 && shop.verdicts(world::ShotVerdict::nearTooFar) == 1
              && shop.verdicts(world::ShotVerdict::notObserved) == 1 && shop.counts().damage == 1,
          "a wrong reference, a creature that is not near, and a creature that the session does not observe are refused (no hit)");
    // The fire staff has no near range: a receiver in its shot item is refused.
    shop.hold("TestStaff");
    InputShotItem fire = with_receiver(*shop.creature(id), facet);
    fire.row = 0;
    fire.ammo = 0;
    shop.shoot(fire);
    check(shop.verdicts(world::ShotVerdict::badReference) == 2, "a profile with no near range refuses a receiver");
}

/** PJ6: the rules of a launch. */
void launch_rules(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    // A shot item with no press of the attack input.
    InputEntry lone = shop.walker.entry();
    lone.shotItems = {ProjectileShop::item("basic", "TestBowLight")};
    shop.walker.send(lone);
    check(shop.verdicts(world::ShotVerdict::noAttack) == 1, "a shot with no press of the attack input is refused");
    shop.shoot(ProjectileShop::item("nothing", "TestBowLight"));
    shop.shoot(ProjectileShop::item("basic", "TestBowHeavy"));
    shop.shoot(ProjectileShop::item("basic", "TestBowLight", 100.0F));
    shop.shoot(ProjectileShop::item("basic_heavy", "TestBowHeavy", 100.0F), 5);
    shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultShot"));
    InputShotItem far = ProjectileShop::item("basic", "TestBowLight");
    far.origin = {kPlace.x + 10.0F, kPlace.y, kHeight + 0.6F};
    shop.shoot(far);
    shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", {2.0F, 0.0F, 0.0F}));
    shop.shoot(ProjectileShop::item("spread", "TestBowLight"));
    shop.shoot(ProjectileShop::item("scan", "TestBowLight"));
    // A release long before the shot: 40 steps.
    shop.walker.bits({kActionAttack});
    shop.walker.bits({kRelease});
    shop.walker.idle(40);
    InputEntry late = shop.walker.entry();
    late.shotItems = {ProjectileShop::item("basic", "TestBowLight")};
    shop.walker.send(late);
    using V = world::ShotVerdict;
    note("unknown " + std::to_string(shop.verdicts(V::unknownAttack)) + ", row " + std::to_string(shop.verdicts(V::badRow)) + ", charge "
         + std::to_string(shop.verdicts(V::badCharge)) + ", ammo " + std::to_string(shop.verdicts(V::badAmmo)) + ", origin "
         + std::to_string(shop.verdicts(V::badOrigin)) + ", direction " + std::to_string(shop.verdicts(V::badDirection)) + ", unsupported "
         + std::to_string(shop.verdicts(V::unsupported)) + ", no attack " + std::to_string(shop.verdicts(V::noAttack)));
    check(shop.verdicts(V::unknownAttack) == 1 && shop.verdicts(V::badRow) == 2 && shop.verdicts(V::badCharge) == 1
              && shop.verdicts(V::badAmmo) == 1 && shop.verdicts(V::badOrigin) == 1 && shop.verdicts(V::badDirection) == 1
              && shop.verdicts(V::unsupported) == 2 && shop.verdicts(V::noAttack) == 2,
          "each rule refuses its shot: the attack, the row and the charge of the rule, the hold of a heavy shot, the ammunition, the "
          "origin, the direction, a spread or a hit scan, a release long before the shot");
    check(shop.verdicts(V::accepted) == 0 && shop.host.world.projectiles().active().empty() && shop.host.world.projectiles().ledger_size() == 0,
          "a refused shot makes no projectile and no ledger entry");
    // A heavy shot with the hold of the data (16 steps), and a dead character.
    shop.shoot(ProjectileShop::item("basic_heavy", "TestBowHeavy", 100.0F), 16);
    check(shop.verdicts(V::accepted) == 1, "a heavy shot after a hold of 16 steps is accepted");
    shop.hold("TestAxe");
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    check(shop.verdicts(V::noWeapon) == 1, "a melee weapon in the hand has no ranged profile");
    shop.hold("TestBow");
    (void)shop.world().health().hurt(kCharacterA, shop.character()->vitals.healthMax);
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    check(shop.verdicts(V::dead) == 1, "a dead character does not shoot");
}

/** PJ7: the ledger. */
void one_shot_each_press(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    // The same press with two shot items: the second is refused.
    shop.walker.bits({kActionAttack});
    InputEntry two = shop.walker.entry({kRelease});
    two.shotItems = {ProjectileShop::item("basic", "TestBowLight"), ProjectileShop::item("basic", "TestBowLight")};
    shop.walker.send(two);
    check(shop.verdicts(world::ShotVerdict::accepted) == 1 && shop.verdicts(world::ShotVerdict::repeated) == 1,
          "one press gives one shot: a second item of the press is refused (the ledger)");
    // The world ledger refuses a launch of a press that has one, also for a second call with the same values.
    world::AttackTrack track;
    track.step(9000, true, false, false, shop.host.clock.now());
    track.step(9001, false, false, true, shop.host.clock.now());
    world::ShotClaim claim;
    claim.input = 9001;
    claim.sequence = 77; // A different input sequence: the first shot of the session has the key of the sequence 0, press 1.
    claim.origin = at(kPlace.x + 0.5F, kPlace.y, kHeight + 0.6F);
    claim.direction = {0.0F, 0.0F, 1.0F};
    claim.attack = crc("basic");
    claim.row = crc("TestBowLight");
    claim.ammo = crc("DefaultArrow");
    const auto first = shop.world().projectiles().launch(shop.character()->owner, claim, track);
    const auto again = shop.world().projectiles().launch(shop.character()->owner, claim, track);
    check(first.verdict == world::ShotVerdict::accepted && again.verdict == world::ShotVerdict::repeated, "a repeated launch of one press is refused");
    shop.walker.idle(40);
    // 40 presses with a shot each (more than the 32 local projectiles of the client): 40 projectiles.
    const std::uint64_t before = shop.verdicts(world::ShotVerdict::accepted);
    for (int press = 0; press < 40; ++press) {
        shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", {0.0F, 0.0F, 1.0F}));
        shop.walker.idle(3);
    }
    check(shop.verdicts(world::ShotVerdict::accepted) - before == 40 && shop.counts().largest <= world::projectile_rules::kCharacterMost,
          "40 presses give 40 projectiles, each one with its own ledger entry");
    shop.walker.idle(40);
    // Two fast shots at one wolf: two hits, each one time.
    world::Entity* wolf = shop.wolf_east(15.0F);
    const world::EntityId id = wolf->id;
    const std::uint64_t damage = shop.counts().damage;
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    shop.walker.idle(20);
    check(shop.counts().damage - damage == 2, "two fast shots at one wolf give two hits");
    // Two wolves on one line: the first one takes the hit, and the projectile ends there (no pierce rule in the data).
    world::Entity* behind = shop.wolf_east(25.0F);
    const world::EntityId behindId = behind->id;
    const float behindHealth = behind->vitals.health;
    const float frontHealth = shop.creature(id)->vitals.health;
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    shop.walker.idle(20);
    check(shop.creature(id)->vitals.health < frontHealth && shop.creature(behindId)->vitals.health == behindHealth,
          "a projectile hits one creature one time: the wolf behind the first one takes nothing");
}

/** PJ8: the fire staff. */
void fire_staff(const Options&) {
    ProjectileShop shop;
    shop.hold("TestStaff");
    world::Entity* wolf = shop.wolf_east(30.0F);
    const world::EntityId id = wolf->id;
    float health = wolf->vitals.health;
    // A light fire ball: the item has the row 0 and no ammunition; the row is the row of the spell (TestFireLight).
    shop.shoot(ProjectileShop::item("basic", nullptr, 0.0F, nullptr));
    shop.walker.idle(15);
    const float light = health - shop.creature(id)->vitals.health;
    check(light > 0.0F && shop.verdicts(world::ShotVerdict::accepted) == 1, "the light fire ball hits with the row of its spell");
    // A heavy fire ball needs the hold of the data (4 steps), and its spell has the cost Test_Fire_Cost (10) through the one
    // mana path.
    shop.character()->vitals.mana = 100.0F;
    shop.shoot(ProjectileShop::item("charged", nullptr, 0.0F, nullptr), 2);
    check(shop.verdicts(world::ShotVerdict::badCharge) == 1 && near(shop.character()->vitals.mana, 100.0F), "a heavy fire ball after 2 steps is refused, with no cost");
    health = shop.creature(id)->vitals.health;
    shop.shoot(ProjectileShop::item("charged", nullptr, 0.0F, nullptr), 6);
    check(near(shop.character()->vitals.mana, 90.0F, 0.2F), "the heavy fire ball costs 10 mana (its spell: the mana table) through the one mana path");
    shop.walker.idle(15);
    const float heavy = health - shop.creature(id)->vitals.health;
    check(near(heavy, light * 1.4F, 0.1F), "the heavy fire ball does the damage of its row (1.4 times the light row)");
    // Not enough mana: refused, and nothing changes.
    shop.character()->vitals.mana = 5.0F;
    shop.shoot(ProjectileShop::item("charged", nullptr, 0.0F, nullptr), 6);
    check(shop.verdicts(world::ShotVerdict::noMana) == 1 && near(shop.character()->vitals.mana, 5.0F, 0.5F)
              && shop.host.world.projectiles().active().empty(),
          "a heavy fire ball with 5 mana is refused: no cost, no projectile");
    // A fire ball with a row or an ammunition key in its item is refused.
    shop.shoot(ProjectileShop::item("basic", "TestFireLight", 0.0F, nullptr));
    shop.shoot(ProjectileShop::item("basic", nullptr, 0.0F, "DefaultArrow"));
    check(shop.verdicts(world::ShotVerdict::badRow) == 1 && shop.verdicts(world::ShotVerdict::badAmmo) == 1,
          "a magic shot with a row or with an ammunition key is refused");
}

/** PJ9: two sessions. */
void two_sessions(const Options&) {
    ProjectileShop shop(true);
    shop.enter_b();
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(12.0F);
    const world::EntityId id = wolf->id;
    shop.b.exchange(shop.host);
    world::Entity* beta = shop.host.world.find_character(kCharacterB);
    const float betaMana = beta->vitals.mana;
    const float health = wolf->vitals.health;
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    shop.shoot(ProjectileShop::item("basic", "TestBowLight"));
    for (int step = 0; step < 15; ++step) {
        shop.walker.idle(1);
        shop.b.exchange(shop.host);
    }
    const float dealt = health - shop.creature(id)->vitals.health;
    check(dealt > 0.0F, "the shot of A hits the wolf");
    check(damage_entries(shop.a, fromA).size() == 1 && damage_entries(shop.b, fromB).empty(), "only the client of A gets the damage number");
    bool observed = false;
    for (const Fragment& fragment : fragments_of(shop.b, kWolfSlots[1], 15, fromB)) {
        observed = observed || !fragment.fields.empty();
    }
    check(observed, "the client of B (an observer of the wolf) gets the vitals state of the wolf after the hit");
    check(shop.host.world.find_character(kCharacterB)->vitals.mana == betaMana, "the resources of B do not change");
    // B shoots with no ranged weapon: refused by the rules of its own character.
    world::AttackTrack track;
    track.step(9000, true, false, false, shop.host.clock.now());
    world::ShotClaim claim;
    claim.input = 9000;
    claim.origin = beta->position;
    claim.direction = {1.0F, 0.0F, 0.0F};
    claim.attack = crc("basic");
    claim.row = crc("TestBowLight");
    claim.ammo = crc("DefaultArrow");
    check(shop.world().projectiles().launch(beta->owner, claim, track).verdict == world::ShotVerdict::noWeapon,
          "a shot of B with no bow is refused (the character of a shot is the character of the session)");
    // A shot of the server for A (tools/world shoot) hits the same way and belongs to A.
    const float before = shop.creature(id)->vitals.health;
    // From 1.5 m above the feet of A down to the middle of the wolf 12 m east (4.8 degrees down).
    const world::ShotResult server = shop.world().projectiles().launch_character(kCharacterA, world::ServerShot{90.0F, -4.8F, false});
    for (int step = 0; step < 15; ++step) {
        shop.walker.idle(1);
    }
    check(server.verdict == world::ShotVerdict::accepted && shop.creature(id)->vitals.health < before, "a shot of the server for A hits the wolf");
}

/** PJ10: the load. */
void projectile_load(const Options&) {
    const auto run = [](unsigned farCount, std::uint64_t& looked, double& seconds) {
        ProjectileShop shop;
        shop.hold("TestBow");
        for (unsigned index = 0; index < 40; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 8.0F + static_cast<float>(index % 8), kPlace.y + 4.0F + static_cast<float>(index / 8)), 90.0F);
        }
        for (unsigned index = 0; index < farCount; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 600.0F + static_cast<float>(index % 100) * 3.0F,
                                                                   kPlace.y + 600.0F + static_cast<float>(index / 100) * 3.0F), 90.0F);
        }
        const std::uint64_t start = shop.counts().looked;
        const auto clock = std::chrono::steady_clock::now();
        for (unsigned shot = 0; shot < 100; ++shot) {
            (void)shop.world().projectiles().launch_character(kCharacterA, world::ServerShot{90.0F, shot % 2 == 0 ? 0.0F : 30.0F, false});
            for (int tick = 0; tick < 6; ++tick) {
                shop.host.clock.advance(kTick);
                shop.host.world.tick();
            }
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
        looked = shop.counts().looked - start;
        return shop.counts().shots[0];
    };
    std::uint64_t smallLooked = 0;
    std::uint64_t largeLooked = 0;
    double smallSeconds = 0.0;
    double largeSeconds = 0.0;
    const std::uint64_t smallShots = run(40, smallLooked, smallSeconds);
    const std::uint64_t largeShots = run(4000, largeLooked, largeSeconds);
    note("100 shots: 40 near and 40 far creatures: " + std::to_string(smallLooked) + " looks, " + std::to_string(smallSeconds) + " s; 40 near and 4,000 far: "
         + std::to_string(largeLooked) + " looks, " + std::to_string(largeSeconds) + " s");
    check(smallShots == 100 && largeShots == 100, "100 shots are accepted in each world");
    check(largeLooked == smallLooked && largeLooked > 0, "the flights look at the same creatures: 4,000 far creatures add no look");
    check(largeSeconds < 2.0, "the time limit: 100 shots and their flights in less than 2 s in a world of 4,040 creatures");
}

/** PJ11: the projectile data tool refuses a source that is not complete. */
void projectile_tool(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_projectile_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "projectile-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "projectile-tool").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok  ", 0) == 0) {
            const std::size_t start = line.find_first_not_of(' ', 4);
            passed.insert(line.substr(start, line.find(':') - start));
        }
        note(line.substr(0, 150));
    }
    check(status == 0, "the check of the tool has status 0");
    for (const char* name : {"valid", "missing-column", "range-not-finite", "bad-charge"}) {
        check(passed.contains(name), std::string("tool case ") + name);
    }
}

} // namespace

std::vector<Case> projectile_cases() {
    return {{"PJ1", "projectiles: the data, the creature capsule, the refusals", projectile_data},
            {"PJ2", "projectiles: the capsule of a creature and the sweep", capsule_sweep},
            {"PJ3", "projectiles: a bow shot, its flight, one later hit", bow_hit},
            {"PJ4", "projectiles: a miss, the life time, a wall, the caster leaves", miss_and_wall},
            {"PJ5", "projectiles: the hit near the muzzle and its refusals", near_hit},
            {"PJ6", "projectiles: the rules of a launch", launch_rules},
            {"PJ7", "projectiles: one shot for each press (the ledger), two fast shots, no pierce", one_shot_each_press},
            {"PJ8", "projectiles: the fire staff, its spell row, its hold, and the one mana path", fire_staff},
            {"PJ9", "projectiles: two sessions (MP-10)", two_sessions},
            {"PJ10", "projectiles: the load (time limit)", projectile_load},
            {"PJ11", "projectiles: the data tool refuses a source that is not complete", projectile_tool}};
}

} // namespace soulwarden::backend::tests

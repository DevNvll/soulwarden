// Cases of the corrections of the code review of stage 3.3b-S3 (the projectiles of the players), from the proofs of the
// reviewer (code-review/work/apps/backend/tests/review_projectile_faults.cpp). Each case fails on the tree of stage 3.3b-S3
// and passes after its correction. All ids, names, and values are made up.
//
//   RJ1  F1: a receiver near the muzzle must be on the line of the launch (behind the origin: no hit)
//   RJ2  F2: one press gives one projectile, also after the ledger entry of its projectile ended
//   RJ3  F3: a press with one weapon does not authorize a shot of a different weapon (a charged bow shot after an axe press)
//   RJ4  F4: a shot down at a creature hits the creature before the ground (the first blocked point of a cell)
//   RJ5  F5: input numbers alone do not give the hold of a heavy shot (the hold on the world clock)
//   RJ6  F6: the tolerance of a hit does not reach a creature through a wall
//   RJ7  F7: the projectile data tool keeps the old file for a source that is not complete (check_projectile_tool.py)
//   RJ8  F8: the loader refuses a JSON number that is not a finite float (an acceleration of 1e100)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/projectile_fixture.h"
#include "support/test_navigation.h"
#include "world/projectiles.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using namespace projectile;
using nlohmann::json;
using std::chrono::milliseconds;

/** A shot item of A with a receiver near the muzzle (the wolf, its actor, and the facet of its hit volume). */
InputShotItem receiver_item(world::Entity& target, std::array<float, 3> direction = {1.0F, 0.0F, 0.0F}) {
    InputShotItem shot = ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", direction);
    shot.receiverActor = target.actor.bytes;
    shot.receiver = target.id;
    shot.facet = target.creature.body->target(target.id, target.creature.body->components[1]);
    shot.point = {target.position.x - 0.6F, target.position.y, kHeight + 0.6F};
    return shot;
}

/** RJ1 (F1): a receiver near the muzzle that is behind the launch direction takes no hit. */
void near_direction(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(2.8F);
    const world::EntityId id = wolf->id;
    const float before = wolf->vitals.health;
    // The control: the same receiver with the direction to it is a hit near the muzzle.
    {
        ProjectileShop control;
        control.hold("TestBow");
        world::Entity* target = control.wolf_east(2.8F);
        const float health = target->vitals.health;
        control.shoot(receiver_item(*target));
        check(control.ends(world::ProjectileEnd::nearHit) == 1 && control.creature(target->id)->vitals.health < health,
              "control: the receiver on the line of the launch takes the hit near the muzzle");
    }
    const InputShotItem shot = receiver_item(*wolf, {-1.0F, 0.0F, 0.0F});
    const state::WorldPosition origin{shot.origin[0], shot.origin[1], shot.origin[2]};
    check(!world::sweep(origin, {origin.x - 2.0F, origin.y, origin.z}, world::capsule_of(*wolf), 0.15F + world::projectile_rules::kHitTolerance),
          "control: the segment to the west does not touch the wolf to the east");
    shop.shoot(shot);
    note("a receiver behind the direction of the launch: near hits " + std::to_string(shop.ends(world::ProjectileEnd::nearHit))
         + ", damage " + std::to_string(before - shop.creature(id)->vitals.health));
    check(shop.verdicts(world::ShotVerdict::nearTooFar) == 1, "the shot is refused (the receiver is not on the line of the launch)");
    check(shop.creature(id)->vitals.health == before, "a receiver behind the direction of the launch takes no damage");
}

/** RJ2 (F2): one press, a shot, and the same shot 20.67 s later (after the ledger entry ended): one projectile. */
void old_press(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(1.8F);
    const world::EntityId id = wolf->id;
    const InputShotItem shot = receiver_item(*wolf);
    shop.walker.bits({kActionAttack});
    InputEntry first = shop.walker.entry();
    first.shotItems = {shot};
    shop.walker.send(first); // No hold bit and no release bit.
    const auto accepted = shop.verdicts(world::ShotVerdict::accepted);
    const auto hits = shop.counts().damage;
    const float before = shop.creature(id)->vitals.health;
    shop.walker.idle(620); // 20.67 s: the life time of the projectile and the ledger time ended.
    check(shop.world().projectiles().ledger_size() == 0, "control: the ledger entry of the first shot ended");
    InputEntry repeat = shop.walker.entry();
    repeat.shotItems = {shot};
    shop.walker.send(repeat);
    note("one press, 20.67 s later: more launches " + std::to_string(shop.verdicts(world::ShotVerdict::accepted) - accepted)
         + ", more hits " + std::to_string(shop.counts().damage - hits) + ", more damage "
         + std::to_string(before - shop.creature(id)->vitals.health));
    check(accepted == 1 && hits == 1, "control: the first shot of the press hits one time");
    check(shop.verdicts(world::ShotVerdict::accepted) == accepted && shop.counts().damage == hits
              && shop.creature(id)->vitals.health == before,
          "the press gives no second projectile after its ledger entry ended");
}

/** RJ3 (F3): a press with the axe, then the bow in the hand at the release: no charged bow shot. */
void press_weapon(const Options&) {
    ProjectileShop shop;
    shop.hold("TestAxe");
    check(shop.wear("TestBow", world::paperdoll::kMainHand1 + 1).ok(), "setup: the bow in the second weapon slot");
    world::Entity* wolf = shop.wolf_east(1.8F);
    const world::EntityId id = wolf->id;
    const float before = wolf->vitals.health;
    shop.walker.bits({kActionAttack}); // The axe is in the hand.
    for (unsigned step = 0; step < 15; ++step) {
        shop.walker.bits({kShootHold});
    }
    InputEntry entry = shop.walker.entry({kActionWeapon2, kRelease, kAim});
    InputShotItem shot = receiver_item(*wolf);
    shot.attack = crc("basic_heavy");
    shot.row = crc("TestBowHeavy");
    shot.charge = 100.0F;
    entry.shotItems = {shot};
    shop.walker.send(entry);
    check(world::Combat::weapon_in_hand(*shop.character())->itemId == "TestBow", "control: the input selected the bow");
    note("an axe press, the bow at the launch: accepted " + std::to_string(shop.verdicts(world::ShotVerdict::accepted)) + ", damage "
         + std::to_string(before - shop.creature(id)->vitals.health));
    check(shop.verdicts(world::ShotVerdict::accepted) == 0 && shop.creature(id)->vitals.health == before,
          "a press with the axe does not authorize a charged bow shot");
    // The control: a press with the bow in the hand is a charged shot.
    shop.walker.bits({kActionAttack, kAim});
    for (unsigned step = 0; step < 15; ++step) {
        shop.walker.bits({kShootHold, kAim});
    }
    InputEntry again = shop.walker.entry({kRelease, kAim});
    again.shotItems = {shot};
    shop.walker.send(again);
    check(shop.verdicts(world::ShotVerdict::accepted) == 1, "control: a press and a hold with the bow give the charged shot");
}

/** RJ4 (F4): a shot down from 1.5 m above a creature hits the creature before the ground. */
void ground_order(const Options& options) {
    ProjectileShop shop;
    TestNavRegion region;
    const auto folder = options.workDirectory / "review-projectile-flat";
    region.write(folder);
    check(shop.host.navigation.set_directory(folder.string()), "setup: the flat navigation ground loads");
    shop.walker.idle(20);
    shop.hold("TestStaff");
    world::Entity* wolf = shop.wolf_east(1.8F);
    const world::EntityId id = wolf->id;
    const float before = wolf->vitals.health;
    InputShotItem shot = ProjectileShop::item("basic", nullptr, 0.0F, nullptr, {0.0F, 0.0F, -1.0F});
    shot.origin = {wolf->position.x, wolf->position.y, kHeight + 1.5F};
    const state::WorldPosition origin{shot.origin[0], shot.origin[1], shot.origin[2]};
    const state::WorldPosition end{origin.x, origin.y, origin.z - 100.0F / 30.0F};
    const std::optional<float> ground = shop.world().navigation().line_stop(origin, end);
    const std::optional<float> touch = world::sweep(origin, end, world::capsule_of(*wolf), 0.15F + world::projectile_rules::kHitTolerance);
    note("the first segment down: the ground at the part " + std::to_string(ground.value_or(-1.0F)) + ", the creature at the part "
         + std::to_string(touch.value_or(-1.0F)));
    check(ground && std::fabs(origin.z + (end.z - origin.z) * *ground - kHeight) < 0.1F,
          "line_stop gives the point of the ground in the cell, not the entry of the cell");
    check(touch && origin.z + (end.z - origin.z) * *touch > kHeight + 0.1F, "control: the segment touches the creature above the ground");
    shop.shoot(shot);
    shop.walker.idle(3);
    note("a shot down: wall ends " + std::to_string(shop.ends(world::ProjectileEnd::wall)) + ", damage "
         + std::to_string(before - shop.creature(id)->vitals.health));
    check(shop.creature(id)->vitals.health < before && shop.ends(world::ProjectileEnd::hit) == 1,
          "a creature above the ground is hit before the ground");
}

/** RJ5 (F5): a press and a release 16 input numbers later but 66 ms later on the world clock: no heavy shot. */
void fast_charge(const Options&) {
    ProjectileShop shop;
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(1.8F);
    const world::EntityId id = wolf->id;
    const auto started = shop.host.clock.now();
    shop.walker.bits({kActionAttack, kAim});
    shop.walker.input += 15;
    InputEntry release = shop.walker.entry({kRelease, kAim});
    InputShotItem shot = receiver_item(*wolf);
    shot.attack = crc("basic_heavy");
    shot.row = crc("TestBowHeavy");
    shot.charge = 100.0F;
    release.shotItems = {shot};
    const float before = shop.creature(id)->vitals.health;
    shop.walker.send(release);
    note("16 input numbers in "
         + std::to_string(std::chrono::duration_cast<milliseconds>(shop.host.clock.now() - started).count()) + " ms: damage "
         + std::to_string(before - shop.creature(id)->vitals.health));
    check(shop.verdicts(world::ShotVerdict::badCharge) == 1 && shop.verdicts(world::ShotVerdict::accepted) == 0,
          "input numbers alone do not authorize a heavy shot before the hold time");
    // The control: the same hold with the time of 16 steps is a charged shot.
    shop.walker.bits({kActionAttack, kAim});
    for (unsigned step = 0; step < 15; ++step) {
        shop.walker.bits({kShootHold, kAim});
    }
    InputEntry real = shop.walker.entry({kRelease, kAim});
    real.shotItems = {shot};
    shop.walker.send(real);
    check(shop.verdicts(world::ShotVerdict::accepted) == 1, "control: a hold of 16 steps on the world clock is a charged shot");
}

/** RJ6 (F6): a creature wholly behind a wall, in the tolerance of a hit of the line: no damage, the wall ends the projectile. */
void tolerance_wall(const Options& options) {
    ProjectileShop shop;
    TestNavRegion region;
    region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
        const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
        if (px >= 2010.0F && px < 2010.25F) {
            floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
        }
    };
    const auto folder = options.workDirectory / "review-projectile-tolerance-wall";
    region.write(folder);
    check(shop.host.navigation.set_directory(folder.string()), "setup: the wall of one cell loads");
    shop.walker.idle(20);
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(11.3F);
    const world::EntityId id = wolf->id;
    const float before = wolf->vitals.health;
    const InputShotItem shot = ProjectileShop::item("basic", "TestBowLight");
    const state::WorldPosition origin{shot.origin[0], shot.origin[1], shot.origin[2]};
    const state::WorldPosition end{kPlace.x + 15.0F, kPlace.y, origin.z};
    const std::optional<float> wall = shop.world().navigation().line_stop(origin, end);
    const std::optional<float> exact = world::sweep(origin, end, world::capsule_of(*wolf), 0.15F);
    const std::optional<float> padded = world::sweep(origin, end, world::capsule_of(*wolf), 0.15F + world::projectile_rules::kHitTolerance);
    check(wall && exact && padded && *padded < *wall && *wall < *exact,
          "control: the wall is before the real contact and after the contact with the tolerance");
    shop.shoot(shot);
    shop.walker.idle(10);
    note("a creature wholly behind a wall: damage " + std::to_string(before - shop.creature(id)->vitals.health) + ", wall ends "
         + std::to_string(shop.ends(world::ProjectileEnd::wall)));
    check(shop.creature(id)->vitals.health == before && shop.ends(world::ProjectileEnd::wall) == 1,
          "the tolerance of a hit does not damage a creature through a wall");
}

/** RJ7 (F7): the projectile data tool refuses a source that is not complete (the cases of finding F7 of the tool check). */
void tool_sources(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_projectile_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "review-projectile-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "review-projectile-tool").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok  ", 0) == 0) {
            const std::size_t start = line.find_first_not_of(' ', 4);
            passed.insert(line.substr(start, line.find(':') - start));
        }
        note(line.substr(0, 150));
    }
    for (const char* name : {"valid", "missing-ammo-prefab", "empty-projectile-slice", "no-animation-database", "missing-slice-override"}) {
        check(passed.contains(name), std::string("tool case ") + name);
    }
}

/** RJ8 (F8): an acceleration of 1e100 in the file (finite as a double, not as a float) is refused; set() refuses it too. */
void float_loader(const Options& options) {
    json document{{"format", 1},
                  {"attacks", {{"P", "Basic", false, 100.0, 0.15, 2.0, 0.2, 5.0, 30.0, 80.0, 1.0, 0, true, true, {0.0, 0.0, -9.8}, 0}}},
                  {"slices", {{"slices/test/arrow.dynamicslice", 100.0, 25.0, 1.0, {0.0, 0.0, -9.8}, 10.0, 8.0}}},
                  {"ammo", {{"DefaultArrow", "Arrow", 1.2, 0}}},
                  {"weapons", {{"W", "P", "Arrow"}}},
                  {"shots", {{"P", "Basic", "Row", 0.0, -1, "", "", false, 12, true}}}};
    const auto path = options.workDirectory / "review-projectile-float.json";
    std::filesystem::create_directories(options.workDirectory);
    const auto load = [&](gamedata::ProjectileData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        return data.load(path.string());
    };
    gamedata::ProjectileData control;
    check(load(control), "control: the valid projectile data loads");
    document["slices"][0][4][2] = 1.0e100;
    gamedata::ProjectileData bad;
    const bool loaded = load(bad);
    note("an acceleration of 1e100: loaded " + std::to_string(loaded));
    check(!loaded && !bad.loaded(), "a JSON number that is not a finite float is refused");
    gamedata::ProjectileTables tables = control.tables();
    tables.slices[0].accel[2] = std::numeric_limits<float>::infinity();
    gamedata::ProjectileData direct;
    check(!direct.set(tables), "set() refuses an acceleration that is not finite");
}

} // namespace

std::vector<Case> review_projectile_cases() {
    return {{"RJ1", "review of 3.3b-S3, F1: a receiver near the muzzle must be on the line of the launch", near_direction},
            {"RJ2", "review of 3.3b-S3, F2: one press gives one projectile after its ledger entry ended", old_press},
            {"RJ3", "review of 3.3b-S3, F3: an axe press does not authorize a charged bow shot", press_weapon},
            {"RJ4", "review of 3.3b-S3, F4: a shot down hits a creature before the ground", ground_order},
            {"RJ5", "review of 3.3b-S3, F5: input numbers alone do not give the hold of a heavy shot", fast_charge},
            {"RJ6", "review of 3.3b-S3, F6: the tolerance of a hit does not reach through a wall", tolerance_wall},
            {"RJ7", "review of 3.3b-S3, F7: the projectile data tool refuses a source that is not complete (check_projectile_tool.py)",
             tool_sources},
            {"RJ8", "review of 3.3b-S3, F8: the projectile loader refuses a number that is not a finite float", float_loader}};
}

} // namespace soulwarden::backend::tests

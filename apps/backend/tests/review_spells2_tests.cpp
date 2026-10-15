// Cases of the corrections of the code review of stage 3.3b-S2b (made in stage 1.1b). Each case fails on the tree of stage
// 3.3b-S2b (commit 78b8c5f, with this file, the fixture header, and the check scripts of the tools) and passes after the
// correction. The C++ cases come from the proof cases of the reviewer (RSB1 to RSB10). All ids, names, and values are made up.
//
//   RK1   F1: the mute of a development run changes no settings file of the user through a link (tools/check_dev_mute.py)
//   RK2   F2: a near-hit claim does not hurt a creature behind a corner wall on the line of the launch
//   RK3   F3: the death of the entity of a Beacon ends the Beacon and its statuses; a lethal impact makes no Beacon
//   RK4   F4: the end of one of two Beacons keeps the statuses of the other one on both friends (two sessions)
//   RK5   F5: a Beacon of 15 s keeps its statuses of 10 s on a friend in it
//   RK6   F6: a friend that left the world with the statuses of a Beacon does not have them at its return outside it
//   RK7   F7: a session does not observe a Beacon whose attachment it does not observe
//   RK8   F8: the spell data refuses a slice whose vitals row it does not have; a Pylon with no vitals row is not made or paid
//   RK9   F9: a Beacon press whose area names a status row that the data does not have is refused before the payment
//   RK10  F10: a Beacon shot straight down hits the creature below the muzzle
//   RK11  F11: the projectile tool keeps the old file for a basic row or an ability spell with no shot (check_projectile_tool.py)
//   RK12  F12: the spell tool keeps the old file for an AutoSpell time that is not a 32-bit float (check_spell_tool.py)

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/spell_kind_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using namespace projectile;
using namespace spell_kind;
using std::chrono::milliseconds;

/** Runs a check script of the tools (a path below the repository) and @return its output lines. */
std::vector<std::string> run_check(const Options& options, const std::string& script, const std::string& argument) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + path.lexically_normal().string() + "\"" + (argument.empty() ? "" : " \"" + argument + "\"") + " > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 150));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** @return True when a line of the output starts with "ok" and names the case (the text before its first colon). */
bool passed(const std::vector<std::string>& lines, const std::string& name) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        if (line.rfind("ok", 0) != 0) {
            return false;
        }
        const std::size_t start = line.find_first_not_of(' ', 2);
        return start != std::string::npos && line.compare(start, name.size() + 1, name + ":") == 0;
    });
}

world::Entity* make_aura(KindShop& shop, world::EntityId caster, world::EntityId attach, state::WorldPosition place, int life = 10000) {
    world::SpellRequest request;
    request.spellId = kBeacon;
    request.caster = caster;
    request.attach = attach;
    request.position = place;
    request.role = world::SpellRole::aura;
    request.life = milliseconds(life);
    request.radius = 3.0F;
    request.statuses = {kBeaconStatus, kBeaconPotency};
    return shop.host.world.spells().make(request);
}

/** RK1 (F1): the mute script against made-up prefixes (a link, a "/.", a savedata link, and the controls). */
void mute(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/check_dev_mute.py", "");
    for (const char* name : {"separate prefix with spaces", "exact user prefix", "sound override", "prefix link", "dot alias", "savedata link",
                             "settings file link", "user path through a link"}) {
        const bool clean = std::none_of(lines.begin(), lines.end(), [&](const std::string& line) {
            return line.rfind("FAIL", 0) == 0 && line.find(name) != std::string::npos;
        });
        check(clean && passed(lines, name), std::string("mute: ") + name + ": the settings of the user stay, each check of the case passes");
    }
}

/** RK2 (F2): a wall on the line of the launch before the padded contact blocks a near hit too. */
void near_wall(const Options& options) {
    for (const bool receiver : {false, true}) {
        ProjectileShop shop;
        TestNavRegion region;
        region.tiles[{31, 31}] = [](int x, int y, std::vector<TestFloor>& floors) {
            const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
            const float py = 1984.0F + (static_cast<float>(y) + 0.5F) * 0.25F;
            if (px >= 2001.5F && px < 2001.75F && py >= 2000.0F && py < 2000.25F) {
                floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
            }
        };
        const auto folder = options.workDirectory / (receiver ? "rk2-near-claim" : "rk2-flight");
        region.write(folder);
        check(shop.host.navigation.set_directory(folder.string()), "control: the data of the corner wall loads");
        shop.walker.idle(20);
        shop.hold("TestBow");
        world::Entity* target = shop.wolf_east(2.5F, 0.85F);
        const world::EntityId id = target->id;
        const float before = target->vitals.health;
        InputShotItem shot = ProjectileShop::item("basic", "TestBowLight");
        if (receiver) {
            shot.receiverActor = target->actor.bytes;
            shot.receiver = target->id;
            shot.facet = target->creature.body->target(target->id, target->creature.body->components[1]);
            shot.point = {target->position.x, target->position.y, kHeight + 0.6F};
        }
        const state::WorldPosition origin{shot.origin[0], shot.origin[1], shot.origin[2]};
        const state::WorldPosition end{origin.x + 2.0F, origin.y, origin.z};
        const auto wall = shop.world().navigation().line_stop(origin, end);
        const auto touch = world::sweep(origin, end, world::capsule_of(*target), 0.15F + world::projectile_rules::kHitTolerance);
        check(wall && touch && *wall < *touch, "control: the line of the launch meets the wall before its padded contact");
        shop.shoot(shot);
        shop.walker.idle(10);
        note(std::string(receiver ? "near receiver" : "flight") + ": damage " + std::to_string(before - shop.creature(id)->vitals.health));
        check(shop.creature(id)->vitals.health == before,
              std::string(receiver ? "a near-hit claim" : "a flight") + ": the wall before the contact blocks the damage");
    }
}

/** RK3 (F3): the death of the attachment ends the Beacon and its statuses; a lethal impact makes no Beacon on the body. */
void dead_target(const Options&) {
    for (const bool player : {false, true}) {
        KindShop shop(true);
        world::World& world = shop.host.world;
        world::EntityId target = 0;
        if (player) {
            shop.enter_b_near(at(kPlace.x - 2.0F, kPlace.y));
            target = world.find_character(kCharacterB)->id;
        } else {
            target = shop.wolf_east(-2.0F)->id;
        }
        const world::EntityId aura = make_aura(shop, shop.character()->id, target, world.find(target)->position)->id;
        shop.walker.idle(10);
        check(has_status(*shop.character(), kBeaconStatus), "control: a Beacon on a living entity gives the caster its status");
        if (player) {
            check(world.health().hurt(kCharacterB, world.find(target)->vitals.health).died, "control: the character of B dies");
        } else {
            check(world.creatures().set_health(target, 0.0F).error == world::CreatureError::none, "control: the creature dies");
        }
        shop.walker.idle(12);
        check(world.find(aura) == nullptr && !has_status(*shop.character(), kBeaconStatus),
              std::string("the death of the ") + (player ? "character" : "creature") + " ends the Beacon and its status");
    }
    KindShop shop;
    shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
    const world::EntityId target = shop.wolf_east(-2.5F)->id;
    (void)shop.host.world.creatures().set_health(target, 1.0F);
    shop.walker.press(0);
    shop.walker.idle(3);
    shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
    shop.walker.idle(12);
    check(shop.creature(target)->creature.dead, "control: the impact of the Beacon killed its target");
    check(shop.spells().empty(), "a lethal impact makes no Beacon on the body");
}

/** RK4 (F4): two Beacons give one status row; the end of the Beacon of its source keeps the row from the other one. */
void overlap(const Options&) {
    KindShop shop(true);
    shop.enter_b_near(at(kPlace.x - 2.0F, kPlace.y));
    world::World& world = shop.host.world;
    world.set_test_friends(kCharacterA, kCharacterB);
    const world::EntityId first = make_aura(shop, shop.character()->id, 0, shop.character()->position)->id;
    shop.walker.idle(10);
    const world::EntityId second = make_aura(shop, world.find_character(kCharacterB)->id, 0, shop.character()->position)->id;
    shop.walker.idle(10);
    check(world.spells().members(first).size() == 2 && world.spells().members(second).size() == 2, "control: the two Beacons have both friends");
    const auto rows = [&](const char* character) {
        const auto* statuses = world.statuses().statuses_of(character);
        return statuses == nullptr ? 0L : std::count_if(statuses->begin(), statuses->end(), [](const world::ActiveStatus& status) {
            return status.status == kBeaconStatus;
        });
    };
    check(rows(kCharacterA) == 1 && rows(kCharacterB) == 1, "one status row for each friend, not one for each Beacon");
    world.spells().end(second, world::SpellEnd::command);
    shop.walker.idle(12);
    check(world.find(first) != nullptr && has_status(*shop.character(), kBeaconStatus) && has_status(*world.find_character(kCharacterB), kBeaconStatus),
          "after the end of the second Beacon, both friends keep the status of the first one (MP-10)");
    world.spells().end(first, world::SpellEnd::command);
    shop.walker.idle(4);
    check(!has_status(*shop.character(), kBeaconStatus) && !has_status(*world.find_character(kCharacterB), kBeaconStatus),
          "after the end of the last Beacon, no friend has the status");
}

/** RK5 (F5): a Beacon of 15 s keeps its rows of 10 s on a friend in it. */
void longer_beacon(const Options&) {
    KindShop shop;
    world::World& world = shop.host.world;
    const world::EntityId aura = make_aura(shop, shop.character()->id, 0, shop.character()->position, 15000)->id;
    shop.walker.idle(10);
    check(has_status(*shop.character(), kBeaconStatus), "control: the Beacon of 15 s gives its row of 10 s");
    shop.pass(11.0F);
    check(world.find(aura) != nullptr && has_status(*shop.character(), kBeaconStatus) && world.spells().counts().auraRenewed >= 1,
          "after 11 s the friend in the Beacon has its row again (one new application)");
    shop.pass(5.0F);
    check(world.find(aura) == nullptr && !has_status(*shop.character(), kBeaconStatus), "the end of the Beacon ends the row");
}

/** RK6 (F6): a friend that left the world with the status of a Beacon has no status at its return outside the Beacon. */
void friend_leave(const Options&) {
    KindShop shop(true);
    shop.enter_b_near(at(kPlace.x - 2.0F, kPlace.y));
    world::World& world = shop.host.world;
    world.set_test_friends(kCharacterA, kCharacterB);
    const world::EntityId aura = make_aura(shop, shop.character()->id, 0, shop.character()->position)->id;
    shop.walker.idle(12);
    check(has_status(*world.find_character(kCharacterB), kBeaconStatus), "control: the friend in the Beacon has its status");
    check(world.leave(world.find_character(kCharacterB)->owner), "control: the friend leaves the world");
    shop.walker.idle(12);
    check(world.find(aura) != nullptr && world.spells().members(aura).size() == 1, "control: the Beacon stays with its caster only");
    const auto* stored = world.statuses().statuses_of(kCharacterB);
    check(stored == nullptr || std::none_of(stored->begin(), stored->end(), [](const world::ActiveStatus& status) { return status.status == kBeaconStatus; }),
          "the status of the Beacon ended at the leave (the status of a character stays with its character id)");
    check(world.spawn_character(kCharacterB, at(kPlace.x + 30.0F, kPlace.y)) == world::SpawnError::none, "control: the character comes back far away");
    shop.walker.idle(12);
    check(!has_status(*world.find_character(kCharacterB), kBeaconStatus), "the character outside the Beacon has no status of it");
}

/** RK7 (F7): a session observes an aura only with its attachment. */
void hidden_target(const Options&) {
    KindShop shop(true);
    gamedata::CreatureSlice data = capsule_wolf();
    data.interestRadius = 20.0F;
    check(shop.host.creatures.set_full({data}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}}), "control: a creature with a 20 m interest");
    world::Entity* wolf = shop.wolf_east(-30.0F);
    const world::EntityId target = wolf->id;
    const world::EntityId aura = make_aura(shop, shop.character()->id, target, wolf->position)->id;
    shop.walker.idle(25);
    const auto& observer = shop.host.server->session(1)->observer();
    check(!observer.known().contains(target), "control: the creature 30 m away is outside the interest of the session");
    check(!observer.known().contains(aura), "the session does not observe the Beacon whose attachment it does not observe");
    // The creature comes near: the session observes it, then the Beacon.
    shop.host.world.place_creature(*shop.host.world.find(target), at(kPlace.x - 5.0F, kPlace.y));
    shop.walker.idle(60);
    check(observer.known().contains(target) && observer.known().contains(aura), "the creature near: the session observes it and its Beacon");
}

/** RK8 (F8): a slice with a vitals row that the data does not have is refused; a row of a different table makes no paid summon. */
void missing_vitals(const Options&) {
    KindShop shop;
    gamedata::SpellTables tables = kind_tables();
    tables.vitals.erase(tables.vitals.begin()); // The row of the Pylon.
    check(!shop.host.spells.set(tables), "the spell data refuses a Pylon slice whose vitals row it does not have");
    tables.vitalsNotInData = {"TestPylonVitals"};
    check(shop.host.spells.set(tables), "control: the same row as a row of a different table (vitalsNotInData) loads");
    shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
    const float mana = shop.character()->vitals.mana;
    world::Entity* pylon = shop.cast(30);
    check(pylon == nullptr && near(shop.character()->vitals.mana, mana, 0.5F), "no Pylon with no vitals row, and no payment");
}

/** RK9 (F9): a Beacon whose area names a status row that the data does not have is refused before the payment. */
void missing_beacon_status(const Options&) {
    KindShop shop;
    gamedata::AbilityTables tables = kind_abilities();
    std::erase_if(tables.statusEffects, [](const gamedata::StatusRow& row) { return row.id == kBeaconStatus; });
    tables.statusEffects = numbered(std::move(tables.statusEffects));
    check(shop.host.abilities.set(tables), "control: the ability data with no main status row of the Beacon loads");
    shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
    shop.wolf_east(-2.5F);
    const float before = shop.character()->vitals.mana;
    shop.walker.press(0);
    shop.walker.idle(3);
    shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
    shop.walker.idle(12);
    check(shop.presses(world::CastVerdict::noRule) == 1 && shop.presses(world::CastVerdict::accepted) == 0 && shop.spells().empty()
              && near(shop.character()->vitals.mana, before, 0.5F),
          "the press is refused (no rule), with no Beacon and no payment");
}

/** RK10 (F10): a Beacon shot straight down hits the creature below the muzzle. */
void vertical_shot(const Options&) {
    for (const float east : {0.02F, 0.0F}) {
        KindShop shop;
        gamedata::CreatureSlice slice = capsule_wolf();
        slice.capsule = gamedata::CreatureCapsule{0.1F, 0.0F, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
        check(shop.host.creatures.set_full({slice}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}}), "control: a small capsule");
        shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
        const world::EntityId target = shop.wolf_east(-0.3F)->id;
        shop.walker.press(0);
        shop.walker.idle(3);
        shop.send_shot("Heavy", {east, 0.0F, -1.0F});
        shop.walker.idle(25);
        const world::Entity* aura = shop.spell();
        check(aura != nullptr && aura->spell.attached == target,
              "a shot with the horizontal part " + std::to_string(east) + " hits the creature below the muzzle, and the Beacon follows it");
    }
}

/** RK11 (F11): the projectile tool requires a shot of each basic row and of each ability spell that the backend launches. */
void projectile_tool(const Options& options) {
    const std::vector<std::string> lines =
        run_check(options, "tools/gamedata/check_projectile_tool.py", (options.workDirectory / "review-spells2-projectiles").string());
    check(passed(lines, "valid"), "control: the light and the heavy clip give two shots");
    check(passed(lines, "partial-basic-sources"), "a source of the light row with no source of the heavy row: the old file stays");
    check(passed(lines, "missing-ability-prefab"), "a Beacon projectile with no slice: the old file stays");
}

/** RK12 (F12): the spell tool requires the times of an AutoSpell as 32-bit floats. */
void spell_tool(const Options& options) {
    const std::vector<std::string> lines =
        run_check(options, "tools/gamedata/check_spell_tool.py", (options.workDirectory / "review-spells2-spells").string());
    check(passed(lines, "autospell"), "control: an AutoSpell lifetime of 4 bytes gives 15 s");
    check(passed(lines, "autospell-lifetime-width"), "an AutoSpell lifetime of 1 byte: the old file stays");
}

} // namespace

std::vector<Case> review_spells2_cases() {
    return {{"RK1", "review of 3.3b-S2b, F1: the mute of a development run changes no settings file of the user through a link", mute},
            {"RK2", "review of 3.3b-S2b, F2: a wall on the line of the launch blocks a near-hit claim", near_wall},
            {"RK3", "review of 3.3b-S2b, F3: the death of its entity ends a Beacon; a lethal impact makes no Beacon", dead_target},
            {"RK4", "review of 3.3b-S2b, F4: two Beacons: the end of one keeps the status of the other on both friends (MP-10)", overlap},
            {"RK5", "review of 3.3b-S2b, F5: a Beacon of 15 s keeps its rows of 10 s on a friend in it", longer_beacon},
            {"RK6", "review of 3.3b-S2b, F6: a friend that left has no Beacon status at its return outside", friend_leave},
            {"RK7", "review of 3.3b-S2b, F7: a session observes a Beacon only with its attachment", hidden_target},
            {"RK8", "review of 3.3b-S2b, F8: a missing vitals row: refused data, no paid Pylon", missing_vitals},
            {"RK9", "review of 3.3b-S2b, F9: a Beacon area with a missing status row is refused before the payment", missing_beacon_status},
            {"RK10", "review of 3.3b-S2b, F10: a Beacon shot straight down hits the creature below", vertical_shot},
            {"RK11", "review of 3.3b-S2b, F11: the projectile tool requires each basic row and each ability shot", projectile_tool},
            {"RK12", "review of 3.3b-S2b, F12: the spell tool requires the AutoSpell times as 32-bit floats", spell_tool}};
}

} // namespace soulwarden::backend::tests

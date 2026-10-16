// Tests of stage 3.4f-A: the first creature that shoots (an archer; research creature-ranged.md, protocol notes 4.37).
//
//   CRG1  the export and the dictionary: the loader of the ranged attacks, the projectile slices, and the capsule of the player
//         slice; a file whose parts do not fit together; a file of an older tool (the creatures stay melee creatures); the check of
//         the data tool (tools/gamedata/check_creature_tool.py, the ranged cases)
//   CRG2  the time and the generation: the windup, the release at the time of the data, the repeat time; a knockdown, the death of
//         the archer, and the leave of the target before the release make no shot
//   CRG3  the segment: one health change only at the contact; a moving target that leaves the line, a wall, and the life time make no
//         damage
//   CRG4  two sessions: one record of the shot for each observer (the same bytes), the damage mark only for the damaged owner, the
//         vitals for each observer; the first character on the line takes the shot
//   CRG5  the late interest and the stop: a session that comes near in the flight gets the launch with its time anchor and no second
//         launch; no record after the end; the stop at the hold; the death of the archer in the flight ends it with no damage
//   CRG6  the bounds: the shots of one creature in the air, the hold list, no projectile entity after the ends
//   CRG9  the load: 32,840 far spawn points, 50 characters, 50 archers that shoot, 200 flights, 1,000 ticks, a time limit; the work
//         does not change with the far points
//
// The accounts, the characters, the creature kinds, the rows, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/health_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"
#include "world/behavior.h"
#include "world/projectiles.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using nlohmann::json;

/** The actions of the test archer: the melee bash (3) and the basic shot FireBow (5). */
constexpr std::uint32_t kBash = 3;
constexpr std::uint32_t kFireBow = 5;
constexpr float kRelease = 1.33333F;
constexpr float kShotLength = 2.46667F;
constexpr float kArrowSpeed = 50.0F;
constexpr float kArrowLife = 1.5F;
constexpr std::uint32_t kArrowSlot = 1;
constexpr float kPlayerRadius = 0.41F;
constexpr float kPlayerSegment = 0.8F;
constexpr std::uint32_t kAttackIndex = 7;
constexpr float kArcherDamage = 100.0F;

std::uint32_t crc_of(const char* text) {
    return gamedata::progression_id(text);
}

gamedata::RangedRelease arrow_release() {
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

gamedata::ProjectileSliceData arrow_slice() {
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
gamedata::CreatureSlice archer_slice(bool melee = true) {
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

gamedata::CreatureKind archer_kind() {
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

std::vector<gamedata::CreatureLevel> archer_levels() {
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

std::chrono::microseconds seconds(float value) {
    return std::chrono::microseconds(static_cast<long long>(std::lround(value * 1.0e6F)));
}

/** @return The interest number of an entity in a session of the host, or 0. */
std::uint32_t interest_in(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** @return The records of an interest number from the message `from` on: the fragments of each record, in their sequence. */
std::vector<Fragment> fragments_for(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
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
std::size_t stops_of(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
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
std::vector<Fragment> projectile_states(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (const Fragment& fragment : fragments_for(client, interest, from)) {
        if (fragment.slot == kArrowSlot && fragment.type == 16) {
            found.push_back(fragment);
        }
    }
    return found;
}

std::uint64_t field_u64(const Fragment& fragment, unsigned field) {
    server::hub::Reader reader(fragment.fields.at({0, field}));
    return reader.u64();
}

/** The projectile entity of the newest flight of the system, or 0. */
world::EntityId newest_projectile(Range& range) {
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

/** CRG1: the export and the dictionary. */
void ranged_data(const Options& options) {
    const auto slice = [](const char* path, const char* asset, const char* root) {
        return json{{"path", path}, {"asset", asset}, {"sub", 2}, {"root", root}, {"interestRadius", 60.0}, {"movement", 5},
                    {"vitals", 9}, {"marker", 15}, {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                    {"components", json::array({json::array({root, "0000000000000A01", "Root/Vitals"})})},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}, {"FireBow", "Fire_Bow", 35, false},
                                 {"Archer_Fast_Ranged_Attack", "Archer_Fast_Ranged_Attack", 36, false}}},
                    {"behavior", {{"sight", {75.0, 12.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.1, 70.0}}, {"speed", {5.5, 5.5}}}}};
    };
    const auto release = [](double time, const char* row, int rowIndex, int projectile) {
        return json{{"time", time}, {"profile", "AI_Bow"}, {"attack", "Basic"}, {"index", 7}, {"ammo", "Arrow_AI"},
                    {"ammoKey", crc_of("Arrow_AI")}, {"projectile", projectile}, {"override", false}, {"table", "TestBowDamageTable"},
                    {"tableKey", crc_of("TestBowDamageTable")}, {"row", row}, {"rowIndex", rowIndex}, {"factor", 0.35}, {"added", 0.0},
                    {"type", "Thrust"}, {"wire", 5}, {"category", "Physical"}, {"charge", 0.1625}, {"lead", true}, {"aimSpeed", 50.0},
                    {"leadAngle", 10.0}, {"aimAngle", 45.0}, {"miss", {1.5, 3.0}}, {"range", 100.0}, {"radius", 0.0}, {"accelDelay", 0.0}};
    };
    json document{{"format", 3},
                  {"levels", {{1, 337.5, 145.52, 50.0}}},
                  {"slices", {slice("slices/test/archer.dynamicslice", "31313131-3131-3131-3131-313131313131", "1000000000000031")}},
                  {"kinds",
                   {{{"id", "Test_Archer"}, {"vitals", "Test_Archer"}, {"category", "Test_Archer_Category"}, {"vitalsId", 0x31112222},
                     {"categoryId", 0x31112223}, {"name", "Test_Archer"}, {"vitalsName", "Test_Archer"}, {"family", "Human"},
                     {"creatureType", "Solo"}, {"health", 1.0}, {"level", 1}, {"bodies", json::array({json::array({0, 100})})}, {"points", 0}}}},
                  {"spawnPoints", json::array()},
                  {"routes", json::array()}};
    const json melee = document;
    document["slices"][0]["ranged"] = json::array(
        {json::array({3, 3.25, json::array({release(2.25, "Fast_Ranged3", 5, 0), release(0.6, "Fast_Ranged", 3, 0)})}),
         json::array({2, 2.46667, json::array({release(1.33333, "Damned_2H_Ranged_Bow", 0, 0)})})});
    document["projectiles"] = json::array(
        {json::array({"slices/test/arrow.dynamicslice", "61616161-6161-6161-6161-616161616161", 2, "6C1BE29704877324", 1, 50.0, 10.0, 1.5,
                      json::array({0.0, 0.0, -2.0}), 1.5, 8.0})});
    document["playerCapsule"] = json::array({0.41, 0.8, json::array({0.0, 0.0, 1.0}), json::array({0.0, 0.0, 0.0})});
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "ranged-creatures.json";
    const auto load = [&path](const json& content, gamedata::CreatureData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    check(load(document, data) && data.projectiles().size() == 1 && data.player_capsule() && data.shooting_slices() == 1,
          "a creature file with the ranged attacks, the projectile slices, and the capsule of the player slice loads");
    const gamedata::CreatureSlice& archer = data.slices()[0];
    const gamedata::RangedRelease& basic = archer.ranged[1].releases[0];
    check(archer.ranged.size() == 2 && archer.basicShot == 1U && archer.basic_shot() == &archer.ranged[1]
              && archer.ranged[0].releases[0].time < archer.ranged[0].releases[1].time && near(archer.ranged[0].releases[0].time, 0.6F)
              && basic.attackIndex == 7 && basic.ammoKey == crc_of("Arrow_AI") && basic.tableKey == crc_of("TestBowDamageTable")
              && basic.rowIndex == 0 && basic.damage.row == "Damned_2H_Ranged_Bow" && basic.damage.wire == 5 && basic.lead
              && near(basic.aimSpeed, 50.0F) && near(basic.leadAngle, 10.0F) && near(basic.aimAngle, 45.0F) && near(basic.range, 100.0F),
          "the slice has its ranged attacks with the releases in the sequence of their times, and its basic shot (FireBow) with its "
          "joins: the attack index, the ammunition key, the damage table key, the row place, the row, and the aim values");
    const gamedata::ProjectileSliceData& arrow = data.projectiles()[0];
    check(arrow.slot == 1 && arrow.subId == 2 && arrow.root == 0x6C1BE29704877324ULL && arrow.asset[0] == 0x61 && near(arrow.speed, 50.0F)
              && near(arrow.accel[2], -2.0F) && near(arrow.flyingLifetime, 1.5F) && near(arrow.stuckLifetime, 8.0F)
              && near(data.player_capsule()->radius, 0.41F) && near(data.player_capsule()->height, 0.8F),
          "the projectile slice has its asset, its root, the slot of its Projectile component, and its flight values; the capsule of "
          "the player slice has its radius and its segment");
    check(archer.attacks.empty() && archer.fights() && data.fighting_slices() == 1,
          "a slice with a basic shot and no melee attack fights");
    const auto refused = [&](const std::function<void(json&)>& change, const char* what) {
        json bad = document;
        change(bad);
        gamedata::CreatureData other;
        check(!load(bad, other) && !other.loaded() && !other.error().empty(), std::string("the loader refuses a file: ") + what);
    };
    refused([](json& bad) { bad["slices"][0]["ranged"][1][2][0]["projectile"] = 1; }, "a release names a projectile slice that is not there");
    refused([](json& bad) { bad["slices"][0]["ranged"][1][2][0]["time"] = 3.0; }, "a release after the end of its action");
    refused([](json& bad) { bad["slices"][0]["ranged"][1][0] = 9; }, "a ranged attack names an action that the table does not have");
    refused([](json& bad) { bad["projectiles"][0][4] = 0; }, "a projectile slice with no slot of its Projectile component");
    refused([](json& bad) { bad["projectiles"][0][5] = 0.0; }, "a projectile slice with no speed");
    refused([](json& bad) { bad["slices"][0]["ranged"][1][2][0].erase("tableKey"); }, "a release with no damage table key");
    refused([](json& bad) { bad["slices"][0]["ranged"] = "FireBow"; }, "a member ranged that is not a list");
    refused([](json& bad) { bad["playerCapsule"] = json::array({0.41}); }, "a capsule of the player slice with a wrong form");
    // The default: a file of an older tool (no ranged member, no projectile list) loads, and its slices have no shot.
    gamedata::CreatureData old;
    json olderFile = melee;
    olderFile["slices"][0]["attacks"] =
        json::array({json::array({1, 1.4, json::array({json::array({0.5, 2.2, 0.4, 0.0, "Strike", 6, "Physical", "Test_Bash"})})})});
    check(load(olderFile, old) && old.projectiles().empty() && !old.player_capsule() && old.slices()[0].ranged.empty()
              && !old.slices()[0].basicShot && old.slices()[0].fights() && old.shooting_slices() == 0,
          "a file of a tool from before the stage loads: no ranged attack, no projectile slice; a slice with a melee attack fights");
    // The world with that data: the creature fights in melee, and no shot flies.
    {
        Shop shop(false, false, false);
        gamedata::CreatureSlice melee_archer = archer_slice(true);
        melee_archer.ranged.clear();
        check(shop.host.creatures.set_full({melee_archer}, {archer_kind()}, {}, archer_levels()), "setup: a melee creature with no shot");
        shop.host.world.set_creature_behavior(true);
        world::Entity* creature = shop.host.world.creatures().spawn("Test_Archer", at(kPlace.x + 1.5F, kPlace.y), 90.0F).entity;
        shop.walker.idle(90);
        check(creature != nullptr && shop.host.world.creatures().behavior().counts().attacks >= 1
                  && shop.host.world.creatures().behavior().counts().shots == 0 && shop.host.world.projectiles().creature_counts().launches == 0,
              "a creature of such a file attacks in melee and shoots no projectile");
    }
    // The check of the data tool: the ranged cases of tools/gamedata/check_creature_tool.py.
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::filesystem::path work = options.workDirectory / "check-creature-ranged";
    std::filesystem::create_directories(work);
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + tool.string() + "\" \"" + work.string() + "\"";
    FILE* pipe = popen(command.c_str(), "r");
    std::string output;
    if (pipe != nullptr) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
    }
    const int status = pipe != nullptr ? pclose(pipe) : -1;
    bool all = true;
    for (const char* name : {"ranged-dictionary", "ranged-release", "ranged-override", "ranged-refused", "ranged-projectile"}) {
        all = all && output.find(std::string("ok    ") + name) != std::string::npos;
    }
    check(status == 0 && all, "the check of the data tool passes its ranged cases: the attack index, the joins of a release, the "
                              "override, the refusals, and the projectile slice");
}

/** CRG2: the windup, the release, the repeat time, and the cancels before the release. */
void release_and_generation(const Options&) {
    Range range(false);
    world::Entity* archer = range.archer(10.0F);
    const world::EntityId id = archer->id;
    check(range.until([&] { return range.behavior().counts().shots == 1; }, 60), "the archer sees A at 10 m and starts its shot");
    const auto started = range.now();
    world::Entity* creature = range.host.world.creatures().find(id);
    check(creature->creature.action && creature->creature.action->action == kFireBow && creature->creature.mind.shooting,
          "the creature plays the action FireBow of its slice (the windup)");
    const float health = range.character()->vitals.health;
    range.to(started + seconds(kRelease - 0.05F));
    check(range.shots().launches == 0 && range.projectile_entities() == 0 && range.character()->vitals.health == health,
          "no projectile and no damage before the time of the release of the data");
    range.to(started + seconds(kRelease + 0.05F));
    check(range.shots().launches == 1 && range.projectile_entities() == 1 && range.character()->vitals.health == health,
          "the release at its time: one projectile entity, no damage at the release");
    range.until([&] { return range.ends(world::CreatureShotEnd::hit) == 1; }, 30);
    const world::CreatureDamage expected = range.host.world.combat().creature_damage(
        *range.host.world.creatures().find(id), range.host.creatures.slices()[0].ranged[0].releases[0].damage, *range.character());
    check(range.ends(world::CreatureShotEnd::hit) == 1 && near(range.character()->vitals.health, health - expected.dealt, 0.05F),
          "the projectile hits A: one health change of the damage rule of the creature with the row of the release");
    // The repeat time: the next shot 3 s after the start of the last one (kShotRepeat; the action is 2.47 s).
    check(range.until([&] { return range.behavior().counts().shots == 2; }, 150), "a second shot");
    const float repeat = std::chrono::duration<float>(range.now() - started).count();
    check(repeat >= 3.0F && repeat <= 3.0F + 0.25F, "the second shot starts 3 s after the start of the first one (" + std::to_string(repeat) + " s)");
    // A knockdown before the release: the action is a different one, and the release does not fly.
    const auto second = range.now();
    range.to(second + seconds(0.8F));
    check(range.host.world.creatures().knock(*range.host.world.creatures().find(id), 4, std::chrono::milliseconds(2000)), "setup: a knockdown");
    const std::uint64_t launches = range.shots().launches;
    range.to(second + seconds(kRelease + 0.3F));
    check(range.shots().launches == launches && range.behavior().counts().releasesCancelled == 1,
          "a knockdown before the release: the shot does not fly (a release of a different action)");
    // The death of the archer before the release.
    Range other(false);
    world::Entity* doomed = other.archer(10.0F);
    const world::EntityId doomedId = doomed->id;
    other.until([&] { return other.behavior().counts().shots == 1; }, 60);
    const auto shot = other.now();
    other.to(shot + seconds(0.5F));
    (void)other.host.world.creatures().kill(doomedId);
    other.to(shot + seconds(kRelease + 0.3F));
    check(other.shots().launches == 0 && other.behavior().counts().releasesCancelled == 1, "the death of the archer before the release: no shot");
    // The leave of the target before the release: a character with no session, the target of a command shot.
    Range third(false);
    add_player(third.host.store, "76561202255233029", "Tester C", "cccccccc-1111-4000-8000-0000000000c1", "Gamma", std::nullopt);
    check(third.host.world.spawn_character("cccccccc-1111-4000-8000-0000000000c1", at(kPlace.x, kPlace.y + 30.0F)) == world::SpawnError::none,
          "setup: a character with no session");
    world::Entity* shooter = third.archer(0.0F, 40.0F);
    const world::EntityId gamma = third.host.world.find_character("cccccccc-1111-4000-8000-0000000000c1")->id;
    check(third.behavior().command_shot(*shooter, gamma), "the command shot of the archer at the character");
    const auto commanded = third.now();
    third.to(commanded + seconds(0.6F));
    check(third.host.world.remove_character("cccccccc-1111-4000-8000-0000000000c1", world::LostCause::deleted), "setup: the target leaves");
    third.to(commanded + seconds(kRelease + 0.3F));
    check(third.shots().launches == 0 && third.behavior().counts().releasesCancelled == 1, "the target left the world before the release: no shot");
}

/** CRG3: the segment: one change at the contact; a moving miss, a wall, the life time. */
void segment_wall_and_miss(const Options& options) {
    {
        // The moving miss: A runs to the north from the release on (the lead uses the velocity at the release: 0).
        Range range(false);
        range.archer(10.0F);
        check(range.until([&] { return range.behavior().counts().shots == 1; }, 60), "the archer starts its shot at A");
        const float health = range.character()->vitals.health;
        range.until([&] { return range.shots().launches == 1; }, 60);
        for (int step = 0; step < 15; ++step) {
            range.walker.position.y += 0.17F; // 5 m/s in steps of 1/30 s
            range.step();
        }
        range.step(45);
        check(range.shots().launches == 1 && range.ends(world::CreatureShotEnd::hit) == 0
                  && range.ends(world::CreatureShotEnd::lifetime) + range.ends(world::CreatureShotEnd::range) == 1
                  && range.shots().damage == 0 && range.character()->vitals.health == health,
              "A runs out of the line after the release: the shot keeps its direction and misses (no damage)");
    }
    {
        // A wall of the navigation data 5 m east of A, the archer 10 m east of it.
        Range range(false);
        TestNavRegion region;
        region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
            const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
            if (px >= 2005.0F && px < 2005.5F) {
                floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
            }
        };
        const auto folder = options.workDirectory / "ranged-wall";
        region.write(folder);
        check(range.host.navigation.set_directory(folder.string()), "setup: the navigation data with the wall loads");
        range.step(20);
        world::Entity* archer = range.archer(10.0F);
        range.step(20);
        check(range.behavior().counts().shots == 0, "the archer does not see A through the wall: no shot of the behavior");
        const float health = range.character()->vitals.health;
        check(range.behavior().command_shot(*range.host.world.creatures().find(archer->id), range.character()->id), "the command shot");
        range.until([&] { return range.shots().launches == 1; }, 60);
        const world::EntityId projectile = newest_projectile(range);
        range.step(15);
        const world::Entity* stuck = range.host.world.find(projectile);
        check(range.ends(world::CreatureShotEnd::wall) == 1 && range.shots().damage == 0 && range.character()->vitals.health == health,
              "the wall stops the shot: no damage behind it");
        check(stuck != nullptr && stuck->shot.ended && stuck->shot.contact && stuck->shot.contact->x >= 2005.0F
                  && stuck->shot.contact->x <= 2005.6F,
              "the projectile stays at its contact with the wall (the stuck life time of the slice): the contact is at the wall");
    }
    {
        // The life time: a character 80 m from the archer (1.5 s at 50 m/s is 75 m).
        Range range(false);
        world::Entity* archer = range.archer(80.0F);
        const float health = range.character()->vitals.health;
        check(range.behavior().command_shot(*archer, range.character()->id), "the command shot at 80 m");
        range.step(110);
        check(range.shots().launches == 1 && range.ends(world::CreatureShotEnd::lifetime) == 1 && range.character()->vitals.health == health,
              "the shot ends at the life time of its slice before A: no damage");
    }
}

/** CRG4: two sessions. */
void two_sessions(const Options&) {
    Range range(true);
    range.enter_both();
    range.behavior().set_ignored(kCharacterB, true); // The archer takes A as its target (B is not a target, and no shot hits it).
    range.step(10);
    world::Entity* archer = range.archer(10.0F, 0.5F);
    const world::EntityId archerId = archer->id;
    const std::size_t fromA = range.a.got.size();
    const std::size_t fromB = range.b.got.size();
    const float healthA = range.character()->vitals.health;
    const world::Entity* beta = range.host.world.find_character(kCharacterB);
    const float healthB = beta->vitals.health;
    range.until([&] { return range.shots().launches == 1; }, 90);
    const world::EntityId projectile = newest_projectile(range);
    range.step(3);
    const std::uint32_t inA = interest_in(range.host, 1, projectile);
    const std::uint32_t inB = interest_in(range.host, 2, projectile);
    range.until([&] { return range.ends(world::CreatureShotEnd::hit) == 1; }, 30);
    range.step(20);
    const std::vector<Fragment> statesA = projectile_states(range.a, inA, fromA);
    const std::vector<Fragment> statesB = projectile_states(range.b, inB, fromB);
    std::vector<Fragment> metaA;
    for (const Fragment& fragment : fragments_for(range.a, inA, fromA)) {
        if (fragment.type == 10) {
            metaA.push_back(fragment);
        }
    }
    check(inA != 0 && inB != 0 && metaA.size() == 1 && metaA[0].fields.at({0, 0})[0] == 0x61, "each session observes the projectile: one "
                                                                                                "metadata state with the arrow slice");
    check(statesA.size() == 2 && statesB.size() == 2 && statesA[0].fields == statesB[0].fields && statesA[1].fields == statesB[1].fields,
          "each observer gets the same launch state and the same collision state");
    check(!statesA.empty() && field_u64(statesA[0], 10) == archerId && statesA[0].fields.at({0, 9}) == Bytes{0x00}
              && statesA[0].fields.count({0, 11}) == 1 && statesA[0].fields.at({0, 11}) == Bytes{static_cast<std::uint8_t>(kAttackIndex)}
              && statesA.size() == 2 && statesA[1].fields.at({0, 9}) == Bytes{0x01} && statesA[1].fields.count({0, 7}) == 1,
          "the launch has the owner (the archer), the flags 0, and the attack index; the collision has the flags 1");
    check(stops_of(range.a, inA, fromA) == 1 && stops_of(range.b, inB, fromB) == 1, "each observer gets the stop of the record");
    const float afterA = range.character()->vitals.health;
    check(afterA < healthA && range.host.world.find_character(kCharacterB)->vitals.health == healthB,
          "only the character on the line (A) loses health");
    check(messages_of(range.a, server::rep::kTypeDamageTaken, fromA).size() == 1 && messages_of(range.b, server::rep::kTypeDamageTaken, fromB).empty(),
          "only the damaged owner gets the damage mark (type 3601)");
    const std::uint32_t aInB = interest_in(range.host, 2, range.character()->id);
    bool seen = false;
    for (const Fragment& fragment : fragments_for(range.b, aInB, fromB)) {
        if (fragment.type == 15 && fragment.fields.count({0, 0}) == 1) {
            server::hub::Reader reader(fragment.fields.at({0, 0}));
            seen = seen || near(reader.f32(), afterA, 0.05F);
        }
    }
    check(aInB != 0 && seen, "B observes A and gets the vitals state of A with its new health");
    range.behavior().set_ignored(kCharacterB, false);
    // B steps between the archer and A: the first character on the line takes the shot.
    range.host.world.set_position(range.host.world.find_character(kCharacterB)->id, at(kPlace.x + 5.0F, kPlace.y + 0.25F), false);
    const float beforeB = range.host.world.find_character(kCharacterB)->vitals.health;
    const float beforeA = range.character()->vitals.health;
    range.step(2);
    check(range.behavior().command_shot(*range.host.world.creatures().find(archerId), range.character()->id), "a command shot at A");
    range.until([&] { return range.ends(world::CreatureShotEnd::hit) == 2; }, 90);
    check(range.host.world.find_character(kCharacterB)->vitals.health < beforeB && range.character()->vitals.health == beforeA,
          "B on the line takes the shot that the archer aimed at A");
}

/** CRG5: the late interest, the stop, the death of the archer in the flight. */
void late_interest_and_stop(const Options&) {
    {
        Range range(true);
        range.enter_both();
        // B goes far away: it observes nothing near A.
        (void)range.host.world.travel(kCharacterB, at(kPlace.x - 500.0F, kPlace.y));
        range.step(30);
        world::Entity* archer = range.archer(60.0F);
        check(range.behavior().command_shot(*archer, range.character()->id), "the command shot at A 60 m away");
        range.until([&] { return range.shots().launches == 1; }, 60);
        const world::EntityId projectile = newest_projectile(range);
        const std::uint32_t inA = interest_in(range.host, 1, projectile);
        const std::size_t fromB = range.b.got.size();
        // In the flight (1.2 s), B comes near.
        (void)range.host.world.travel(kCharacterB, at(kPlace.x + 2.0F, kPlace.y + 2.0F));
        range.step(6);
        const std::uint32_t inB = interest_in(range.host, 2, projectile);
        const std::vector<Fragment> launchA = projectile_states(range.a, inA);
        const std::vector<Fragment> lateB = projectile_states(range.b, inB, fromB);
        check(inB != 0 && !lateB.empty() && !launchA.empty() && field_u64(lateB[0], 4) == field_u64(launchA[0], 4)
                  && lateB[0].fields.at({0, 0}) == launchA[0].fields.at({0, 0}) && range.shots().launches == 1,
              "a session that comes near in the flight gets the launch with its time anchor of the release (its client flies it from "
              "there), and no second launch");
        range.until([&] { return range.ends(world::CreatureShotEnd::hit) + range.ends(world::CreatureShotEnd::lifetime) == 1; }, 60);
        range.step(20);
        check(range.projectile_entities() == 0 && stops_of(range.a, inA) == 1 && stops_of(range.b, inB, fromB) == 1,
              "the record stops for each observer at the end, and the entity leaves the world");
    }
    {
        // A wall end: the record stays for the stuck life time; a session that comes near then gets no record of it.
        Range range(true);
        range.enter_both();
        (void)range.host.world.travel(kCharacterB, at(kPlace.x - 500.0F, kPlace.y));
        range.step(30);
        world::Entity* archer = range.archer(10.0F);
        range.until([&] { return range.shots().launches == 1; }, 90);
        const world::EntityId projectile = newest_projectile(range);
        // The end: the projectile system marks the entity ended; the session of B comes near after that.
        range.until([&] { return range.ends(world::CreatureShotEnd::hit) == 1; }, 30);
        const std::size_t fromB = range.b.got.size();
        (void)range.host.world.travel(kCharacterB, at(kPlace.x + 2.0F, kPlace.y + 2.0F));
        range.step(3);
        check(interest_in(range.host, 2, projectile) == 0 && projectile_states(range.b, 0, fromB).empty(),
              "a session that comes near after the end gets no record of the projectile");
        (void)archer;
    }
    {
        // The death of the archer in the flight: the shot ends there, with no damage, and the record stops.
        Range range(false);
        world::Entity* archer = range.archer(60.0F);
        const world::EntityId id = archer->id;
        const float health = range.character()->vitals.health;
        check(range.behavior().command_shot(*archer, range.character()->id), "the command shot at 60 m");
        range.until([&] { return range.shots().launches == 1; }, 60);
        const world::EntityId projectile = newest_projectile(range);
        const std::uint32_t inA = interest_in(range.host, 1, projectile);
        range.step(5);
        (void)range.host.world.creatures().kill(id);
        range.step(5);
        check(range.ends(world::CreatureShotEnd::source) == 1 && range.character()->vitals.health == health && range.projectile_entities() == 0
                  && stops_of(range.a, inA) == 1,
              "the death of the archer in the flight ends its shot with no damage, and the record stops at once");
    }
}

/** CRG6: the bounds of the creature shots. */
void bounded_flights(const Options&) {
    Range range(false);
    world::Entity* archer = range.archer(60.0F);
    const gamedata::RangedRelease& release = range.host.creatures.slices()[0].ranged[0].releases[0];
    std::size_t accepted = 0;
    for (int index = 0; index < 9; ++index) {
        const world::CreatureShot shot{archer->id, &release, at(kPlace.x + 59.0F, kPlace.y, kHeight + 1.5F), {-1.0F, 0.0F, 0.0F}, 0};
        accepted += range.projectiles().launch_creature(shot).has_value() ? 1 : 0;
    }
    check(accepted == world::creature_shot_rules::kCreatureMost && range.shots().refused == 1 && range.projectiles().creature_flights() == 8,
          "a creature has 8 shots in the air at most: the ninth is refused");
    const world::CreatureShot bad{archer->id, &release, at(kPlace.x + 59.0F, kPlace.y, kHeight + 1.5F), {0.0F, 0.0F, 0.0F}, 0};
    check(!range.projectiles().launch_creature(bad), "a direction that is not a unit vector is refused");
    range.step(60);
    check(range.projectiles().creature_flights() == 0 && range.projectile_entities() == 0 && range.projectiles().creature_holds() == 0,
          "each flight ends: no flight, no hold, and no projectile entity stays");
}

/** CRG9: the load. */
void ranged_load(const Options&) {
    // The spawn points of the world: 32,840 far points of the archer (no character near them), or 840 for the compare.
    const auto points = [](std::size_t count) {
        std::vector<gamedata::SpawnPoint> list;
        for (std::size_t index = 0; index < count; ++index) {
            gamedata::SpawnPoint point;
            point.id = "p" + std::to_string(index);
            point.kind = 0;
            point.level = 1;
            point.position = state::WorldPosition{10000.0F + static_cast<float>(index % 200) * 20.0F,
                                                  10000.0F + static_cast<float>(index / 200) * 20.0F, kHeight};
            point.ground = kHeight;
            list.push_back(point);
        }
        return list;
    };
    struct Result {
        double milliseconds{0.0};
        world::CreatureShotCounts counts;
        std::uint64_t shots{0};
        std::uint64_t sensed{0};
        std::size_t creatures{0};
    };
    const auto run = [&](std::size_t count) {
        ServerHost host;
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        check(host.creatures.set_full({archer_slice(false)}, {archer_kind()}, points(count), archer_levels(), {}, {arrow_slice()},
                                      gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}),
              "setup: the archer data with " + std::to_string(count) + " spawn points");
        check(host.combat.set(shop_combat()), "setup: the combat tables");
        const unsigned kPlayers = 50;
        std::vector<world::EntityId> archers;
        for (unsigned index = 0; index < kPlayers; ++index) {
            char id[40];
            std::snprintf(id, sizeof(id), "%08x-2222-4000-8000-%012x", 0x20000000U + index, index);
            add_player(host.store, "765612022553" + std::to_string(20000 + index), "Tester " + std::to_string(index), id,
                       "Shot" + std::to_string(index), std::nullopt);
            const state::WorldPosition place{500.0F + 120.0F * static_cast<float>(index % 10), 500.0F + 120.0F * static_cast<float>(index / 10),
                                             kHeight};
            (void)host.world.spawn_character(id, place);
            // An archer 10 m east of each character, its face to it: it sees it and shoots it again and again.
            world::Entity* archer = host.world.creatures().spawn("Test_Archer", {place.x + 10.0F, place.y, kHeight}, 90.0F).entity;
            archers.push_back(archer->id);
        }
        // 150 more flights at the start (3 for each archer, to the south, 75 m each): 200 flights in the air with the shots.
        const gamedata::RangedRelease& release = host.creatures.slices()[0].ranged[0].releases[0];
        for (const world::EntityId id : archers) {
            const world::Entity* archer = host.world.creatures().find(id);
            for (int extra = 0; extra < 3; ++extra) {
                (void)host.world.projectiles().launch_creature(
                    world::CreatureShot{id, &release, {archer->position.x, archer->position.y - 1.0F, kHeight + 1.5F}, {0.0F, -1.0F, 0.0F}, 0});
            }
        }
        Result result;
        const auto started = std::chrono::steady_clock::now();
        for (int tick = 0; tick < 1000; ++tick) {
            host.clock.advance(std::chrono::milliseconds(33));
            host.world.tick();
        }
        result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        result.counts = host.world.projectiles().creature_counts();
        result.shots = host.world.creatures().behavior().counts().shots;
        result.sensed = host.world.creatures().behavior().counts().sensed;
        result.creatures = host.world.creatures().count();
        return result;
    };
    const Result full = run(32840);
    const Result few = run(840);
    note("32,840 points, 50 characters, 50 archers: 1000 ticks (33 s of the world clock) need " + std::to_string(full.milliseconds)
         + " ms; " + std::to_string(full.shots) + " shots, " + std::to_string(full.counts.launches) + " launches, "
         + std::to_string(full.counts.largest) + " in the air at most, " + std::to_string(full.counts.steps) + " flight steps, "
         + std::to_string(full.counts.looked) + " characters looked at, " + std::to_string(full.counts.wallTests)
         + " wall tests; with 840 points: " + std::to_string(few.milliseconds) + " ms, " + std::to_string(few.counts.looked) + " looks");
    check(full.counts.largest >= 200 && full.shots >= 50ULL * 10 && full.counts.launches >= 150 + 50ULL * 10,
          "200 flights in the air at one time, and each archer shoots again and again (10 shots or more in 33 s)");
    check(full.counts.looked == few.counts.looked && full.counts.wallTests == few.counts.wallTests && full.counts.steps == few.counts.steps
              && full.shots == few.shots && full.sensed == few.sensed,
          "the work depends on the archers and their flights, not on the far spawn points: the same steps, looks, and wall tests with "
          "840 and with 32,840 points");
    check(full.milliseconds < 3000.0, "1000 ticks with 50 shooting archers and 200 flights need less than 3 s of real time");
}

} // namespace

std::vector<Case> ranged_cases() {
    return {{"CRG1", "ranged (3.4f-A): the export, the dictionary, the loader, the default with no ranged data, the tool check", ranged_data},
            {"CRG2", "ranged (3.4f-A): the windup, the release at its time, the repeat time, the cancels before the release",
             release_and_generation},
            {"CRG3", "ranged (3.4f-A): one change at the contact, a moving miss, a wall, the life time", segment_wall_and_miss},
            {"CRG4", "ranged (3.4f-A): two sessions, one record for each, the damage mark of the owner, the first on the line", two_sessions},
            {"CRG5", "ranged (3.4f-A): the late interest, the stop, the death of the archer in the flight", late_interest_and_stop},
            {"CRG6", "ranged (3.4f-A): the bounds of the shots of a creature", bounded_flights},
            {"CRG9", "ranged (3.4f-A): the load with 32,840 points, 50 archers, 200 flights, a time limit", ranged_load}};
}

} // namespace soulwarden::backend::tests

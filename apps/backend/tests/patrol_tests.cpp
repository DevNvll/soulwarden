// Tests of stage 3.4e: patrol and wander, and the reset of a creature (protocol notes 4.14 and
// 4.17).
//
//   PT1   the creature file of the format 3: the route of a spawn point and the wander values of
//         a slice, a file whose parts do not fit together, and a file of the format 2
//   PT2   the patrol: a loop of four waypoints, the ambient speed, the walk action, the wait at
//         each waypoint, the direction of a waypoint, and the task of a waypoint
//   PT3   the policies of a route: back and forth, at random, and a route of one waypoint whose
//         task loops (the creature stays)
//   PT4   the wander: random places of the wander range from the generator of the world (one
//         start value gives one sequence), the wait between two walks
//   PT5   a creature on a patrol takes a target: the anchor of the leash, the leash from the
//         anchor, the way back to the anchor, and the patrol goes on
//   PT6   the reset: on its way back a creature takes no damage and no target by a hit, its
//         vitals state has the immune mark, and it has its full health at its anchor
//   PT7   a way back with no path: the creature walks its way out back, and it jumps to its
//         anchor only when no session can observe it
//   PT8   no way to the target: the place nearest to it, the wait with the target, an attack in
//         the reach, and the reset after the wait
//   PT9   the load: 50 players with 30 creatures that roam near each one, and creatures far from
//         each player that stand, with a time limit
//   PT10  two sessions: the two observers get the same patrol, and the creature takes one of
//         the two players as its target
//
// The accounts, the characters, the creature kinds, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_navigation.h"
#include "support/test_players.h"
#include "world/behavior.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::CreatureMode;
using world::StepKind;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000e1";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-0000000000e2";
constexpr float kHeight = 50.0F;
constexpr std::uint32_t kSlots[] = {5, 9, 15};
/** The actions of the test slice. */
constexpr std::uint32_t kBite = 3;
constexpr std::uint32_t kRun = 4;
constexpr std::uint32_t kWalk = 5;
constexpr std::uint32_t kTask = 6;
constexpr std::uint32_t kLoopTask = 7;
constexpr float kAmbient = 2.0F;
constexpr float kRunSpeed = 8.0F;
/** Fields of the vitals state: the immune mark. */
constexpr unsigned kFieldInvulnerable = 11;
constexpr unsigned kFieldDisplayImmune = 12;

struct Sink final : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

state::WorldPosition at(float x, float y, float z = kHeight) {
    return state::WorldPosition{x, y, z};
}

bool near(float value, float expected, float tolerance = 0.01F) {
    return std::fabs(value - expected) <= tolerance;
}

gamedata::CreatureSlice test_slice(const char* path, std::uint8_t mark, bool wander) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x1000000000000000ULL + mark;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kSlots[0];
    slice.vitalsSlot = kSlots[1];
    slice.markerSlot = kSlots[2];
    slice.slots = {{kSlots[0], "ActionList", 11}, {kSlots[1], "Vitals", 15}, {kSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false},       {"Reaction", "r_R2_F", 5, true},
                     {"Death", "r_Death", 4, true},             {"Attack_Bite", "Attack_Bite", 7, false},
                     {"Move_Combat", "Move_Combat", 11, false}, {"Move", "Move", 12, false},
                     {"POI_Test_Sit", "Sit", 13, false},        {"POI_Test_Eat_LOOP", "Eat", 14, false}};
    gamedata::CreatureBehavior values;
    values.sightAngle = 75.0F;
    values.sightDistance = 12.0F;
    values.combatAngle = 180.0F;
    values.combatDistance = 30.0F;
    values.restLeash = 20.0F;
    values.combatLeash = 70.0F;
    values.walkSpeed = 4.0F;
    values.runSpeed = kRunSpeed;
    values.ambientSpeed = kAmbient;
    values.wander = wander;
    values.wanderNearest = 4.0F;
    values.wanderFarthest = 6.0F;
    slice.behavior = values;
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = 2.2F;
    contact.factor = 0.2F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Bite";
    slice.attacks = {gamedata::CreatureAttack{kBite, 1.4F, {contact}}};
    return slice;
}

gamedata::CreatureKind test_kind(const char* id, std::uint32_t vitalsId, std::uint32_t slice) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = vitalsId + 1;
    kind.name = id;
    kind.vitalsName = id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 18;
    kind.bodies = {{slice, 100}};
    return kind;
}

gamedata::PatrolWaypoint waypoint(float x, float y, float low, float high, const char* task = "", bool align = false, float yaw = 0.0F) {
    gamedata::PatrolWaypoint point;
    point.position = at(x, y);
    point.minWait = low;
    point.maxWait = high;
    point.task = task;
    point.align = align;
    point.yaw = yaw;
    return point;
}

/**
 * The routes of the cases: 0 a loop of four waypoints around the point (1010, 1000), 1 back and
 * forth on a line, 2 at random, 3 one waypoint with a task that loops.
 */
std::vector<gamedata::PatrolRoute> test_routes() {
    gamedata::PatrolRoute loop{gamedata::PatrolPolicy::loop,
                               {waypoint(1006.0F, 1000.0F, 1.0F, 2.0F), waypoint(1010.0F, 1004.0F, 2.0F, 3.0F, "POI_Test_Sit", true, 180.0F),
                                waypoint(1014.0F, 1000.0F, 1.0F, 1.0F, "POI_Not_In_The_Table"), waypoint(1010.0F, 996.0F, -1.0F, -1.0F)}};
    gamedata::PatrolRoute bounce{gamedata::PatrolPolicy::bounce,
                                 {waypoint(1100.0F, 1000.0F, 0.5F, 0.5F), waypoint(1104.0F, 1000.0F, 0.5F, 0.5F),
                                  waypoint(1108.0F, 1000.0F, 0.5F, 0.5F)}};
    gamedata::PatrolRoute random{gamedata::PatrolPolicy::random,
                                 {waypoint(1200.0F, 1000.0F, 0.2F, 0.2F), waypoint(1204.0F, 1000.0F, 0.2F, 0.2F),
                                  waypoint(1204.0F, 1004.0F, 0.2F, 0.2F), waypoint(1200.0F, 1004.0F, 0.2F, 0.2F)}};
    gamedata::PatrolRoute stay{gamedata::PatrolPolicy::loop, {waypoint(1303.0F, 1000.0F, -1.0F, -1.0F, "POI_Test_Eat_LOOP", true, 90.0F)}};
    return {loop, bounce, random, stay};
}

gamedata::SpawnPoint spawn_point(const char* id, std::uint32_t kind, float x, float y, std::optional<std::uint32_t> route) {
    gamedata::SpawnPoint point;
    point.id = id;
    point.kind = kind;
    point.level = 18;
    point.position = at(x, y);
    point.ground = kHeight;
    point.route = route;
    return point;
}

/** The data of the cases: a patroller (kind 0, no wander) and a wanderer (kind 1), and four spawn points with routes. */
bool set_patrol_data(ServerHost& host, std::vector<gamedata::SpawnPoint> points, std::vector<gamedata::PatrolRoute> routes) {
    return host.creatures.set_full({test_slice("slices/test/patroller.dynamicslice", 0x41, false),
                                    test_slice("slices/test/wanderer.dynamicslice", 0x42, true)},
                                   {test_kind("Test_Patroller", 0x41414141, 0), test_kind("Test_Wanderer", 0x42424242, 1)},
                                   std::move(points), {{18, 1690.875977F, 616.41F, 180.0F}}, std::move(routes));
}

std::vector<gamedata::SpawnPoint> test_points() {
    return {spawn_point("p-loop", 0, 1010.0F, 1000.0F, 0), spawn_point("p-bounce", 0, 1104.0F, 1000.0F, 1),
            spawn_point("p-random", 0, 1202.0F, 1002.0F, 2), spawn_point("p-stay", 0, 1300.0F, 1000.0F, 3)};
}

/** Ticks of 10 ms of the game server. */
void pass(ServerHost& host, float seconds, std::initializer_list<FakeClient*> clients = {}) {
    host.steps(static_cast<int>(std::lround(seconds * 100.0F)), clients);
}

/** A host with the patrol data, a flat terrain, and one character with no session that the creatures ignore. */
struct PatrolHost {
    ServerHost host;
    Player alpha;

    explicit PatrolHost(const state::WorldPosition& place, const service::RepServerOptions& options = {},
                        std::vector<gamedata::SpawnPoint> points = test_points())
        : host(options), alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", place)) {
        if (!set_patrol_data(host, std::move(points), test_routes())) {
            throw std::runtime_error("patrol test: the creature data is not valid: " + host.creatures.error());
        }
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        host.world.set_random_seed(0x5EED0001ULL);
    }
    world::Creatures& creatures() {
        return host.world.creatures();
    }
    world::Behavior& behavior() {
        return host.world.creatures().behavior();
    }
    /** @return The creature of a spawn point of the data, or null. */
    world::Entity* of_point(std::size_t index) {
        return creatures().find(creatures().spawn_points()[index].entity);
    }
};

/** @return The action of the movement record of a creature: the server action, the locomotion, or none (-1). */
int playing(const world::Entity& creature) {
    const server::rep::CreatureScope scope = server::rep::creature_scope(creature.creature, false);
    return scope.action != nullptr ? static_cast<int>(scope.action->action) : -1;
}

/** @return The fields of a vitals state of a creature for a session with the revision `since` (0: its first state). */
Fragment vitals_for(ServerHost& host, const world::Entity& creature, world::Revision since) {
    const std::vector<Bytes> parts =
        server::rep::encode_creature_vitals_state(creature, server::rep::View::observer, server::rep::PartsRequest{since, 4096, &host.clock});
    Fragment part;
    part.type = 15;
    std::size_t consumed = 0;
    if (parts.empty() || !detail::read_state(parts.front(), consumed, part) || consumed != parts.front().size()) {
        throw std::runtime_error("patrol test: a vitals state did not decode");
    }
    return part;
}

int field_byte(const Fragment& part, unsigned field) {
    const auto found = part.fields.find({0, field});
    return found == part.fields.end() || found->second.empty() ? -1 : found->second[0];
}

/** PT1: the creature file of the format 3. */
void patrol_data_file(const Options& options) {
    const auto slice = [](const char* path, const char* asset, const char* root) {
        return json{{"path", path}, {"asset", asset}, {"sub", 2}, {"root", root}, {"interestRadius", 60.0},
                    {"movement", 5}, {"vitals", 9}, {"marker", 15},
                    {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                    {"components", json::array({json::array({root, "0000000000000A01", "Root/Vitals"})})},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Move", "Move", 12, false}, {"Move_Combat", "Move_Combat", 11, false}}},
                    {"behavior", {{"sight", {75.0, 12.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}},
                                  {"speed", {4.75, 9.23}}, {"ambient", 1.75}, {"wander", {0, 5.0, 10.0}}}}};
    };
    const auto kind = [](const char* id, std::uint32_t vitals, int body) {
        return json{{"id", id}, {"vitals", id}, {"category", std::string(id) + "_Category"}, {"vitalsId", vitals},
                    {"categoryId", vitals + 1}, {"name", id}, {"vitalsName", id}, {"family", "Wildlife"},
                    {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({body, 100})})}};
    };
    json document{{"format", 3},
                  {"levels", {{18, 1690.875977, 616.41, 180.0}}},
                  {"slices", {slice("slices/test/wolf.dynamicslice", "11111111-1111-1111-1111-111111111111", "1000000000000011"),
                              slice("slices/test/risen.dynamicslice", "22222222-2222-2222-2222-222222222222", "1000000000000022")}},
                  {"kinds", {kind("Test_Wolf", 0x11112222, 0), kind("Test_Risen", 0x33334444, 1)}},
                  {"spawnPoints",
                   {{"p-route", 0, 18, 1010.0, 1000.0, 50.0, 90.0, 360.0, 360.0, 50.0, 0},
                    {"p-no-route", 1, 18, 1100.0, 1000.0, 50.0, 0.0, 360.0, 360.0, 50.0, nullptr}}},
                  {"routes",
                   {{0, {{1007.5, 1000.0, 50.0, 50.0, 20.0, 30.0, "POI_Sit", 0}, {1012.5, 1000.0, 50.0, 270.0, 10.0, 15.0, "POI_Eat", 1},
                         {1010.0, 1002.5, 50.0, 45.0, -1.0, -1.0, "", 0}}}}}};
    document["slices"][1]["behavior"]["wander"] = {1, 4.0, 6.0};
    document["slices"][1]["behavior"]["ambient"] = 1.15;
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "patrol-creatures.json";
    const auto load = [&path](const json& content, gamedata::CreatureData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    check(load(document, data) && data.loaded() && data.routes().size() == 1 && data.spawn_points().size() == 2,
          "a creature file of the format 3 loads, with its route");
    const gamedata::PatrolRoute* route = data.route_of(data.spawn_points()[0]);
    check(route != nullptr && route->policy == gamedata::PatrolPolicy::loop && route->points.size() == 3
              && route->points[0].position.x == 1007.5F && route->points[0].minWait == 20.0F && route->points[0].maxWait == 30.0F
              && route->points[0].task == "POI_Sit" && !route->points[0].align && route->points[1].align
              && route->points[1].yaw == 270.0F && route->points[2].minWait < 0.0F
              && data.route_of(data.spawn_points()[1]) == nullptr,
          "a spawn point has its route: the policy, and each waypoint with its place, its wait range, its task, and its direction");
    const gamedata::CreatureSlice& wolf = data.slices()[0];
    const gamedata::CreatureSlice& risen = data.slices()[1];
    check(wolf.behavior && near(wolf.behavior->ambientSpeed, 1.75F) && !wolf.behavior->wander && risen.behavior->wander
              && risen.behavior->wanderNearest == 4.0F && risen.behavior->wanderFarthest == 6.0F && near(risen.behavior->ambientSpeed, 1.15F)
              && wolf.walkAction == 1U && wolf.moveAction == 2U,
          "a slice has its ambient speed and its wander values, and its walk action (Move) and its run action (Move_Combat)");
    check(data.routed_points() == 1 && data.wandering_kinds() == 1, "the counts: 1 spawn point with a route, 1 kind that wanders");

    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json wrong = document;
        change(wrong);
        gamedata::CreatureData other;
        const bool loaded = load(wrong, other);
        if (loaded || other.loaded()) {
            note(std::string("accepted: ") + what);
        }
        return !loaded && !other.loaded() && !other.error().empty();
    };
    bool all = true;
    all = refused("a route index outside the list", [](json& d) { d["spawnPoints"][0][10] = 4; }) && all;
    all = refused("a route index that is a text", [](json& d) { d["spawnPoints"][0][10] = "0"; }) && all;
    all = refused("a spawn point with 10 values in the format 3", [](json& d) { d["spawnPoints"][1].erase(10); }) && all;
    all = refused("no list of the routes", [](json& d) { d.erase("routes"); }) && all;
    all = refused("a route with no waypoint", [](json& d) { d["routes"][0][1] = json::array(); }) && all;
    all = refused("a policy 4", [](json& d) { d["routes"][0][0] = 4; }) && all;
    all = refused("a waypoint 300 m from its spawn point", [](json& d) { d["routes"][0][1][0][0] = 1310.0; }) && all;
    all = refused("a wait whose largest value is below the smallest", [](json& d) { d["routes"][0][1][0][5] = 10.0; }) && all;
    all = refused("a wait of 2 hours", [](json& d) { d["routes"][0][1][0][5] = 7200.0; }) && all;
    all = refused("one negative wait value and one positive", [](json& d) { d["routes"][0][1][2][5] = 5.0; }) && all;
    all = refused("a waypoint with 7 values", [](json& d) { d["routes"][0][1][0].erase(7); }) && all;
    all = refused("an alignment value 2", [](json& d) { d["routes"][0][1][0][7] = 2; }) && all;
    all = refused("a wander with a nearest value above its farthest", [](json& d) { d["slices"][1]["behavior"]["wander"] = {1, 7.0, 6.0}; }) && all;
    all = refused("a wander flag 2", [](json& d) { d["slices"][1]["behavior"]["wander"][0] = 2; }) && all;
    all = refused("an ambient speed below 0", [](json& d) { d["slices"][0]["behavior"]["ambient"] = -1.0; }) && all;
    check(all, "a file whose parts do not fit together gives no data, as a whole: 15 forms of a wrong route, spawn point, "
               "and wander value (a note above names each form that was accepted)");

    // A file of the format 2: the creatures have no route and no wander.
    json old = document;
    old["format"] = 2;
    old.erase("routes");
    for (json& point : old["spawnPoints"]) {
        point.erase(10);
    }
    for (json& entry : old["slices"]) {
        entry["behavior"].erase("ambient");
        entry["behavior"].erase("wander");
    }
    gamedata::CreatureData before;
    check(load(old, before) && before.routes().empty() && !before.spawn_points()[0].route && !before.slices()[1].behavior->wander
              && before.slices()[1].behavior->ambientSpeed == 0.0F && before.routed_points() == 0,
          "a file of the format 2 (a tool from before stage 3.4e) loads, with no route and no wander");
    std::filesystem::remove(path);
}

/** PT2: the patrol of a loop. */
void patrol_loop(const Options&) {
    PatrolHost p(at(1000.0F, 1000.0F));
    ServerHost& host = p.host;
    check(host.world.spawn_character(kCharacterA, at(1000.0F, 1000.0F)) == world::SpawnError::none, "a character is in the world");
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 1.2F);
    world::Entity* loop = p.of_point(0);
    world::Entity* bounce = p.of_point(1);
    check(loop != nullptr && bounce != nullptr && p.behavior().roaming(loop->id) && !p.behavior().roaming(bounce->id),
          "the creature 10 m from the character roams; the creature 104 m from it (more than the 80 m of the leave distance of its "
          "interest) stands");
    // The first waypoint is 4 m to the west: 2 s with the ambient speed of 2 m/s.
    const world::Revision before = host.world.revision();
    float largestStep = 0.0F;
    state::WorldPosition last = loop->position;
    bool walked = false;
    for (int tick = 0; tick < 300 && world::horizontal_distance(loop->position, at(1006.0F, 1000.0F)) > 0.3F; ++tick) {
        host.steps(1, {});
        largestStep = std::max(largestStep, world::horizontal_distance(loop->position, last));
        last = loop->position;
        walked = walked || playing(*loop) == static_cast<int>(kWalk);
    }
    note("the largest step " + std::to_string(largestStep) + " m");
    check(walked && largestStep <= kAmbient * 0.2F + 0.01F && largestStep >= kAmbient * 0.2F - 0.05F
              && loop->creature.moving && loop->creature.moving->action == kWalk && host.world.revision() > before,
          "the creature walks to the first waypoint: steps of 0.4 m each 200 ms (the ambient speed of its slice), with the "
          "walk action Move in one record (not Move_Combat)");
    host.steps(25, {});
    check(world::horizontal_distance(loop->position, at(1006.0F, 1000.0F)) <= 0.3F && !loop->creature.moving && playing(*loop) == -1,
          "at the waypoint the walk ends: the observers get the scope fields with no record (the idle action)");
    // The waits and the waypoints of 40 s: the sequence 0 1 2 3 0 ..., and each wait in the range of its waypoint.
    struct Visit {
        std::uint32_t waypoint;
        float arrived;
        float left;
    };
    const std::vector<state::WorldPosition> waypoints = {at(1006.0F, 1000.0F), at(1010.0F, 1004.0F), at(1014.0F, 1000.0F),
                                                         at(1010.0F, 996.0F)};
    std::vector<Visit> visits;
    std::optional<std::uint32_t> at_waypoint = 0;
    visits.push_back({0, 2.0F, -1.0F});
    bool tasked = false;
    bool aligned = false;
    for (int tick = 0; tick < 4000; ++tick) {
        const state::WorldPosition place = loop->position;
        host.steps(1, {});
        const float now = 2.0F + static_cast<float>(tick) * 0.01F;
        if (at_waypoint && world::horizontal_distance(loop->position, place) > 1.0e-4F) {
            visits.back().left = now;
            at_waypoint.reset();
        }
        for (std::uint32_t index = 0; index < waypoints.size(); ++index) {
            if (!at_waypoint && world::horizontal_distance(loop->position, waypoints[index]) <= 0.3F && !loop->creature.moving) {
                at_waypoint = index;
                visits.push_back({index, now, -1.0F});
            }
        }
        if (at_waypoint == 1U) {
            tasked = tasked || (loop->creature.action && loop->creature.action->action == kTask);
            aligned = aligned || near(loop->creature.yaw, 180.0F);
        }
    }
    bool sequence = visits.size() >= 6;
    bool waits = true;
    for (std::size_t index = 0; index + 1 < visits.size(); ++index) {
        sequence = sequence && visits[index + 1].waypoint == (visits[index].waypoint + 1) % 4;
        if (visits[index].left < 0.0F) {
            continue;
        }
        const float wait = visits[index].left - visits[index].arrived;
        const float low = visits[index].waypoint == 0 ? 1.0F : visits[index].waypoint == 1 ? 2.0F : visits[index].waypoint == 2 ? 1.0F : 0.0F;
        const float high = visits[index].waypoint == 0 ? 2.0F : visits[index].waypoint == 1 ? 3.0F : visits[index].waypoint == 2 ? 1.0F : 0.0F;
        // A step of the behavior is 200 ms: a wait ends at the first step after its time.
        waits = waits && wait >= low - 0.01F && wait <= high + 0.45F;
        note("waypoint " + std::to_string(visits[index].waypoint) + ": wait " + std::to_string(wait) + " s");
    }
    check(sequence, "the creature goes to its waypoints in the sequence of the loop: 0, 1, 2, 3, 0, ...");
    check(waits, "it waits at each waypoint for a time of its range (1 s to 2 s, 2 s to 3 s, 1 s), and it does not wait at a "
                 "waypoint with the wait -1");
    check(tasked && aligned, "at the waypoint with a task that the action table has, the creature plays the task, and it takes "
                             "the direction of the waypoint (the mark m_matchAlignment)");
    check(p.behavior().counts().waypoints >= 6 && p.behavior().active_count() == 0 && loop->creature.mind.mode == CreatureMode::rest,
          "the patrol is a roam at rest: the creature has no target, and it is not in the step of the fights");
}

/** PT3: the policies of a route, and a route of one waypoint. */
void patrol_policies(const Options&) {
    PatrolHost p(at(1150.0F, 1000.0F));
    ServerHost& host = p.host;
    (void)host.world.spawn_character(kCharacterA, at(1150.0F, 1000.0F));
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 1.2F);
    world::Entity* bounce = p.of_point(1);
    world::Entity* random = p.of_point(2);
    check(bounce != nullptr && random != nullptr && p.behavior().roaming(bounce->id) && p.behavior().roaming(random->id),
          "the creatures of the route back and forth and of the random route roam");
    std::vector<std::uint32_t> bounceOrder;
    std::vector<std::uint32_t> randomOrder;
    std::uint32_t lastBounce = 99;
    std::uint32_t lastRandom = 99;
    for (int tick = 0; tick < 6000; ++tick) {
        host.steps(1, {});
        if (bounce->creature.mind.waypoint != lastBounce) {
            lastBounce = bounce->creature.mind.waypoint;
            bounceOrder.push_back(lastBounce);
        }
        if (random->creature.mind.waypoint != lastRandom) {
            lastRandom = random->creature.mind.waypoint;
            randomOrder.push_back(lastRandom);
        }
    }
    std::string text;
    for (const std::uint32_t index : bounceOrder) {
        text += std::to_string(index) + " ";
    }
    note("back and forth: " + text);
    const std::vector<std::uint32_t> expected = {0, 1, 2, 1, 0, 1, 2, 1, 0};
    check(bounceOrder.size() >= expected.size() && std::equal(expected.begin(), expected.end(), bounceOrder.begin()),
          "a route back and forth: 0, 1, 2, 1, 0, 1, 2, ...");
    bool different = randomOrder.size() >= 6;
    std::set<std::uint32_t> seen(randomOrder.begin(), randomOrder.end());
    for (std::size_t index = 0; index + 1 < randomOrder.size(); ++index) {
        different = different && randomOrder[index] != randomOrder[index + 1];
    }
    check(different && seen.size() == 4, "a random route: each next waypoint is a different one of the route, and the creature "
                                         "goes to each waypoint");
    // The route of one waypoint with a task that loops and the wait -1: the creature goes to it and stays.
    host.world.set_position(host.world.find_character(kCharacterA)->id, at(1290.0F, 1000.0F), false);
    pass(host, 6.0F);
    world::Entity* stay = p.of_point(3);
    check(stay != nullptr && world::horizontal_distance(stay->position, at(1303.0F, 1000.0F)) <= 0.3F && stay->creature.mind.settled
              && !p.behavior().roaming(stay->id) && stay->creature.action && stay->creature.action->action == kLoopTask
              && !stay->creature.action->ends && near(stay->creature.yaw, 90.0F),
          "a route of one waypoint with the wait -1 and a task that loops: the creature walks to it and stays there with its "
          "task (an action with no end) and the direction of the waypoint, and it needs no more time");
}

/** PT4: the wander. */
void wander(const Options&) {
    const auto goals = [](std::uint64_t seed, std::vector<state::WorldPosition>& found, std::vector<float>& waits) {
        PatrolHost p(at(1000.0F, 1000.0F));
        ServerHost& host = p.host;
        host.world.set_random_seed(seed);
        (void)host.world.spawn_character(kCharacterA, at(1000.0F, 1000.0F));
        p.behavior().set_ignored(kCharacterA, true);
        world::Entity* risen = p.creatures().spawn("Test_Wanderer", at(1050.0F, 1000.0F), 0.0F).entity;
        std::optional<state::WorldPosition> goal;
        float arrived = -1.0F;
        for (int tick = 0; tick < 6000 && risen != nullptr; ++tick) {
            host.steps(1, {});
            const float now = static_cast<float>(tick) * 0.01F;
            if (risen->creature.mind.roamGoal && (!goal || world::horizontal_distance(*goal, *risen->creature.mind.roamGoal) > 0.01F)) {
                goal = risen->creature.mind.roamGoal;
                found.push_back(*goal);
                if (arrived >= 0.0F) {
                    waits.push_back(now - arrived);
                }
                arrived = -1.0F;
            }
            if (!risen->creature.mind.roamGoal && goal && arrived < 0.0F) {
                arrived = now;
            }
        }
        return risen != nullptr && p.behavior().roaming(risen->id) && p.behavior().counts().wanderPlaces >= 3;
    };
    std::vector<state::WorldPosition> first;
    std::vector<state::WorldPosition> again;
    std::vector<state::WorldPosition> other;
    std::vector<float> waits;
    std::vector<float> unused;
    check(goals(0x1111, first, waits), "a creature of a kind that wanders, with no route (a creature of a command), wanders: it "
                                       "goes to three places and more in 60 s");
    bool inRange = !first.empty();
    for (const state::WorldPosition& place : first) {
        const float away = world::horizontal_distance(place, at(1050.0F, 1000.0F));
        inRange = inRange && away >= 4.0F - 0.01F && away <= 6.0F + 0.01F && place.z == kHeight;
    }
    check(inRange, "each place of the wander is 4 m to 6 m from the home of the creature (its wander range), on the ground");
    bool waited = !waits.empty();
    for (const float wait : waits) {
        waited = waited && wait >= 4.0F - 0.01F && wait <= 12.0F + 0.25F;
    }
    check(waited, "between two walks the creature waits 4 s to 12 s");
    (void)goals(0x1111, again, unused);
    (void)goals(0x2222, other, unused);
    const auto same = [](const std::vector<state::WorldPosition>& a, const std::vector<state::WorldPosition>& b) {
        if (a.size() < 3 || b.size() < 3) {
            return false;
        }
        for (std::size_t index = 0; index < 3; ++index) {
            if (world::horizontal_distance(a[index], b[index]) > 0.001F) {
                return false;
            }
        }
        return true;
    };
    check(same(first, again) && !same(first, other),
          "the places come from the random numbers of the world: one start value gives the same places, a different one "
          "different places");
}

/** PT5: a creature on a patrol takes a target: the anchor, the leash from it, and the patrol after the way back. */
void patrol_and_fight(const Options&) {
    PatrolHost p(at(1000.0F, 1000.0F));
    ServerHost& host = p.host;
    (void)host.world.spawn_character(kCharacterA, at(1000.0F, 1000.0F));
    world::Entity* player = host.world.find_character(kCharacterA);
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 1.2F);
    world::Entity* loop = p.of_point(0);
    const world::EntityId id = loop->id;
    // The creature walks to its first waypoint; the character hits it on its way.
    pass(host, 1.0F);
    check(loop->creature.moving && loop->creature.moving->action == kWalk, "setup: the creature walks its patrol");
    p.behavior().set_ignored(kCharacterA, false);
    const state::WorldPosition anchor = loop->position;
    const std::uint32_t waypoint = loop->creature.mind.waypoint;
    check(p.creatures().hurt(id, 10.0F, player->id).error == world::CreatureError::none && loop->creature.mind.mode == CreatureMode::fight
              && world::horizontal_distance(loop->creature.mind.anchor, anchor) < 0.01F && !p.behavior().roaming(id),
          "a hit gives the creature its target: its anchor is the place where it took the target (not its spawn point), and it "
          "does not roam");
    pass(host, 0.4F);
    check(loop->creature.moving && loop->creature.moving->action == kRun, "on the way to its target the creature runs (Move_Combat)");
    // The character goes away to the east, 12 m each second: the leash of 70 m is a distance from the anchor.
    for (int step = 0; step < 60 && loop->creature.mind.mode == CreatureMode::fight; ++step) {
        host.world.set_position(player->id, at(player->position.x + 2.4F, 1000.0F), true);
        pass(host, 0.2F);
    }
    const float fromAnchor = world::horizontal_distance(loop->position, anchor);
    const float fromHome = world::horizontal_distance(loop->position, at(1010.0F, 1000.0F));
    note("the creature lost its target " + std::to_string(fromAnchor) + " m from its anchor, " + std::to_string(fromHome)
         + " m from its spawn point");
    check(loop->creature.mind.mode == CreatureMode::home && fromAnchor > 70.0F && fromAnchor < 73.0F,
          "the creature loses its target at the end of its leash: 70 m from its anchor");
    for (int step = 0; step < 100 && loop->creature.mind.mode == CreatureMode::home; ++step) {
        pass(host, 0.2F);
    }
    check(loop->creature.mind.mode == CreatureMode::rest && world::horizontal_distance(loop->position, anchor) < 0.01F
              && log_count("is at its anchor") >= 1,
          "the way back ends at the anchor (the place on the patrol where the creature took its target)");
    p.behavior().set_ignored(kCharacterA, true);
    host.world.set_position(player->id, at(1000.0F, 1000.0F), false); // Near the creature again: it roams.
    bool goesOn = false;
    for (int tick = 0; tick < 150 && !goesOn; ++tick) {
        host.steps(1, {});
        goesOn = p.behavior().roaming(id) && loop->creature.mind.waypoint == waypoint && loop->creature.moving
                 && loop->creature.moving->action == kWalk;
    }
    check(goesOn, "then the patrol goes on: the creature walks to the waypoint that it went to before the fight");
}

/** PT6: the reset: immune on the way back, the mark of the vitals state, and the full health at the anchor. */
void immune_reset(const Options&) {
    PatrolHost p(at(1000.0F, 1000.0F));
    ServerHost& host = p.host;
    (void)host.world.spawn_character(kCharacterA, at(1000.0F, 1000.0F));
    world::Entity* player = host.world.find_character(kCharacterA);
    world::Entity* wolf = p.creatures().spawn("Test_Patroller", at(1006.0F, 1000.0F), 90.0F).entity;
    const world::EntityId id = wolf->id;
    pass(host, 0.5F);
    check(wolf->creature.mind.mode == CreatureMode::fight, "setup: the creature sees the character and takes it as its target");
    const Fragment first = vitals_for(host, *wolf, 0);
    check(field_byte(first, kFieldInvulnerable) == 0 && field_byte(first, kFieldDisplayImmune) == 0 && !wolf->creature.immune(),
          "in a fight the creature is not immune: the first vitals state of a session has invulnerability 0");
    p.creatures().hurt(id, 400.0F, player->id);
    const float hurt = wolf->vitals.health;
    const world::Revision sent = host.world.revision();
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 0.2F);
    check(wolf->creature.mind.mode == CreatureMode::home && wolf->creature.immune(), "setup: the creature goes back (the reset)");
    const Fragment later = vitals_for(host, *wolf, sent);
    const Fragment fresh = vitals_for(host, *wolf, 0);
    check(field_byte(later, kFieldInvulnerable) == 1 && field_byte(later, kFieldDisplayImmune) == 1
              && field_byte(fresh, kFieldInvulnerable) == 1 && field_byte(fresh, kFieldDisplayImmune) == 1,
          "on its way back the vitals state of a session that had the creature, and the first state of a new session, have "
          "invulnerability 1 and displayImmuneWhenInvulnerable 1");
    const Fragment unchanged = vitals_for(host, *wolf, host.world.revision());
    check(field_byte(unchanged, kFieldInvulnerable) == -1, "a later state with no change of the mark does not have the two fields");
    p.behavior().set_ignored(kCharacterA, false);
    const std::uint32_t actions = wolf->creature.actions;
    const world::CreatureResult result = p.creatures().hurt(id, 100.0F, player->id);
    check(result.error == world::CreatureError::immune && wolf->vitals.health == hurt && wolf->creature.mind.mode == CreatureMode::home
              && wolf->creature.mind.target == 0 && wolf->creature.actions == actions,
          "a hit on the way back changes nothing: no damage, no reaction (no new action), and no target");
    pass(host, 3.0F);
    check(wolf->creature.mind.mode != CreatureMode::home && wolf->vitals.health == wolf->vitals.healthMax && !wolf->creature.immune(),
          "at its anchor the creature has its full health, and it is not immune");
    const Fragment after = vitals_for(host, *wolf, sent + 1);
    check(field_byte(after, kFieldInvulnerable) == 0 && field_byte(after, kFieldDisplayImmune) == 0,
          "the end of the way back gives the observers invulnerability 0");
}

// ---- the cases with navigation data ----------------------------------------------------------

/** A host with navigation data in a folder of the work directory, and characters with a session number and no client. */
struct NavPatrolHost {
    ServerHost host;
    std::filesystem::path folder;
    std::vector<std::unique_ptr<Sink>> sinks;
    std::vector<Player> players;

    NavPatrolHost(const Options& options, const char* name, std::vector<gamedata::SpawnPoint> points = {},
                  std::vector<gamedata::PatrolRoute> routes = {})
        : folder(options.workDirectory / name) {
        std::filesystem::remove_all(folder);
        if (!set_patrol_data(host, std::move(points), std::move(routes))) {
            throw std::runtime_error("patrol test: the creature data is not valid: " + host.creatures.error());
        }
        host.world.set_random_seed(0x5EED0002ULL);
    }
    bool load() {
        return host.navigation.set_directory(folder.string());
    }
    world::Creatures& creatures() {
        return host.world.creatures();
    }
    world::Behavior& behavior() {
        return host.world.creatures().behavior();
    }
    world::Entity* character(const state::WorldPosition& position) {
        const std::size_t number = players.size();
        char steam[32];
        char id[64];
        char name[32];
        std::snprintf(steam, sizeof(steam), "765612022552%05zu", 44000 + number);
        std::snprintf(id, sizeof(id), "eeeeeeee-0000-4000-8000-%012zu", number + 1);
        std::snprintf(name, sizeof(name), "Rover%zu", number + 1);
        players.push_back(add_player(host.store, steam, name, id, name, position));
        sinks.push_back(std::make_unique<Sink>());
        world::Entity* entity = host.world.enter(static_cast<world::SessionId>(number + 1), *sinks.back(), players.back().characterId,
                                                 players.back().account.personaId)
                                    .entity;
        if (entity != nullptr) {
            (void)host.world.travel(entity->characterId, position, world::TravelScreen::off);
        }
        return entity;
    }
    void ticks(int count) {
        for (int index = 0; index < count; ++index) {
            host.clock.advance(std::chrono::milliseconds(10));
            host.world.tick();
        }
    }
    void pass(float seconds) {
        ticks(static_cast<int>(std::lround(seconds * 100.0F)));
    }
};

/** The floors of the cells of a tile, from a function of the world position of the middle of each cell. */
TestCells cells_of(int tileX, int tileY, std::function<void(float x, float y, std::vector<TestFloor>& floors)> describe) {
    return [=](int cellX, int cellY, std::vector<TestFloor>& floors) {
        describe(static_cast<float>(tileX) * 64.0F + (static_cast<float>(cellX) + 0.5F) * 0.25F,
                 static_cast<float>(tileY) * 64.0F + (static_cast<float>(cellY) + 0.5F) * 0.25F, floors);
    };
}

void wall(std::vector<TestFloor>& floors) {
    floors.clear();
    floors.push_back({kHeight + 3.0F, gamedata::NavFloor::kBlocked});
}

/** PT7: a way back with no path. */
void no_path_back(const Options& options) {
    NavPatrolHost world(options, "patrol-no-path-back");
    TestNavRegion region;
    // A closed yard to the west of the place of the fight: the anchor of the case is in it.
    const auto yard = [](float x, float y, std::vector<TestFloor>& floors) {
        const bool outer = x > 980.0F && x < 990.0F && y > 995.0F && y < 1005.0F;
        const bool inner = x > 980.5F && x < 989.5F && y > 995.5F && y < 1004.5F;
        if (outer && !inner) {
            wall(floors);
        }
    };
    region.tiles[{15, 15}] = cells_of(15, 15, yard);
    region.tiles[{16, 15}] = cells_of(16, 15, yard);
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Entity* player = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    world::Entity* wolf = world.creatures().spawn("Test_Patroller", at(998.0F, 1000.0F), 270.0F).entity;
    const state::WorldPosition atRest = wolf->creature.mind.anchor;
    check(!world.behavior().move_anchor(*wolf, at(985.0F, 1000.0F))
              && world::horizontal_distance(wolf->creature.mind.anchor, atRest) < 0.01F,
          "the anchor command refuses a creature with no target");
    world.pass(0.3F);
    (void)world.creatures().hurt(wolf->id, 1.0F, player->id);
    check(wolf->creature.mind.mode == CreatureMode::fight, "setup: the creature has the character as its target");
    // The anchor of the case (the development command of the live test): a place in the closed
    // yard (a way back with no path, as for a path longer than the limit of a search).
    const state::WorldPosition taken = wolf->creature.mind.anchor;
    check(world.behavior().move_anchor(*wolf, at(985.0F, 1000.0F)) && wolf->creature.mind.trail.size() == 2
              && world::horizontal_distance(wolf->creature.mind.trail[0], at(985.0F, 1000.0F)) < 0.01F
              && world::horizontal_distance(wolf->creature.mind.trail[1], taken) < 0.01F,
          "the anchor command: the new anchor is the first place of the trail, and the place where the creature took its "
          "target is the second one");
    // The character runs 20 m to the east: the creature follows, and its trail has its way.
    for (int step = 0; step < 20; ++step) {
        world.host.world.set_position(player->id, at(player->position.x + 1.0F, 1000.0F), true);
        world.pass(0.2F);
    }
    const std::size_t trail = wolf->creature.mind.trail.size();
    check(trail >= 15 && wolf->position.x > 1010.0F, "the trail has the places of the way out, one for each meter");
    const state::WorldPosition lastPlace = wolf->creature.mind.trail[1];
    world.behavior().set_ignored(player->characterId, true);
    float largest = 0.0F;
    state::WorldPosition last = wolf->position;
    for (int tick = 0; tick < 600; ++tick) {
        world.ticks(1);
        largest = std::max(largest, world::horizontal_distance(wolf->position, last));
        last = wolf->position;
    }
    note("the creature is at x " + std::to_string(wolf->position.x) + ", the largest step " + std::to_string(largest) + " m");
    check(wolf->creature.mind.mode == CreatureMode::home && wolf->creature.mind.retracing && world.behavior().counts().retraces == 1
              && world::horizontal_distance(wolf->position, lastPlace) <= 0.35F && largest <= kRunSpeed * 0.2F + 0.05F
              && world.behavior().counts().jumps == 0,
          "with no path back the creature walks the places of its way out back, in steps of its run (no jump), to the last "
          "place before the anchor; a session observes it: it does not jump");
    // The session goes: no session can observe the creature, and it is at its anchor in one step.
    check(world.host.world.leave(1), "setup: the character leaves the world");
    (void)world.character(at(1500.0F, 1500.0F));
    world.pass(1.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest && world::horizontal_distance(wolf->position, at(985.0F, 1000.0F)) < 0.01F
              && world.behavior().counts().jumps == 1 && log_count("no session observes it") == 1,
          "when no session can observe it, the creature is at its anchor in one step");
    std::filesystem::remove_all(world.folder);
}

/** PT8: no way to the target. */
void no_way_to_the_target(const Options& options) {
    NavPatrolHost world(options, "patrol-no-way");
    TestNavRegion region;
    // A stone 0.6 m higher than the floor around it, from x 1010 to 1020: a creature steps 0.35 m.
    region.tiles[{15, 15}] = cells_of(15, 15, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1010.0F && x < 1020.0F && y > 995.0F && y < 1005.0F) {
            floors.clear();
            floors.push_back({kHeight + 0.6F, 0});
        }
    });
    region.tiles[{16, 15}] = cells_of(16, 15, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1010.0F && x < 1020.0F && y > 995.0F && y < 1005.0F) {
            floors.clear();
            floors.push_back({kHeight + 0.6F, 0});
        }
    });
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Entity* player = world.character(at(1011.0F, 1000.0F, kHeight + 0.6F));
    world.ticks(60);
    player->vitals.healthMax = player->vitals.health = 100000.0F;
    world::Entity* wolf = world.creatures().spawn("Test_Patroller", at(1000.0F, 1000.0F), 270.0F).entity;
    world.pass(0.3F);
    (void)world.creatures().hurt(wolf->id, 1.0F, player->id);
    world.pass(3.0F);
    note("the creature is at x " + std::to_string(wolf->position.x) + " z " + std::to_string(wolf->position.z));
    // The wait of 12 s starts when the creature is at that place and the target is out of its
    // reach (code review of stage 3.4e, finding F4): here it attacks from the edge first.
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->position.x > 1008.5F && wolf->position.x <= 1010.1F
              && near(wolf->position.z, kHeight, 0.05F) && world.host.world.navigation().counts().partialPaths >= 1,
          "the stone has no way up for the creature: it goes to the place nearest to the character (the edge of the stone) and "
          "stays there with its target");
    check(wolf->creature.mind.attacks >= 1 && player->vitals.health < 100000.0F,
          "the character is in the reach of its attack from the edge: the creature attacks it");
    // The character goes to the middle of the stone, 5 m from each edge: out of the reach. After the wait the creature goes back.
    world.host.world.set_position(player->id, at(1015.0F, 1000.0F, kHeight + 0.6F), false);
    const std::uint32_t attacks = wolf->creature.mind.attacks;
    world.pass(world::behavior_rules::kNoWayHold.count() / 1000.0F + 1.0F);
    check(wolf->creature.mind.mode != CreatureMode::fight && wolf->creature.mind.attacks == attacks
              && world.behavior().counts().targetsNoWay == 1 && world.behavior().counts().holds >= 1,
          "out of the reach, after the wait of 12 s the creature loses its target and goes back (the reset)");
    world.pass(3.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest && near(wolf->position.x, 1000.0F) && wolf->vitals.health == wolf->vitals.healthMax,
          "it is at its anchor with its full health, and its vision does not take the character again at once");
    std::filesystem::remove_all(world.folder);
}

/** PT9: the load of the roam. */
void roam_load(const Options& options) {
    // 50 characters at 50 places, each with 20 spawn points with a route of four waypoints near it,
    // and 500 spawn points with a route far from each character.
    std::vector<gamedata::SpawnPoint> points;
    std::vector<gamedata::PatrolRoute> routes;
    std::vector<state::WorldPosition> places;
    for (int index = 0; index < 50; ++index) {
        places.push_back(at(1100.0F + static_cast<float>(index % 10) * 90.0F, 1100.0F + static_cast<float>(index / 10) * 90.0F));
    }
    char id[48];
    for (std::size_t place = 0; place < places.size(); ++place) {
        for (int index = 0; index < 20; ++index) {
            const float angle = static_cast<float>(index) * 0.314159F;
            const float x = places[place].x + std::cos(angle) * 25.0F;
            const float y = places[place].y + std::sin(angle) * 25.0F;
            std::snprintf(id, sizeof(id), "near-%zu-%d", place, index);
            points.push_back(spawn_point(id, 0, x, y, static_cast<std::uint32_t>(routes.size())));
            routes.push_back({gamedata::PatrolPolicy::loop,
                              {waypoint(x - 3.0F, y, 0.5F, 1.0F), waypoint(x, y + 3.0F, -1.0F, -1.0F), waypoint(x + 3.0F, y, 0.5F, 1.0F),
                               waypoint(x, y - 3.0F, -1.0F, -1.0F)}});
        }
    }
    for (int index = 0; index < 500; ++index) {
        const float x = 4000.0F + static_cast<float>(index % 25) * 40.0F;
        const float y = 4000.0F + static_cast<float>(index / 25) * 40.0F;
        std::snprintf(id, sizeof(id), "far-%d", index);
        points.push_back(spawn_point(id, 0, x, y, static_cast<std::uint32_t>(routes.size())));
        routes.push_back({gamedata::PatrolPolicy::bounce, {waypoint(x, y, 0.5F, 1.0F), waypoint(x + 5.0F, y, 0.5F, 1.0F)}});
    }
    NavPatrolHost world(options, "patrol-load", points, routes);
    TestNavRegion region;
    for (int tileY = 16; tileY < 32; ++tileY) {
        for (int tileX = 16; tileX < 32; ++tileX) {
            region.tiles[{tileX, tileY}] = cells_of(tileX, tileY, [](float, float, std::vector<TestFloor>&) {});
        }
    }
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    std::vector<world::Entity*> characters;
    for (const state::WorldPosition& place : places) {
        characters.push_back(world.character(place));
        world.behavior().set_ignored(characters.back()->characterId, true);
    }
    // 10 creatures of a kind that wanders near each character (commands), and 20 far from each one.
    std::vector<world::Entity*> farAway;
    for (std::size_t place = 0; place < places.size(); ++place) {
        for (int index = 0; index < 10; ++index) {
            (void)world.creatures().spawn("Test_Wanderer", at(places[place].x + 10.0F + static_cast<float>(index) * 3.0F, places[place].y - 15.0F), 0.0F);
        }
    }
    for (int index = 0; index < 20; ++index) {
        // On the navigation data, 400 m and more from each character.
        farAway.push_back(world.creatures().spawn("Test_Wanderer", at(1100.0F + static_cast<float>(index) * 10.0F, 1950.0F), 0.0F).entity);
    }
    world.pass(2.0F);
    const std::size_t inWorld = world.creatures().count();
    const std::size_t roaming = world.behavior().roaming_count();
    std::size_t wanderers = 0;
    std::size_t wanderersRoaming = 0;
    std::size_t notWalking = 0;
    for (const world::EntityId id : world.creatures().ids()) {
        const world::Entity* creature = world.creatures().find(id);
        if (creature->creature.kind->id == "Test_Wanderer" && creature->position.y < 1900.0F) {
            ++wanderers;
            wanderersRoaming += world.behavior().roaming(id) ? 1 : 0;
            notWalking += !creature->creature.mind.groundKnown || !creature->creature.mind.walks ? 1 : 0;
        }
    }
    note("creatures in the world " + std::to_string(inWorld) + ", roaming " + std::to_string(roaming) + "; wanderers near "
         + std::to_string(wanderers) + ", of them roaming " + std::to_string(wanderersRoaming) + ", with no known ground "
         + std::to_string(notWalking));
    check(inWorld == 50 * 30 + 20 && roaming == 50 * 30,
          "the 1,000 creatures of the spawn points near the 50 characters and the 500 creatures of commands near them roam; the 500 "
          "spawn points far from each character have no creature, and the 20 creatures of commands far from each character stand");
    const std::uint64_t roamSteps = world.behavior().counts().roamSteps;
    const world::LoadMeter stepsBefore = world.behavior().load();
    const auto started = std::chrono::steady_clock::now();
    std::chrono::nanoseconds largestTick{0};
    for (int tick = 0; tick < 1000; ++tick) {
        const auto tickStart = std::chrono::steady_clock::now();
        world.ticks(1);
        largestTick = std::max<std::chrono::nanoseconds>(largestTick, std::chrono::steady_clock::now() - tickStart);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const world::LoadMeter& steps = world.behavior().load();
    std::printf("        1,000 ticks with 1,500 creatures that roam near 50 characters: %.1f ms (the largest tick %.3f ms); %llu roam "
                "steps, %llu waypoints, %llu places of a wander; the steps of the behavior %.3f ms each, the largest %.3f ms\n",
                seconds * 1000.0, std::chrono::duration<double, std::milli>(largestTick).count(),
                static_cast<unsigned long long>(world.behavior().counts().roamSteps - roamSteps),
                static_cast<unsigned long long>(world.behavior().counts().waypoints),
                static_cast<unsigned long long>(world.behavior().counts().wanderPlaces),
                steps.count > stepsBefore.count
                    ? std::chrono::duration<double, std::milli>(steps.total - stepsBefore.total).count() / static_cast<double>(steps.count - stepsBefore.count)
                    : 0.0,
                std::chrono::duration<double, std::milli>(steps.largest).count());
    bool stood = true;
    for (const world::Entity* creature : farAway) {
        stood = stood && !world.behavior().roaming(creature->id) && creature->position.y == 1950.0F && !creature->creature.moving;
    }
    check(stood, "a creature with no character near it does not roam: it stands, and it needs no time");
    check(world.behavior().counts().waypoints >= 1000 && world.behavior().counts().wanderPlaces >= 400,
          "the creatures near the characters walk their routes and their wander");
    check(seconds < 3.0 && largestTick < std::chrono::milliseconds(80),
          "1,000 ticks of the world with 1,500 creatures that roam near 50 characters need less than 3 s (the time limit of the case)");
    std::filesystem::remove_all(world.folder);
}

// ---- two sessions ------------------------------------------------------------------------------

constexpr std::chrono::microseconds kTick{33333};

void enter_at(ServerHost& host, FakeClient& client, const Player& player, const state::WorldPosition& position, std::uint16_t& input) {
    host.enter(client, player.registration());
    for (int index = 0; index < 20; ++index) {
        host.clock.advance(kTick);
        host.datagram(client.peer, client.report_at(++input, position.x, position.y, position.z));
        host.tick();
        client.exchange(host);
    }
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

std::vector<Fragment> fragments_of(const FakeClient& client, std::size_t from, std::uint32_t interest, std::uint32_t slot) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot) {
                    found.push_back(fragment);
                }
            }
        }
    }
    return found;
}

/** @return The action of the scope record of a movement state, -1 for the fields with no record, -2 for a state with no scope fields. */
int scope_of(const Fragment& movement) {
    const auto field = movement.fields.find({0, 33});
    if (field == movement.fields.end()) {
        return -2;
    }
    server::hub::Reader reader(field->second);
    if (reader.size() == 0) {
        return -1;
    }
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

std::pair<float, float> place_of(const Fragment& movement) {
    server::hub::Reader reader(movement.fields.at({0, 27}));
    const float x = reader.f32();
    const float y = reader.f32();
    return {x, y};
}

/** PT10: two sessions near a creature on its patrol. */
void two_sessions_and_a_patrol(const Options&) {
    service::RepServerOptions settings;
    settings.serverVersion = "test-version";
    settings.observePlayers = true;
    PatrolHost p(at(1000.0F, 1000.0F), settings);
    ServerHost& host = p.host;
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", at(1000.0F, 1010.0F));
    FakeClient a(27100);
    FakeClient b(27101);
    std::uint16_t inputA = 0;
    std::uint16_t inputB = 0;
    enter_at(host, a, p.alpha, at(1000.0F, 1000.0F), inputA);
    enter_at(host, b, beta, at(1000.0F, 1010.0F), inputB);
    p.behavior().set_ignored(kCharacterA, true);
    p.behavior().set_ignored(kCharacterB, true);
    pass(host, 1.0F, {&a, &b});
    world::Entity* loop = p.of_point(0);
    check(loop != nullptr && interest_of(host, 1, loop->id) >= 2 && interest_of(host, 2, loop->id) >= 2,
          "setup: the two sessions observe the creature of the patrol");
    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    pass(host, 15.0F, {&a, &b});
    const std::vector<Fragment> statesA = fragments_of(a, fromA, interest_of(host, 1, loop->id), kSlots[0]);
    const std::vector<Fragment> statesB = fragments_of(b, fromB, interest_of(host, 2, loop->id), kSlots[0]);
    bool same = statesA.size() == statesB.size() && statesA.size() >= 10;
    for (std::size_t index = 0; same && index < statesA.size(); ++index) {
        same = place_of(statesA[index]) == place_of(statesB[index]) && scope_of(statesA[index]) == scope_of(statesB[index]);
    }
    const auto walks = std::count_if(statesA.begin(), statesA.end(), [](const Fragment& state) { return scope_of(state) == static_cast<int>(kWalk); });
    const auto stops = std::count_if(statesA.begin(), statesA.end(), [](const Fragment& state) { return scope_of(state) == -1; });
    note("movement states: A " + std::to_string(statesA.size()) + ", B " + std::to_string(statesB.size()) + "; walk records "
         + std::to_string(walks) + ", stops " + std::to_string(stops));
    check(same && walks >= 2 && stops >= 2,
          "the two sessions get the same movement states of the patrol: the same places, a walk record for each walk (the action "
          "Move), and the scope fields with no record at each waypoint");
    // The creature takes one of the two players as its target.
    p.behavior().set_ignored(kCharacterA, false);
    p.behavior().set_ignored(kCharacterB, false);
    world::Entity* alpha = host.world.find_character(kCharacterA);
    world::Entity* second = host.world.find_character(kCharacterB);
    (void)p.creatures().hurt(loop->id, 10.0F, alpha->id);
    (void)p.creatures().hurt(loop->id, 10.0F, second->id);
    pass(host, 2.0F, {&a, &b});
    check(loop->creature.mind.mode == CreatureMode::fight && loop->creature.mind.target == alpha->id,
          "the creature on its patrol takes one of the two players as its target (a value of the creature in the world), and "
          "a hit of the second player does not change it");
    const std::vector<Fragment> fightA = fragments_of(a, fromA, interest_of(host, 1, loop->id), kSlots[0]);
    const std::vector<Fragment> fightB = fragments_of(b, fromB, interest_of(host, 2, loop->id), kSlots[0]);
    const auto runs = [](const std::vector<Fragment>& states) {
        return std::count_if(states.begin(), states.end(), [](const Fragment& state) { return scope_of(state) == static_cast<int>(kRun); });
    };
    check(runs(fightA) >= 1 && runs(fightA) == runs(fightB), "the two sessions get the run of the fight (Move_Combat)");
}

} // namespace

std::vector<Case> patrol_cases() {
    return {{"PT1", "patrol: the creature file of the format 3 (routes, wander values), a file whose parts do not fit together, "
                    "and a file of the format 2",
             patrol_data_file},
            {"PT2", "patrol: a loop of four waypoints with the ambient speed, the walk action, the waits, the direction, and the task",
             patrol_loop},
            {"PT3", "patrol: the policies of a route (back and forth, random) and a route of one waypoint whose task loops",
             patrol_policies},
            {"PT4", "patrol: the wander, with the random numbers of the world", wander},
            {"PT5", "patrol: a creature on a patrol takes a target: the anchor, the leash from it, and the patrol after the way back",
             patrol_and_fight},
            {"PT6", "patrol: the reset: immune on the way back, the mark of the vitals state, the full health at the anchor", immune_reset},
            {"PT7", "patrol: a way back with no path walks the way out back, and jumps only when no session observes the creature",
             no_path_back},
            {"PT8", "patrol: no way to the target: the place nearest to it, the wait, an attack in the reach, and the reset",
             no_way_to_the_target},
            {"PT9", "patrol: the load of the roam with 50 players and 1,500 creatures near them, with a time limit", roam_load},
            {"PT10", "patrol: two sessions get the same patrol, and the creature takes one of two players as its target",
             two_sessions_and_a_patrol}};
}

} // namespace soulwarden::backend::tests

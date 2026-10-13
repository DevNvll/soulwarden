// Cases for stage 3.4c (creatures in the whole world): the creature data and the spawn point
// rule at the size of the world, and the travel of a character (the wire for the owner, the
// position check after it, the observers, the commands, the travel places, and the creatures
// of the new place). All ids, names, and places are made up: no value of a game file is here.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/travel_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::CreatureError;
using world::PositionVerdict;
using world::TravelError;
using world::TravelScreen;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kTokenA = "token-of-the-start-of-A-0123456789abcdef";
constexpr const char* kTokenB = "token-of-the-start-of-B-fedcba9876543210";
constexpr float kHeight = 50.0F;
/** The place of the character of A at the start of each case, and a place 2 km to the east of it. */
constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
constexpr state::WorldPosition kFar{3000.0F, 1000.0F, 80.0F};
constexpr std::chrono::microseconds kTick{33333};
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kRabbitSlots[] = {2, 5, 17};
/** Field numbers of the movement state (server/rep/states/movement_state.cpp). */
constexpr unsigned kFieldPosition = 27;
constexpr unsigned kFieldTeleport = 31;
constexpr unsigned kFieldAnchor = 40;
constexpr unsigned kFieldIdRel = 0;

gamedata::CreatureSlice test_slice(const char* path, std::uint8_t mark, const std::uint32_t slots[3], float radius) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x1000000000000000ULL + mark;
    slice.interestRadius = radius;
    slice.movementSlot = slots[0];
    slice.vitalsSlot = slots[1];
    slice.markerSlot = slots[2];
    slice.slots = {{slots[0], "ActionList", 11}, {slots[1], "Vitals", 15}, {slots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    return slice;
}

gamedata::CreatureKind test_kind(const char* id, std::uint32_t vitalsId, float factor, std::uint32_t level, std::uint32_t slice) {
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
    kind.healthFactor = factor;
    kind.level = level;
    kind.bodies = {{slice, 100}};
    return kind;
}

gamedata::SpawnPoint test_point(const std::string& id, std::uint32_t kind, float x, float y) {
    gamedata::SpawnPoint point;
    point.id = id;
    point.kind = kind;
    point.level = kind == 0 ? 18 : 1;
    point.position = state::WorldPosition{x, y, kHeight};
    point.respawnMin = 360.0F;
    return point;
}

/** Edge of the made-up world, in meters, and the distance of two spawn points of its grid. */
constexpr float kWorldEdge = 14274.0F;
constexpr float kPointStep = 78.0F;

/**
 * @return The spawn points of a made-up world of the size of the game world: a grid of points
 *         with 78 m between two of them, 184 points on each edge (33,856 points; the creature
 *         file of the supported build has 33,000 and more).
 */
std::vector<gamedata::SpawnPoint> world_points() {
    std::vector<gamedata::SpawnPoint> points;
    std::uint32_t index = 0;
    for (float x = 0.0F; x <= kWorldEdge; x += kPointStep) {
        for (float y = 0.0F; y <= kWorldEdge; y += kPointStep) {
            points.push_back(test_point("p" + std::to_string(index), index % 2, x, y));
            ++index;
        }
    }
    return points;
}

bool set_creatures(ServerHost& host, std::vector<gamedata::SpawnPoint> points) {
    return host.creatures.set({test_slice("slices/test/wolf.dynamicslice", 0x11, kWolfSlots, 100.0F),
                               test_slice("slices/test/rabbit.dynamicslice", 0x22, kRabbitSlots, 35.0F)},
                              {test_kind("Test_Wolf", 0x11112222, 1.0F, 18, 0), test_kind("Test_Rabbit", 0x55556666, 0.05F, 1, 1)},
                              std::move(points), {{1, 337.5F}, {18, 1690.875977F}});
}

/** @return The number of spawn points that are `range` or less from one of the places (each point one time). */
std::size_t points_near(const std::vector<gamedata::SpawnPoint>& points, const std::vector<state::WorldPosition>& places,
                        float range) {
    std::size_t count = 0;
    for (const gamedata::SpawnPoint& point : points) {
        count += std::any_of(places.begin(), places.end(), [&](const state::WorldPosition& place) {
            return world::horizontal_distance(place, point.position) <= range;
        });
    }
    return count;
}

double milliseconds_of(std::chrono::nanoseconds time) {
    return std::chrono::duration<double, std::milli>(time).count();
}

json world_document(std::size_t pointCount) {
    json points = json::array();
    std::size_t index = 0;
    for (float x = 0.0F; x <= kWorldEdge && index < pointCount; x += kPointStep) {
        for (float y = 0.0F; y <= kWorldEdge && index < pointCount; y += kPointStep) {
            points.push_back({"00000000-0000-4000-8000-" + std::to_string(100000000000ULL + index), index % 2, index % 2 == 0 ? 18 : 1,
                              x, y, 50.0, 90.0, 360.0, 360.0});
            ++index;
        }
    }
    const auto slice = [](const char* path, const char* asset, const char* root, int movement, int vitals, int marker) {
        return json{{"path", path}, {"asset", asset}, {"sub", 2}, {"root", root}, {"interestRadius", 60.0},
                    {"movement", movement}, {"vitals", vitals}, {"marker", marker},
                    {"slots", {{movement, "ActionList", 11}, {vitals, "Vitals", 15}, {marker, "Marker", 5399}}},
                    {"components", json::array({json::array({root, "0000000000000A01", "Root/Vitals"})})},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}}}};
    };
    const auto kind = [](const char* id, std::uint32_t vitals, double health, int level, int body) {
        return json{{"id", id}, {"vitals", id}, {"category", std::string(id) + "_Category"}, {"vitalsId", vitals},
                    {"categoryId", vitals + 1}, {"name", id}, {"vitalsName", id}, {"family", "Wildlife"},
                    {"creatureType", "Solo"}, {"health", health}, {"level", level}, {"bodies", json::array({json::array({body, 100})})},
                    {"points", 0}};
    };
    return json{{"format", 1},
                {"counts", {{"spawnPoints", points.size()}, {"kinds", 2}, {"slices", 2}}},
                {"levels", {{1, 337.5}, {18, 1690.875977}}},
                {"slices",
                 {slice("slices/test/wolf.dynamicslice", "11111111-1111-1111-1111-111111111111", "1000000000000011", 5, 9, 15),
                  slice("slices/test/rabbit.dynamicslice", "22222222-2222-2222-2222-222222222222", "1000000000000022", 2, 5, 17)}},
                {"kinds", {kind("Test_Wolf", 0x11112222, 1.0, 18, 0), kind("Test_Rabbit", 0x55556666, 0.05, 1, 1)}},
                {"spawnPoints", points}};
}

/** WS1: the loader of the creature file at the size of the world. */
void loader_at_world_size(const Options& options) {
    const std::size_t count = world_points().size();
    const auto path = options.workDirectory / "world-creatures.json";
    std::filesystem::create_directories(options.workDirectory);
    const std::string text = world_document(count).dump();
    std::ofstream(path, std::ios::binary) << text;
    gamedata::CreatureData data;
    const auto started = std::chrono::steady_clock::now();
    const bool loaded = data.load(path.string());
    const double loadTime = milliseconds_of(std::chrono::steady_clock::now() - started);
    note("a made-up creature file with " + std::to_string(count) + " spawn points (" + std::to_string(text.size() / 1024)
         + " kB): loaded in " + std::to_string(loadTime) + " ms");
    check(loaded && data.loaded() && data.spawn_points().size() == count && data.kinds().size() == 2 && data.slices().size() == 2
              && data.error().empty(),
          "a creature file of the size of the world loads: each spawn point, each kind, and each slice");
    check(count > 32840 && data.spawn_points().back().kind == (count - 1) % 2 && data.spawn_points().back().id.size() == 36
              && data.spawn_points()[count / 2].position.z == 50.0F,
          "the file has more spawn points than the creature file of the supported build (32,840), and the last one and the "
          "middle one have their values");
    check(loadTime < 5000.0, "the load of a file of this size needs less than 5 s (the measured value is in the line above)");

    // A world with this data: the table of the spawn points and the grid.
    ServerHost host;
    check(host.creatures.load(path.string()) && host.world.creatures().has_data(), "a world gets the data of the file");
    host.world.tick();
    check(host.world.creatures().spawn_points().size() == count && host.world.creatures().count() == 0
              && host.world.creatures().waiting_count() == 0,
          "the creature system has one state for each spawn point, and with no player the world has no creature");

    // A file that is not complete is not used, as a whole.
    const auto broken = [&](const std::string& content, const char* what) {
        const auto file = options.workDirectory / "world-creatures-broken.json";
        std::ofstream(file, std::ios::binary) << content;
        gamedata::CreatureData other;
        const bool accepted = other.load(file.string());
        note(std::string(what) + ": " + (accepted ? "loaded" : other.error()));
        return !accepted && !other.loaded() && other.spawn_points().empty() && other.kinds().empty() && !other.error().empty();
    };
    json shorter = world_document(count);
    shorter["spawnPoints"].erase(shorter["spawnPoints"].size() - 1);
    json outside = world_document(count);
    outside["spawnPoints"][count / 2][3] = 5.0e9;
    json noKind = world_document(count);
    noKind["spawnPoints"][count - 1][1] = 7;
    check(broken(text.substr(0, text.size() * 6 / 10), "the first 60 percent of the file")
              && broken(shorter.dump(), "a file whose list is one spawn point shorter than its count")
              && broken(outside.dump(), "a file with one spawn point outside the world")
              && broken(noKind.dump(), "a file whose last spawn point names a kind that is not there"),
          "a file of this size that is not complete gives no data: a file that ends early, a list that is shorter than its "
          "count, a position outside the world, and a kind index outside the list");
    std::filesystem::remove(path);
    std::filesystem::remove(options.workDirectory / "world-creatures-broken.json");
}

/** WS2: the spawn point rule at the size of the world. Its cost depends on the players and on what is near them. */
void spawn_rule_at_world_size(const Options&) {
    ServerHost host;
    const std::vector<gamedata::SpawnPoint> points = world_points();
    check(set_creatures(host, points), "the made-up world has " + std::to_string(points.size()) + " spawn points");
    world::Creatures& creatures = host.world.creatures();
    const world::WorldOptions& rule = host.world.options();
    // The players: characters with no session, at different places of the world.
    const unsigned kPlayers = 50;
    std::vector<std::string> ids;
    std::vector<state::WorldPosition> places;
    for (unsigned index = 0; index < kPlayers; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x10000000U + index, index);
        add_player(host.store, "765612022553" + std::to_string(10000 + index), "Tester " + std::to_string(index), id,
                   "Player" + std::to_string(index), std::nullopt);
        ids.emplace_back(id);
        // A path through the world: no two players have a spawn point of the grid in common.
        places.push_back(state::WorldPosition{700.0F + 271.0F * static_cast<float>(index),
                                              500.0F + 263.0F * static_cast<float>((index * 7) % kPlayers), kHeight});
    }
    const auto scan = [&] {
        host.clock.advance(world::Creatures::kScanInterval);
        host.world.tick();
    };
    // One player.
    check(host.world.spawn_character(ids[0], places[0]) == world::SpawnError::none, "one player is in the world");
    scan();
    const std::size_t nearOne = points_near(points, {places[0]}, rule.creatureSpawnEnter);
    note("1 player: " + std::to_string(creatures.count()) + " creatures; the scan looked at "
         + std::to_string(creatures.counts().lastScanPoints) + " of " + std::to_string(points.size()) + " spawn points");
    check(nearOne > 0 && creatures.count() == nearOne && creatures.counts().spawnedByPoint == nearOne,
          "with one player the world has the creature of each spawn point that is 150 m or less from it, and no other one");
    check(creatures.counts().lastScanPoints >= nearOne && creatures.counts().lastScanPoints <= 36,
          "the scan looked only at the spawn points of the grid cells that the circle of the player touches (36 cells at "
          "most), not at each spawn point of the world");
    const std::uint64_t scansBefore = creatures.counts().scans;
    const auto oneStarted = std::chrono::steady_clock::now();
    for (int index = 0; index < 200; ++index) {
        scan();
    }
    const double oneTime = milliseconds_of(std::chrono::steady_clock::now() - oneStarted);
    check(creatures.counts().scans == scansBefore + 200 && creatures.count() == nearOne && creatures.counts().spawnedByPoint == nearOne,
          "200 more scans make no second creature of a point");

    // 50 players at different places.
    bool spawned = true;
    for (unsigned index = 1; index < kPlayers; ++index) {
        spawned = spawned && host.world.spawn_character(ids[index], places[index]) == world::SpawnError::none;
    }
    check(spawned, "50 players are in the world, each one at a different place");
    scan();
    const std::size_t nearAll = points_near(points, places, rule.creatureSpawnEnter);
    note("50 players: " + std::to_string(creatures.count()) + " creatures; the scan looked at "
         + std::to_string(creatures.counts().lastScanPoints) + " spawn points and at "
         + std::to_string(creatures.counts().lastScanCreatures) + " entities near a player");
    check(creatures.count() == nearAll && nearAll > 40 * nearOne / 2,
          "with 50 players the world has the creature of each spawn point that is 150 m or less from a player");
    check(creatures.counts().lastScanPoints <= kPlayers * 36 && creatures.counts().lastScanPoints * 10 < points.size(),
          "the scan of 50 players looked at 36 spawn points or less for each player: less than one tenth of the spawn points "
          "of the world");
    // The time of 200 scans, against the time of the same number of scans that look at each
    // spawn point for each player (the cost of a rule with no grid).
    const std::uint64_t ticksBefore = host.world.load().worldTick.count;
    const auto ruleStarted = std::chrono::steady_clock::now();
    for (int index = 0; index < 200; ++index) {
        scan();
    }
    const double ruleTime = milliseconds_of(std::chrono::steady_clock::now() - ruleStarted);
    volatile std::size_t found = 0;
    const auto fullStarted = std::chrono::steady_clock::now();
    for (int index = 0; index < 200; ++index) {
        std::size_t near = 0;
        for (const state::WorldPosition& place : places) {
            for (const gamedata::SpawnPoint& point : points) {
                near += world::horizontal_distance(place, point.position) <= rule.creatureSpawnEnter ? 1 : 0;
            }
        }
        found = found + near;
    }
    const double fullTime = milliseconds_of(std::chrono::steady_clock::now() - fullStarted);
    const world::LoadMeter& scanLoad = creatures.scan_load();
    note("200 scans with 1 player: " + std::to_string(oneTime) + " ms; 200 scans with 50 players: " + std::to_string(ruleTime)
         + " ms (" + std::to_string(ruleTime / 200.0) + " ms for one tick of the world with its scan); 200 scans of each spawn "
         + "point for each player: " + std::to_string(fullTime) + " ms");
    note("all scans of the case: " + std::to_string(scanLoad.count) + ", the largest one "
         + std::to_string(milliseconds_of(scanLoad.largest)) + " ms; the largest tick of the world "
         + std::to_string(milliseconds_of(host.world.load().worldTick.largest)) + " ms");
    check(host.world.load().worldTick.count == ticksBefore + 200 && found == 200 * nearAll,
          "the two measurements did 200 scans each, and the full scan found the same spawn points");
    check(ruleTime * 10.0 < fullTime && ruleTime < 400.0,
          "the time limit: 200 scans with 50 players need less than one tenth of the time of 200 scans of all spawn points, "
          "and less than 400 ms (a rule that looks at each spawn point two times in a second does not pass)");

    // The players go to different places: the creatures of the old places leave, and the
    // creatures of the new places come.
    std::vector<state::WorldPosition> moved;
    for (unsigned index = 0; index < kPlayers; ++index) {
        moved.push_back(state::WorldPosition{places[index].x + 900.0F, places[index].y + 450.0F, kHeight});
        host.world.travel(ids[index], moved.back());
    }
    host.world.tick();
    check(creatures.count() == points_near(points, moved, rule.creatureSpawnEnter) && creatures.counts().removedByDistance == nearAll,
          "after a travel of each player, in the next tick: the world has the creatures of the new places, and each "
          "creature of the old places left");
    // The reset command walks only the points that wait.
    const world::EntityId first = *creatures.ids().begin();
    creatures.kill(first);
    check(creatures.waiting_count() == 1 && creatures.reset_spawn_points() == 1 && creatures.waiting_count() == 0,
          "one spawn point waits after a kill, and the reset makes it free");
}

/** The messages that a client got from the message `from` on, as the steps of a travel. */
struct TravelSteps {
    /** "screen" (type 3951), "state" (a bundle with a movement state of the own character that has a position), "done" (type 3954). */
    std::vector<std::string> order;
    std::vector<Fragment> states;
    std::vector<Bytes> screens;
    std::vector<Bytes> done;
    /** Fragments of the own character in the bundles with a position, by slot. */
    std::set<std::uint32_t> slots;
};

TravelSteps travel_steps(const FakeClient& client, std::size_t from) {
    TravelSteps steps;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        const Sent& sent = client.got[index];
        if (sent.type == server::rep::kTypeTeleportWithLoadingScreen) {
            steps.order.emplace_back("screen");
            steps.screens.push_back(sent.body);
        } else if (sent.type == server::rep::kTypePlayerTeleport) {
            steps.order.emplace_back("done");
            steps.done.push_back(sent.body);
        } else if (sent.type == 8) {
            for (const Record& record : read_bundle(sent.body).records) {
                const Fragment* movement = record.interest == 1 ? fragment_of(record, 16) : nullptr;
                if (movement != nullptr && movement->fields.count({0, kFieldPosition}) == 1) {
                    steps.order.emplace_back("state");
                    steps.states.push_back(*movement);
                    for (const Fragment& fragment : record.fragments) {
                        steps.slots.insert(fragment.slot);
                    }
                }
            }
        }
    }
    return steps;
}

std::string joined(const std::vector<std::string>& words) {
    std::string text;
    for (const std::string& word : words) {
        text += (text.empty() ? "" : " ") + word;
    }
    return text;
}

/** @return The height of a world position field as the 16 bits of the wire. */
std::uint16_t height_bits(const Fragment& movement) {
    const Bytes& value = movement.fields.at({0, kFieldPosition});
    return static_cast<std::uint16_t>((value[8] << 8) | value[9]);
}

/** The height of a position as the backend sends it: the nearest of the 65,536 values of the range. */
std::uint16_t sent_bits(float height) {
    return static_cast<std::uint16_t>(std::lround((height + 100.0F) / 1100.0F * 65535.0F));
}

/** A client enters the world, and its reports of 20 simulation steps give the exact position. */
void enter_at(ServerHost& host, FakeClient& client, const Player& player, const state::WorldPosition& position, std::uint16_t& input) {
    host.enter(client, player.registration());
    for (int index = 0; index < 20; ++index) {
        host.clock.advance(kTick);
        host.datagram(client.peer, client.report_at(++input, position.x, position.y, position.z));
        host.tick();
        client.exchange(host);
    }
}

void report(ServerHost& host, FakeClient& client, std::uint16_t input, const state::WorldPosition& position) {
    host.clock.advance(kTick);
    host.datagram(client.peer, client.report_at(input, position.x, position.y, position.z));
    host.tick();
    client.exchange(host);
}

/** TV1: the travel of a character, for the session of its owner. */
void travel_of_the_owner(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, alpha, kPlace, input);
    world::Entity* entity = host.world.find_character(kCharacterA);
    const world::EntityId id = entity->id;
    check(entity->teleport == 1 && travel_steps(a, 0).states.size() == 1
              && travel_steps(a, 0).states[0].fields.at({0, kFieldTeleport})[0] == 1 && travel_steps(a, 0).order.size() == 1,
          "the first movement state of a session has the teleport number 1 of the entity, and the client gets no travel message "
          "at its world entry");

    // A travel to a place that is far (2 km).
    std::size_t from = a.got.size();
    const world::TravelResult far = host.world.travel(kCharacterA, kFar);
    check(far.error == TravelError::none && far.entity == entity && std::abs(far.distance - 2000.22F) < 0.1F
              && entity->position.x == kFar.x && entity->position.y == kFar.y && entity->position.z == kFar.z
              && entity->teleport == 2 && entity->positionEstimated,
          "travel: the entity has the new position and the next teleport number");
    check(saved_position(host.store, kCharacterA) && saved_position(host.store, kCharacterA)->x == kFar.x
              && saved_position(host.store, kCharacterA)->z == kFar.z,
          "the position row has the new place at once");
    host.steps(3, {&a});
    TravelSteps steps = travel_steps(a, from);
    note("travel of 2 km: the client gets: " + joined(steps.order));
    check(joined(steps.order) == "screen state done",
          "the client of the owner gets the message \"on teleport with loading screen\" (type 3951), then a bundle with the "
          "movement state of its character, then the message \"on player teleport\" (type 3954)");
    if (joined(steps.order) == "screen state done") {
        const Fragment& movement = steps.states[0];
        server::hub::Reader screen(steps.screens[0]);
        server::hub::Reader done(steps.done[0]);
        check(steps.screens[0].size() == 17 && screen.u64() == id + server::rep::kPlayerComponent && screen.u64() == id
                  && screen.u8() == 1 && steps.done[0].size() == 16 && done.u64() == id + server::rep::kPlayerComponent
                  && done.u64() == id,
              "the two messages have the facet target of the player component of the character and its entity; the first "
              "one has the kind 1 (17 bytes and 16 bytes)");
        check(float_at(movement.fields.at({0, kFieldPosition}), 0) == kFar.x && float_at(movement.fields.at({0, kFieldPosition}), 4) == kFar.y
                  && height_bits(movement) == sent_bits(kFar.z + world::kSpawnLift) && movement.fields.at({0, kFieldTeleport})[0] == 2
                  && movement.fields.count({0, kFieldAnchor}) == 1 && movement.fields.count({0, 11}) == 1
                  && movement.fields.count({0, 12}) == 1 && movement.fields.count({0, 41}) == 1 && movement.fields.count({0, 42}) == 1
                  && movement.fields.count({0, kFieldIdRel}) == 0,
              "the movement state has the new position (with the spawn lift of 0.5 m), the teleport number 2, the time anchor, "
              "the rotation, the look direction, and the two permission fields, and no id of a state");
        check(steps.slots.count(0) == 0, "the bundle has no second metadata state of the own character");
    }
    // The time anchor states continue, with no position.
    from = a.got.size();
    host.steps(60, {&a});
    check(travel_steps(a, from).order.empty() && a.got.size() > from,
          "after the travel the client gets its time anchor states again, and no second position");

    // A travel to a place that is near (60 m): no loading screen.
    from = a.got.size();
    const state::WorldPosition near{kFar.x + 60.0F, kFar.y, kFar.z};
    check(host.world.travel(kCharacterA, near).error == TravelError::none && entity->teleport == 3, "a second travel in one session");
    host.steps(3, {&a});
    steps = travel_steps(a, from);
    check(joined(steps.order) == "state done" && steps.states[0].fields.at({0, kFieldTeleport})[0] == 3
              && float_at(steps.states[0].fields.at({0, kFieldPosition}), 0) == near.x,
          "a travel of 60 m: the movement state with the teleport number 3 and the message \"on player teleport\", with no "
          "loading screen (the limit is 100 m)");
    // The loading screen on and off by the caller.
    from = a.got.size();
    host.world.travel(kCharacterA, state::WorldPosition{near.x + 10.0F, near.y, near.z}, TravelScreen::on);
    host.steps(3, {&a});
    const std::string forced = joined(travel_steps(a, from).order);
    from = a.got.size();
    host.world.travel(kCharacterA, kPlace, TravelScreen::off);
    host.steps(3, {&a});
    check(forced == "screen state done" && joined(travel_steps(a, from).order) == "state done",
          "the caller can turn the loading screen on for a place that is near, and off for a place that is far");
    // The teleport number: 1 to 15, never 0.
    std::string numbers;
    bool never = true;
    for (int index = 0; index < 16; ++index) {
        host.world.travel(kCharacterA, state::WorldPosition{kPlace.x + static_cast<float>(index), kPlace.y, kPlace.z});
        numbers += std::to_string(entity->teleport) + " ";
        never = never && entity->teleport >= 1 && entity->teleport <= 15;
    }
    note("the teleport numbers of 16 more travels: " + numbers);
    check(never && numbers == "6 7 8 9 10 11 12 13 14 15 1 2 3 4 5 6 ",
          "the teleport number goes from 15 to 1: it is never 0, the start number of a client");
    // A place that is not valid, and a character that is not in the world.
    const float x = entity->position.x;
    const std::uint8_t number = entity->teleport;
    const float notNumber = std::nanf("");
    check(host.world.travel(kCharacterA, state::WorldPosition{notNumber, 0.0F, 0.0F}).error == TravelError::badPosition
              && host.world.travel(kCharacterA, state::WorldPosition{100.0F, 100.0F, 1000.0F}).error == TravelError::badPosition
              && host.world.travel(kCharacterA, state::WorldPosition{100.0F, 100.0F, -100.0F}).error == TravelError::badPosition
              && host.world.travel(kCharacterA, state::WorldPosition{2.0e6F, 100.0F, 50.0F}).error == TravelError::badPosition
              && entity->position.x == x && entity->teleport == number,
          "a place that is not valid (a number that is not finite, a height at a limit of the range of the wire, a place outside "
          "the world) is refused, and the entity does not change");
    check(host.world.travel(kCharacterB, kFar).error == TravelError::notInWorld && host.world.travel("no-character", kFar).entity == nullptr,
          "a character that is not in the world has no entity to move");
    // A session that comes later gets the teleport number of the entity, not 0.
    host.datagram(a.peer, a.carrier_close());
    host.steps(2, {&a});
    check(host.world.find_character(kCharacterA) == nullptr && saved_position(host.store, kCharacterA)->x == x,
          "the session ends: the row has the place of the last travel");
}

/** TV2: the check of the position reports after a travel. */
void position_check_after_a_travel(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, alpha, kPlace, input);
    const world::Entity* entity = host.world.find_character(kCharacterA);
    const world::PositionTrack& track = host.session(1).position_track();
    const std::uint64_t acceptedBefore = track.count(PositionVerdict::accepted);
    check(acceptedBefore == 20 && !entity->positionEstimated, "before the travel: 20 accepted reports, and the position is exact");
    host.world.travel(kCharacterA, kFar);
    // The client made these reports before it got the new position.
    for (int index = 0; index < 5; ++index) {
        report(host, a, ++input, state::WorldPosition{kPlace.x + 0.1F * static_cast<float>(index), kPlace.y, kPlace.z});
    }
    check(track.count(PositionVerdict::beforeTravel) == 5 && track.count(PositionVerdict::accepted) == acceptedBefore
              && track.refused() == 0 && entity->position.x == kFar.x && entity->position.z == kFar.z
              && saved_position(host.store, kCharacterA)->x == kFar.x,
          "5 reports from the place before the travel are not used (not near the place of the travel): the entity and the row "
          "keep the new place, and the reports are not in the count of the refused reports");
    check(log_count("which is not near the place of the travel") == 1, "the log has one line for these reports");
    // The loading screen of the client: 3 s with no position. Then the reports of the new
    // place: the character falls 0.5 m to the ground.
    host.advance(std::chrono::milliseconds(3000));
    for (int index = 0; index < 15; ++index) {
        const float fall = std::min(0.04F * static_cast<float>(index), 0.5F);
        report(host, a, ++input, state::WorldPosition{kFar.x, kFar.y, kFar.z + world::kSpawnLift - fall});
    }
    check(track.count(PositionVerdict::accepted) == acceptedBefore + 15 && track.refused() == 0 && !entity->positionEstimated
              && entity->position.x == kFar.x && std::abs(entity->position.z - kFar.z) < 0.02F,
          "the reports of the new place are accepted: the entity has the exact position on the ground");
    // The character walks: the check works as before the travel.
    for (int index = 1; index <= 30; ++index) {
        report(host, a, ++input, state::WorldPosition{kFar.x + 0.18F * static_cast<float>(index), kFar.y, kFar.z});
    }
    check(track.count(PositionVerdict::accepted) == acceptedBefore + 45 && track.refused() == 0
              && std::abs(entity->position.x - (kFar.x + 5.4F)) < 0.001F,
          "a walk of 5.4 m in 1 s at the new place: each report is accepted");
    report(host, a, ++input, state::WorldPosition{kFar.x + 90.0F, kFar.y, kFar.z});
    check(track.count(PositionVerdict::tooFast) == 1 && std::abs(entity->position.x - (kFar.x + 5.4F)) < 0.001F,
          "a jump of 85 m in one step is refused (too fast), as before the travel");
    // A second travel: the same rule.
    const state::WorldPosition third{500.0F, 8000.0F, 300.0F};
    host.world.travel(kCharacterA, third);
    report(host, a, ++input, state::WorldPosition{kFar.x + 5.4F, kFar.y, kFar.z});
    report(host, a, ++input, state::WorldPosition{third.x, third.y, third.z + 0.4F});
    check(track.count(PositionVerdict::beforeTravel) == 6 && entity->position.x == third.x && !entity->positionEstimated
              && std::abs(entity->position.z - (third.z + 0.4F)) < 0.02F,
          "a second travel: the report of the place before it is not used, and the report of the new place is accepted");

    // The limits of the first report after a travel, with no session.
    const auto time = world::WorldClock::time_point(std::chrono::seconds(5000));
    const auto verdict = [&](float east, float up, std::chrono::milliseconds later) {
        world::PositionTrack fresh;
        const state::WorldPosition start{100.0F, 200.0F, height_of_wire(wire_height(kHeight))};
        (void)fresh.judge(world::PositionReport{1, start, wire_height(kHeight)}, start, time);
        fresh.traveled();
        const state::WorldPosition place{5000.0F, 200.0F, height_of_wire(wire_height(kHeight))};
        // A report of the place before the travel sets the time of the first report.
        const bool stale = fresh.judge(world::PositionReport{2, start, wire_height(kHeight)}, place, time + std::chrono::milliseconds(10))
                           == PositionVerdict::beforeTravel;
        const float z = kHeight + up;
        const world::PositionReport next{3, state::WorldPosition{place.x + east, place.y, height_of_wire(wire_height(z))}, wire_height(z)};
        return stale ? fresh.judge(next, place, time + std::chrono::milliseconds(10) + later) : PositionVerdict::noEntity;
    };
    using std::chrono::milliseconds;
    check(verdict(4.9F, 0.0F, milliseconds(0)) == PositionVerdict::accepted && verdict(5.2F, 0.0F, milliseconds(0)) == PositionVerdict::beforeTravel
              && verdict(0.0F, 4.9F, milliseconds(0)) == PositionVerdict::accepted && verdict(0.0F, -5.2F, milliseconds(0)) == PositionVerdict::beforeTravel
              && verdict(9.9F, 0.0F, milliseconds(1000)) == PositionVerdict::accepted
              && verdict(10.2F, 0.0F, milliseconds(1000)) == PositionVerdict::beforeTravel,
          "the first report after a travel has the limits of the first report of a session, from the new place: 5 m for the "
          "place and for the height at the time of the first report, and 10 m after 1 s (the time of a walk)");

    // A session with the camera estimate (a client with no position field).
    ServerHost other;
    const Player beta = add_player(other.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kPlace);
    FakeClient b(27001);
    other.enter(b, beta.registration());
    std::uint16_t number = 0;
    const auto camera = [&](const state::WorldPosition& position) {
        other.clock.advance(kTick);
        other.datagram(b.peer, b.input_at(++number, position.x, position.y, position.z));
        other.tick();
        b.exchange(other);
    };
    for (int index = 0; index < 10; ++index) {
        camera(kPlace);
    }
    const world::Entity* second = other.world.find_character(kCharacterB);
    other.world.travel(kCharacterB, kFar);
    for (int index = 0; index < 10; ++index) {
        camera(state::WorldPosition{kPlace.x + 2.0F * static_cast<float>(index), kPlace.y, kPlace.z});
    }
    check(second->position.x == kFar.x && second->position.y == kFar.y,
          "the camera estimate: a camera at the place before the travel does not move the entity back");
    for (int index = 0; index < 30; ++index) {
        camera(state::WorldPosition{kFar.x, kFar.y + 0.2F * static_cast<float>(index), kFar.z});
    }
    note("the camera estimate after a walk of 5.8 m at the new place: " + std::to_string(second->position.x) + " "
         + std::to_string(second->position.y));
    check(std::abs(second->position.x - kFar.x) < 1.0F && second->position.y > kFar.y + 3.0F && second->position.y < kFar.y + 7.0F,
          "with a camera at the new place the estimate follows the camera there");
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** What a client got for one interest number from the message `from` on. */
struct Seen {
    std::vector<Record> records;
    bool stopped{false};
    /** True when a record came after the stop of the number. */
    bool recordAfterStop{false};
};

Seen seen_of(const FakeClient& client, std::size_t from, std::uint32_t interest) {
    Seen seen;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        seen.stopped = seen.stopped || std::find(bundle.stops.begin(), bundle.stops.end(), interest) != bundle.stops.end();
        for (const Record& record : bundle.records) {
            if (record.interest == interest) {
                seen.records.push_back(record);
                seen.recordAfterStop = seen.recordAfterStop || seen.stopped;
            }
        }
    }
    return seen;
}

/** TV3: the travel of a character with two sessions (rule MP-10): what the observer gets. */
void travel_with_two_sessions(const Options&) {
    ServerHost host(observing());
    const state::WorldPosition placeB{kPlace.x + 10.0F, kPlace.y, kHeight};
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", placeB);
    FakeClient a(27000);
    FakeClient b(27001);
    std::uint16_t inputA = 0;
    std::uint16_t inputB = 0;
    enter_at(host, a, alpha, kPlace, inputA);
    enter_at(host, b, beta, placeB, inputB);
    host.advance(std::chrono::milliseconds(600));
    host.steps(3, {&a, &b});
    const world::Entity* entityA = host.world.find_character(kCharacterA);
    const world::Entity* entityB = host.world.find_character(kCharacterB);
    const std::uint32_t numberB = interest_of(host, 1, entityB->id);
    const std::uint32_t numberA = interest_of(host, 2, entityA->id);
    check(numberB >= 2 && numberA >= 2, "each of the two sessions observes the character of the other one");
    const std::uint64_t acceptedA = host.session(1).position_track().count(PositionVerdict::accepted);

    // B goes 30 m away: it stays in the interest distance of A.
    std::size_t fromA = a.got.size();
    std::size_t fromB = b.got.size();
    const state::WorldPosition near{kPlace.x + 40.0F, kPlace.y, kHeight};
    host.world.travel(kCharacterB, near);
    host.steps(3, {&a, &b});
    Seen seen = seen_of(a, fromA, numberB);
    const Fragment* movement = seen.records.size() == 1 ? fragment_of(seen.records[0], 16) : nullptr;
    check(!seen.stopped && movement != nullptr && seen.records[0].fragments.size() == 1
              && float_at(movement->fields.at({0, kFieldPosition}), 0) == near.x && movement->fields.at({0, kFieldTeleport})[0] == 2
              && movement->fields.count({0, 15}) == 1 && movement->fields.count({0, 41}) == 0,
          "B travels 30 m: the session of A gets one record of B with the movement state in the observer view (the new "
          "position, the teleport number 2, and the action of a player that stands), and no stop");
    check(joined(travel_steps(b, fromB).order) == "state done" && travel_steps(a, fromA).order.empty()
              && entityA->teleport == 1 && host.session(1).position_track().count(PositionVerdict::beforeTravel) == 0,
          "only the session of B gets the movement state of the owner and the travel message: the session of A gets no travel "
          "message, and the entity and the position check of A do not change");

    // B goes 2 km away: A must get the stop, and no record with the far place.
    fromA = a.got.size();
    fromB = b.got.size();
    host.world.travel(kCharacterB, kFar);
    host.steps(1, {&a, &b});
    seen = seen_of(a, fromA, numberB);
    const Seen ofA = seen_of(b, fromB, numberA);
    check(seen.stopped && seen.records.empty() && interest_of(host, 1, entityB->id) == 0,
          "B travels 2 km: in the same tick the session of A gets the stop of the interest number of B, and no record of B at "
          "the far place");
    check(ofA.stopped && ofA.records.empty() && interest_of(host, 2, entityA->id) == 0
              && joined(travel_steps(b, fromB).order) == "screen state done",
          "the session of B gets its loading screen message, its new position, and the stop of the interest number of A");
    // A walks in this time: its reports are accepted.
    report(host, a, ++inputA, state::WorldPosition{kPlace.x + 0.2F, kPlace.y, kPlace.z});
    check(host.session(1).position_track().count(PositionVerdict::accepted) == acceptedA + 1
              && host.session(1).position_track().refused() == 0,
          "a position report of A in this time is accepted: the travel of B does not change the check of A");

    // B comes back, 5 m from A: A gets a new first record of B.
    fromA = a.got.size();
    fromB = b.got.size();
    const state::WorldPosition back{kPlace.x + 5.0F, kPlace.y, kHeight};
    host.world.travel(kCharacterB, back);
    host.steps(3, {&a, &b});
    const std::uint32_t again = interest_of(host, 1, entityB->id);
    seen = seen_of(a, fromA, again);
    const Fragment* first = seen.records.empty() ? nullptr : fragment_of(seen.records[0], 16);
    check(again >= 2 && !seen.records.empty() && fragment_of(seen.records[0], 0) != nullptr && first != nullptr
              && float_at(first->fields.at({0, kFieldPosition}), 0) == back.x && first->fields.at({0, kFieldTeleport})[0] == 4,
          "B travels back to 5 m from A: the session of A gets a new first record of B (with the metadata state) at the new "
          "place, with the teleport number 4");
    check(interest_of(host, 2, entityA->id) >= 2 && !seen_of(b, fromB, interest_of(host, 2, entityA->id)).records.empty(),
          "and the session of B gets a new first record of A");
}

/** A backend with the development routes and the admin routes, with travel places. */
struct CommandHost {
    ServerHost host;
    Player alpha;
    Player beta;
    server::http::AdminAccess access;
    server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;

    explicit CommandHost(bool withPlaces = true)
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace)),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1300.0F, 1000.0F, kHeight})) {
        if (withPlaces && !host.places.set(places())) {
            throw std::runtime_error("travel test: the places are not valid");
        }
        if (!access.add(kTokenA, kSteamIdA) || !access.add(kTokenB, kSteamIdB)) {
            throw std::runtime_error("travel test: the setup failed");
        }
        server::http::register_dev_routes(router, services);
        server::http::register_admin_routes(router, services, access);
        host.tick();
    }

    static std::vector<gamedata::TravelPlace> places() {
        return {{"shrine_101", "shrine", "Made-up Shrine", "East Land", 7, state::WorldPosition{5000.0F, 6000.0F, 120.0F}},
                {"inn_202", "inn", "Inn of Nowhere", "West Land", 9, state::WorldPosition{700.0F, 9000.0F, 33.5F}}};
    }

    service::HttpResponse send(const char* method, const std::string& path, const std::string& query, const std::string& body = {},
                               const char* token = nullptr, const std::string& serviceHost = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = query.empty() ? path : path + "?" + query;
        request.host = serviceHost;
        request.body = body;
        if (token != nullptr) {
            request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        }
        return server::http::handle_request(router, request);
    }
    service::HttpResponse travel(const std::string& query) {
        return send("POST", "/v1/dev/world/travel", query);
    }
    service::HttpResponse admin(const char* character, const json& arguments, const char* token = kTokenA) {
        return send("POST", "/admin/v1/commands", {}, json{{"command", "travel"}, {"character", character}, {"args", arguments}}.dump(), token);
    }
};

/** TV4: the travel command of the development routes and of the admin interface, and the travel places. */
void travel_commands(const Options& options) {
    CommandHost c;
    ServerHost& host = c.host;
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, c.alpha, kPlace, input);
    const world::Entity* entity = host.world.find_character(kCharacterA);
    const std::string character = std::string("character=") + kCharacterA;

    // The lists.
    json list = json::parse(c.send("GET", "/v1/dev/world/places", "").body, nullptr, false);
    const json adminList = json::parse(c.send("GET", "/admin/v1/catalog/places", "", {}, kTokenA).body, nullptr, false);
    check(list.is_object() && list["loaded"] == true && list["places"].size() == 2 && list["places"][0]["id"] == "shrine_101"
              && list["places"][0]["kind"] == "shrine" && list["places"][0]["name"] == "Made-up Shrine"
              && list["places"][0]["territory"] == "East Land" && list["places"][0]["territoryId"] == 7
              && list["places"][0]["position"] == json::array({5000.0, 6000.0, 120.0}) && adminList == list,
          "the list of the travel places: the id, the kind, the name, the territory, and the position of each place, for the "
          "development route and for the admin route");
    check(c.send("GET", "/admin/v1/catalog/places", "").status == 401
              && c.send("GET", "/admin/v1/catalog/places", "", {}, "a-wrong-token").status == 401
              && c.send("GET", "/v1/dev/world/places", "", {}, nullptr, "gateway.soulwarden.local").status == 403,
          "the admin list needs the token of the start (401), and a development route gives 403 to a request with a service host");

    // The development route: a place, and numbers.
    json reply = json::parse(c.travel(character + "&place=shrine_101").body, nullptr, false);
    check(reply.is_object() && reply["applied"] == "world" && reply["place"]["id"] == "shrine_101"
              && reply["position"] == json::array({5000.0, 6000.0, 120.0}) && entity->position.x == 5000.0F
              && entity->position.y == 6000.0F && entity->position.z == 120.0F && entity->teleport == 2,
          "travel to a place: the character that is in the world is at the place (\"applied\":\"world\")");
    reply = json::parse(c.travel(character + "&x=1234.5&y=2345.25&z=67&screen=off").body, nullptr, false);
    check(reply.is_object() && reply["applied"] == "world" && !reply.contains("place") && entity->position.x == 1234.5F
              && entity->position.y == 2345.25F && entity->position.z == 67.0F && entity->teleport == 3
              && c.travel(character + "&place=INN_202").status == 200 && entity->position.x == 700.0F,
          "travel to a position in numbers; the id of a place has no letter case");
    const float x = entity->position.x;
    const std::uint8_t number = entity->teleport;
    check(c.travel(character + "&place=shrine_999").status == 404 && c.travel(character).status == 400
              && c.travel(character + "&place=shrine_101&x=1&y=2&z=3").status == 400
              && c.travel(character + "&x=100&y=200").status == 400 && c.travel(character + "&x=100&y=200&z=5000").status == 400
              && c.travel(character + "&x=100&y=200&z=-100").status == 400 && c.travel(character + "&x=nan&y=200&z=50").status == 400
              && c.travel(character + "&x=abc&y=200&z=50").status == 400 && c.travel(character + "&x=3e6&y=200&z=50").status == 400
              && c.travel(character + "&place=shrine_101&screen=maybe").status == 400 && c.travel(character + "&place=").status == 400
              && c.travel("place=shrine_101").status == 400 && c.travel("character=no-character&place=shrine_101").status == 404
              && c.send("POST", "/v1/dev/world/travel", character + "&place=shrine_101", {}, nullptr, "gateway.soulwarden.local").status == 403
              && entity->position.x == x && entity->teleport == number,
          "the command refuses a place that is not known (404), no place, a place and numbers together, a position with no "
          "height, a height outside the range, a number that is not finite or not a number, a place outside the world, a wrong "
          "value of the screen argument (400), no character (400), a character that is not known (404), and a request with a "
          "service host (403): the entity does not change");
    // A character that is not in the world: its row gets the place.
    reply = json::parse(c.travel(std::string("character=") + kCharacterB + "&place=shrine_101").body, nullptr, false);
    check(reply.is_object() && reply["applied"] == "stored" && host.world.find_character(kCharacterB) == nullptr
              && saved_position(host.store, kCharacterB) && saved_position(host.store, kCharacterB)->x == 5000.0F
              && saved_position(host.store, kCharacterB)->z == 120.0F,
          "travel of a character that is not in the world: its row has the place (\"applied\":\"stored\"), and it has no entity");
    FakeClient b(27001);
    host.enter(b, c.beta.registration());
    const world::Entity* second = host.world.find_character(kCharacterB);
    check(second != nullptr && second->position.x == 5000.0F && second->position.y == 6000.0F,
          "the next world entry of that character is at the place");

    // The admin interface.
    reply = json::parse(c.admin(kCharacterA, {{"place", "shrine_101"}}).body, nullptr, false);
    check(reply.is_object() && reply["applied"] == "world" && entity->position.x == 5000.0F,
          "the admin command travel moves the character of the account of the token");
    reply = json::parse(c.admin(kCharacterA, {{"x", 800}, {"y", 900.5}, {"z", 40}}).body, nullptr, false);
    check(reply.is_object() && reply["applied"] == "world" && entity->position.x == 800.0F && entity->position.y == 900.5F,
          "the admin command with a position in numbers");
    const float secondX = second->position.x;
    check(c.admin(kCharacterB, {{"place", "inn_202"}}).status == 404 && second->position.x == secondX
              && c.admin(kCharacterA, {{"place", "inn_202"}}, "a-wrong-token").status == 401
              && c.admin(kCharacterA, {{"place", 5}}).status == 404 && c.admin(kCharacterA, {{"place", json::array({1})}}).status == 400
              && c.admin(kCharacterA, {{"x", "east"}, {"y", 1}, {"z", 1}}).status == 400
              && c.admin(kCharacterA, {{"place", "inn_202"}, {"screen", 1}}).status == 400 && entity->position.x == 800.0F,
          "the admin command refuses the character of a different account (404: nothing changes for it), a wrong token (401), "
          "and arguments with a wrong form");
    const json stateReply = json::parse(c.send("GET", "/admin/v1/state", "", {}, kTokenA).body, nullptr, false);
    check(stateReply.is_object() && stateReply.contains("position") && stateReply["position"] == json::array({800.0, 900.5, 40.0}),
          "the state of the admin interface has the position of the character");

    // A backend with no travel places.
    CommandHost none(false);
    FakeClient n(27002);
    none.host.enter(n, none.alpha.registration());
    list = json::parse(none.send("GET", "/v1/dev/world/places", "").body, nullptr, false);
    check(list.is_object() && list["loaded"] == false && list["places"].empty() && none.travel(character + "&place=shrine_101").status == 400
              && none.travel(character + "&x=10&y=20&z=30").status == 200
              && none.host.world.find_character(kCharacterA)->position.x == 10.0F,
          "a backend with no travel places: the list is empty, a travel to a place is refused, and a travel to a position works");

    // The file of the travel places.
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "travel-places.json";
    const auto place = [](const char* id, double x, double y, double z) {
        return json{{"id", id}, {"kind", "shrine"}, {"name", "A Shrine"}, {"territory", "A Land"}, {"territoryId", 4},
                    {"position", {x, y, z}}};
    };
    const json document{{"format", 1}, {"counts", {{"places", 2}, {"territories", 1}}},
                        {"places", {place("shrine_1", 100.0, 200.0, 30.0), place("shrine_2", 300.0, 400.5, 50.25)}}};
    const auto load = [&](const json& content, gamedata::TravelPlaces& places) {
        std::ofstream(path, std::ios::binary) << content.dump();
        return places.load(path.string());
    };
    gamedata::TravelPlaces places;
    check(load(document, places) && places.loaded() && places.places().size() == 2 && places.territory_count() == 1
              && places.find("SHRINE_2") != nullptr && places.find("shrine_2")->position.y == 400.5F
              && places.find("shrine_3") == nullptr && places.error().empty(),
          "the travel places file loads, and a place is found by its id with no letter case");
    const auto refused = [&](json content, const char* what) {
        gamedata::TravelPlaces broken;
        const bool accepted = load(content, broken);
        note(std::string(what) + ": " + (accepted ? "loaded" : broken.error()));
        return !accepted && !broken.loaded() && broken.places().empty() && !broken.error().empty();
    };
    json twoIds = document;
    twoIds["places"][1]["id"] = "Shrine_1";
    json badId = document;
    badId["places"][0]["id"] = "shrine 1";
    json high = document;
    high["places"][0]["position"][2] = 5000.0;
    json counted = document;
    counted["counts"]["places"] = 3;
    json noName = document;
    noName["places"][1].erase("name");
    json format = document;
    format["format"] = 2;
    gamedata::TravelPlaces absent;
    check(refused(twoIds, "two places with one id") && refused(badId, "an id with a space") && refused(high, "a height of 5000 m")
              && refused(counted, "a count that is not the size of the list") && refused(noName, "a place with no name")
              && refused(format, "a different format") && !absent.load((options.workDirectory / "no-such-file.json").string())
              && absent.error() == "the file is not there",
          "a file that is not complete gives no place, as a whole: two places with one id, an id of a wrong form, a height "
          "outside the range, a wrong count, a place with no name, a different format; and a file that is not there");
    std::ofstream(path, std::ios::binary) << document.dump().substr(0, 150);
    gamedata::TravelPlaces cut;
    check(!cut.load(path.string()) && !cut.loaded(), "a file that ends early gives no place");
    std::filesystem::remove(path);
}

/** TV5: the creatures of the place of a travel. */
void creatures_of_the_new_place(const Options&) {
    ServerHost host;
    check(set_creatures(host, {test_point("point-at-the-start", 0, kPlace.x + 20.0F, kPlace.y),
                               test_point("point-far", 0, kFar.x + 30.0F, kFar.y),
                               test_point("point-far-2", 1, kFar.x - 12.0F, kFar.y + 4.0F)}),
          "the world has one spawn point near the start place and two near the far place");
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, alpha, kPlace, input);
    host.advance(std::chrono::milliseconds(600));
    host.steps(3, {&a});
    world::Creatures& creatures = host.world.creatures();
    const world::EntityId first = creatures.spawn_points()[0].entity;
    const std::uint32_t firstNumber = interest_of(host, 1, first);
    check(first != 0 && firstNumber >= 2 && creatures.count() == 1 && creatures.spawn_points()[1].entity == 0,
          "at the start place the world has the creature of the near spawn point, and the session observes it");
    const std::size_t from = a.got.size();
    host.world.travel(kCharacterA, kFar);
    // One tick of the game server: the task of the travel ran before it, the creature system
    // scans in this tick, and the session compares its distances.
    host.steps(1, {&a});
    const world::EntityId wolf = creatures.spawn_points()[1].entity;
    const world::EntityId rabbit = creatures.spawn_points()[2].entity;
    check(wolf != 0 && rabbit != 0 && creatures.find(first) == nullptr && creatures.count() == 2
              && creatures.counts().removedByDistance == 1,
          "in the first tick after the travel: the world has the creatures of the two spawn points of the new place, and the "
          "creature of the old place left the world");
    Seen old = seen_of(a, from, firstNumber);
    check(old.stopped && interest_of(host, 1, first) == 0, "the session gets the stop of the creature of the old place");
    host.steps(3, {&a});
    const std::uint32_t wolfNumber = interest_of(host, 1, wolf);
    const std::uint32_t rabbitNumber = interest_of(host, 1, rabbit);
    const Seen ofWolf = seen_of(a, from, wolfNumber);
    const Seen ofRabbit = seen_of(a, from, rabbitNumber);
    // The stop of a number goes out before a new entity gets the number.
    check(wolfNumber >= 2 && rabbitNumber >= 2 && !ofWolf.records.empty() && !ofRabbit.records.empty()
              && fragment_of(ofWolf.records.back(), 0) != nullptr && fragment_of(ofRabbit.records.back(), 0) != nullptr,
          "and it gets the first record of each creature of the new place (30 m and 13 m from the character)");
    check(joined(travel_steps(a, from).order) == "screen state done", "with the travel messages of its own character");
    // Back to the start: the creature of the first spawn point is made again (the distance rule).
    host.world.travel(kCharacterA, kPlace);
    host.steps(4, {&a});
    check(creatures.spawn_points()[0].entity != 0 && creatures.spawn_points()[0].entity != first && creatures.count() == 1
              && interest_of(host, 1, creatures.spawn_points()[0].entity) >= 2 && creatures.waiting_count() == 0,
          "after a travel back, the spawn point of the start place has a new creature, the session observes it, and the "
          "creatures of the far place left the world");
}


/** @return The movement fragment of the newest record that a client got for an interest number, or no value. */
std::optional<Fragment> movement_of(const FakeClient& client, std::size_t from, std::uint32_t interest, std::uint32_t slot) {
    std::optional<Fragment> newest;
    for (const Record& record : seen_of(client, from, interest).records) {
        if (const Fragment* fragment = fragment_of(record, slot)) {
            newest = *fragment;
        }
    }
    return newest;
}

/** @return The action index of the scope record of a creature movement state (the wire has the index plus 1), or -1. */
int scope_action(const std::optional<Fragment>& movement) {
    if (!movement || movement->fields.count({0, 33}) == 0) {
        return -1;
    }
    // scopeInfoBlob: byte count, start count:u16, fragment index + 1, action index + 1, ...
    server::hub::Reader reader(movement->fields.at({0, 33}));
    reader.size();
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

/**
 * WS3: the spawn action of a slice. The client shows the body and the name plate of some slices
 * only after the spawn action of the slice (77 of the 553 slices of the supported build): the
 * backend starts that action for each creature of such a slice, and each later record has it.
 */
void spawn_action_of_a_slice(const Options&) {
    CommandHost c;
    ServerHost& host = c.host;
    // The wolf slice needs its spawn action, and the rabbit slice does not.
    std::vector<gamedata::CreatureSlice> slices{test_slice("slices/test/wolf.dynamicslice", 0x11, kWolfSlots, 100.0F),
                                                test_slice("slices/test/rabbit.dynamicslice", 0x22, kRabbitSlots, 35.0F)};
    for (gamedata::CreatureSlice& slice : slices) {
        slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Spawn", "Spawn", 9, false},
                         {"Attack_Bite", "Attack_Bite", 3, false}, {"Sound", "", -1, false}};
    }
    slices[0].spawnActionName = "Spawn";
    const std::vector<gamedata::CreatureKind> kinds{test_kind("Test_Wolf", 0x11112222, 1.0F, 18, 0),
                                                    test_kind("Test_Rabbit", 0x55556666, 0.05F, 1, 1)};
    const std::vector<std::pair<std::uint32_t, float>> levels{{1, 337.5F}, {18, 1690.875977F}};
    check(host.creatures.set(slices, kinds, {}, levels) && host.creatures.slices()[0].spawnAction == 2U
              && !host.creatures.slices()[1].spawnAction,
          "the creature data: a slice with a spawn action has the index of that action, and a slice with none has no index");
    std::vector<gamedata::CreatureSlice> wrong = slices;
    wrong[1].spawnActionName = "Sound";
    gamedata::CreatureData refused;
    check(!refused.set(wrong, kinds, {}, levels) && !refused.loaded() && refused.error().find("spawn action") != std::string::npos,
          "data whose slice names a spawn action with no fragment in its action table is not used");

    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, c.alpha, kPlace, input);
    world::Creatures& creatures = host.world.creatures();
    const world::Entity* wolf = creatures.spawn("Test_Wolf", state::WorldPosition{kPlace.x + 4.0F, kPlace.y, kHeight}, 0.0F).entity;
    const world::Entity* rabbit = creatures.spawn("Test_Rabbit", state::WorldPosition{kPlace.x - 4.0F, kPlace.y, kHeight}, 0.0F).entity;
    check(wolf != nullptr && rabbit != nullptr && wolf->creature.action && wolf->creature.action->action == 2
              && wolf->creature.action->fragment == 9 && !wolf->creature.action->ends && !rabbit->creature.action,
          "a new creature of the slice with a spawn action has that action from its creation, with no end; a creature of the "
          "other slice has no action");
    std::size_t from = a.got.size();
    host.advance(std::chrono::milliseconds(600));
    host.steps(3, {&a});
    const std::uint32_t wolfNumber = interest_of(host, 1, wolf->id);
    const std::uint32_t rabbitNumber = interest_of(host, 1, rabbit->id);
    check(scope_action(movement_of(a, from, wolfNumber, kWolfSlots[0])) == 2
              && scope_action(movement_of(a, from, rabbitNumber, kRabbitSlots[0])) == -1,
          "the first record of the creature has the spawn action as the scope record of its movement state, and the record "
          "of the other creature has no scope record");
    // A hit: the reaction, and after its end the spawn action again for a later observer.
    const world::EntityId wolfId = wolf->id;
    check(creatures.hurt(wolfId, 10.0F).error == CreatureError::none && wolf->creature.action->action == 1 && wolf->creature.action->ends,
          "a hit starts the reaction of the creature, which has an end");
    host.advance(std::chrono::milliseconds(1600));
    host.steps(2, {&a});
    check(wolf->creature.action && wolf->creature.action->action == 2 && !wolf->creature.action->ends
              && wolf->creature.action->started == wolf->creature.madeAt,
          "after the end of the reaction the creature has its spawn action again, with the time of its creation");
    // A session that observes the creature later: travel away and back.
    host.world.travel(kCharacterA, kFar);
    host.steps(3, {&a});
    host.advance(std::chrono::seconds(20));
    from = a.got.size();
    host.world.travel(kCharacterA, kPlace);
    host.steps(4, {&a});
    const std::optional<Fragment> later = movement_of(a, from, interest_of(host, 1, wolfId), kWolfSlots[0]);
    const unsigned ticks = later && later->fields.count({0, 28}) == 1 ? later->fields.at({0, 28})[0] | (later->fields.at({0, 38})[0] << 8) : 0U;
    note("the time of the spawn action in the record of the later observer: " + std::to_string(ticks) + " ticks of 1/30 s");
    check(scope_action(later) == 2 && ticks > 600,
          "a session that observes the creature 20 s later gets the spawn action with the time from the creation of the "
          "creature (more than 600 ticks): its client shows the body at once");
    // The slice of the test has no death action: the dead creature keeps its spawn action, so
    // that its body shows for a later observer too.
    check(creatures.kill(wolfId).error == CreatureError::none && wolf->creature.dead && wolf->creature.action
              && wolf->creature.action->action == 2,
          "a dead creature of a slice with no death action keeps its spawn action: its body stays for a later observer");

    // The development command for an action of a creature.
    const char* rabbitId = nullptr;
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(rabbit->id));
    rabbitId = text;
    const auto action = [&](const std::string& query) { return c.send("POST", "/v1/dev/creatures/action", query); };
    check(action(std::string("creature=") + rabbitId + "&action=Attack_Bite").status == 200 && rabbit->creature.action
              && rabbit->creature.action->action == 3 && !rabbit->creature.action->ends,
          "the command action starts an action of the action table of the slice by its name");
    check(action(std::string("creature=") + rabbitId + "&action=Spawn&seconds=2").status == 200 && rabbit->creature.action->action == 2
              && rabbit->creature.action->ends,
          "with a time, the action has an end");
    host.advance(std::chrono::milliseconds(2100));
    host.steps(1, {&a});
    check(!rabbit->creature.action && action(std::string("creature=") + rabbitId + "&action=No_Action").status == 404
              && action(std::string("creature=") + rabbitId + "&action=Sound").status == 404
              && action(std::string("creature=") + rabbitId).status == 400
              && action(std::string("creature=") + rabbitId + "&action=Spawn&seconds=abc").status == 400
              && action("creature=0000000000001234&action=Spawn").status == 404 && !rabbit->creature.action,
          "after its time the creature has no action; an action that the table does not have, or that has no fragment, and a "
          "creature that is not there give 404; no action and a wrong time give 400");
}

} // namespace

std::vector<Case> world_spawn_cases() {
    return {
        {"WS1", "world size: the loader of a creature file with 33,856 spawn points, and a file of that size that is not complete",
         loader_at_world_size},
        {"WS2", "world size: the spawn point rule with 33,856 spawn points and 1 and 50 players, with a time limit", spawn_rule_at_world_size},
        {"WS3", "world size: the spawn action of a slice that shows its body only after it, and the command for an action",
         spawn_action_of_a_slice},
        {"TV1", "travel: the entity, the row, and what the client of the owner gets (the two messages and the movement state)",
         travel_of_the_owner},
        {"TV2", "travel: the check of the position reports after it, and the camera estimate", position_check_after_a_travel},
        {"TV3", "travel: two sessions (the new position, the stop, and the new record for the observer)", travel_with_two_sessions},
        {"TV4", "travel: the command of the development routes and of the admin interface, and the travel places file", travel_commands},
        {"TV5", "travel: the creatures of the new place come, and the creatures of the old place leave", creatures_of_the_new_place},
    };
}

} // namespace soulwarden::backend::tests

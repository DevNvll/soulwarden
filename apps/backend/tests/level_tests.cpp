// The cases of stage DG2: the level data of the own levels (docs/systems/instances.md, "The level data"). The levels, the maps, the kinds,
// the accounts, the characters, and the places are made up.
//
//   LV1  the reader of a level data set (gamedata/level_data.h): a complete set; a set with no manifest, a manifest of a different format
//        (an old file), a manifest of a different level: the set is refused as a whole; a part that its reader refuses has no data and the
//        other parts stay; a level name that is not a folder name; the check of the level tool (runs check_level_tool.py)
//   LV2  two instances of one own level and the main world (MP-10): the first instance reads the set, the second one does not read it
//        again; each space has its own creature of one spawn point (the kind and the slice of the level set); the session of A has no
//        record of the creature of B; a character of the main world at the same coordinates makes and sees no creature; a kill in one
//        instance makes its point wait, the other instance keeps its creature; the end of an instance removes its creature and its states,
//        and a later instance of the level reads nothing
//   LV3  the navigation of an own level (World::navigation(space)): the floor of the level set (30 m) in the space of the level, the floor
//        of the main world (22 m) at the same coordinates in the main world; the path of the development command in the space; a space of
//        an own level with no set has no ground; the creature of the level stands on the floor of its level
//   LV4  a level with no set and a refused set (an old file): the instance works as in stage DG1 (the move, no content, the return); a
//        world with no level source starts and enters an instance
//   LV5  the load: the first instance of a level of 2,000 spawn points (the read of the set in the world thread), then 20 instances of
//        that level with one member each, 200 ticks: the time limits, and each scan looks only at the points near the members

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/game_modes.h"
#include "gamedata/level_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_navigation.h"
#include "support/test_players.h"
#include "world/world.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using std::chrono::milliseconds;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000e1";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-0000000000e2";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-0000000000e3";
constexpr const char* kSteamIdC = "76561202255233020";
constexpr const char* kLevel = "nw_test_level_00";
constexpr const char* kMap = "Test_Level";
constexpr const char* kBareMap = "Test_Bare";
constexpr float kHeight = 30.0F;
/** The start of the made-up map (the floor of the level set is at 30 m there) and the place of the spawn point of the level. */
constexpr state::WorldPosition kStart{300.0F, 300.0F, kHeight};
constexpr state::WorldPosition kPoint{306.0F, 300.0F, kHeight};
/** A place of the main world. */
constexpr state::WorldPosition kHome{100.0F, 200.0F, 50.0F};

gamedata::GameMap map_of(const char* id, const char* level, std::uint32_t capacity) {
    gamedata::GameMap map;
    map.id = id;
    map.mode = id;
    map.level = level;
    map.terrain = std::string("coatlicue/") + level;
    map.starts = {gamedata::ModeStart{kStart, 90.0F}};
    map.nameKey = std::string("@") + id;
    map.name = id;
    map.recommendedLevel = 25;
    map.teamCapacity = capacity;
    map.minGroupSize = 3;
    map.dungeon = true;
    map.timeoutSeconds = 600;
    return map;
}

void set_maps(ServerHost& host) {
    if (!host.gameModes.set({map_of(kMap, kLevel, 5), map_of(kBareMap, "nw_test_bare_00", 5)})) {
        throw std::runtime_error("level test: the maps were not set: " + host.gameModes.error());
    }
}

/** The creature data of a file of the tool: one slice of a ghoul that fights, one kind, and the spawn points of `points`. */
json creature_document(const char* kindId, std::uint32_t vitalsId, const std::vector<state::WorldPosition>& points) {
    const json slice{{"path", "slices/test/ghoul.dynamicslice"}, {"asset", "22222222-2222-2222-2222-222222222222"}, {"sub", 2},
                     {"root", "2000000000000021"}, {"interestRadius", 60.0}, {"movement", 5}, {"vitals", 9}, {"marker", 15},
                     {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                     {"components", json::array({json::array({"2000000000000021", "0000000000000A01", "Root/Vitals"})})},
                     {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}, {"Attack_Claw", "Attack_Claw", 7, false}}},
                     {"behavior", {{"sight", {75.0, 30.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}}, {"speed", {2.0, 4.0}}}},
                     {"tags", "000000000000000000000000"},
                     {"attacks", json::array({json::array({2, 1.4, json::array({json::array({0.5, 2.0, 0.2, 0.0, "Slash", 4, "Physical",
                                                                                             "Attack_01_Claw"})})})})}};
    const json kind{{"id", kindId}, {"vitals", kindId}, {"category", std::string(kindId) + "_Category"}, {"vitalsId", vitalsId},
                    {"categoryId", vitalsId + 1}, {"name", kindId}, {"vitalsName", kindId}, {"family", "Lost"},
                    {"creatureType", "Dungeon"}, {"health", 1.0}, {"level", 25}, {"bodies", json::array({json::array({0, 100})})},
                    {"points", points.size()}, {"damage", 1.0}};
    json list = json::array();
    for (std::size_t index = 0; index < points.size(); ++index) {
        list.push_back(json::array({"p-" + std::to_string(index), 0, 25, points[index].x, points[index].y, points[index].z, 0.0, 300.0, 300.0,
                                    nullptr}));
    }
    return json{{"format", 2},
                {"levels", {{1, 337.5, 145.52, 50.0}, {25, 2325.5, 700.0, 250.0}}},
                {"slices", {slice}},
                {"kinds", {kind}},
                {"spawnPoints", list}};
}

/** Writes a level set into the folder levels/LEVEL of `root`: the manifest, the creature data, and (with `floor`) the navigation. */
std::filesystem::path write_set(const std::filesystem::path& root, const char* level, const json& creatures, std::optional<float> floor,
                                std::uint64_t format = gamedata::kLevelFormat) {
    const std::filesystem::path folder = root / gamedata::kLevelsFolder / level;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / gamedata::kLevelManifestFile, std::ios::trunc)
        << json{{"format", format}, {"level", level}, {"terrain", std::string("coatlicue/") + level}}.dump();
    if (!creatures.is_null()) {
        std::ofstream(folder / gamedata::kCreatureDataFile, std::ios::trunc) << creatures.dump();
    }
    if (floor) {
        TestNavRegion region;
        region.ground = 5.0F;
        const float height = *floor;
        // The tile of the start (tile 4, 4 of region 0, 0: 256 m to 320 m) has a floor at `height` above the terrain.
        region.tiles[{4, 4}] = [height](int, int, std::vector<TestFloor>& floors) {
            floors[0].sightTo = height - 5.0F - 0.5F;
            floors.push_back({height, 0});
        };
        region.write(folder / gamedata::kNavigationFolder);
    }
    return root / gamedata::kLevelsFolder;
}

bool near_value(float value, float wanted) {
    return std::fabs(value - wanted) < 0.05F;
}

void report(ServerHost& host, FakeClient& client, std::uint16_t number, const state::WorldPosition& place) {
    host.datagram(client.peer, client.report_at(number, place.x, place.y, place.z));
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& known = host.session(session).observer().known();
    const auto found = known.find(id);
    return found != known.end() ? found->second.interest : 0;
}

/** @return The creatures of a space. */
std::vector<const world::Entity*> creatures_of(ServerHost& host, world::SpaceId space) {
    std::vector<const world::Entity*> list;
    for (const auto& [id, entity] : host.world.entities()) {
        if (entity.kind == world::EntityKind::creature && entity.space == space) {
            list.push_back(&entity);
        }
    }
    return list;
}

void load_main_creatures(ServerHost& host, const std::filesystem::path& work) {
    // The creature data of the backend: a kind of the main world with its point far from each place of the cases.
    const auto path = work / "dg2-main-creatures.json";
    std::ofstream(path, std::ios::trunc) << creature_document("Test_Main_Ghoul", 0x33330000, {state::WorldPosition{1500.0F, 1500.0F, 50.0F}}).dump();
    if (!host.creatures.load(path.string())) {
        throw std::runtime_error("level test: the main creature data: " + host.creatures.error());
    }
}

/** LV1 */
void set_reader(const Options& options) {
    const std::filesystem::path root = options.workDirectory / "dg2-lv1";
    std::filesystem::remove_all(root);
    const std::filesystem::path levels = write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), 30.0F);
    const gamedata::LevelFolder folder(levels.string());
    auto data = folder.open(kLevel);
    check(data != nullptr && data->accepted() && data->terrain() == std::string("coatlicue/") + kLevel && data->creature_part().loaded
              && data->creatures().spawn_points().size() == 1 && data->navigation_part().loaded && data->navigation().regions() == 1
              && !data->height_part().loaded && !data->interaction_part().loaded && data->creature_part().bytes > 0,
          "a set with a manifest, creature data, and navigation data: the three are read; no heights and no interactions");
    check(folder.levels() == std::vector<std::string>{kLevel} && folder.open("nw_not_there_00") == nullptr,
          "the folders of the levels; a level with no folder has no set");
    check(!gamedata::level_name_valid("../nw_test_level_00") && !gamedata::level_name_valid("NW_Upper") && !gamedata::level_name_valid("")
              && gamedata::level_name_valid(kLevel) && folder.open("../levels") == nullptr,
          "a level text that is not a folder name of the levels is refused");
    std::filesystem::remove(levels / kLevel / gamedata::kLevelManifestFile);
    data = folder.open(kLevel);
    check(data != nullptr && !data->accepted() && data->problem().find("level.json") != std::string::npos && !data->creature_part().loaded,
          "a set with no manifest is refused as a whole");
    write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), std::nullopt, gamedata::kLevelFormat + 1);
    data = folder.open(kLevel);
    check(data != nullptr && !data->accepted() && data->problem().find("old file") != std::string::npos,
          "a manifest of a different format (an old file) is refused as a whole");
    write_set(root, "nw_other_level_00", json(), std::nullopt);
    std::filesystem::rename(levels / "nw_other_level_00", levels / "nw_wrong_name_00");
    data = folder.open("nw_wrong_name_00");
    check(data != nullptr && !data->accepted(), "a manifest that names a different level is refused");
    write_set(root, kLevel, json{{"format", 4}, {"levels", json::array()}}, 30.0F);
    data = folder.open(kLevel);
    check(data != nullptr && data->accepted() && !data->creature_part().loaded && !data->creature_part().problem.empty()
              && data->navigation_part().loaded,
          "a creature file that its reader refuses: no creature part, the navigation part stays");
    std::filesystem::remove_all(root);
    // The level tool (tools/gamedata/check_level_tool.py): the level from the map row, the set in one step, the terrain scale of a level.
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_level_tool.py";
    const auto output = options.workDirectory / "level-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "level-tool").string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::filesystem::is_regular_file(script) ? std::system(command.c_str()) : -1;
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    // Stage QG9e: 13 cases (the part "encounters" of a level with recipes, and the encounter tool that stops).
    check(status == 0 && correct == 13 && wrong == 0,
          "the level tool takes the level from the map row, makes the set in one step (a tool that stops keeps the old set), and the terrain "
          "of a level goes to the scale of the main world");
}

/** LV2 */
void two_instances(const Options& options) {
    const std::filesystem::path root = options.workDirectory / "dg2-lv2";
    std::filesystem::remove_all(root);
    ServerHost host(observing());
    set_maps(host);
    load_main_creatures(host, options.workDirectory);
    host.levels.folder = gamedata::LevelFolder(write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), 30.0F).string());
    host.world.set_creature_behavior(false);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, 50.0F});
    add_player(host.store, kSteamIdC, "Tester C", kCharacterC, "Gamma", std::nullopt);
    FakeClient a(27100);
    FakeClient b(27101);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    const world::InstanceResult first = host.world.instances().enter(kCharacterA, kMap);
    const world::InstanceResult second = host.world.instances().enter(kCharacterB, kMap);
    check(first.error == world::InstanceError::none && second.error == world::InstanceError::none && first.instance && second.instance
              && first.instance->space != second.instance->space,
          "setup: A and B are in two instances of one own level");
    if (!first.instance || !second.instance) {
        return;
    }
    const world::SpaceId spaceA = first.instance->space;
    const world::SpaceId spaceB = second.instance->space;
    check(host.levels.reads == 1 && log_count("world: level nw_test_level_00: its data set is read") == 1,
          "the first space of the level reads the set; the second space of the level reads nothing");
    // C stays in the main world at the coordinates of the spawn point of the level.
    check(host.world.spawn_character(kCharacterC, kPoint) == world::SpawnError::none, "setup: C is in the main world at the point of the level");
    for (std::uint16_t number = 1; number < 40; ++number) {
        report(host, a, number, kStart);
        report(host, b, number, kStart);
        host.steps(3, {&a, &b});
    }
    const std::vector<const world::Entity*> inA = creatures_of(host, spaceA);
    const std::vector<const world::Entity*> inB = creatures_of(host, spaceB);
    const gamedata::LevelData* data = host.world.levels().data_of(spaceA);
    check(inA.size() == 1 && inB.size() == 1 && inA[0]->id != inB[0]->id && data != nullptr,
          "each instance of the level has its own creature of the one spawn point of the level");
    check(creatures_of(host, world::kMainSpace).empty(), "the main world has no creature at the coordinates of the point of the level (C)");
    if (inA.size() != 1 || inB.size() != 1 || data == nullptr) {
        return;
    }
    check(inA[0]->creature.kind->id == "Test_Level_Ghoul" && inA[0]->creature.kind == &data->creatures().kinds()[0]
              && host.world.creatures().data_of(*inA[0]) == &data->creatures() && near_value(inA[0]->position.z, kHeight)
              && host.world.creatures().spawn_point_of(*inA[0]) == &data->creatures().spawn_points()[0],
          "the creature has the kind, the slice, and the spawn point of the level set");
    check(interest_of(host, 1, inA[0]->id) != 0 && interest_of(host, 1, inB[0]->id) == 0 && interest_of(host, 2, inB[0]->id) != 0
              && interest_of(host, 2, inA[0]->id) == 0,
          "the session of A has a record of the creature of its instance only, and the session of B of its own only");
    const world::EntityId killed = inA[0]->id;
    const world::EntityId kept = inB[0]->id;
    check(host.world.creatures().kill(killed).error == world::CreatureError::none, "setup: the creature of A dies");
    host.steps(20, {&a, &b});
    const world::SpawnSet* setA = host.world.creatures().spawn_set(spaceA);
    const world::SpawnSet* setB = host.world.creatures().spawn_set(spaceB);
    check(setA != nullptr && setB != nullptr && setA->waiting.size() == 1 && setB->waiting.empty() && host.world.find(kept) != nullptr
              && !host.world.find(kept)->creature.dead,
          "the point of A waits its respawn time; the point of B keeps its creature (each space its own states)");
    // Stage DG3: an instance of an own level with creatures has content: it waits empty for TimerThresholdDefault of its mode (600 s).
    const bool left = host.world.instances().leave(kCharacterA).error == world::InstanceError::none;
    const world::Instance* waiting = host.world.instances().instance_of_space(spaceA);
    check(left && waiting != nullptr && waiting->members.empty() && waiting->emptyUntil.has_value(),
          "stage DG3: the instance of A waits empty for the time of its mode (its level set is content)");
    // The two clients report each 10 s of the wait (a silent session ends): A at its return place, B in its instance.
    for (std::uint16_t number = 40; number < 101; ++number) {
        host.advance(std::chrono::seconds(10));
        report(host, a, number, host.world.find_character(kCharacterA)->position);
        report(host, b, number, kStart);
        host.steps(2, {&a, &b});
    }
    check(host.world.find(killed) == nullptr && host.world.creatures().spawn_set(spaceA) == nullptr
              && host.world.creatures().spawn_set(spaceB) != nullptr && host.world.find(kept) != nullptr,
          "the end of the instance of A removes its creature and its states; the instance of B keeps its own");
    check(host.world.instances().enter(kCharacterA, kMap).error == world::InstanceError::none && host.levels.reads == 1
              && host.world.levels().counts().spacesStarted == 3,
          "a later instance of the level reads nothing (the data stays)");
    std::filesystem::remove_all(root);
}

/** LV3 */
void level_navigation(const Options& options) {
    const std::filesystem::path root = options.workDirectory / "dg2-lv3";
    std::filesystem::remove_all(root);
    ServerHost host;
    set_maps(host);
    load_main_creatures(host, options.workDirectory);
    // The main world: the terrain at 22 m and its navigation data with the terrain only at the same coordinates.
    TestNavRegion main;
    main.ground = 22.0F;
    main.write(root / "main-navigation");
    check(host.navigation.set_directory((root / "main-navigation").string()), "setup: the navigation of the main world");
    host.terrain.source = [](float, float) { return std::optional<float>(22.0F); };
    host.levels.folder = gamedata::LevelFolder(write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), 30.0F).string());
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    check(host.world.spawn_character(kCharacterA, kHome) == world::SpawnError::none
              && host.world.spawn_character(kCharacterB, kHome) == world::SpawnError::none,
          "setup: A and B are in the main world");
    const world::InstanceResult own = host.world.instances().enter(kCharacterA, kMap);
    const world::InstanceResult bare = host.world.instances().enter(kCharacterB, kBareMap);
    check(own.instance && bare.instance, "setup: A is in an instance of the own level, B in an instance of a level with no set");
    if (!own.instance || !bare.instance) {
        return;
    }
    world::Navigation& levelNavigation = host.world.navigation(own.instance->space);
    check(&levelNavigation != &host.world.navigation() && &host.world.navigation(world::kMainSpace) == &host.world.navigation(),
          "a space of an own level has the navigation of its level; the main world its own");
    for (int tick = 0; tick < 3; ++tick) {
        host.advance(milliseconds(600));
        host.tick();
    }
    const std::optional<float> levelFloor = levelNavigation.ground_height(state::WorldPosition{kStart.x, kStart.y, 31.0F});
    const std::optional<float> mainFloor = host.world.navigation(world::kMainSpace).ground_height(state::WorldPosition{kStart.x, kStart.y, 31.0F});
    note("LV3 the floor at the start: level " + std::to_string(levelFloor.value_or(-1.0F)) + " m, main world " + std::to_string(mainFloor.value_or(-1.0F))
         + " m");
    check(levelFloor && std::fabs(*levelFloor - 30.0F) < 0.05F && mainFloor && std::fabs(*mainFloor - 22.0F) < 0.05F,
          "the ground of the space of the level is the floor of its set (30 m); the main world has its terrain at the same coordinates (22 m)");
    const world::PathReport path = levelNavigation.find_path(kStart, kPoint);
    check(path.found && !path.corners.empty() && std::fabs(path.corners.back().z - 30.0F) < 0.05F,
          "the path of the development command in the space of the level is on the floor of the level");
    check(!host.world.navigation(bare.instance->space).ground_height(kStart).has_value()
              && !host.world.navigation(bare.instance->space).has_data(),
          "a space of a level with no set has no ground and no data (no data of the main world at its coordinates)");
    for (int tick = 0; tick < 20; ++tick) {
        host.advance(milliseconds(100));
        host.tick();
    }
    const std::vector<const world::Entity*> creatures = creatures_of(host, own.instance->space);
    check(creatures.size() == 1 && creatures[0]->creature.mind.groundKnown && creatures[0]->creature.mind.walks,
          "the creature of the level stands on the floor of its level (it walks)");
    std::filesystem::remove_all(root);
}

/** LV4 */
void no_set(const Options& options) {
    const std::filesystem::path root = options.workDirectory / "dg2-lv4";
    std::filesystem::remove_all(root);
    {
        ServerHost host;
        set_maps(host);
        load_main_creatures(host, options.workDirectory);
        add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
        check(host.world.spawn_character(kCharacterA, kHome) == world::SpawnError::none, "setup: A is in the world (no level source)");
        const world::InstanceResult result = host.world.instances().enter(kCharacterA, kMap);
        for (int tick = 0; tick < 5; ++tick) {
            host.advance(milliseconds(600));
            host.tick();
        }
        check(result.instance && creatures_of(host, result.instance->space).empty() && host.world.levels().counts().noSet == 1
                  && host.world.instances().leave(kCharacterA).error == world::InstanceError::none,
              "a world with no level source: the instance of an own level has no content; the entry and the return work (stage DG1)");
    }
    ServerHost host;
    set_maps(host);
    load_main_creatures(host, options.workDirectory);
    host.levels.folder = gamedata::LevelFolder(
        write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), 30.0F, gamedata::kLevelFormat + 1).string());
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    check(host.world.spawn_character(kCharacterA, kHome) == world::SpawnError::none, "setup: A is in the world");
    const world::InstanceResult old = host.world.instances().enter(kCharacterA, kMap);
    for (int tick = 0; tick < 5; ++tick) {
        host.advance(milliseconds(600));
        host.tick();
    }
    check(old.instance && creatures_of(host, old.instance->space).empty() && host.world.levels().counts().refused == 1
              && log_count("its data set is refused (level.json has a different format") == 1
              && !host.world.navigation(old.instance->space).has_data(),
          "an old set: refused with one log line; the instance has no content");
    check(host.world.instances().leave(kCharacterA).error == world::InstanceError::none
              && host.world.find_character(kCharacterA)->space == world::kMainSpace,
          "the return to the main world");
    std::filesystem::remove_all(root);
}

/** LV5 */
void level_load(const Options& options) {
    const std::filesystem::path root = options.workDirectory / "dg2-lv5";
    std::filesystem::remove_all(root);
    // 2,000 spawn points on a grid of 40 by 50 with 20 m between two points (a level larger than each level of the game: Amrine has 51).
    std::vector<state::WorldPosition> points;
    for (int index = 0; index < 2000; ++index) {
        points.push_back(state::WorldPosition{100.0F + 20.0F * static_cast<float>(index % 40), 100.0F + 20.0F * static_cast<float>(index / 40),
                                              kHeight});
    }
    ServerHost host;
    set_maps(host);
    load_main_creatures(host, options.workDirectory);
    host.levels.folder = gamedata::LevelFolder(write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, points), 30.0F).string());
    host.world.set_creature_behavior(false);
    std::vector<std::string> ids;
    for (unsigned index = 0; index < 20; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "e%07u-0000-4000-8000-0000000000e9", index);
        char steam[24];
        std::snprintf(steam, sizeof(steam), "765612022553%05u", 50000 + index);
        add_player(host.store, steam, "Tester " + std::to_string(index), id, "Level" + std::to_string(index), std::nullopt);
        ids.emplace_back(id);
        if (host.world.spawn_character(id, kHome) != world::SpawnError::none) {
            check(false, "setup: a character is in the world");
            return;
        }
    }
    const auto readStarted = std::chrono::steady_clock::now();
    const bool firstEntered = host.world.instances().enter(ids[0], kMap).error == world::InstanceError::none;
    const double readTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - readStarted).count();
    const auto entriesStarted = std::chrono::steady_clock::now();
    bool entered = firstEntered;
    for (unsigned index = 1; index < ids.size(); ++index) {
        entered = host.world.instances().enter(ids[index], kMap).error == world::InstanceError::none && entered;
    }
    const double entryTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - entriesStarted).count();
    const auto ticksStarted = std::chrono::steady_clock::now();
    std::size_t largestScan = 0;
    for (int tick = 0; tick < 200; ++tick) {
        host.advance(milliseconds(10));
        host.tick();
        largestScan = std::max(largestScan, host.world.creatures().counts().lastScanPoints);
    }
    const double tickTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ticksStarted).count();
    note("the first entry (the read of the set of 2,000 points): " + std::to_string(readTime) + " ms; 19 entries: " + std::to_string(entryTime)
         + " ms; 200 ticks with 20 spaces: " + std::to_string(tickTime) + " ms; the largest scan looked at " + std::to_string(largestScan)
         + " points");
    check(entered && host.levels.reads == 1 && host.world.creatures().level_sets().size() == 20, "20 instances of the level, one read of its set");
    check(largestScan > 0 && largestScan < 20 * 2000 / 4, "a scan looks at the points near the members, not at each point of each space");
    check(readTime < 1000.0 && entryTime < 2000.0 && tickTime < 2000.0,
          "the time limits: the read of the set less than 1 s, 19 entries less than 2 s, 200 ticks less than 2 s");
    std::filesystem::remove_all(root);
}

} // namespace

std::vector<Case> level_cases() {
    return {{"LV1", "level data DG2 (I2): the reader of a level data set, the refused sets and parts", set_reader},
            {"LV2", "level data DG2 (I2, I6, MP-10): two instances of one own level and the main world at the same coordinates", two_instances},
            {"LV3", "level data DG2 (I2): the navigation of an own level and of the main world at the same coordinates", level_navigation},
            {"LV4", "level data DG2 (I2): a level with no set, a world with no level source, and an old set", no_set},
            {"LV5", "level data DG2 (I7): the load of the read of a set of 2,000 points and of 20 instances of one level, with time limits",
             level_load}};
}

} // namespace soulwarden::backend::tests

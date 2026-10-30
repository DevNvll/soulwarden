// The corrections of the code review of stage DG2 (~/Games/soulwarden/work/evidence/dungeons/dg2/code-review/FINDINGS.md), stage DG3.
// Each case fails on the tree of stage DG2 (trees/after-dg2). The base of each case is the proof case of the review (DG2RV1 to DG2RV4,
// DG2RVT1). The ids and the inputs are made up.
//
//   DG2R1  finding DG2R-R1: the respawn command and the creature list of a creature of a level use the spawn point of the spawn set of its
//          space (A in an own level, B in the main world with a creature of the point of the same index)
//   DG2R2  finding DG2R-R2: a gather of a level object of the main world level in a story instance (A) changes no state of the object of
//          the main world, and B in the main world at the same coordinates gets no HasInteractors
//   DG2R3  finding DG2R-R3: a gathered level object of the instance of A is a Looted entry of the LootTracker of A in that instance (B in
//          a second instance); the respawn of the row ends it
//   DG2R4  finding DG2R-R4: the navigation tool makes the regions of an output of a different level or world again
//          (tools/gamedata/check_navigation_cache_tool.py)
//   DG2R5  finding DG2R-R5: a level whose instances ended (A and B in two levels, then back in the main world) has no periodic scan of
//          the characters of the world when its navigation has no tile, route, or search

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/level_data.h"
#include "server/http/character_commands.h"
#include "support/chest_fixture.h"
#include "support/story_fixture.h"
#include "support/test_navigation.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace storyfix;
using std::chrono::milliseconds;

constexpr const char* kLevel = "nw_test_review_00";
constexpr const char* kMap = "Test_Review_Level";
constexpr state::WorldPosition kStart{300.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kPoint{306.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kHome{100.0F, 200.0F, 50.0F};

gamedata::GameMap map_of(const char* id, const char* level) {
    gamedata::GameMap map;
    map.id = id;
    map.mode = id;
    map.level = level;
    map.terrain = std::string("coatlicue/") + level;
    map.starts = {gamedata::ModeStart{kStart, 90.0F}};
    map.nameKey = std::string("@") + id;
    map.name = id;
    map.recommendedLevel = 25;
    map.teamCapacity = 5;
    map.minGroupSize = 3;
    map.dungeon = true;
    map.timeoutSeconds = 600;
    return map;
}

/** The creature data of a file of the tool: one slice of a ghoul, one kind, the spawn points of `points` (the form of case LV2). */
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

/** Writes a level set into levels/LEVEL of `root`: the manifest, the creature data, and (with `floor`) the navigation. */
std::filesystem::path write_set(const std::filesystem::path& root, const char* level, const json& creatures, std::optional<float> floor) {
    const std::filesystem::path folder = root / gamedata::kLevelsFolder / level;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / gamedata::kLevelManifestFile, std::ios::trunc)
        << json{{"format", gamedata::kLevelFormat}, {"level", level}, {"terrain", std::string("coatlicue/") + level}}.dump();
    if (!creatures.is_null()) {
        std::ofstream(folder / gamedata::kCreatureDataFile, std::ios::trunc) << creatures.dump();
    }
    if (floor) {
        TestNavRegion region;
        region.ground = 5.0F;
        const float height = *floor;
        region.tiles[{4, 4}] = [height](int, int, std::vector<TestFloor>& floors) {
            floors[0].sightTo = height - 5.0F - 0.5F;
            floors.push_back({height, 0});
        };
        region.write(folder / gamedata::kNavigationFolder);
    }
    return root / gamedata::kLevelsFolder;
}

service::RepServerOptions observing_players() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

std::vector<const world::Entity*> creatures_of(ServerHost& host, world::SpaceId space) {
    std::vector<const world::Entity*> list;
    for (const auto& [id, entity] : host.world.entities()) {
        if (entity.kind == world::EntityKind::creature && entity.space == space) {
            list.push_back(&entity);
        }
    }
    return list;
}

/** DG2R1 */
void respawn_command_space(const Options& options) {
    const auto root = options.workDirectory / "dg2r1";
    std::filesystem::create_directories(root);
    ServerHost host(observing_players());
    check(host.gameModes.set({map_of(kMap, kLevel)}), "setup: the map of the level");
    json main = creature_document("Test_Main_Ghoul", 0x33330000, {kPoint});
    main["spawnPoints"][0][8] = 100.0;
    main["spawnPoints"][0][7] = 100.0;
    const auto file = root / "main.json";
    std::ofstream(file) << main.dump();
    check(host.creatures.load(file.string()), "setup: the main data with one point and a timer of 100 s");
    host.levels.folder =
        gamedata::LevelFolder(write_set(root, kLevel, creature_document("Test_Level_Ghoul", 0x44440000, {kPoint}), std::nullopt).string());
    host.world.set_creature_behavior(false);
    const auto alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const auto beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kStart);
    FakeClient a(27200);
    FakeClient b(27201);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    const auto entry = host.world.instances().enter(kCharacterA, kMap);
    check(entry.instance.has_value(), "setup: A enters its own level");
    if (!entry.instance) {
        return;
    }
    const auto space = entry.instance->space;
    for (std::uint16_t number = 1; number < 25; ++number) {
        host.datagram(a.peer, a.report_at(number, kStart.x, kStart.y, kStart.z));
        host.datagram(b.peer, b.report_at(number, kStart.x, kStart.y, kStart.z));
        host.steps(3, {&a, &b});
    }
    const auto own = creatures_of(host, space);
    const auto mainWorld = creatures_of(host, world::kMainSpace);
    check(own.size() == 1 && mainWorld.size() == 1, "setup: one creature for each of the two sessions in different spaces");
    if (own.size() != 1 || mainWorld.size() != 1) {
        return;
    }
    const auto ownId = own[0]->id;
    const auto mainId = mainWorld[0]->id;
    check(host.world.creatures().kill(ownId).error == world::CreatureError::none
              && host.world.creatures().kill(mainId).error == world::CreatureError::none,
          "setup: both creatures die");
    const world::SpawnSet* set = host.world.creatures().spawn_set(space);
    const auto mainBefore = host.world.creatures().spawn_points()[0].respawnAt;
    const auto ownBefore = set->points[0].respawnAt;
    check(mainBefore && ownBefore && *ownBefore - *mainBefore == std::chrono::seconds(200), "setup: separate timers of 100 s and 300 s");
    const auto view = server::http::commands::creatures_view(host.world, kCharacterA, std::nullopt);
    double shown = -1.0;
    for (const auto& row : view["creatures"]) {
        if (row["kind"] == "Test_Level_Ghoul") {
            shown = row.value("respawnIn", -1.0);
        }
    }
    note("level timer shown " + std::to_string(shown));
    check(std::fabs(shown - 300.0) < 0.1, "the creature list shows the timer of the point of the level (300 s), not of the main world");
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(ownId));
    const json arguments{{"creature", hex}, {"seconds", 1}};
    const server::http::commands::JsonArguments args(arguments);
    service::HttpResponse refusal;
    auto command = server::http::commands::set_respawn_time(kCharacterA, args, server::http::commands::CreatureReach::nearCharacter, refusal);
    check(static_cast<bool>(command), "setup: the respawn command is valid");
    if (!command) {
        return;
    }
    const auto reply = command(host.world);
    const auto mainAfter = host.world.creatures().spawn_points()[0].respawnAt;
    const auto ownAfter = set->points[0].respawnAt;
    check(reply.status == 200 && mainAfter == mainBefore && ownAfter && *ownAfter == host.clock.now() + std::chrono::seconds(1),
          "the respawn command changes the point of the level and leaves the point of the main world of B as it was");
    std::filesystem::remove_all(root);
}

// DG2R2 and DG2R3: the statue of a quest of the mode in a story instance of the main world level (the form of the statue of Taurus).
constexpr const char* kQuest = "Review_Statue";
constexpr const char* kTag = "IO_Review_Statue";
constexpr const char* kEvent = "Review_Statue_Interact";
constexpr state::WorldPosition kStatueAt{303.0F, 300.0F, 30.0F};

gamedata::InteractionObject statue_object() {
    auto object = object_of(0x0E, InteractionKind::gathering, kStatueAt, {kGatherOptionIndex});
    object.bounds[0].tag = kTag;
    object.bounds[0].onlyOnQuest = true;
    return object;
}

json statue_quests() {
    json document = storyfix::story_quests();
    document["tasks"]["review_statue"] =
        questfix::task("Review_Statue", "taskgameevent", gamedata::task_type::kGameEvent,
                       {{"KillEnemyType", kEvent}, {"TargetQty", "2"}, {"GameModeID", storyfix::kMode}, {"InteractTag", kTag}});
    document["objectives"].push_back({{"id", kQuest},
                                      {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                      {"tree", json::array({json::array({0, "review_statue", json::array()})})}});
    return document;
}

void link_statue(storyfix::StoryShop& shop) {
    json document = gathering_document();
    json row = row_json("Review_Statue_Row", "Gatherable_Quest", "None", "NoToolNecessary", "", 0, 2.0, 60.0, 60.0, "Empty", kEvent, true);
    row["quest"] = true;
    document["rows"].push_back(row);
    document["capitals"][statue_object().key] = {kOreSlice, document["rows"].size() - 1, kTag, true};
    check(shop.host.gathering.set(document.dump(), {{kRegion, region_records()}}), "setup: the statue has a quest gathering row and a capital link");
}

InteractionBlock statue_block() {
    const auto statue = statue_object();
    InteractionBlock block;
    block.actor = statue.actor;
    block.group = statue.group;
    block.target = statue.bounds[0].target;
    block.action = 14;
    block.baseAction = 14;
    block.optionEntity = statue.group ^ 0x40ULL;
    block.committed = true;
    block.optionId = gamedata::interaction_option_id("GatherOption");
    return block;
}

void start_statue(storyfix::StoryShop& shop, bool bothInInstance) {
    shop.host.world.set_creature_behavior(false);
    link_statue(shop);
    shop.enter();
    auto& a = *shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(a, kQuest, "command", 0).ok(), "setup: A has the active task of the mode and of the tag");
    shop.enter_story(kCharacterA);
    if (bothInInstance) {
        shop.enter_story(kCharacterB);
    }
    shop.placeA = {302.0F, 300.0F, 30.0F};
    shop.placeB = shop.placeA;
    shop.move(kCharacterA, shop.placeA);
    shop.move(kCharacterB, shop.placeB);
    shop.wait(0.5F);
    check(a.space != world::kMainSpace && !a.positionEstimated, "setup: A is at the statue in its instance");
}

/** DG2R2 */
void object_state_space(const Options&) {
    storyfix::StoryShop shop(":memory:", storyfix::story_document(), statue_quests(), storyfix::kMode, {statue_object()});
    start_statue(shop, false);
    const auto before = shop.clientB.got.size();
    const auto began = shop.host.world.gathering().counts().begins;
    shop.request(shop.clientA, statue_block());
    auto& a = *shop.entity(kCharacterA);
    check(a.interaction && shop.host.world.gathering().counts().begins == began + 1, "setup: the gathering handler accepts the request of A");
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    const world::Entity* mainObject = shop.host.world.find(statue_object().group);
    bool leaked = false;
    for (const auto& [enabled, users] : object_states(shop.clientB, before)) {
        leaked = leaked || users > 0;
    }
    note("main object " + std::to_string(mainObject != nullptr) + ", users " + std::to_string(mainObject ? mainObject->object.bounds[0].users : 0)
         + ", B got users " + std::to_string(leaked));
    check((mainObject == nullptr || mainObject->space != world::kMainSpace || mainObject->object.bounds[0].users == 0) && !leaked
              && shop.entity(kCharacterB)->space == world::kMainSpace,
          "a gather in an instance sets no HasInteractors of the statue of the main world, and B in the main world gets no such state");
    shop.end_block(shop.clientA);
}

/** DG2R3 */
void object_tracker_space(const Options&) {
    storyfix::StoryShop shop(":memory:", storyfix::story_document(), statue_quests(), storyfix::kMode, {statue_object()});
    start_statue(shop, true);
    auto& a = *shop.entity(kCharacterA);
    const auto before = shop.clientA.got.size();
    const auto beforeB = shop.clientB.got.size();
    const auto completed = shop.host.world.gathering().counts().completions;
    shop.gather(shop.clientA, statue_block(), 2500);
    shop.wait(0.3F);
    const auto* quest = questfix::quest_of(a, kQuest);
    check(shop.host.world.gathering().counts().completions == completed + 1 && quest != nullptr && quest->tasks[0].progress == 1
              && a.questGathers.size() == 1,
          "setup: the gather of the statue succeeds and the task needs its next event");
    const auto maps = chests::tracker_maps(shop.clientA, 1, before);
    bool listed = false;
    if (!maps.empty()) {
        for (const auto& entry : maps.back()) {
            listed = listed || entry.actor == statue_object().actor;
        }
    }
    bool listedB = false;
    for (const auto& map : chests::tracker_maps(shop.clientB, 1, beforeB)) {
        for (const auto& entry : map) {
            listedB = listedB || entry.actor == statue_object().actor;
        }
    }
    check(listed && !listedB, "the gathered statue is a Looted entry of the LootTracker of A in its instance; B in its own instance has none");
    const auto begin = shop.host.world.gathering().counts().begins;
    shop.request(shop.clientA, statue_block());
    shop.end_block(shop.clientA);
    check(shop.host.world.gathering().counts().begins == begin, "control: the gathered statue is refused until its respawn");
    shop.wait(61.0F);
    check(a.questGathers.empty(), "control: the respawn of its row ends the gather");
    shop.gather(shop.clientA, statue_block(), 2500);
    quest = questfix::quest_of(a, kQuest);
    check(quest != nullptr && quest->tasks[0].progress == 2, "control: a second gather after the respawn gives the next event");
}

/** DG2R4 */
void navigation_cache(const Options& options) {
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_navigation_cache_tool.py";
    const auto output = options.workDirectory / "navigation-cache-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "navigation-cache").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::filesystem::is_regular_file(script) ? std::system(command.c_str()) : -1;
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && correct == 4 && wrong == 0,
          "the navigation tool keeps the regions of an output of the same level, and makes the regions of an output of a different level or of "
          "the main world again");
}

/** DG2R5 */
void inactive_level_work(const Options& options) {
    const auto root = options.workDirectory / "dg2r5";
    ServerHost host(observing_players());
    check(host.gameModes.set({map_of("Review_A", "nw_review_a"), map_of("Review_B", "nw_review_b")}), "setup: two own levels");
    write_set(root, "nw_review_a", json(), 30.0F);
    host.levels.folder = gamedata::LevelFolder(write_set(root, "nw_review_b", json(), 30.0F).string());
    const auto alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const auto beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kHome);
    FakeClient a(27202);
    FakeClient b(27203);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    const auto entryA = host.world.instances().enter(kCharacterA, "Review_A");
    const auto entryB = host.world.instances().enter(kCharacterB, "Review_B");
    check(entryA.instance && entryB.instance, "setup: both levels start");
    if (!entryA.instance || !entryB.instance) {
        return;
    }
    world::Navigation& navigationA = host.world.navigation(entryA.instance->space);
    world::Navigation& navigationB = host.world.navigation(entryB.instance->space);
    const std::uint64_t activeA = navigationA.counts().wantScans;
    host.advance(milliseconds(600));
    host.tick();
    check(navigationA.counts().wantScans > activeA, "control: a level with a space scans for the tiles of its characters");
    check(host.world.instances().leave(kCharacterA).error == world::InstanceError::none
              && host.world.instances().leave(kCharacterB).error == world::InstanceError::none,
          "setup: both instances end");
    host.advance(std::chrono::seconds(15));
    host.tick();
    check(navigationA.counts().tilesHeld == 0 && navigationB.counts().tilesHeld == 0 && !navigationA.busy() && !navigationB.busy(),
          "setup: both levels have no tile, no route, and no search");
    const std::uint64_t scansA = navigationA.counts().wantScans;
    const std::uint64_t scansB = navigationB.counts().wantScans;
    for (int tick = 0; tick < 4; ++tick) {
        host.advance(milliseconds(600));
        host.tick();
    }
    note("scans after the end: A " + std::to_string(navigationA.counts().wantScans - scansA) + ", B "
         + std::to_string(navigationB.counts().wantScans - scansB));
    check(navigationA.counts().wantScans == scansA && navigationB.counts().wantScans == scansB,
          "a level with no space and no work has no periodic scan of the characters of the world");
    std::filesystem::remove_all(root);
}

} // namespace

std::vector<Case> review_dg2_cases() {
    return {{"DG2R1", "review of DG2 (R1): the respawn command and the creature list use the spawn set of the space of the creature",
             respawn_command_space},
            {"DG2R2", "review of DG2 (R2): a level object in a story instance has the state of its space only", object_state_space},
            {"DG2R3", "review of DG2 (R3): a gathered level object of an instance has a LootTracker entry in that instance", object_tracker_space},
            {"DG2R4", "review of DG2 (R4): the navigation tool makes the regions of an output of a different level again", navigation_cache},
            {"DG2R5", "review of DG2 (R5): a level with no space and no work has no periodic world scan", inactive_level_work}};
}

} // namespace soulwarden::backend::tests

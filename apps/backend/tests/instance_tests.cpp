// The cases of stage DG1: the spaces of the world and the instances (docs/systems/instances.md). The game modes are made up (Test_Dungeon,
// Test_Solo, Test_Arena_01, Test_Arena_02); all accounts, characters, names, and places are made up.
//
//   IN1   the position index by space: entities at one place in different spaces are never candidates of a search in a different space
//         (the cells of a circle, and the walk of the cells of a large circle); the entities of a space
//   IN2   World::move_to_space: the refusals (no entity, no space, the same space, a bad place); the move: the space, the place, the index,
//         the teleport number; the position row keeps the main world position (reports, the save interval, and a travel in the instance)
//   IN3   the game modes (game-modes.json): the checks of a file, the lookup by map id and by the id of a mode with one map; the refusals of
//         "instance enter" with no data and with an unknown map; the check of the data tool (runs check_game_modes_tool.py)
//   IN4   the level change of the session in one connection (research dungeons.md 2.1): 1628 with the new instance, 1635 with the level of
//         the space, a new version and a new instance, 1617, an empty bundle with sequence 1, then the full owner record at interest 1
//         with its metadata state (no wait for 5181); the 5181 of the client is a log line; the return and a second entry
//   IN5   the account rule of the version and the context instance (open problem P1): the table; a session of the account after a level
//         change and a disconnect gets the next instance (3), not the instance of its session id (2, the instance that the client has)
//   IN6   the instance object and its two transactions: the rows of the entry and of the exit, a refused row changes nothing, the
//         refusals (in an instance, in no instance), the join (the map, the capacity, a solo map)
//   IN7   the restart rule (decision D9): a session that ends in an instance, a backend stop, and the member rows of a stop with no exit:
//         the next login is at the return place in the main world
//   IN8   two sessions (MP-10): one character moves into an instance and the observer in the main world gets the stop; the observer keeps
//         its level, its context, and its bundle sequence; the return gives the observer a new record
//   IN9   two characters in one instance see each other; a character in a second instance of the same map sees neither, and a character
//         of the main world at the same coordinates sees none of them (MP-10)
//   IN10  the scans of the systems in an instance (criterion I4): no creature of a main world spawn point for a character in an instance at
//         its coordinates, no target across spaces, no creature record; a probe of the space goes with the end of the instance
//   IL1   the load: 100 instances with 4 members each and 50 characters of the main world: the entries, 200 ticks, and 10,000 searches in a
//         space, with time limits

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include "gamedata/creature_data.h"
#include "gamedata/game_modes.h"
#include "server/rep/level_contexts.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/world.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using std::chrono::milliseconds;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000d1";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-0000000000d2";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-0000000000d3";
constexpr const char* kCharacterD = "dddddddd-0000-4000-8000-0000000000d4";
// Made-up Steam ids of the accounts C and D (the form of kSteamIdA and kSteamIdB).
constexpr const char* kSteamIdC = "76561202255233010";
constexpr const char* kSteamIdD = "76561202255233011";
constexpr float kHeight = 50.0F;
/** The start of the made-up dungeon map (the place of the Amrine start of the data). */
constexpr state::WorldPosition kStart{645.0F, 663.0F, 153.0F};
/** A place of the main world. */
constexpr state::WorldPosition kHome{100.0F, 200.0F, kHeight};

gamedata::GameMap map_of(const char* id, const char* mode, const char* level, state::WorldPosition start, std::uint32_t capacity, bool solo) {
    gamedata::GameMap map;
    map.id = id;
    map.mode = mode;
    map.level = level;
    map.terrain = std::string("coatlicue/") + level;
    map.starts = {gamedata::ModeStart{start, 90.0F}};
    map.nameKey = std::string("@") + id;
    map.name = id;
    map.recommendedLevel = 25;
    map.teamCapacity = capacity;
    map.minGroupSize = capacity == 0 ? 0 : 3;
    map.dungeon = true;
    map.soloTrial = solo;
    map.timeoutSeconds = 600;
    return map;
}

std::vector<gamedata::GameMap> test_maps() {
    return {map_of("Test_Dungeon", "Test_Dungeon", "nw_test_dungeon_00", kStart, 5, false),
            map_of("Test_Solo", "Test_Solo", "nw_test_solo_00", state::WorldPosition{300.0F, 300.0F, 20.0F}, 0, true),
            map_of("Test_Arena_01", "Test_Arena", "nw_test_arena_01", state::WorldPosition{939.0F, 935.0F, 226.5F}, 3, false),
            map_of("Test_Arena_02", "Test_Arena", "nw_test_arena_02", state::WorldPosition{1206.0F, 1300.0F, 50.7F}, 3, false)};
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** The level information 1635 (writer 0x1500850): level, terrain, the four floats, u64, the list, flag, instance, reload, flag, version. */
struct LevelInfo {
    std::string level;
    std::string terrain;
    std::uint8_t instance{0};
    std::uint8_t reload{0};
    std::uint64_t version{0};
    bool ok{false};
};

LevelInfo level_info(const Sent& sent) {
    LevelInfo info;
    server::hub::Reader reader(sent.body);
    info.level = reader.string();
    info.terrain = reader.string();
    for (int index = 0; index < 4; ++index) {
        reader.u32();
    }
    reader.u64();
    reader.size();
    reader.u8();
    info.instance = reader.u8();
    info.reload = reader.u8();
    reader.u8();
    info.version = reader.u64();
    info.ok = reader.ok() && reader.left() == 0 && sent.type == 1635;
    return info;
}

/** @return The context instance of the level block of a self identification 1628 (identity 17 bytes, three references of 36 bytes). */
std::uint32_t self_instance(const Sent& sent) {
    server::hub::Reader reader(sent.body);
    reader.raw(17 + 3 * 36);
    return reader.u32();
}

/** @return The messages of a client from an index on. */
std::vector<const Sent*> since(const FakeClient& client, std::size_t first) {
    std::vector<const Sent*> list;
    for (std::size_t index = first; index < client.got.size(); ++index) {
        list.push_back(&client.got[index]);
    }
    return list;
}

std::string types_of(const std::vector<const Sent*>& list) {
    std::string text;
    for (const Sent* sent : list) {
        text += std::to_string(sent->type) + " ";
    }
    return text;
}

/** @return The interest number of an entity in the table of a session, or 0. */
std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& known = host.session(session).observer().known();
    const auto found = known.find(id);
    return found != known.end() ? found->second.interest : 0;
}

bool sql(const std::filesystem::path& path, const std::string& statement) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.string().c_str(), &database) != SQLITE_OK) {
        sqlite3_close(database);
        return false;
    }
    char* error = nullptr;
    const int status = sqlite3_exec(database, statement.c_str(), nullptr, nullptr, &error);
    sqlite3_free(error);
    sqlite3_close(database);
    return status == SQLITE_OK;
}

world::Instances& instances(ServerHost& host) {
    return host.world.instances();
}

void set_maps(ServerHost& host) {
    if (!host.gameModes.set(test_maps())) {
        throw std::runtime_error("instance test: the maps were not set: " + host.gameModes.error());
    }
}

bool near(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::fabs(a.x - b.x) < 0.01F && std::fabs(a.y - b.y) < 0.01F && std::fabs(a.z - b.z) < 0.01F;
}

/** IN1 */
void index_by_space(const Options&) {
    world::PositionIndex index(64.0F);
    index.insert(1, 0, state::WorldPosition{10.0F, 10.0F, 0.0F});
    index.insert(2, 1, state::WorldPosition{10.0F, 10.0F, 0.0F});
    index.insert(3, 2, state::WorldPosition{10.0F, 10.0F, 0.0F});
    index.insert(4, 1, state::WorldPosition{500.0F, 500.0F, 0.0F});
    const auto candidates = [&](world::SpaceId space, float x, float y, float range) {
        std::set<world::EntityId> ids;
        index.for_each_candidate(space, state::WorldPosition{x, y, 0.0F}, range, [&](world::EntityId id) { ids.insert(id); });
        return ids;
    };
    check(candidates(0, 10.0F, 10.0F, 5.0F) == std::set<world::EntityId>{1} && candidates(1, 10.0F, 10.0F, 5.0F) == std::set<world::EntityId>{2}
              && candidates(2, 10.0F, 10.0F, 5.0F) == std::set<world::EntityId>{3} && candidates(3, 10.0F, 10.0F, 5.0F).empty(),
          "three entities at one place in three spaces: a search finds only the entity of its space");
    check(candidates(1, 0.0F, 0.0F, 100000.0F) == std::set<world::EntityId>{2, 4} && candidates(0, 0.0F, 0.0F, 100000.0F) == std::set<world::EntityId>{1},
          "the walk of the cells of a large circle stays in its space");
    std::set<world::EntityId> inSpace;
    index.for_each_in_space(1, [&](world::EntityId id) { inSpace.insert(id); });
    check(inSpace == std::set<world::EntityId>{2, 4}, "the entities of space 1 (the end of an instance)");
    index.move(2, 1, state::WorldPosition{10.0F, 10.0F, 0.0F}, state::WorldPosition{500.0F, 500.0F, 0.0F});
    check(candidates(1, 500.0F, 500.0F, 5.0F) == std::set<world::EntityId>{2, 4} && candidates(0, 500.0F, 500.0F, 5.0F).empty(),
          "a move in a space stays in that space");
    index.erase(2, 1, state::WorldPosition{500.0F, 500.0F, 0.0F});
    index.erase(4, 1, state::WorldPosition{500.0F, 500.0F, 0.0F});
    std::size_t left = 0;
    index.for_each_in_space(1, [&](world::EntityId) { ++left; });
    check(left == 0 && index.cell_count() == 2, "an erase removes the entity from the cells of its space; spaces 0 and 2 at one place have a cell each");
}

/** IN2 */
void move_to_space(const Options&) {
    ServerHost host(observing());
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    host.datagram(a.peer, a.report_at(1, kHome.x, kHome.y, kHome.z));
    host.steps(3, {&a});
    check(host.world.spawn_character(kCharacterB, kHome) == world::SpawnError::none, "setup: B is in the world at the place of A");
    world::Entity& entity = *host.world.find_character(kCharacterA);
    check(host.world.move_to_space(kCharacterA, 7, kStart) == world::SpaceMoveError::noSpace
              && host.world.move_to_space("eeeeeeee-0000-4000-8000-0000000000e5", world::kMainSpace, kStart) == world::SpaceMoveError::notInWorld
              && host.world.move_to_space(kCharacterA, world::kMainSpace, kStart) == world::SpaceMoveError::sameSpace,
          "the refusals: a space that is not there, a character that is not in the world, the space of the character");
    const std::uint8_t teleportBefore = entity.teleport;
    const world::InstanceResult entered = instances(host).enter(kCharacterA, "Test_Dungeon");
    host.steps(2, {&a});
    check(entered.error == world::InstanceError::none && entered.instance && entity.space == entered.instance->space && entity.space != 0
              && near(entity.position, kStart),
          "the entry: the entity is in the space of the instance, at the start of the map");
    std::size_t inInstance = 0;
    std::size_t inMain = 0;
    host.world.for_each_near(entity.space, kStart, 1.0F, [&](world::Entity& found) { inInstance += found.id == entity.id ? 1 : 0; });
    host.world.for_each_near(world::kMainSpace, kStart, 1.0F, [&](world::Entity& found) { inMain += found.id == entity.id ? 1 : 0; });
    std::size_t atHome = 0;
    host.world.for_each_near(entity.space, kHome, 5.0F, [&](world::Entity&) { ++atHome; });
    check(inInstance == 1 && inMain == 0 && atHome == 0 && host.world.space_entity_count(entity.space) == 1,
          "the index: the character is in the cells of its space, not in the cells of the main world; B (main world) is not in the space");
    check(entity.teleport != teleportBefore && entity.changed[world::ComponentKey::movement] != 0, "a new teleport number and a marked movement state");
    check(host.world.move_to_space(kCharacterA, world::kMainSpace, state::WorldPosition{NAN, 0.0F, 0.0F}) == world::SpaceMoveError::badPosition,
          "a place that is not a place for a character is refused");
    // The position row keeps the main world position: the reports of the instance, the save interval, a travel in the instance.
    for (std::uint16_t number = 2; number < 40; ++number) {
        host.datagram(a.peer, a.report_at(number, kStart.x, kStart.y + 0.1F * static_cast<float>(number), kStart.z));
        host.steps(1, {&a});
    }
    host.advance(milliseconds(6000));
    host.steps(2, {&a});
    const world::TravelResult travel = host.world.travel(kCharacterA, state::WorldPosition{660.0F, 680.0F, 153.0F});
    host.steps(2, {&a});
    const auto row = saved_position(host.store, kCharacterA);
    check(travel.error == world::TravelError::none && entity.space != 0 && row && near(*row, kHome),
          "the position row keeps the main world position (the return place) after the reports, the save interval, and a travel in the "
          "instance");
}

/** IN3 */
void game_modes_data(const Options& options) {
    gamedata::GameModes modes;
    check(!modes.set({}) && !modes.loaded(), "no map: not loaded");
    std::vector<gamedata::GameMap> twice = test_maps();
    twice.push_back(test_maps()[0]);
    twice.back().id = "TEST_DUNGEON";
    check(!modes.set(twice), "two maps with one id (case folded): refused");
    std::vector<gamedata::GameMap> high = test_maps();
    high[0].starts[0].position.z = 1200.0F;
    check(!modes.set(high), "a start above the height range of the wire: refused");
    check(modes.set(test_maps()) && modes.find("test_dungeon") != nullptr && modes.find("Test_Solo") != nullptr
              && modes.find("Test_Arena_02") != nullptr && modes.find("Test_Arena") == nullptr && modes.find("Nothing") == nullptr,
          "the lookup by map id (no case), and by a mode with one map; a mode with two maps needs its map id");
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "dg1-game-modes.json";
    json document{{"format", 1},
                  {"counts", {{"modes", 1}, {"maps", 1}, {"mapsWithNoPlace", 0}}},
                  {"maps",
                   {{{"id", "Test_Dungeon"}, {"mode", "Test_Dungeon"}, {"row", 19}, {"modeRow", 10}, {"level", "nw_test_dungeon_00"},
                     {"terrain", "coatlicue/nw_test_dungeon_00"}, {"starts", {{645.0, 663.0, 153.0, 0.0}}}, {"nameKey", "@x"}, {"name", "X"},
                     {"recommendedLevel", 25}, {"requiredLevel", 0}, {"teamCapacity", 5}, {"minGroupSize", 3}, {"dungeon", true},
                     {"soloTrial", false}, {"raidTrial", false}, {"timeoutSeconds", 600}}}}};
    std::ofstream(path) << document.dump();
    gamedata::GameModes file;
    check(file.load(path.string()) && file.maps().size() == 1 && file.find("Test_Dungeon")->starts[0].position.x == 645.0F
              && file.find("Test_Dungeon")->level == "nw_test_dungeon_00",
          "a file of the tool loads");
    document["counts"]["maps"] = 2;
    std::ofstream(path, std::ios::trunc) << document.dump();
    check(!file.load(path.string()) && !file.loaded(), "a file whose list does not have the count of the file: refused as a whole");
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::noData, "no game modes: the entry is refused");
    set_maps(host);
    check(instances(host).enter(kCharacterA, "Nothing").error == world::InstanceError::unknownMap
              && instances(host).enter(kCharacterA, "Test_Arena").error == world::InstanceError::unknownMap
              && host.world.find_character(kCharacterA)->space == world::kMainSpace,
          "an unknown map, and a mode with two maps: refused, nothing changed");
    // The data tool (tools/gamedata/check_game_modes_tool.py).
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_game_modes_tool.py";
    const auto output = options.workDirectory / "game-modes-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "game-modes-tool").string() + "\" > \""
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
    check(status == 0 && correct == 8 && wrong == 0,
          "the game modes tool writes the lower-case level and terrain texts and the start of each team, and it writes no file for two "
          "modes or two maps with one id, a map of an unknown mode, and a start place that is not four finite numbers or is too high");
}

/** IN4 */
void level_change(const Options&) {
    ServerHost host;
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    host.datagram(a.peer, a.report_at(1, kHome.x, kHome.y, kHome.z));
    host.steps(3, {&a});
    const LevelInfo first = level_info(a.got.at(2));
    check(first.ok && first.level == "newworld_vitaeeterna" && first.instance == 1 && first.version == 1,
          "setup: the world entry has the main world, instance 1, version 1 (the values of the stored bytes of T2)");
    const world::Entity& entity = *host.world.find_character(kCharacterA);
    const auto change = [&](const char* what, const std::function<bool()>& run, const char* level, std::uint8_t instance) {
        const std::size_t before = a.got.size();
        check(run(), std::string("setup: ") + what);
        host.steps(3, {&a});
        const std::vector<const Sent*> got = since(a, before);
        check(got.size() >= 5 && got[0]->type == 1628 && got[1]->type == 1635 && got[2]->type == 1617 && got[3]->type == 8,
              std::string(what) + ": the messages 1628, 1635, 1617, 8 first (" + types_of(got) + ")");
        if (got.size() < 5) {
            return;
        }
        const LevelInfo info = level_info(*got[1]);
        check(info.ok && info.level == level && info.terrain == std::string("coatlicue/") + level && info.instance == instance
                  && info.version == instance && info.reload == 1,
              std::string(what) + ": the level information has the level " + level + ", a new version and a new instance (" + std::to_string(instance)
                  + "), and the reload flag");
        check(self_instance(*got[0]) == instance, std::string(what) + ": the self identification has the new instance in its level block");
        const Bundle empty = read_bundle(got[3]->body);
        check(empty.complete && empty.sequence == 1 && empty.instance == instance && empty.records.empty() && empty.stops.empty(),
              std::string(what) + ": an empty bundle with sequence 1 and the new instance");
        bool owner = false;
        std::uint64_t sequence = 1;
        bool ordered = true;
        for (std::size_t index = 4; index < got.size(); ++index) {
            if (got[index]->type != 8) {
                continue;
            }
            const Bundle bundle = read_bundle(got[index]->body);
            ordered = ordered && bundle.complete && bundle.sequence == sequence + 1 && bundle.instance == instance;
            sequence = bundle.sequence;
            for (const Record& record : bundle.records) {
                owner = owner || (bundle.sequence == 2 && record.interest == 1 && fragment_of(record, 0) != nullptr);
            }
        }
        check(owner && ordered, std::string(what) + ": the next bundle (sequence 2) has the full owner record at interest 1 with its metadata "
                                                    "state, and the sequence has no gap (no 5181 came)");
    };
    change("the entry", [&] { return instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none; },
           "nw_test_dungeon_00", 2);
    // The 5181 of the client (the new context instance): a log line, no change.
    server::hub::Writer body;
    body.u64(0x1234);
    body.u64(entity.id);
    body.u8(2);
    host.datagram(a.peer, a.component_message(server::rep::kTypeAckLevelInfoChanged, entity.actor.bytes, body.data()));
    host.steps(1, {&a});
    check(log_count("rep: the client acknowledged the level information: context instance 2 (the session has 2)") == 1,
          "the 5181 of the client is a log line (the backend does not wait for it)");
    change("the return", [&] { return instances(host).leave(kCharacterA).error == world::InstanceError::none; }, "newworld_vitaeeterna", 3);
    check(entity.space == world::kMainSpace && near(entity.position, kHome), "the return: the character is at its return place in the main world");
    change("a second entry", [&] { return instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none; },
           "nw_test_dungeon_00", 4);
}

/** IN5 */
void account_rule(const Options&) {
    server::rep::LevelContexts table;
    const server::rep::LevelContext a1 = table.next("persona-a", 1);
    const server::rep::LevelContext a2 = table.next("persona-a", 1);
    const server::rep::LevelContext b1 = table.next("persona-b", 2);
    const server::rep::LevelContext a3 = table.next("persona-a", 3);
    check(a1.version == 1 && a1.instance == 1 && a2.version == 2 && a2.instance == 2 && b1.version == 2 && b1.instance == 2 && a3.version == 3
              && a3.instance == 3,
          "the table: the first of an account has the values of the session id; then one above the last values of the account");
    server::rep::LevelContexts wrap;
    (void)wrap.next("persona-a", 255);
    check(wrap.next("persona-a", 256).instance == 1, "the instance after 255 is 1 (never 0)");
    ServerHost host;
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kHome);
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A enters the instance");
    host.steps(3, {&a});
    std::uint8_t last = 0;
    for (const Sent& sent : a.got) {
        if (sent.type == 1635) {
            last = level_info(sent).instance;
        }
    }
    check(last == 2, "the client of A has the context instance 2 (a level change in session 1)");
    host.datagram(a.peer, a.carrier_close());
    host.steps(2, {});
    FakeClient again(27002);
    host.enter(again, alpha.registration());
    const LevelInfo second = level_info(again.got.at(2));
    check(server::rep::session_context_instance(2) == last, "the trap of the session rule: session 2 would have instance 2, the instance of the client");
    check(second.ok && second.instance == 3 && second.version == 3 && second.level == "newworld_vitaeeterna" && read_bundle(again.got.at(4).body).instance == 3,
          "the account rule: the new session of A has instance 3 and version 3, and its bundles have instance 3 (the client makes its context)");
    FakeClient b(27001);
    host.enter(b, beta.registration());
    const LevelInfo other = level_info(b.got.at(2));
    check(other.ok && other.instance == 3 && other.version == 3, "a different account (session 3): the values of its session id");
}

/** IN6 */
void instance_rows(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "dg1-instance-rows.sqlite3";
    std::filesystem::remove(path);
    ServerHost host(observing(), path.string());
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    host.datagram(a.peer, a.report_at(1, kHome.x, kHome.y, kHome.z));
    host.steps(3, {&a, &b});
    const world::Entity& entity = *host.world.find_character(kCharacterA);
    // A refused member row: nothing changes.
    check(sql(path, "CREATE TRIGGER refuse_member BEFORE INSERT ON character_instances BEGIN SELECT RAISE(ABORT, 'test'); END;"),
          "setup: a trigger refuses the member rows");
    std::size_t before = a.got.size();
    const world::InstanceResult refused = instances(host).enter(kCharacterA, "Test_Dungeon");
    host.steps(2, {&a, &b});
    check(refused.error == world::InstanceError::storage && entity.space == world::kMainSpace && instances(host).instances().empty()
              && host.store.list_instance_members().empty() && near(*saved_position(host.store, kCharacterA), kHome)
              && types_of(since(a, before)).find("1635") == std::string::npos,
          "a refused row: no instance, no space change, no row, the position row as before, no level change for the client");
    check(sql(path, "DROP TRIGGER refuse_member"), "setup: the trigger goes");
    // The entry: the instance object and its rows.
    const world::InstanceResult entered = instances(host).enter(kCharacterA, "Test_Dungeon");
    host.steps(2, {&a, &b});
    const world::Instance* instance = instances(host).instance_of(kCharacterA);
    const std::vector<state::InstanceMemberRow> rows = host.store.list_instance_members();
    check(entered.error == world::InstanceError::none && instance != nullptr && instance->id == 1 && instance->map == "Test_Dungeon"
              && instance->mode == "Test_Dungeon" && instance->level.level == "nw_test_dungeon_00" && instance->capacity == 5
              && near(instance->start, kStart) && instance->members.size() == 1 && near(instance->members.at(kCharacterA).returnPlace, kHome)
              && instance->space == entity.space,
          "the instance is a world object: its id, its map and mode, its level, its start, its capacity, its member with its return place");
    check(rows.size() == 1 && rows[0].characterId == kCharacterA && rows[0].instanceId == 1 && rows[0].map == "Test_Dungeon"
              && near(rows[0].returnPlace, kHome) && rows[0].enteredNs > 0 && near(*saved_position(host.store, kCharacterA), kHome),
          "the entry transaction: the member row (the instance, the map, the return place, the time) and the position row at the return place");
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::inInstance && instances(host).instances().size() == 1,
          "a second entry of a member is refused");
    // The join: the same map, a free place; a different map; no instance; a solo map.
    check(instances(host).enter(kCharacterB, "Test_Solo", instance->id).error == world::InstanceError::wrongMap
              && instances(host).enter(kCharacterB, "Test_Dungeon", 99).error == world::InstanceError::unknownInstance,
          "a join of a different map and of an instance that is not there: refused");
    check(instances(host).enter(kCharacterB, "Test_Dungeon", instance->id).error == world::InstanceError::none
              && instances(host).instance_of(kCharacterB) == instance && instance->members.size() == 2,
          "B joins the instance of A: two members");
    check(instances(host).leave(kCharacterB).error == world::InstanceError::none && instance->members.size() == 1, "setup: B returns");
    // A refused exit: the character stays in the instance, the rows stay.
    check(sql(path, "CREATE TRIGGER refuse_exit BEFORE DELETE ON character_instances BEGIN SELECT RAISE(ABORT, 'test'); END;"),
          "setup: a trigger refuses the removal of a member row");
    const world::SpaceId space = entity.space;
    check(instances(host).leave(kCharacterA).error == world::InstanceError::storage && entity.space == space
              && host.store.list_instance_members().size() == 1 && instances(host).instance_of(kCharacterA) != nullptr,
          "a refused exit: the character stays in the instance, its row stays");
    check(sql(path, "DROP TRIGGER refuse_exit"), "setup: the trigger goes");
    const world::InstanceResult left = instances(host).leave(kCharacterA);
    host.steps(2, {&a, &b});
    check(left.error == world::InstanceError::none && left.ended && instances(host).instances().empty() && host.store.list_instance_members().empty()
              && entity.space == world::kMainSpace && near(entity.position, kHome) && near(*saved_position(host.store, kCharacterA), kHome),
          "the exit transaction: no member row, the position row at the return place; the character in the main world; the instance with "
          "no member ended");
    check(instances(host).leave(kCharacterA).error == world::InstanceError::notInInstance, "an exit of a character in no instance is refused");
    // A solo map: one member place.
    check(instances(host).enter(kCharacterA, "Test_Solo").error == world::InstanceError::none
              && instances(host).enter(kCharacterB, "Test_Solo", instances(host).instance_of(kCharacterA)->id).error == world::InstanceError::full,
          "a solo map (capacity 0 in the data) has one member place");
    const world::InstanceCounts& counts = instances(host).counts();
    check(counts.made == 2 && counts.entries == 3 && counts.exits == 2 && counts.refused >= 7, "the counts of the instances");
}

/** IN7 */
void restart_rule(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "dg1-restart.sqlite3";
    std::filesystem::remove(path);
    {
        // A session that ends in an instance (decision D9).
        ServerHost host(service::RepServerOptions{}, path.string());
        set_maps(host);
        const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
        add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kHome);
        FakeClient a(27000);
        host.enter(a, alpha.registration());
        check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A enters the instance");
        host.steps(3, {&a});
        host.datagram(a.peer, a.carrier_close());
        host.steps(2, {});
        check(host.world.find_character(kCharacterA) == nullptr && host.store.list_instance_members().empty()
                  && near(*saved_position(host.store, kCharacterA), kHome) && instances(host).instances().empty()
                  && log_count("left the world in instance 1 (Test_Dungeon): its next world entry is at its return place") == 1,
              "D9: the session ends in the instance: no member row, the position row at the return place, the instance ended");
        FakeClient again(27002);
        host.enter(again, alpha.registration());
        const LevelInfo info = level_info(again.got.at(2));
        check(info.ok && info.level == "newworld_vitaeeterna" && host.world.find_character(kCharacterA)->space == world::kMainSpace
                  && near(host.world.find_character(kCharacterA)->position, kHome),
              "the next login: the main world, at the return place");
        // A backend stop with a member in an instance.
        check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A enters again");
        host.steps(2, {&again});
        host.server->stop();
        check(host.store.list_instance_members().empty() && near(*saved_position(host.store, kCharacterA), kHome),
              "a backend stop ends the session in the instance: no member row, the position row at the return place");
    }
    // The member rows of a stop with no exit (a crash): the next start puts each character at its return place.
    const state::WorldPosition elsewhere{400.0F, 400.0F, kHeight};
    check(sql(path, "INSERT INTO character_instances(character_id, instance_id, map, return_x, return_y, return_z, entered_ns) VALUES('"
                        + std::string(kCharacterB) + "', 9, 'Test_Dungeon', 400, 400, 50, 1)")
              && sql(path, "UPDATE characters SET position_x=645, position_y=663, position_z=153 WHERE character_id='" + std::string(kCharacterB) + "'"),
          "setup: a member row of a stop with no exit, and a position row at a place of the instance");
    ServerHost host(service::RepServerOptions{}, path.string());
    set_maps(host);
    host.tick();
    check(host.store.list_instance_members().empty() && near(*saved_position(host.store, kCharacterB), elsewhere)
              && instances(host).counts().recovered == 1
              && log_count("instance 9 (Test_Dungeon) of an earlier start ended with the backend stop") == 1,
          "the first tick of the next start: the character of the row is at its return place, and the row goes");
    state::AccountOrigin origin{};
    const Player beta{host.store.login_account(kSteamIdB, "Tester B", false, origin).value(), kSteamIdB, "Tester B", kCharacterB, "Beta"};
    FakeClient b(27001);
    host.enter(b, beta.registration());
    check(host.world.find_character(kCharacterB) != nullptr && near(host.world.find_character(kCharacterB)->position, elsewhere)
              && host.world.find_character(kCharacterB)->space == world::kMainSpace && level_info(b.got.at(2)).level == "newworld_vitaeeterna",
          "the login is at the return place in the main world");
}

/** The place report and the camera of a client (the interest scan of its session runs after a camera report). */
void report(ServerHost& host, FakeClient& client, std::uint16_t number, const state::WorldPosition& place) {
    host.datagram(client.peer, client.report_at(number, place.x, place.y, place.z));
}

/** IN8 */
void stop_for_observer(const Options&) {
    ServerHost host(observing());
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    report(host, a, 1, kHome);
    report(host, b, 1, state::WorldPosition{102.0F, 200.0F, kHeight});
    host.steps(60, {&a, &b});
    const world::EntityId entityA = host.world.find_character(kCharacterA)->id;
    const world::EntityId entityB = host.world.find_character(kCharacterB)->id;
    const std::uint32_t interest = interest_of(host, 2, entityA);
    check(interest != 0 && interest_of(host, 1, entityB) != 0, "setup: A and B observe each other in the main world");
    std::uint64_t lastSequenceB = 0;
    for (const Sent& sent : b.got) {
        if (sent.type == 8) {
            lastSequenceB = read_bundle(sent.body).sequence;
        }
    }
    const std::size_t beforeB = b.got.size();
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A enters the instance");
    host.steps(3, {&a, &b});
    for (std::uint16_t number = 2; number < 30; ++number) {
        report(host, a, number, kStart);
        host.steps(2, {&a, &b});
    }
    bool stopped = false;
    bool ordered = true;
    for (const Sent* sent : since(b, beforeB)) {
        if (sent->type == 8) {
            const Bundle bundle = read_bundle(sent->body);
            stopped = stopped || std::find(bundle.stops.begin(), bundle.stops.end(), interest) != bundle.stops.end();
            ordered = ordered && bundle.sequence == lastSequenceB + 1 && bundle.instance == 2;
            lastSequenceB = bundle.sequence;
        }
    }
    check(stopped && interest_of(host, 2, entityA) == 0, "B gets the stop of the interest number of A (the body and the name of A go)");
    check(ordered && types_of(since(b, beforeB)).find("1635") == std::string::npos && types_of(since(b, beforeB)).find("1628") == std::string::npos,
          "B keeps its level, its context instance (2), and its bundle sequence (no gap, no level information)");
    check(host.session(1).observer().known().size() <= 2 && interest_of(host, 1, entityB) == 0,
          "A in the instance has no record of B (only its own entity and the world entity)");
    check(instances(host).leave(kCharacterA).error == world::InstanceError::none, "setup: A returns");
    host.steps(3, {&a, &b});
    for (std::uint16_t number = 30; number < 50; ++number) {
        report(host, a, number, kHome);
        host.steps(2, {&a, &b});
    }
    check(interest_of(host, 2, entityA) != 0 && interest_of(host, 1, entityB) != 0, "the return: A and B observe each other again");
}

/** IN9 */
void same_and_second_instance(const Options&) {
    ServerHost host(observing());
    set_maps(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kHome);
    const Player gamma = add_player(host.store, kSteamIdC, "Tester C", kCharacterC, "Gamma", kHome);
    const Player delta = add_player(host.store, kSteamIdD, "Tester D", kCharacterD, "Delta", kStart);
    FakeClient a(27000);
    FakeClient b(27001);
    FakeClient c(27002);
    FakeClient d(27003);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    host.enter(c, gamma.registration());
    host.enter(d, delta.registration());
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A makes an instance");
    const std::uint64_t first = instances(host).instance_of(kCharacterA)->id;
    check(instances(host).enter(kCharacterC, "Test_Dungeon", first).error == world::InstanceError::none, "setup: C joins the instance of A");
    check(instances(host).enter(kCharacterB, "Test_Dungeon").error == world::InstanceError::none
              && instances(host).instance_of(kCharacterB)->id != first
              && instances(host).instance_of(kCharacterB)->space != instances(host).instance_of(kCharacterA)->space,
          "setup: B is in a second instance of the same map");
    host.steps(3, {&a, &b, &c, &d});
    for (std::uint16_t number = 1; number < 40; ++number) {
        report(host, a, number, kStart);
        report(host, b, number, kStart);
        report(host, c, number, state::WorldPosition{kStart.x + 2.0F, kStart.y, kStart.z});
        report(host, d, number, kStart);
        host.steps(2, {&a, &b, &c, &d});
    }
    const auto id = [&](const char* character) { return host.world.find_character(character)->id; };
    check(interest_of(host, 1, id(kCharacterC)) != 0 && interest_of(host, 3, id(kCharacterA)) != 0,
          "two characters in one instance see each other (a record of each one)");
    check(interest_of(host, 2, id(kCharacterA)) == 0 && interest_of(host, 2, id(kCharacterC)) == 0 && interest_of(host, 1, id(kCharacterB)) == 0
              && interest_of(host, 3, id(kCharacterB)) == 0,
          "a character in a second instance of the same map, at the same place, sees neither, and they do not see it");
    check(interest_of(host, 4, id(kCharacterA)) == 0 && interest_of(host, 4, id(kCharacterB)) == 0 && interest_of(host, 1, id(kCharacterD)) == 0
              && interest_of(host, 2, id(kCharacterD)) == 0,
          "a character of the main world at the same coordinates sees none of them, and they do not see it");
}

/** A creature document with one wolf and one spawn point at the start of the made-up dungeon (the coordinates of an instance). */
json wolf_document() {
    const json slice{{"path", "slices/test/wolf.dynamicslice"}, {"asset", "11111111-1111-1111-1111-111111111111"}, {"sub", 2},
                     {"root", "1000000000000011"}, {"interestRadius", 60.0}, {"movement", 5}, {"vitals", 9}, {"marker", 15},
                     {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                     {"components", json::array({json::array({"1000000000000011", "0000000000000A01", "Root/Vitals"})})},
                     {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}, {"Attack_Bite", "Attack_Bite", 7, false}}},
                     {"behavior", {{"sight", {75.0, 30.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}}, {"speed", {4.75, 9.23}}}},
                     {"tags", "240400000000000000000000"},
                     {"attacks", json::array({json::array({2, 1.43333, json::array({json::array({0.566667, 2.18, 0.2, 0.0, "Slash", 4, "Physical",
                                                                                                 "Attack_01_Bite"})})})})}};
    const json kind{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
                    {"categoryId", 0x11112223}, {"name", "Test_Wolf"}, {"vitalsName", "Test_Wolf"}, {"family", "Wildlife"},
                    {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})}, {"points", 1},
                    {"damage", 0.85}};
    return json{{"format", 2},
                {"levels", {{1, 337.5, 145.52, 50.0}, {18, 1690.875977, 616.41, 180.0}}},
                {"slices", {slice}},
                {"kinds", {kind}},
                {"spawnPoints", {{"p-wolf-at-the-start", 0, 18, kStart.x + 3.0, kStart.y, kStart.z, 0.0, 360.0, 360.0, kStart.z}}}};
}

/** IN10 */
void scans_in_instance(const Options& options) {
    ServerHost host(observing());
    set_maps(host);
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "dg1-creatures.json";
    std::ofstream(path, std::ios::trunc) << wolf_document().dump();
    check(host.creatures.load(path.string()) && host.creatures.spawn_points().size() == 1, "setup: one wolf spawn point 3 m from the start");
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHome);
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    check(instances(host).enter(kCharacterA, "Test_Dungeon").error == world::InstanceError::none, "setup: A is in the instance at the start");
    for (std::uint16_t number = 1; number < 60; ++number) {
        report(host, a, number, kStart);
        host.steps(5, {&a});
    }
    const world::Entity& entity = *host.world.find_character(kCharacterA);
    check(near(entity.position, kStart) && entity.space != world::kMainSpace && host.world.creatures().count() == 0,
          "a character in an instance at the coordinates of a main world spawn point makes no creature (the spawn point rule takes the "
          "characters of the main world)");
    check(host.world.territory_of(entity) == 0, "a character in an instance is in no territory of the main world");
    // A character of the main world 20 m away makes the wolf; the wolf takes no target in the instance (3 m from A, in its sight).
    check(host.world.spawn_character(kCharacterB, state::WorldPosition{kStart.x + 3.0F, kStart.y + 20.0F, kStart.z}) == world::SpawnError::none,
          "setup: B (no session) is in the main world 20 m from the spawn point");
    host.world.creatures().behavior().set_ignored(kCharacterB, true);
    for (std::uint16_t number = 60; number < 120; ++number) {
        report(host, a, number, kStart);
        host.steps(5, {&a});
    }
    const world::Entity* wolf = nullptr;
    for (const auto& [id, candidate] : host.world.entities()) {
        wolf = candidate.kind == world::EntityKind::creature ? &candidate : wolf;
    }
    check(wolf != nullptr && wolf->space == world::kMainSpace, "the control: the character of the main world makes the wolf (main world)");
    check(wolf != nullptr && wolf->creature.mind.target == 0 && wolf->creature.mind.mode != world::CreatureMode::fight,
          "the wolf takes no target in a different space (A is 3 m from it at the same coordinates)");
    check(wolf != nullptr && interest_of(host, 1, wolf->id) == 0, "the session of A has no record of the wolf");
    // A death in the instance: the respawn is at the start of the instance, in its space (not at a travel place of the main world).
    check(host.world.health().hurt(kCharacterA, 100000.0F).error == world::HealthError::none && entity.life.dead, "setup: A dies in the instance");
    const world::RespawnPlace respawn = host.world.health().respawn_place(entity);
    const world::HealthResult revived = host.world.health().respawn(entity.owner);
    host.steps(2, {&a});
    check(revived.revived && near(respawn.position, kStart) && respawn.name == "the start of instance 1" && entity.space != world::kMainSpace
              && near(entity.position, kStart) && near(*saved_position(host.store, kCharacterA), kHome),
          "a respawn in an instance: the start of the instance, in its space; the position row keeps the return place");
    // A probe of a command in the space goes with the end of the instance.
    world::ProbeBody probe;
    probe.interestRadius = 50.0F;
    const world::SpaceId space = entity.space;
    check(host.world.make_probe(probe, kStart, {}, space) != nullptr && host.world.space_entity_count(space) == 2,
          "setup: a probe in the space of the instance");
    check(instances(host).leave(kCharacterA).error == world::InstanceError::none && host.world.space_entity_count(space) == 0
              && log_count("(1 other entity removed)") == 1,
          "the end of the instance removes the probe of its space");
}

/** IL1 */
void instance_load(const Options&) {
    ServerHost host;
    set_maps(host);
    constexpr unsigned kInstances = 100;
    constexpr unsigned kMembers = 4;
    constexpr unsigned kMainPlayers = 50;
    std::vector<std::string> ids;
    for (unsigned index = 0; index < kInstances * kMembers + kMainPlayers; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "f%07u-0000-4000-8000-0000000000d9", index);
        char steam[24];
        std::snprintf(steam, sizeof(steam), "765612022553%05u", 40000 + index);
        add_player(host.store, steam, "Tester " + std::to_string(index), id, "Load" + std::to_string(index), std::nullopt);
        ids.emplace_back(id);
        const state::WorldPosition place{1000.0F + 30.0F * static_cast<float>(index % 40), 1000.0F + 30.0F * static_cast<float>(index / 40), kHeight};
        if (host.world.spawn_character(id, place) != world::SpawnError::none) {
            check(false, "setup: a character is in the world");
            return;
        }
    }
    const auto entriesStarted = std::chrono::steady_clock::now();
    bool entered = true;
    for (unsigned instance = 0; instance < kInstances; ++instance) {
        const world::InstanceResult first = instances(host).enter(ids[instance * kMembers], "Test_Dungeon");
        entered = entered && first.error == world::InstanceError::none;
        for (unsigned member = 1; member < kMembers && first.instance; ++member) {
            entered = entered && instances(host).enter(ids[instance * kMembers + member], "Test_Dungeon", first.instance->id).error
                                     == world::InstanceError::none;
        }
    }
    const double entryTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - entriesStarted).count();
    check(entered && instances(host).instances().size() == kInstances && host.store.list_instance_members().size() == kInstances * kMembers,
          "100 instances with 4 members each (400 rows), and 50 characters in the main world");
    const auto ticksStarted = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 200; ++tick) {
        host.advance(milliseconds(10));
        host.tick();
    }
    const double tickTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ticksStarted).count();
    const auto searchesStarted = std::chrono::steady_clock::now();
    bool alone = true;
    for (unsigned search = 0; search < 10000; ++search) {
        const world::Instance* instance = instances(host).instance_of(ids[(search % kInstances) * kMembers]);
        std::size_t found = 0;
        host.world.for_each_near(instance->space, kStart, 100.0F, [&](world::Entity& entity) {
            found += instances(host).instance_of(entity.characterId) == instance ? 1 : 0;
            alone = alone && entity.space == instance->space;
        });
        alone = alone && found == kMembers;
    }
    const double searchTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - searchesStarted).count();
    const auto exitsStarted = std::chrono::steady_clock::now();
    bool returned = true;
    for (unsigned index = 0; index < kInstances * kMembers; ++index) {
        returned = returned && instances(host).leave(ids[index]).error == world::InstanceError::none;
    }
    const double exitTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - exitsStarted).count();
    note("400 entries: " + std::to_string(entryTime) + " ms; 200 ticks with 450 characters in 101 spaces: " + std::to_string(tickTime)
         + " ms; 10,000 searches in a space: " + std::to_string(searchTime) + " ms; 400 exits: " + std::to_string(exitTime) + " ms");
    check(alone, "each search in the space of an instance finds the 4 members of that instance and no other entity");
    check(returned && instances(host).instances().empty() && host.store.list_instance_members().empty(), "400 exits: no instance, no row");
    check(entryTime < 4000.0 && exitTime < 4000.0 && tickTime < 400.0 && searchTime < 400.0,
          "the time limits: 400 entries and 400 exits (each one transaction) less than 4 s each, 200 ticks less than 400 ms, 10,000 searches "
          "less than 400 ms (no rule looks at each instance or at each space)");
}

} // namespace

std::vector<Case> instance_cases() {
    return {{"IN1", "instances DG1 (I1): the position index by space", index_by_space},
            {"IN2", "instances DG1 (I1): World::move_to_space, the index, and the position row in an instance", move_to_space},
            {"IN3", "instances DG1 (I1): the game modes data and its tool (runs check_game_modes_tool.py)", game_modes_data},
            {"IN4", "instances DG1 (I2): the level change of the session in one connection, the return, and a second entry", level_change},
            {"IN5", "instances DG1 (I2, I5): the account rule of the version and the context instance over two connections", account_rule},
            {"IN6", "instances DG1 (I3): the instance object, its two transactions, the refusals, and the join", instance_rows},
            {"IN7", "instances DG1 (I3): the restart rule (D9): a session end, a backend stop, and the rows of a stop with no exit", restart_rule},
            {"IN8", "instances DG1 (I6, MP-10): the observer in the main world gets the stop of the mover and keeps its context", stop_for_observer},
            {"IN9", "instances DG1 (I6, MP-10): one instance, a second instance of the same map, and the main world at the same place",
             same_and_second_instance},
            {"IN10", "instances DG1 (I4): the scans of the systems for a character in an instance", scans_in_instance},
            {"IL1", "instances DG1 (I7): the load of 100 instances with 4 members each and 50 characters of the main world, with time limits",
             instance_load}};
}

} // namespace soulwarden::backend::tests

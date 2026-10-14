// The cases of the code review of stage 3.4d (the path search of a creature): each finding of
// the review has a case that failed before its correction (review file FINDINGS.md of the
// reviewer, cases RP1 to RP9 there). The tool findings F10 and F11 have their check in
// tools/gamedata/check_navigation_tool.py, which case RN10 runs.
//
//   RN1  a contact of an attack after the target went behind a wall (F1)
//   RN2  a creature and a character on one vertical line, with a ceiling between them (F2)
//   RN3  a tile with 17 floors in one cell (F3)
//   RN4  a byte after the zlib stream of a tile (F4)
//   RN5  a path in the memory of a creature that goes into a tile that the world removed (F5)
//   RN6  the footing of a place 0.53 m from the nearest free floor (F6)
//   RN7  the patience of the crowd rule in the last steps of the way (F7)
//   RN8  a goal whose tile is still on its way (F8)
//   RN9  a folder with different agent values in its files (F9)
//   RN10 the navigation tool: a physics file that does not decode (F10), the terrain at the edge (F11)
//
// The places and the values are made up.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "gamedata/navigation_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_navigation.h"
#include "support/test_players.h"
#include "world/behavior.h"
#include "world/navigation.h"

namespace soulwarden::backend::tests {
namespace {

using gamedata::NavFloor;
using world::CreatureMode;
using world::StepKind;

constexpr float kGround = 50.0F;
constexpr float kRunSpeed = 10.0F;
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kBite = 3;
/** The tile of most cases: tile (15, 15) of the region (0, 0), from 960 m to 1024 m on each axis. */
constexpr int kTile = 15;

struct Sink final : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

state::WorldPosition at(float x, float y, float z = kGround) {
    return state::WorldPosition{x, y, z};
}

/** The floors of the cells of a tile, from a function of the world position of the middle of each cell. */
TestCells cells_of(int tileX, int tileY, std::function<void(float x, float y, std::vector<TestFloor>& floors)> describe) {
    return [=](int cellX, int cellY, std::vector<TestFloor>& floors) {
        describe(static_cast<float>(tileX) * 64.0F + (static_cast<float>(cellX) + 0.5F) * 0.25F,
                 static_cast<float>(tileY) * 64.0F + (static_cast<float>(cellY) + 0.5F) * 0.25F, floors);
    };
}

/** A cell of a wall: no floor for a creature, and no sight below the top of the wall. */
void wall(std::vector<TestFloor>& floors, float top = kGround + 3.0F) {
    floors.clear();
    floors.push_back({top, NavFloor::kBlocked});
}

gamedata::CreatureSlice wolf_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/wolf.dynamicslice";
    slice.asset.fill(0x11);
    slice.subId = 2;
    slice.root = 0x1000000000000011ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kWolfSlots[0];
    slice.vitalsSlot = kWolfSlots[1];
    slice.markerSlot = kWolfSlots[2];
    slice.slots = {{kWolfSlots[0], "ActionList", 11}, {kWolfSlots[1], "Vitals", 15}, {kWolfSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Death", "r_Death", 4, true},
                     {"Attack_Bite", "Attack_Bite", 7, false}};
    gamedata::CreatureBehavior values;
    values.sightAngle = 180.0F;
    values.sightDistance = 12.0F;
    values.combatAngle = 180.0F;
    values.combatDistance = 30.0F;
    values.restLeash = 20.0F;
    values.combatLeash = 70.0F;
    values.walkSpeed = 4.0F;
    values.runSpeed = kRunSpeed;
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

gamedata::CreatureKind wolf_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Wolf";
    kind.vitals = kind.id;
    kind.category = "Test_Wolf_Category";
    kind.vitalsId = 0x11112222;
    kind.categoryId = 0x11112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 18;
    kind.bodies = {{0, 100}};
    return kind;
}

bool set_wolves(ServerHost& host, std::vector<gamedata::SpawnPoint> points = {}) {
    return host.creatures.set_full({wolf_slice()}, {wolf_kind()}, std::move(points), {{18, 1690.875977F, 616.41F, 180.0F}});
}

/** A host with navigation data in a folder of the work directory, creature data, and characters with no client. */
struct NavHost {
    ServerHost host;
    std::filesystem::path folder;
    std::vector<std::unique_ptr<Sink>> sinks;
    std::vector<Player> players;

    NavHost(const Options& options, const char* name) : folder(options.workDirectory / name) {
        std::filesystem::remove_all(folder);
        if (!set_wolves(host)) {
            throw std::runtime_error("navigation test: the creature data is not valid");
        }
    }

    /** Reads the headers of the files of the folder. */
    bool load() {
        return host.navigation.set_directory(folder.string());
    }
    world::Navigation& navigation() {
        return host.world.navigation();
    }
    world::Creatures& creatures() {
        return host.world.creatures();
    }
    /** Puts a new character into the world at a place: the character has a session number and no client. */
    world::Entity* character(const state::WorldPosition& position) {
        const std::size_t number = players.size();
        const std::string id = "review-character-" + std::to_string(number);
        players.push_back(add_player(host.store, std::to_string(number + 1), "", id, "", position));
        sinks.push_back(std::make_unique<Sink>());
        world::Entity* entity = host.world.enter(static_cast<world::SessionId>(number + 1), *sinks.back(), players.back().characterId,
                                                 players.back().account.personaId)
                                    .entity;
        if (entity != nullptr) {
            (void)host.world.travel(entity->characterId, position, world::TravelScreen::off);
        }
        return entity;
    }
    world::Entity* wolf(const state::WorldPosition& position, float yaw = 0.0F) {
        return creatures().spawn("Test_Wolf", position, yaw).entity;
    }
    /** Ticks of 10 ms of the world. */
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


void contact_after_wall(const Options& options) {
    NavHost world(options, "review-contact-wall");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1000.5F && x < 1000.75F && y > 999.5F && y < 1000.5F) wall(floors);
    });
    region.write(world.folder);
    check(world.load(), "setup: the wall data loads");
    auto* player = world.character(at(1000.0F, 1000.75F));
    world.ticks(60);
    player->vitals.healthMax = player->vitals.health = 10000.0F;
    auto* creature = world.wolf(at(1000.0F, 1000.0F));
    for (int n = 0; n < 25 && !creature->creature.mind.attack; ++n) world.ticks(1);
    check(creature->creature.mind.attack.has_value(), "setup: an attack starts with clear sight");
    const auto before = player->vitals.health;
    check(world.host.world.set_position(player->id, at(1001.25F, 1000.75F), true), "setup: the player goes around the end of the wall");
    check(!world.navigation().sees(creature->position, player->position), "setup: the wall now stops sight and the player is still in reach");
    world.pass(0.6F);
    note("health before=" + std::to_string(before) + "; after=" + std::to_string(player->vitals.health));
    check(player->vitals.health == before, "a delayed contact cannot deal damage through the wall");
}

void vertical_sight(const Options& options) {
    NavHost world(options, "review-vertical-sight");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 995 && x < 1005 && y > 995 && y < 1005) {
            floors[0].sightTo = 2.0F;
            floors.push_back({kGround + 2.5F, 0});
        }
    });
    region.write(world.folder);
    check(world.load(), "setup: two floors with a solid ceiling load");
    auto* player = world.character(at(1000.125F, 1000.125F, kGround + 2.5F));
    world.ticks(60);
    const auto from = at(1000.125F, 1000.125F);
    check(!world.navigation().sees(from, at(1001.125F, 1000.125F, kGround + 2.5F)), "control: a slanted line crossing the ceiling is blocked");
    const bool sees = world.navigation().sees(from, at(1000.125F, 1000.125F, kGround + 2.5F));
    note(std::string("vertical sight through ceiling=") + (sees ? "1" : "0"));
    check(!sees, "a vertical line between two floors cannot see through the ceiling");
    player->vitals.healthMax = player->vitals.health = 10000.0F;
    auto* creature = world.wolf(from);
    world.pass(0.8F);
    note("attacks through ceiling=" + std::to_string(creature->creature.mind.attacks) + "; health=" + std::to_string(player->vitals.health));
    check(creature->creature.mind.attacks == 0 && player->vitals.health == 10000.0F,
          "the creature cannot see or attack a character on the floor above its ceiling");
}

void too_many_floors(const Options&) {
    TestNavRegion region;
    TestCells cells = [](int x, int y, std::vector<TestFloor>& floors) {
        if (x == 0 && y == 0) {
            floors.clear();
            for (int n = 0; n < 17; ++n) floors.push_back({50.0F + n * 3.0F, 0});
        }
    };
    std::string problem;
    auto tile = gamedata::NavTile::decode(0, 0, region.tile_bytes(&cells), region.heightScale, region.agent, &problem);
    note("over-limit tile accepted=" + std::to_string(tile != nullptr) + "; problem: " + problem);
    check(tile == nullptr, "a cell with more than 16 floors invalidates the tile");
}

void extra_stream_bytes(const Options& options) {
    const auto folder = options.workDirectory / "review-stream-tail";
    TestNavRegion region;
    region.write(folder, [](std::vector<std::uint8_t>& file) {
        std::uint32_t size = 0;
        for (int i = 0; i < 4; ++i) size |= static_cast<std::uint32_t>(file[72 + i]) << (i * 8);
        const std::size_t end = 64 + 1024 * 16 + size;
        file.insert(file.begin() + static_cast<std::ptrdiff_t>(end), 0xA5);
        ++size;
        for (int i = 0; i < 4; ++i) file[72 + i] = static_cast<std::uint8_t>(size >> (i * 8));
        // Move every later stream by one byte. The entries do not overlap.
        for (std::size_t entry = 1; entry < 1024; ++entry) {
            const std::size_t base = 64 + entry * 16;
            std::uint64_t offset = 0;
            for (int i = 0; i < 8; ++i) offset |= static_cast<std::uint64_t>(file[base + i]) << (i * 8);
            ++offset;
            for (int i = 0; i < 8; ++i) file[base + i] = static_cast<std::uint8_t>(offset >> (i * 8));
        }
    });
    gamedata::NavigationData data;
    check(data.set_directory(folder.string()), "setup: the directory entry remains in bounds");
    check(data.load(1, 0) != nullptr, "control: the ordinary next tile loads");
    const auto tile = data.load(0, 0);
    note("tile with a byte after its zlib checksum accepted=" + std::to_string(tile != nullptr));
    check(tile == nullptr, "a tile stream longer than one zlib stream is refused");
}

void path_after_drop(const Options& options) {
    NavHost world(options, "review-path-drop");
    TestNavRegion region;
    region.write(world.folder);
    check(world.load(), "setup: a flat region loads");
    auto* player = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    auto& nav = world.navigation();
    auto from = at(1000.0F, 1000.0F);
    const auto goal = at(1140.0F, 1000.0F);
    const auto report = nav.find_path(from, goal);
    check(report.found && report.direct, "setup: the direct route crosses a tile farther than the keep distance");
    auto first = nav.next_step(1234, from, goal, 1.0F);
    check(first.kind == StepKind::moved, "setup: the route is cached");
    from = first.position;
    check(world.host.world.leave(1), "setup: the character leaves, and only the route keeps nearby tiles");
    // Keep the route fresh while using short steps. Only the route keeps nearby tiles.
    for (int n = 0; n < 120; ++n) {
        world.ticks(10);
        auto next = nav.next_step(1234, from, goal, 0.01F);
        if (next.kind == StepKind::moved) from = next.position;
    }
    check(nav.counts().tilesDropped > 0 && nav.footing(goal) == world::Footing::unknown,
          "setup: the route's far tile was removed");
    const auto next = nav.next_step(1234, from, goal, 100.0F);
    note("step kind=" + std::to_string(static_cast<int>(next.kind)) + "; x=" + std::to_string(next.position.x)
         + "; destination has no tile=" + std::to_string(nav.footing(next.position) == world::Footing::unknown));
    check(next.kind == StepKind::waiting, "a step whose path enters a missing tile waits for the tile");
    (void)player;
}

void footing_radius(const Options& options) {
    NavHost world(options, "review-footing-radius");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x < 1000.5F || y < 1000.5F) floors.clear();
    });
    region.write(world.folder);
    check(world.load(), "setup: a corner of walkable ground loads");
    world.character(at(1001.0F, 1001.0F));
    world.ticks(60);
    check(world.navigation().footing(at(1000.75F, 1000.75F)) == world::Footing::walks, "control: a place on free ground walks");
    const auto footing = world.navigation().footing(at(1000.125F, 1000.125F));
    note("footing for ground 0.707 m away=" + std::to_string(static_cast<int>(footing)));
    check(footing == world::Footing::rooted, "a place with no free floor within 0.5 m is rooted");
}

void crowd_patience(const Options& options) {
    NavHost world(options, "review-crowd-patience");
    auto blockerSlice = wolf_slice();
    blockerSlice.path = "slices/test/blocker.dynamicslice";
    blockerSlice.asset.fill(0x22);
    blockerSlice.root += 1;
    blockerSlice.attacks.clear();
    auto blockerKind = wolf_kind();
    blockerKind.id = "Test_Blocker";
    blockerKind.bodies = {{1, 100}};
    auto fightingSlice = wolf_slice();
    fightingSlice.behavior->runSpeed = 1.0F;
    check(world.host.creatures.set_full({fightingSlice, blockerSlice}, {wolf_kind(), blockerKind}, {},
                                       {{18, 1690.875977F, 616.41F, 180.0F}}), "setup: a fighting creature and a passive creature load");
    TestNavRegion region;
    region.write(world.folder);
    check(world.load(), "setup: flat navigation data loads");
    auto* player = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    player->vitals.health = player->vitals.healthMax = 10000.0F;
    auto* blocker = world.creatures().spawn("Test_Blocker", at(1002.125F, 1000.0F), 0.0F).entity;
    auto* creature = world.wolf(at(1006.0F, 1000.0F), 90.0F);
    world.pass(20.0F);
    check(blocker->creature.mind.target == 0 && creature->creature.mind.target == player->id,
          "setup: the blocker has no target, and the fighting creature still has its target");
    note("blockedSteps=" + std::to_string(creature->creature.mind.blockedSteps) + "; attacks=" +
         std::to_string(creature->creature.mind.attacks) + "; distance=" + std::to_string(world::horizontal_distance(creature->position, player->position)));
    check(creature->creature.mind.attacks > 0, "crowd patience lets the creature pass a passive blocker after ten steps");
    world.creatures().kill(blocker->id);
    world.pass(3.0F);
    check(creature->creature.mind.attacks > 0, "control: removing the blocker allows the attack");
}

void cold_goal_tile(const Options& options) {
    const auto folder = options.workDirectory / "review-cold-goal";
    TestNavRegion region;
    region.tiles[{17, 15}] = [](int, int, std::vector<TestFloor>& floors) {
        floors[0].sightTo = 2.5F;
        floors.push_back({53.0F, 0});
    };
    region.write(folder);
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    gamedata::NavigationData data;
    check(store.open(":memory:") && data.set_directory(folder.string()), "setup: the threaded loader has a valid region");
    world::WorldOptions settings;
    settings.navigationData = &data;
    settings.navigationThread = true;
    settings.shelterTime = std::chrono::milliseconds(0);
    world::World host(clock, store, tables, settings);
    Sink sink;
    const auto from = at(1035.0F, 1000.0F);
    const auto goal = at(1091.0F, 1000.0F);
    const auto player = add_player(store, "1", "", "review-cold-character", "", goal);
    check(host.enter(1, sink, player.characterId, player.account.personaId).entity != nullptr, "setup: the target asks for both tiles");
    auto& nav = host.navigation();
    bool observed = false;
    world::StepKind first = StepKind::waiting;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < limit) {
        clock.advance(std::chrono::milliseconds(10));
        host.tick();
        if (nav.footing(from) == world::Footing::walks && nav.footing(goal) == world::Footing::unknown) {
            observed = true;
            first = nav.next_step(999, from, goal, 1.0F).kind;
            break;
        }
        if (nav.footing(goal) == world::Footing::walks) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    check(observed, "setup: the source is ready while the target tile is still loading");
    note("step kind while target tile loads=" + std::to_string(static_cast<int>(first)));
    check(!observed || first == StepKind::waiting, "an unfinished target tile causes waiting instead of no way");
    while (nav.footing(goal) != world::Footing::walks && std::chrono::steady_clock::now() < limit) {
        clock.advance(std::chrono::milliseconds(10));
        host.tick();
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    nav.forget(999);
    check(nav.next_step(999, from, goal, 1.0F).kind == StepKind::moved,
          "control: the same goal has a direct path when its tile is ready");
}

void mixed_agent_folder(const Options& options) {
    const auto folder = options.workDirectory / "review-mixed-agents";
    TestNavRegion first;
    first.write(folder);
    TestNavRegion second;
    second.x = 1;
    second.agent.radius = 0.4F;
    second.write(folder);
    gamedata::NavigationData data;
    const bool loaded = data.set_directory(folder.string());
    note("mixed folder loaded=" + std::to_string(loaded) + "; retained regions=" + std::to_string(data.regions()));
    check(!data.problem().empty(), "setup: the loader sees the mismatched agent values");
    check(!loaded && !data.available() && data.regions() == 0,
          "a folder with different agent values is refused as a whole");
}

/** RN10: the check of the navigation tool (findings F10 and F11), tools/gamedata/check_navigation_tool.py. */
void navigation_tool(const Options& options) {
    // The golden folder of the options is REPOSITORY/apps/backend/tests/golden.
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_navigation_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the navigation tool is in the tree (the case needs --golden-dir)");
    if (!std::filesystem::is_regular_file(script)) {
        return;
    }
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "navigation-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + options.workDirectory.string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && correct == 6 && wrong == 0,
          "the navigation tool stops for a placed collider whose physics file does not decode (no complete report, no "
          "region file, status 1: F10), and its terrain reader gives the height of height_maps.py at the east edge and "
          "at the north edge of a region with no neighbor (F11)");
}

} // namespace
std::vector<Case> review_path_cases() {
    return {{"RN1", "review of 3.4d: a contact of an attack after the target went behind a wall does no damage", contact_after_wall},
            {"RN2", "review of 3.4d: a creature does not see or attack a character above a ceiling on one vertical line", vertical_sight},
            {"RN3", "review of 3.4d: a tile with 17 floors in one cell is refused", too_many_floors},
            {"RN4", "review of 3.4d: a tile stream with a byte after its zlib stream is refused", extra_stream_bytes},
            {"RN5", "review of 3.4d: a path into a tile that the world removed waits for the tile", path_after_drop},
            {"RN6", "review of 3.4d: a place with no free floor in 0.5 m does not walk", footing_radius},
            {"RN7", "review of 3.4d: the patience of the crowd rule in the last steps lets a creature pass a passive creature", crowd_patience},
            {"RN8", "review of 3.4d: a goal whose tile is still on its way gives a wait, not \"no way\"", cold_goal_tile},
            {"RN9", "review of 3.4d: a folder with different agent values is refused as a whole", mixed_agent_folder},
            {"RN10", "review of 3.4d: the navigation tool with a physics file that does not decode, and its terrain at the "
                     "edge of the world (runs tools/gamedata/check_navigation_tool.py)",
             navigation_tool}};
}
} // namespace soulwarden::backend::tests

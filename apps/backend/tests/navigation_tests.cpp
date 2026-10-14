// Tests of stage 3.4d: the navigation data, the path of a creature, and its sight (protocol notes 4.19).
//
//   NV1   the reader of the navigation files: a file in the form of the tool, the floors and the
//         areas of a tile, and each file and tile that is not correct
//   NV2   the ground: more than one floor at one place, the terrain at the exact place, a place
//         with no tile
//   NV3   the path: a creature goes around a wall and through its opening, not through the wall
//   NV4   floors: a creature on a floor above the terrain walks on its floor and goes up stairs
//   NV5   a goal with no way: no search, the creature loses its target and goes home
//   NV6   the limit of a tick: many searches at one time wait for the next ticks
//   NV7   the tiles: the world reads the tiles near a character, removes them again, and a
//         thread of its own reads them in the backend
//   NV8   the sight: a wall stops the vision of a creature, a low object does not, and a hit
//         gives a creature behind a wall its target
//   NV9   water: a creature walks in shallow water and not in deep water
//   NV10  the load: 50 characters with fights in a world with navigation data of the real size,
//         with a time limit, and the memory of the tiles
//   NV11  two sessions in two regions: each one with its creatures and its tiles, and a creature
//         that follows one character while the second one observes it (the case is in
//         fight_tests.cpp: it needs the clients of the fight cases)
//   NV12  which creatures walk: a spawn point on a floor, in the air, and before the data of its
//         place is there
//
// The rules for a fight that a player can play (stage 3.4d, world/behavior.h and world/health.h):
//
//   PL1   the shelter: no creature takes a character for 10 s after it came to a place (a world
//         entry, a travel, a respawn), a hit gives the target in that time, two characters
//   PL2   the rest: the health of a character that is not in a fight comes back
//   PL3   the attacks of a slice: the hits of an attack, light and heavy attacks, their
//         sequence, and the pause after each one
//   PL4   the crowd: creatures do not end their way at the same place
//
// The accounts, the characters, the creature kinds, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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

bool near(float value, float expected, float tolerance = 0.01F) {
    return std::fabs(value - expected) <= tolerance;
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
        char steam[32];
        char id[64];
        char name[32];
        std::snprintf(steam, sizeof(steam), "765612022552%05zu", 33000 + number);
        std::snprintf(id, sizeof(id), "aaaaaaaa-0000-4000-8000-%012zu", number + 1);
        std::snprintf(name, sizeof(name), "Walker%zu", number + 1);
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

/** NV1: the reader of the navigation files. */
void navigation_files(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "navigation-files";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    region.x = 0;
    region.y = 0;
    // One tile with: a wall, a cell with two floors (the terrain and a floor 3 m above it), a pond, and a closed yard.
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1010.0F && x < 1010.5F) {
            wall(floors);
        } else if (x > 1000.0F && x < 1002.0F && y > 1000.0F && y < 1002.0F) {
            floors[0].sightTo = 2.5F;
            floors.push_back({kGround + 3.0F, 0});
        } else if (x > 980.0F && x < 984.0F && y > 980.0F && y < 984.0F) {
            floors[0].flags |= x < 982.0F ? NavFloor::kShallowWater : NavFloor::kDeepWater;
        }
    });
    region.write(folder);
    gamedata::NavigationData data;
    check(data.set_directory(folder.string()) && data.available() && data.regions() == 1 && data.problem().empty()
              && data.agent().cell == 0.25F && near(data.agent().step, 0.35F) && data.has_region(kTile, kTile) && !data.has_region(40, 0),
          "a folder with one file in the form of the tool: one region, the agent values of the file, and no problem");
    const std::shared_ptr<const gamedata::NavTile> tile = data.load(kTile, kTile);
    check(tile != nullptr && tile->x == kTile && tile->y == kTile && data.counts().tilesRead == 1, "a tile of the file loads");
    if (tile == nullptr) {
        return;
    }
    gamedata::NavSpot spots[gamedata::NavTile::kMaxFloors];
    // The cell of the position (x, y): (x - 960) * 4.
    const auto cell = [](float meters) { return static_cast<std::int32_t>((meters - 960.0F) * 4.0F); };
    check(tile->spots(cell(990.0F), cell(990.0F), spots) == 1 && near(spots[0].height, kGround, 0.02F) && spots[0].floor.walkable()
              && (spots[0].floor.flags & NavFloor::kTerrain) != 0 && spots[0].floor.area != 0 && spots[0].floor.sightHigh == NavFloor::kSky,
          "a cell of the flat terrain has one free floor at the terrain height, with an area and the sky above it");
    const std::uint16_t west = spots[0].floor.area;
    check(tile->spots(cell(1010.2F), cell(990.0F), spots) == 1 && !spots[0].floor.walkable() && spots[0].floor.area == 0
              && near(spots[0].height, kGround + 3.0F, 0.05F),
          "a cell of the wall has one blocked floor at the top of the wall, with no area");
    check(tile->spots(cell(1001.0F), cell(1001.0F), spots) == 2 && near(spots[0].height, kGround, 0.02F)
              && near(spots[1].height, kGround + 3.0F, 0.05F) && spots[0].floor.area == west && spots[1].floor.area != 0
              && spots[1].floor.area != west && spots[0].floor.sightHigh == 10,
          "a cell with two floors: the terrain with the area of the ground and its sight limit, and the floor above it with an area of its own");
    check(tile->spots(cell(981.0F), cell(981.0F), spots) == 1 && spots[0].floor.walkable() && spots[0].floor.area == west
              && tile->spots(cell(983.0F), cell(983.0F), spots) == 1 && !spots[0].floor.walkable() && spots[0].floor.area == 0,
          "a floor below shallow water is a floor of the ground area, and a floor below deep water is no floor for a creature that walks");
    // The wall goes through the whole tile: the two sides are two areas (the tile has no way around the wall).
    check(tile->spots(cell(1020.0F), cell(990.0F), spots) == 1 && spots[0].floor.area != 0 && spots[0].floor.area != west && tile->areas == 3,
          "the two sides of a wall through the tile are two areas, and the tile has three areas");
    check(near(tile->terrain_height(10.3F, 20.7F), kGround, 0.02F) && tile->bytes() > 262144 && tile->bytes() < 400000,
          "the terrain height of the tile, and the bytes of a tile in memory (one word for each cell)");
    check(data.load(kTile, 40) == nullptr && data.load(40, 0) == nullptr && data.counts().tilesNoFile == 2,
          "a tile of a region with no file gives no tile");

    // Files that are not correct: the reader refuses each one as a whole, and it says which one.
    const auto refused = [&](const char* what, const std::function<void(TestNavRegion&)>& prepare,
                             const std::function<void(std::vector<std::uint8_t>&)>& change = {}) {
        const std::filesystem::path bad = options.workDirectory / "navigation-files-bad";
        std::filesystem::remove_all(bad);
        TestNavRegion other;
        prepare(other);
        other.write(bad, change);
        gamedata::NavigationData reader;
        const bool loaded = reader.set_directory(bad.string());
        check(!loaded && !reader.available() && !reader.problem().empty() && reader.load(0, 0) == nullptr, what);
        std::filesystem::remove_all(bad);
    };
    refused("a file of a different version is refused", [](TestNavRegion& other) { other.version = 2; });
    refused("a file with a step of the agent that is not a number is refused", [](TestNavRegion& other) { other.agent.step = std::nanf(""); });
    refused("a file with a wade value outside its range is refused", [](TestNavRegion& other) { other.agent.wade = 1.0e9F; });
    refused("a file with a cell that is not 0.25 m is refused", [](TestNavRegion& other) { other.agent.cell = 0.5F; });
    refused("a file with a terrain scale of 1e38 is refused (no height that is not finite gets to the world)",
            [](TestNavRegion& other) { other.heightScale = 1.0e38F; });
    refused("a file whose name is not the region of its header is refused", [](TestNavRegion& other) { other.x = 1; },
            [](std::vector<std::uint8_t>& file) { file[8] = 2; });
    refused("a file that ends in its directory is refused", [](TestNavRegion&) {}, [](std::vector<std::uint8_t>& file) { file.resize(5000); });
    refused("a file with a tile of the directory after the end of the file is refused", [](TestNavRegion&) {},
            [](std::vector<std::uint8_t>& file) {
                // Tile 7: an offset far after the file.
                const std::size_t entry = 64 + 7 * 16;
                file[entry + 3] = 0x7F;
            });

    // Tiles that are not correct: the file loads, and the reader refuses the tile.
    const auto bad_tile = [&](const char* what, const std::function<void(std::vector<std::uint8_t>&)>& change) {
        std::string problem;
        std::vector<std::uint8_t> plain = region.tile_bytes(&region.tiles.at({kTile, kTile}));
        change(plain);
        check(gamedata::NavTile::decode(kTile, kTile, plain, 2048.0F, gamedata::NavAgent{}, &problem) == nullptr && !problem.empty(), what);
    };
    const std::size_t terrainBytes = 65 * 65 * 2;
    check(gamedata::NavTile::decode(kTile, kTile, region.tile_bytes(&region.tiles.at({kTile, kTile})), 2048.0F, gamedata::NavAgent{}, nullptr)
              != nullptr,
          "the bytes of the test tile with no change give a tile");
    bad_tile("a tile that ends in its terrain samples is refused", [&](std::vector<std::uint8_t>& plain) { plain.resize(terrainBytes - 10); });
    bad_tile("a tile whose runs have more cells than the tile is refused", [&](std::vector<std::uint8_t>& plain) {
        plain[terrainBytes + 4 + 1] = 0xFF; // the length of the first run
        plain[terrainBytes + 4 + 2] = 0xFF;
    });
    bad_tile("a tile whose runs do not have each cell is refused", [&](std::vector<std::uint8_t>& plain) { plain[terrainBytes + 4 + 1] ^= 0x01; });
    bad_tile("a tile with a run count that is larger than the tile is refused", [&](std::vector<std::uint8_t>& plain) {
        plain[terrainBytes + 3] = 0x7F;
    });
    bad_tile("a tile with bytes after its floor list is refused", [&](std::vector<std::uint8_t>& plain) { plain.push_back(0); });
    bad_tile("a tile with a floor that has a flag that is not known is refused", [&](std::vector<std::uint8_t>& plain) {
        plain[plain.size() - 3] = 0x40; // the flags of the last floor
    });
    bad_tile("a tile whose floors of one cell are not in the sequence of their heights is refused", [&](std::vector<std::uint8_t>& plain) {
        // The last cell with two floors: the second floor gets the height 0.
        std::size_t place = plain.size();
        for (std::size_t index = plain.size() - 5; index >= terrainBytes + 5; index -= 5) {
            if (plain[index + 2] == 0 && plain[index - 5 + 2] == NavFloor::kTerrain) {
                place = index;
                break;
            }
        }
        plain[place] = 0;
        plain[place + 1] = 0;
    });
    // A zlib stream that does not decode: the reader gives no tile for it, and the other tiles load.
    TestNavRegion damaged;
    const std::filesystem::path damagedFolder = options.workDirectory / "navigation-files-damaged";
    std::filesystem::remove_all(damagedFolder);
    damaged.write(damagedFolder, [](std::vector<std::uint8_t>& file) {
        const std::size_t first = 64 + 1024 * 16;
        file[first + 4] ^= 0xFF; // the length field of the stored block of tile 0
    });
    gamedata::NavigationData partly;
    check(partly.set_directory(damagedFolder.string()) && partly.load(0, 0) == nullptr && partly.counts().tilesBad == 1
              && partly.load(1, 0) != nullptr,
          "a tile whose zlib stream does not decode gives no tile, and the next tile of the file loads");
    std::filesystem::remove_all(damagedFolder);
    std::filesystem::remove_all(folder);
}

/** NV2: the ground. */
void ground_and_floors(const Options& options) {
    NavHost world(options, "navigation-ground");
    TestNavRegion region;
    // A platform of 4 m by 4 m, 3 m above the terrain, and a second one 6 m above it.
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1000.0F && x < 1004.0F && y > 1000.0F && y < 1004.0F) {
            floors[0].sightTo = 2.5F;
            floors.push_back({kGround + 3.0F, 0, 0.0F, 2.5F});
            floors.push_back({kGround + 6.0F, 0});
        }
    });
    region.write(world.folder);
    world.host.terrain.source = [](float, float) { return std::optional<float>(77.0F); };
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    check(navigation.has_data() && near(*navigation.ground_height(at(1002.0F, 1002.0F, kGround)), 77.0F),
          "before a character is near, the world has no tile: the ground is the terrain height of the height files");
    world::Entity* walker = world.character(at(990.0F, 990.0F));
    check(walker != nullptr, "a character is in the world");
    world.ticks(60);
    check(navigation.counts().tilesHeld >= 9 && navigation.counts().tilesLoaded == navigation.counts().tilesHeld,
          "the world has the tiles near the character");
    check(near(*navigation.ground_height(at(1002.0F, 1002.0F, kGround + 0.4F)), kGround, 0.02F)
              && near(*navigation.ground_height(at(1002.0F, 1002.0F, kGround + 2.0F)), kGround + 3.0F, 0.02F)
              && near(*navigation.ground_height(at(1002.0F, 1002.0F, kGround + 5.0F)), kGround + 6.0F, 0.02F)
              && near(*navigation.ground_height(at(1002.0F, 1002.0F, kGround + 40.0F)), kGround + 6.0F, 0.02F),
          "a place with three floors: the ground is the floor that is nearest to the height of the position");
    check(near(*navigation.ground_height(at(995.0F, 995.0F, kGround + 30.0F)), kGround, 0.02F),
          "a place with one floor: the ground is that floor for each height of the position");
    check(navigation.footing(at(1002.0F, 1002.0F, kGround + 3.1F)) == world::Footing::walks
              && navigation.footing(at(1002.0F, 1002.0F, kGround + 1.5F)) == world::Footing::rooted
              && navigation.footing(at(995.0F, 995.0F, kGround + 0.3F)) == world::Footing::walks
              && navigation.footing(at(2500.0F, 995.0F, kGround)) == world::Footing::rooted,
          "a creature walks when a free floor is 0.6 m or less from its height: on a platform, not in the air below it");
    std::filesystem::remove_all(world.folder);
}

/** A wall on the line x 1010 from y 980 to y 1020, with an opening from y 1012 to y 1013.5. */
void wall_with_opening(float x, float y, std::vector<TestFloor>& floors) {
    if (x > 1010.0F && x < 1010.5F && y > 980.0F && y < 1020.0F && !(y > 1012.0F && y < 1013.5F)) {
        wall(floors);
    }
}

/** How one step of a creature crosses the line of the wall of `wall_with_opening`. */
struct Crossing {
    bool throughWall{false};
    bool throughOpening{false};
    /** Around the south end or the north end of the wall. */
    bool aroundTheEnd{false};

    void step(const state::WorldPosition& from, const state::WorldPosition& to) {
        const float line = 1010.25F;
        if ((from.x - line) * (to.x - line) >= 0.0F) {
            return;
        }
        const float y = from.y + (to.y - from.y) * (line - from.x) / (to.x - from.x);
        std::printf("        a step over the line of the wall at y %.3f\n", static_cast<double>(y));
        (y > 1012.0F && y < 1013.5F ? throughOpening : (y < 980.0F || y > 1020.0F ? aroundTheEnd : throughWall)) = true;
    }
};

/** NV3: the path around a wall. */
void path_around_a_wall(const Options& options) {
    NavHost world(options, "navigation-wall");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, wall_with_opening);
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    world::Entity* target = world.character(at(1015.0F, 1000.0F));
    world.ticks(60);
    world::Entity* wolf = world.wolf(at(1005.0F, 1000.0F), 270.0F);
    check(target != nullptr && wolf != nullptr && wolf->creature.mind.groundKnown && wolf->creature.mind.walks,
          "a character 10 m to the east of a wolf, with a wall between them: the wolf stands on a free floor");
    // The development command of the path.
    const world::PathReport report = navigation.find_path(wolf->position, target->position);
    bool throughOpening = false;
    for (const state::WorldPosition& corner : report.corners) {
        throughOpening = throughOpening || (corner.x > 1009.5F && corner.x < 1011.0F && corner.y > 1012.0F && corner.y < 1013.5F);
    }
    std::printf("        path: %zu corners, %.2f m, %u steps of the search, %zu cells\n", report.corners.size(),
                static_cast<double>(report.length), report.steps, report.cells);
    check(report.found && !report.direct && throughOpening && report.length > 26.0F && report.length < 29.0F && report.steps > 0,
          "the path between the two goes through the opening of the wall (26.5 m in place of 10 m)");
    const world::PathReport open = navigation.find_path(at(1005.0F, 1000.0F), at(1005.0F, 990.0F));
    check(open.found && open.direct && open.steps == 0 && near(open.length, 10.0F, 0.3F) && open.corners.size() == 2,
          "a path with no solid on its line is the straight line, with no search");
    // The wolf gets its target by a hit and goes to it: each of its places is on the west side, in the opening, or on the east side.
    const std::uint64_t searches = navigation.counts().searches;
    (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->creature.mind.target == target->id, "a hit gives the wolf its target");
    Crossing crossing;
    float farthest = 0.0F;
    float way = 0.0F;
    state::WorldPosition before = wolf->position;
    for (int step = 0; step < 600 && wolf->creature.mind.attacks == 0; ++step) {
        world.ticks(1);
        const state::WorldPosition& place = wolf->position;
        crossing.step(before, place);
        farthest = std::max(farthest, world::horizontal_distance(place, before));
        way += world::horizontal_distance(place, before);
        before = place;
    }
    std::printf("        the wolf: %.2f m of way, the longest step %.2f m, at %.2f %.2f, %llu search(es), %llu steps\n", static_cast<double>(way),
                static_cast<double>(farthest), static_cast<double>(wolf->position.x), static_cast<double>(wolf->position.y),
                static_cast<unsigned long long>(navigation.counts().searches - searches),
                static_cast<unsigned long long>(navigation.counts().searchSteps));
    check(!crossing.throughWall && crossing.throughOpening && wolf->creature.mind.attacks == 1,
          "the wolf goes through the opening, not through the wall, and attacks its target");
    check(way > 23.0F && way < 28.0F && farthest <= kRunSpeed * 0.2F + 0.05F && wolf->position.x > 1011.0F
              && world::horizontal_distance(wolf->position, target->position) <= 2.2F + 0.05F,
          "the way of the wolf is the path (not the straight line), each step is not longer than its speed permits, and it stops in its reach");
    check(navigation.counts().searches - searches == 1, "the wolf searches one time: its goal does not move");
    check(near(wolf->position.z, kGround, 0.05F), "the wolf stands on the ground at each step");
    // The target goes 20 m to the south: the wolf follows with a new path.
    (void)world.host.world.travel(target->characterId, at(1015.0F, 985.0F), world::TravelScreen::off);
    world.pass(4.0F);
    check(world::horizontal_distance(wolf->position, target->position) <= 2.3F && wolf->creature.mind.mode == CreatureMode::fight,
          "the target moves: the wolf goes to the new place of its target");
    // The way back: the creatures ignore the target, and the wolf goes home. From this place the way around
    // the south end of the wall is shorter than the way through the opening.
    world.host.world.creatures().behavior().set_ignored(target->characterId, true);
    Crossing back;
    before = wolf->position;
    const float atTheLoss = world::horizontal_distance(wolf->position, target->position);
    float nearest = atTheLoss;
    for (int step = 0; step < 800 && wolf->creature.mind.mode != CreatureMode::rest; ++step) {
        world.ticks(1);
        back.step(before, wolf->position);
        before = wolf->position;
        if (step < 100) {
            nearest = std::min(nearest, world::horizontal_distance(wolf->position, target->position));
        }
    }
    std::printf("        the way home: %.2f m from the character at the loss of the target, %.2f m at the nearest place of the first second\n",
                static_cast<double>(atTheLoss), static_cast<double>(nearest));
    check(nearest >= 1.0F,
          "a wolf that loses its target does not go on with its path to it: its way home does not go to the place of the character");
    check(!back.throughWall && back.aroundTheEnd && !back.throughOpening && wolf->creature.mind.mode == CreatureMode::rest
              && near(wolf->position.x, 1005.0F) && near(wolf->position.y, 1000.0F),
          "the way back goes around the end of the wall (the shortest way from there), not through the wall, and the wolf is at its home at the end");
    std::filesystem::remove_all(world.folder);
}

/**
 * A porch for the cases: a floor 1.4 m above the terrain from x 1000 to 1006 and y 1000 to 1004,
 * with stairs to the east (7 steps of 0.2 m, each one 0.5 m long, from x 1006 to 1009.5 on the
 * line y 1001 to 1003). No floor of the terrain is below the porch and below the stairs.
 */
void porch(float x, float y, std::vector<TestFloor>& floors) {
    if (x > 1000.0F && x < 1006.0F && y > 1000.0F && y < 1004.0F) {
        floors.clear();
        floors.push_back({kGround + 1.4F, 0});
    } else if (x > 1006.0F && x < 1009.5F && y > 1001.0F && y < 1003.0F) {
        const int step = static_cast<int>((1009.5F - x) / 0.5F);
        floors.clear();
        floors.push_back({kGround + 0.2F * static_cast<float>(step + 1) - 0.2F + 0.2F, 0});
    } else if (x > 1006.0F && x < 1006.25F && y > 1000.0F && y < 1004.0F) {
        wall(floors, kGround + 2.2F); // the railing of the porch beside the stairs
    }
}

/** NV4: floors and stairs. */
void floors_and_stairs(const Options& options) {
    NavHost world(options, "navigation-floors");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, porch);
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    world::Entity* target = world.character(at(1020.0F, 1002.0F));
    world.ticks(60);
    // A wolf on the porch, 1.4 m above the terrain. Stage 3.4b: it did not leave its place.
    world::Entity* wolf = world.wolf(at(1002.0F, 1002.0F, kGround + 1.4F), 270.0F);
    check(wolf != nullptr && wolf->creature.mind.walks, "a wolf on a floor 1.4 m above the terrain stands on a free floor: it walks");
    (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    bool onFloor = true;
    bool onStairs = false;
    float highestStep = 0.0F;
    state::WorldPosition before = wolf->position;
    for (int step = 0; step < 500 && wolf->creature.mind.attacks == 0; ++step) {
        world.ticks(1);
        const state::WorldPosition& place = wolf->position;
        const std::optional<float> floor = navigation.ground_height(place);
        onFloor = onFloor && floor && near(place.z, *floor, 0.02F);
        if (place.x > 1006.3F && place.x < 1009.4F) {
            onStairs = onStairs || (place.z > kGround + 0.1F && place.z < kGround + 1.45F && place.y > 1001.0F && place.y < 1003.0F);
        }
        highestStep = std::max(highestStep, std::fabs(place.z - before.z));
        before = place;
    }
    std::printf("        the wolf: at %.2f %.2f %.2f, the largest height change of one step %.2f m\n", static_cast<double>(wolf->position.x),
                static_cast<double>(wolf->position.y), static_cast<double>(wolf->position.z), static_cast<double>(highestStep));
    check(onFloor && onStairs && wolf->creature.mind.attacks == 1 && near(wolf->position.z, kGround, 0.05F),
          "the wolf goes down the stairs to its target: each of its places has the height of the floor of that place");
    // The target goes up to the porch (a travel): the wolf goes up the stairs.
    (void)world.host.world.travel(target->characterId, at(1001.5F, 1002.0F, kGround + 1.4F), world::TravelScreen::off);
    world.pass(5.0F);
    check(near(wolf->position.z, kGround + 1.4F, 0.05F) && wolf->position.x < 1006.0F
              && world::horizontal_distance(wolf->position, target->position) <= 2.3F,
          "the target is on the porch: the wolf goes up the stairs and stands on the porch floor");
    const world::PathReport side = navigation.find_path(at(1003.0F, 995.0F), at(1003.0F, 1002.0F, kGround + 1.4F));
    bool overStairs = false;
    for (const state::WorldPosition& corner : side.corners) {
        overStairs = overStairs || (corner.x > 1006.0F && corner.x < 1010.0F && corner.y > 1000.9F && corner.y < 1003.1F);
    }
    check(side.found && overStairs && side.length > 14.0F,
          "a path from the ground beside the porch to the porch goes around to the stairs: the edge of 1.4 m is no step");
    std::filesystem::remove_all(world.folder);
}

/** A yard with a closed wall around it: x 1030 to 1040, y 990 to 1000. */
void closed_yard(float x, float y, std::vector<TestFloor>& floors) {
    const bool inside = x > 1030.0F && x < 1040.0F && y > 990.0F && y < 1000.0F;
    const bool inner = x > 1030.5F && x < 1039.5F && y > 990.5F && y < 999.5F;
    if (inside && !inner) {
        wall(floors);
    }
}

/** NV5: a goal with no way. */
void goal_with_no_way(const Options& options) {
    NavHost world(options, "navigation-no-way");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) { closed_yard(x, y, floors); });
    region.tiles[{kTile + 1, kTile}] = cells_of(kTile + 1, kTile, closed_yard);
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    // The character is in the closed yard, and the wolf is 5 m to the west of the yard (10 m from the character).
    world::Entity* target = world.character(at(1035.0F, 995.0F));
    world.ticks(60);
    world::Entity* wolf = world.wolf(at(1025.0F, 995.0F), 270.0F);
    check(target != nullptr && wolf != nullptr && wolf->creature.mind.walks, "a character in a closed yard, and a wolf outside");
    const world::PathReport report = navigation.find_path(wolf->position, target->position);
    check(!report.found && report.steps == 0 && !report.cause.empty(),
          "the path command: the two places have no way between them, and the answer needs no search step");
    world.pass(1.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest, "the wall of the yard stops the vision of the wolf: no target");
    const std::uint64_t noWay = navigation.counts().noWay;
    (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    check(wolf->creature.mind.mode == CreatureMode::fight, "a hit gives the wolf the character in the yard as its target");
    world.pass(0.5F);
    check(wolf->creature.mind.mode != CreatureMode::fight && wolf->creature.mind.target == 0 && navigation.counts().noWay > noWay
              && navigation.counts().searches == 0 && navigation.counts().searchSteps == 0
              && world.host.world.creatures().behavior().counts().targetsNoWay == 1 && log_count("no way to the target") == 1,
          "the target has no way to it: the wolf loses its target at its next step, with no search (the areas say it)");
    world.pass(3.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest && near(wolf->position.x, 1025.0F) && wolf->vitals.health == wolf->vitals.healthMax,
          "the wolf is at its home with its full health");
    const std::uint64_t byHit = world.host.world.creatures().behavior().counts().targetsByHit;
    (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    world.pass(1.0F);
    check(world.host.world.creatures().behavior().counts().targetsByHit == byHit + 1 && wolf->creature.mind.target == 0
              && world.host.world.creatures().behavior().counts().targetsNoWay == 2,
          "a second hit gives the target again, and the wolf loses it again: each time with no search");
    std::filesystem::remove_all(world.folder);
}

/** NV6: the limit of a tick. */
void tick_limit(const Options& options) {
    NavHost world(options, "navigation-tick-limit");
    TestNavRegion region;
    // A wall of 90 m from the south to the north (two tiles): each path from its west side to its east side is 70 m long or more.
    const auto longWall = [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1000.0F && x < 1000.5F && y > 930.0F && y < 1020.0F) {
            wall(floors);
        }
    };
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, longWall);
    region.tiles[{kTile, kTile - 1}] = cells_of(kTile, kTile - 1, longWall);
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    world::Entity* target = world.character(at(1003.0F, 965.0F));
    world.ticks(60);
    // The character of this case does not die: 30 wolves bite it.
    target->vitals.healthMax = 1.0e7F;
    target->vitals.health = 1.0e7F;
    // 30 wolves on the west side of the wall, each one 5 m to 8 m from the character, with the wall between.
    std::vector<world::Entity*> wolves;
    for (int index = 0; index < 30; ++index) {
        wolves.push_back(world.wolf(at(995.0F + static_cast<float>(index % 5) * 0.75F, 962.0F + static_cast<float>(index / 5) * 1.5F), 270.0F));
    }
    world.pass(0.5F);
    std::size_t resting = 0;
    for (const world::Entity* wolf : wolves) {
        resting += wolf != nullptr && wolf->creature.mind.mode == CreatureMode::rest && wolf->creature.mind.walks ? 1 : 0;
    }
    check(resting == wolves.size(), "30 wolves behind a wall do not see the character");
    for (world::Entity* wolf : wolves) {
        (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    }
    // The first step of the behavior asks for 30 searches. Each one needs some thousand steps.
    const world::NavigationCounts start = navigation.counts();
    int ticksToTheLast = 0;
    std::size_t waitingAfterTheFirstStep = 0;
    for (int tick = 0; tick < 3000; ++tick) {
        world.ticks(1);
        if (tick == 25) {
            waitingAfterTheFirstStep = navigation.queued();
        }
        if (navigation.counts().searches - start.searches >= wolves.size() && navigation.queued() == 0) {
            ticksToTheLast = tick + 1;
            break;
        }
    }
    const world::NavigationCounts& counts = navigation.counts();
    std::printf("        30 searches: %llu steps, the largest number in one tick %u (limit %u), the largest number that waited %zu, "
                "%d ticks to the last one, the largest time of the searches of one tick %.3f ms\n",
                static_cast<unsigned long long>(counts.searchSteps - start.searchSteps), counts.largestTickSteps, world::navigation_rules::kTickSteps,
                counts.largestQueue, ticksToTheLast, std::chrono::duration<double, std::milli>(navigation.search_load().largest).count());
    check(counts.searches - start.searches == wolves.size() && counts.searchesFailed == 0, "each of the 30 wolves gets one search, and each search finds a path");
    check(counts.largestTickSteps <= world::navigation_rules::kTickSteps && counts.largestTickSteps > world::navigation_rules::kTickSteps / 2,
          "the steps of all searches of one tick are not more than the limit of a tick");
    check(counts.largestQueue >= 20 && waitingAfterTheFirstStep > 0 && ticksToTheLast > 30,
          "the searches that have no steps in a tick wait, and they come in the next ticks");
    world.pass(14.0F);
    // The crowd rule of the behavior: the wolves do not end at one place. The ones in the reach attack, and the others wait behind them.
    std::size_t arrived = 0;
    std::size_t inReach = 0;
    float nearestPair = 1.0e9F;
    for (std::size_t index = 0; index < wolves.size(); ++index) {
        const float away = world::horizontal_distance(wolves[index]->position, target->position);
        arrived += wolves[index]->position.x > 1000.5F && away < 20.0F && wolves[index]->creature.mind.target == target->id ? 1 : 0;
        inReach += away <= 2.2F ? 1 : 0;
        for (std::size_t second = index + 1; second < wolves.size(); ++second) {
            nearestPair = std::min(nearestPair, world::horizontal_distance(wolves[index]->position, wolves[second]->position));
        }
    }
    const world::BehaviorCounts& behavior = world.host.world.creatures().behavior().counts();
    std::printf("        at the end: %zu of 30 wolves on the side of the character, %zu in the reach of their attack, the two nearest wolves %.2f m "
                "from each other, %llu targets lost (%llu with no way), %llu attacks\n",
                arrived, inReach, static_cast<double>(nearestPair), static_cast<unsigned long long>(behavior.targetsLost),
                static_cast<unsigned long long>(behavior.targetsNoWay), static_cast<unsigned long long>(behavior.attacks));
    check(arrived == wolves.size() && inReach >= 1 && behavior.attacks >= 2 && behavior.targetsLost == 0,
          "each wolf goes around the wall to the side of the character and keeps its target, and a wolf in the reach attacks");
    check(nearestPair >= world::behavior_rules::kCrowdDistance - 0.05F, "no two wolves end their way at the same place");
    std::filesystem::remove_all(world.folder);
}

/** NV7: the tiles near the characters, and the thread that reads them. */
void tiles_near_characters(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "navigation-tiles";
    std::filesystem::remove_all(folder);
    TestNavRegion first;
    first.write(folder);
    TestNavRegion second;
    second.x = 3;
    second.y = 2;
    second.write(folder);
    {
        NavHost world(options, "navigation-tiles-unused");
        world.folder = folder;
        check(world.load() && world.host.navigation.regions() == 2, "two regions of navigation data");
        world::Navigation& navigation = world.navigation();
        world.ticks(60);
        check(navigation.counts().tilesHeld == 0 && navigation.counts().tilesLoaded == 0, "a world with no character has no tile");
        world::Entity* walker = world.character(at(1000.0F, 1000.0F));
        world.ticks(60);
        const std::size_t one = navigation.counts().tilesHeld;
        const std::size_t bytesOfOne = navigation.counts().tileBytes;
        std::printf("        one character: %zu tiles, %.1f MB\n", one, static_cast<double>(bytesOfOne) / 1048576.0);
        // The square of 96 m around a place is 3 or 4 tiles on each axis.
        check(walker != nullptr && one >= 9 && one <= 16 && navigation.counts().tilesLoaded == one && navigation.counts().tilesMissing == 0,
              "a character: the world reads the tiles that are 96 m or less from it (9 to 16 tiles)");
        // A travel to a different region: the tiles of the new place come, and the old ones go after 10 s.
        (void)world.host.world.travel(walker->characterId, at(3.0F * 2048.0F + 1000.0F, 2.0F * 2048.0F + 1000.0F), world::TravelScreen::off);
        world.ticks(60);
        check(navigation.counts().tilesHeld == 2 * one && navigation.counts().tilesDropped == 0,
              "after a travel the world has the tiles of the new place, and it keeps the tiles of the old place for some time");
        world.pass(11.0F);
        check(navigation.counts().tilesHeld == one && navigation.counts().tilesDropped == one && navigation.counts().tileBytes == bytesOfOne,
              "10 s later the tiles of the old place are gone, and the bytes of the tiles are the bytes of one place again");
        // A place at the edge of the data: the tiles of a region with no file are not asked.
        (void)world.host.world.travel(walker->characterId, at(2030.0F, 1000.0F), world::TravelScreen::off);
        world.pass(11.0F);
        check(navigation.counts().tilesHeld < one && navigation.counts().tilesHeld >= 6 && navigation.counts().tilesMissing == 0
                  && world.host.navigation.counts().tilesNoFile == 0,
              "at the edge of the data the world has only the tiles of the region that has a file");
    }
    // The backend: a thread of its own reads the tiles. The world thread does not read a file.
    {
        ManualClock clock;
        state::Store store;
        gamedata::AppearanceTables tables;
        gamedata::NavigationData data;
        check(store.open(":memory:") && data.set_directory(folder.string()), "a world with the thread of the navigation");
        world::WorldOptions worldOptions;
        worldOptions.navigationData = &data;
        worldOptions.navigationThread = true;
        world::World world(clock, store, tables, worldOptions);
        Sink sink;
        const Player player = add_player(store, "76561202255233901", "Thread", "cccccccc-0000-4000-8000-000000000001", "Threadwalker",
                                         at(1000.0F, 1000.0F));
        const world::Entity* walker = world.enter(1, sink, player.characterId, player.account.personaId).entity;
        check(walker != nullptr, "a character enters the world");
        clock.advance(std::chrono::milliseconds(10));
        world.tick();
        const std::uint64_t readInTheFirstTick = data.counts().tilesRead;
        check(world.navigation().counts().tilesLoaded == 0 || readInTheFirstTick > 0,
              "the tick that asks for the tiles does not read them: the thread of the navigation does");
        // The thread reads; the world takes the tiles at the start of a tick. Real time: 5 s at most.
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((world.navigation().counts().tilesLoaded < 9 || world.navigation().counts().tilesLoaded < world.navigation().counts().tilesHeld)
               && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            clock.advance(std::chrono::milliseconds(10));
            world.tick();
        }
        const world::NavigationCounts& counts = world.navigation().counts();
        std::printf("        the thread: %llu tiles, a read %.3f ms on average, the largest one %.3f ms; the largest tick of the world %.3f ms\n",
                    static_cast<unsigned long long>(counts.tilesLoaded),
                    std::chrono::duration<double, std::milli>(world.navigation().tile_load().total).count()
                        / static_cast<double>(std::max<std::uint64_t>(world.navigation().tile_load().count, 1)),
                    std::chrono::duration<double, std::milli>(world.navigation().tile_load().largest).count(),
                    std::chrono::duration<double, std::milli>(world.load().worldTick.largest).count());
        check(counts.tilesLoaded >= 9 && counts.tilesLoaded == counts.tilesHeld && world.navigation().ground_height(at(1000.0F, 1000.0F)),
              "the thread of the navigation reads the tiles near the character, and the world has them some ticks later");
        check(world.navigation().footing(at(1000.0F, 1000.0F)) == world::Footing::walks
                  && world.navigation().footing(at(3.0F * 2048.0F + 500.0F, 2.0F * 2048.0F + 500.0F)) == world::Footing::unknown,
              "a place whose tile the world does not have yet has no answer: a creature there waits");
    }
    std::filesystem::remove_all(folder);
}

/** NV8: the sight. */
void sight_and_walls(const Options& options) {
    NavHost world(options, "navigation-sight");
    TestNavRegion region;
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        wall_with_opening(x, y, floors);
        // A low fence (1 m) on the line y 990 from x 985 to 1005: no floor for a creature, and the sight is free above it.
        if (y > 990.0F && y < 990.25F && x > 985.0F && x < 1005.0F) {
            floors.clear();
            floors.push_back({kGround + 1.0F, NavFloor::kBlocked});
        }
        // A roof 2.5 m above the ground from x 985 to 990, y 1000 to 1005: the sight below it is free.
        if (x > 985.0F && x < 990.0F && y > 1000.0F && y < 1005.0F) {
            floors[0].sightTo = 2.5F;
            floors.push_back({kGround + 2.75F, NavFloor::kBlocked});
        }
    });
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    world::Entity* target = world.character(at(1016.0F, 1000.0F));
    world.ticks(60);
    check(!navigation.sees(at(1005.0F, 1000.0F), at(1016.0F, 1000.0F)) && navigation.sees(at(1016.0F, 1000.0F), at(1020.0F, 1004.0F))
              && navigation.sees(at(1005.0F, 1012.75F), at(1016.0F, 1012.75F)),
          "a wall of 3 m stops the sight; no solid, and the opening of the wall, do not");
    check(navigation.sees(at(995.0F, 987.0F), at(995.0F, 993.0F)) && !navigation.find_path(at(995.0F, 987.0F), at(995.0F, 993.0F)).direct,
          "a fence of 1 m does not stop the sight (the line is 1.5 m above the ground), and it stops the straight way");
    check(navigation.sees(at(986.0F, 1002.0F), at(989.0F, 1003.0F)) && !navigation.sees(at(987.0F, 1002.0F, kGround + 3.0F), at(987.0F, 1010.0F, kGround - 2.0F)),
          "the sight below a roof is free, and a line that goes into the ground is not");
    // A hill: the two ends are 1.5 m above the ground, and the wall top between them is higher.
    check(navigation.sees(at(1005.0F, 1000.0F, kGround + 2.0F), at(1016.0F, 1000.0F, kGround + 2.0F)),
          "two creatures on a place 2 m above the ground see each other over the wall of 3 m (the line is at 3.5 m)");
    // The vision of a creature: the wolf is 11 m from the character, in its vision distance, with the wall between.
    world::Entity* wolf = world.wolf(at(1005.0F, 1000.0F), 270.0F);
    const std::uint64_t stopped = navigation.counts().sightStopped;
    world.pass(1.0F);
    check(wolf != nullptr && wolf->creature.mind.mode == CreatureMode::rest && navigation.counts().sightStopped > stopped,
          "a wolf with a wall between it and a character 11 m away does not see the character");
    // A second wolf on the side of the character sees it.
    world::Entity* second = world.wolf(at(1022.0F, 1006.0F), 90.0F);
    world.pass(0.5F);
    check(second != nullptr && second->creature.mind.mode == CreatureMode::fight && second->creature.mind.target == target->id,
          "a wolf with no wall between it and the character sees the character and gets it as its target");
    (void)world.creatures().kill(second->id);
    // The character is 0.5 m from the wall, and the wolf is 0.4 m from it on the other side: in the reach, and no attack through the wall.
    (void)world.host.world.travel(target->characterId, at(1011.0F, 1000.0F), world::TravelScreen::off);
    world::Entity* third = world.wolf(at(1009.6F, 1000.0F), 270.0F);
    (void)world.creatures().hurt(third->id, 1.0F, target->id);
    world.pass(1.0F);
    check(third->creature.mind.mode == CreatureMode::fight && third->creature.mind.attacks == 0 && third->position.y > 1001.0F,
          "a wolf whose target is in its reach behind a wall does not attack through the wall: it goes on its path");
    world.pass(5.0F);
    check(third->creature.mind.attacks >= 1 && third->position.x > 1010.5F, "the wolf goes around the wall and attacks on the side of its target");
    std::filesystem::remove_all(world.folder);
}

/** NV9: water. */
void water(const Options& options) {
    NavHost world(options, "navigation-water");
    TestNavRegion region;
    // A river from the south to the north, 6 m wide: deep water with a ford of shallow water at y 1000 to 1002.
    // A second ford at y 975 to 977 is land (a bridge of the terrain).
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1000.0F && x < 1006.0F) {
            if (y > 1000.0F && y < 1002.0F) {
                floors[0].flags |= NavFloor::kShallowWater;
            } else if (!(y > 975.0F && y < 977.0F)) {
                floors[0].flags |= NavFloor::kDeepWater;
            }
        }
    });
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    world::Entity* target = world.character(at(1010.0F, 1004.0F));
    world.ticks(60);
    const world::PathReport ford = navigation.find_path(at(996.0F, 1004.0F), at(1010.0F, 1004.0F));
    bool inDeep = false;
    bool overFord = false;
    for (const state::WorldPosition& corner : ford.corners) {
        inDeep = inDeep || (corner.x > 1000.3F && corner.x < 1005.7F && !(corner.y > 1000.0F && corner.y < 1002.0F) && !(corner.y > 975.0F && corner.y < 977.0F));
        overFord = overFord || (corner.x > 999.5F && corner.x < 1006.5F && corner.y > 1000.0F && corner.y < 1002.0F);
    }
    check(ford.found && !ford.direct && !inDeep && overFord && ford.length < 20.0F,
          "a path over a river of deep water goes through the shallow water of the ford, not through the deep water");
    // The land bridge is 27 m to the south. From a place 10 m south of the ford, the search takes the ford:
    // 2 m of shallow water count as 4 m, which is less than the way to the bridge.
    const world::PathReport second = navigation.find_path(at(996.0F, 990.0F), at(1010.0F, 990.0F));
    check(second.found && second.length > 25.0F, "a place with no straight way over the river gets a path over the ford or the bridge");
    world::Entity* wolf = world.wolf(at(996.0F, 1004.0F), 270.0F);
    (void)world.creatures().hurt(wolf->id, 1.0F, target->id);
    bool wolfInDeep = false;
    for (int step = 0; step < 400 && wolf->creature.mind.attacks == 0; ++step) {
        world.ticks(1);
        const state::WorldPosition& place = wolf->position;
        wolfInDeep = wolfInDeep || (place.x > 1000.2F && place.x < 1005.8F && !(place.y > 1000.0F && place.y < 1002.0F));
    }
    check(!wolfInDeep && wolf->creature.mind.attacks == 1 && wolf->position.x > 1006.0F,
          "a wolf goes through the ford to its target on the other side of the river");
    // A target in the deep water: no way.
    (void)world.host.world.travel(target->characterId, at(1003.0F, 1012.0F), world::TravelScreen::off);
    world.pass(2.0F);
    check(wolf->creature.mind.target == 0 && world.host.world.creatures().behavior().counts().targetsNoWay == 1,
          "a target in deep water (more than 1.5 m from a floor for a creature) has no way to it: the wolf loses it");
    std::filesystem::remove_all(world.folder);
}

/** NV10: the load with 50 characters. */
void load_with_navigation(const Options& options) {
    NavHost world(options, "navigation-load");
    TestNavRegion region;
    // Each tile of the 10 by 5 tiles of the case has a wall of 12 m, and a closed yard to the south of it.
    const auto place = [](float x, float y, std::vector<TestFloor>& floors) {
        const float localX = std::fmod(x, 64.0F);
        const float localY = std::fmod(y, 64.0F);
        if (localX > 30.0F && localX < 30.5F && localY > 26.0F && localY < 38.0F) {
            wall(floors);
        }
        const bool yard = localX > 20.0F && localX < 28.0F && localY > 8.0F && localY < 16.0F;
        const bool inner = localX > 20.5F && localX < 27.5F && localY > 8.5F && localY < 15.5F;
        if (yard && !inner) {
            wall(floors);
        }
    };
    constexpr int kFirst = 8;
    for (int tileY = kFirst; tileY < kFirst + 5; ++tileY) {
        for (int tileX = kFirst; tileX < kFirst + 10; ++tileX) {
            region.tiles[{tileX, tileY}] = cells_of(tileX, tileY, place);
        }
    }
    region.write(world.folder);
    check(world.load(), "the navigation data of the case loads");
    world::Navigation& navigation = world.navigation();
    // One character: the tiles and their memory.
    std::vector<world::Entity*> characters;
    characters.push_back(world.character(at(static_cast<float>(kFirst) * 64.0F + 34.0F, static_cast<float>(kFirst) * 64.0F + 32.0F)));
    world.ticks(60);
    const std::size_t tilesOfOne = navigation.counts().tilesHeld;
    const std::size_t bytesOfOne = navigation.counts().tileBytes;
    for (int index = 1; index < 50; ++index) {
        characters.push_back(world.character(at(static_cast<float>(kFirst + index % 10) * 64.0F + 34.0F,
                                                static_cast<float>(kFirst + index / 10) * 64.0F + 32.0F)));
    }
    world.ticks(60);
    const std::size_t tilesOfAll = navigation.counts().tilesHeld;
    const std::size_t bytesOfAll = navigation.counts().tileBytes;
    std::printf("        tiles: 1 character %zu tiles (%.1f MB), 50 characters in 50 tiles %zu tiles (%.1f MB); a tile of the case is flat\n",
                tilesOfOne, static_cast<double>(bytesOfOne) / 1048576.0, tilesOfAll, static_cast<double>(bytesOfAll) / 1048576.0);
    check(tilesOfOne >= 9 && tilesOfOne <= 16 && tilesOfAll > 50 && tilesOfAll < 50 * tilesOfOne,
          "the tiles of 50 characters: each tile one time, also when two characters are near it");
    // For each character: a wolf behind the wall (a path of 20 m around it), and a wolf in the closed yard (no way).
    std::vector<world::Entity*> walkers;
    std::vector<world::Entity*> closed;
    for (world::Entity* character : characters) {
        character->vitals.healthMax = 1.0e7F;
        character->vitals.health = 1.0e7F;
        walkers.push_back(world.wolf(at(character->position.x - 8.0F, character->position.y), 270.0F));
        closed.push_back(world.wolf(at(character->position.x - 10.0F, character->position.y - 20.0F), 270.0F));
    }
    world.pass(0.5F);
    std::size_t resting = 0;
    for (std::size_t index = 0; index < walkers.size(); ++index) {
        resting += walkers[index]->creature.mind.mode == CreatureMode::rest && closed[index]->creature.mind.mode == CreatureMode::rest ? 1 : 0;
    }
    check(resting == walkers.size(), "the 100 wolves do not see their character: a wall is between them");
    for (std::size_t index = 0; index < walkers.size(); ++index) {
        (void)world.creatures().hurt(walkers[index]->id, 1.0F, characters[index]->id);
        (void)world.creatures().hurt(closed[index]->id, 1.0F, characters[index]->id);
    }
    const world::NavigationCounts start = navigation.counts();
    const world::LoadMeter ticksBefore = world.host.world.load().worldTick;
    const auto started = std::chrono::steady_clock::now();
    // The largest tick of these 1,000 ticks (the meter of the world has the ticks of the world entries too: a test host reads its tiles in the tick).
    std::chrono::nanoseconds largestTick{0};
    for (int tick = 0; tick < 1000; ++tick) {
        const auto tickStart = std::chrono::steady_clock::now();
        world.ticks(1);
        largestTick = std::max<std::chrono::nanoseconds>(largestTick, std::chrono::steady_clock::now() - tickStart);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const world::NavigationCounts& counts = navigation.counts();
    const world::LoadMeter& ticks = world.host.world.load().worldTick;
    std::size_t fighting = 0;
    std::size_t lost = 0;
    for (std::size_t index = 0; index < walkers.size(); ++index) {
        fighting += walkers[index]->position.x > characters[index]->position.x - 3.5F && walkers[index]->creature.mind.attacks > 0 ? 1 : 0;
        lost += closed[index]->creature.mind.target == 0 && closed[index]->creature.mind.attacks == 0 ? 1 : 0;
    }
    std::printf("        1,000 ticks with 50 fights: %.1f ms (%.3f ms each, the largest %.3f ms); %llu searches with %llu steps (the largest "
                "number in one tick %u), %llu goals with no way, the searches of a tick %.3f ms at most\n",
                seconds * 1000.0, seconds, std::chrono::duration<double, std::milli>(largestTick).count(),
                static_cast<unsigned long long>(counts.searches - start.searches), static_cast<unsigned long long>(counts.searchSteps - start.searchSteps),
                counts.largestTickSteps, static_cast<unsigned long long>(counts.noWay - start.noWay),
                std::chrono::duration<double, std::milli>(navigation.search_load().largest).count());
    check(fighting == walkers.size(), "each of the 50 wolves behind a wall went around it and attacks its character");
    check(lost == closed.size() && counts.noWay - start.noWay >= closed.size(),
          "each of the 50 wolves in a closed yard lost its target: a goal with no way");
    check(counts.searches - start.searches >= walkers.size() && counts.searches - start.searches < 3 * walkers.size()
              && counts.largestTickSteps <= world::navigation_rules::kTickSteps && counts.searchesFailed == 0,
          "one search or two for each wolf that walks, none for a wolf with no way, and the steps of a tick in their limit");
    check(ticks.count - ticksBefore.count == 1000 && seconds < 2.0 && largestTick < std::chrono::milliseconds(50),
          "1,000 ticks of the world with 50 fights and their path searches need less than 2 s (the time limit of the case)");
    std::filesystem::remove_all(world.folder);
}

/** NV12: which creatures walk, with the thread of the navigation. */
void footing_when_the_data_comes(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "navigation-footing";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    // A platform of 4 m by 4 m, 3 m above the ground, with no way to it.
    region.tiles[{kTile, kTile}] = cells_of(kTile, kTile, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1010.0F && x < 1014.0F && y > 1000.0F && y < 1004.0F) {
            floors.push_back({kGround + 3.0F, 0});
        }
    });
    region.write(folder);
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    gamedata::NavigationData data;
    gamedata::CreatureData creatures;
    // Three spawn points: on the ground, 5 m above the ground (in the air), and on the platform.
    std::vector<gamedata::SpawnPoint> points(3);
    const state::WorldPosition places[3] = {at(1004.0F, 1002.0F), at(1004.0F, 1006.0F, kGround + 5.0F), at(1011.0F, 1002.0F, kGround + 3.0F)};
    for (std::size_t index = 0; index < 3; ++index) {
        points[index].id = "footing-point-" + std::to_string(index);
        points[index].kind = 0;
        points[index].level = 18;
        points[index].position = places[index];
        points[index].yaw = 90.0F;
        points[index].ground = places[index].z;
    }
    check(store.open(":memory:") && data.set_directory(folder.string())
              && creatures.set_full({wolf_slice()}, {wolf_kind()}, points, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "a world with the thread of the navigation and three spawn points");
    world::WorldOptions worldOptions;
    worldOptions.navigationData = &data;
    worldOptions.navigationThread = true;
    worldOptions.creatureData = &creatures;
    worldOptions.shelterTime = std::chrono::milliseconds(0);
    world::World world(clock, store, tables, worldOptions);
    Sink sink;
    const Player player =
        add_player(store, "76561202255233902", "Footing", "cccccccc-0000-4000-8000-000000000002", "Groundwalker", at(1000.0F, 1002.0F));
    world::Entity* walker = world.enter(1, sink, player.characterId, player.account.personaId).entity;
    check(walker != nullptr, "a character enters the world");
    walker->vitals.healthMax = 1.0e7F;
    walker->vitals.health = 1.0e7F;
    world.creatures().behavior().set_ignored(player.characterId, true);
    clock.advance(std::chrono::milliseconds(10));
    world.tick();
    std::vector<world::Entity*> made;
    world.for_each_near(at(1006.0F, 1003.0F), 20.0F, [&](world::Entity& entity) {
        if (entity.kind == world::EntityKind::creature) {
            made.push_back(&entity);
        }
    });
    std::sort(made.begin(), made.end(), [](const world::Entity* a, const world::Entity* b) {
        return a->position.x + a->position.y * 0.001F < b->position.x + b->position.y * 0.001F;
    });
    check(made.size() == 3 && world.navigation().counts().tilesLoaded == 0, "the spawn rule made the three creatures in the tick of the world entry: no tile is there yet");
    std::size_t unknown = 0;
    for (const world::Entity* creature : made) {
        unknown += !creature->creature.mind.groundKnown && !creature->creature.mind.walks ? 1 : 0;
    }
    check(unknown == 3, "a creature whose place has no data yet has no answer for its ground: it does not walk yet, and the world does not say that it stays");
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((world.navigation().counts().tilesLoaded < 9 || world.navigation().counts().tilesLoaded < world.navigation().counts().tilesHeld)
           && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        clock.advance(std::chrono::milliseconds(10));
        world.tick();
    }
    check(world.navigation().counts().tilesLoaded >= 9, "the thread of the navigation did read the tiles");
    // The creatures get the character as their target: the first step asks the navigation for their ground.
    world.creatures().behavior().set_ignored(player.characterId, false);
    for (int tick = 0; tick < 300; ++tick) {
        clock.advance(std::chrono::milliseconds(10));
        world.tick();
    }
    const world::Entity* ground = made[0];
    const world::Entity* air = made[1];
    const world::Entity* platform = made[2];
    check(ground->creature.mind.groundKnown && ground->creature.mind.walks && ground->position.x < 1003.0F && ground->creature.mind.attacks > 0,
          "the creature on the ground walks: it went to the character and attacks");
    check(air->creature.mind.groundKnown && !air->creature.mind.walks && air->position.x == 1004.0F && air->position.z == kGround + 5.0F,
          "the creature 5 m above the ground stands on no floor: it does not leave its place");
    check(platform->creature.mind.groundKnown && platform->creature.mind.walks && platform->position.x > 1010.0F
              && platform->creature.mind.mode != CreatureMode::fight && world.creatures().behavior().counts().targetsNoWay >= 1,
          "the creature on the platform stands on a floor and walks, and it has no way down to the character: it loses its target");
    std::filesystem::remove_all(folder);
}

// ---- the rules for a fight that a player can play ---------------------------------------------

/** A host with flat navigation data and the product time of the shelter. */
struct PlayHost {
    NavHost world;

    PlayHost(const Options& options, const char* name, bool shelter) : world(options, name) {
        TestNavRegion region;
        region.write(world.folder);
        if (!world.load()) {
            throw std::runtime_error("play test: the navigation data is not valid");
        }
        if (shelter) {
            world.host.world.set_shelter(world::behavior_rules::kShelterTime, world::behavior_rules::kShelterLongest);
        }
    }
    ~PlayHost() {
        std::filesystem::remove_all(world.folder);
    }
    world::Behavior& behavior() {
        return world.creatures().behavior();
    }
    /** The client of a character reports its position (the camera estimate of its session). */
    void client_reports(world::Entity* character) {
        (void)world.host.world.set_position(character->id, character->position, false);
    }
};

/** PL1: the shelter of a character that came to a place. */
void shelter(const Options& options) {
    PlayHost play(options, "play-shelter", true);
    NavHost& world = play.world;
    world::Behavior& behavior = play.behavior();
    // A world entry. A wolf is 6 m from the character, with its face to it.
    world::Entity* alpha = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    alpha->vitals.healthMax = 1.0e7F;
    alpha->vitals.health = 1.0e7F;
    world::Entity* wolf = world.wolf(at(1006.0F, 1000.0F), 90.0F);
    world.pass(15.0F);
    check(alpha != nullptr && wolf != nullptr && wolf->creature.mind.mode == CreatureMode::rest && behavior.sheltered(*alpha)
              && alpha->life.shelterWaits && behavior.counts().sheltered > 0,
          "a character that came into the world, and whose client reported no position yet (the loading screen): no creature takes it, also 15 s later");
    play.client_reports(alpha);
    world.pass(9.5F);
    check(wolf->creature.mind.mode == CreatureMode::rest && behavior.sheltered(*alpha) && !alpha->life.shelterWaits,
          "the time of the shelter starts at the first position of its client: 9.5 s later the wolf has no target");
    world.pass(1.0F);
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->creature.mind.target == alpha->id && !behavior.sheltered(*alpha),
          "10 s after the first position the shelter is over: the wolf takes the character");
    // A travel of 100 m: the wolf loses its target, and a wolf of the new place does not take the character at once.
    world::Entity* second = world.wolf(at(1106.0F, 1000.0F), 90.0F);
    const std::uint64_t lost = behavior.counts().targetsLost;
    (void)world.host.world.travel(alpha->characterId, at(1100.0F, 1000.0F), world::TravelScreen::off);
    check(wolf->creature.mind.target == 0 && behavior.counts().targetsLost == lost + 1 && log_count("the target went to a different place") == 1,
          "a creature that has a character as its target loses it when the character travels");
    world.pass(2.0F);
    play.client_reports(alpha);
    world.pass(5.0F);
    check(second->creature.mind.mode == CreatureMode::rest && behavior.sheltered(*alpha),
          "after a travel the creatures of the new place do not take the character: 5 s after the first position there");
    // A hit of the character in its shelter time: that creature takes it.
    (void)world.creatures().hurt(second->id, 1.0F, alpha->id);
    check(second->creature.mind.mode == CreatureMode::fight && second->creature.mind.target == alpha->id && behavior.sheltered(*alpha),
          "a creature that the character hurts takes it as its target in the shelter time too");
    world::Entity* third = world.wolf(at(1100.0F, 1006.0F), 180.0F);
    world.pass(2.0F);
    check(third->creature.mind.mode == CreatureMode::rest, "a different creature of the place does not take the character in that time");
    // The longest time: a client that reports nothing has the shelter for 60 s, not for ever.
    (void)world.host.world.travel(alpha->characterId, at(1300.0F, 1000.0F), world::TravelScreen::off);
    world::Entity* fourth = world.wolf(at(1306.0F, 1000.0F), 90.0F);
    world.pass(59.0F);
    const bool before = fourth->creature.mind.mode == CreatureMode::rest;
    world.pass(1.5F);
    check(before && fourth->creature.mind.mode == CreatureMode::fight,
          "a client that reports no position has the shelter for 60 s: then a creature takes its character");
    // A respawn is a travel: the character has the shelter at its respawn place.
    (void)world.host.world.health().hurt(alpha->characterId, 2.0e7F);
    const world::HealthResult respawn = world.host.world.health().respawn(1);
    check(alpha->life.deaths == 1 && respawn.revived && !alpha->life.dead && behavior.sheltered(*alpha),
          "a character that lives again after its death has the shelter at its respawn place");
    // Two characters: the shelter is a value of a character. Beta is at a place for a long time, Gamma comes.
    world::Entity* beta = world.character(at(1500.0F, 1000.0F));
    world.ticks(60);
    play.client_reports(beta);
    world.pass(10.5F);
    world::Entity* gamma = world.character(at(1503.0F, 1000.0F));
    world.ticks(10);
    play.client_reports(gamma);
    beta->vitals.healthMax = 1.0e7F;
    beta->vitals.health = 1.0e7F;
    // The wolf is 3 m from Gamma and 6 m from Beta.
    world::Entity* fifth = world.wolf(at(1506.0F, 1000.0F), 90.0F);
    world.pass(1.0F);
    check(beta != nullptr && gamma != nullptr && fifth->creature.mind.target == beta->id && behavior.sheltered(*gamma) && !behavior.sheltered(*beta),
          "two characters: the wolf takes the character that is at the place for a long time, not the nearer one that came 1 s before");
}

/** PL2: the health that comes back. */
void rest(const Options& options) {
    PlayHost play(options, "play-rest", false);
    NavHost& world = play.world;
    world::Health& health = world.host.world.health();
    world::Entity* alpha = world.character(at(1000.0F, 1000.0F));
    world::Entity* beta = world.character(at(1004.0F, 1000.0F));
    world.ticks(60);
    const float full = alpha->vitals.healthMax;
    check(alpha != nullptr && beta != nullptr && full > 100.0F && health.rests() == 0, "a character with its full health gets nothing from the rest rule");
    (void)health.hurt(alpha->characterId, full * 0.5F);
    world.pass(5.5F);
    check(near(alpha->vitals.health, full * 0.5F), "for 6 s after a damage the health of the character does not change");
    const world::Revision vitals = alpha->changed[world::ComponentKey::vitals];
    world.pass(1.6F);
    check(near(alpha->vitals.health, full * 0.53F, full * 0.001F) && health.rests() == 1 && alpha->changed[world::ComponentKey::vitals] > vitals,
          "6 s after the damage the character gets 3 percent of its maximum health, and its vitals state is marked for each observer");
    world.pass(3.0F);
    check(near(alpha->vitals.health, full * 0.62F, full * 0.001F) && beta->vitals.health == beta->vitals.healthMax,
          "then 3 percent in each second, for the character that took the damage only");
    // A hit of the character on a creature: it is in a fight again.
    health.fought(*alpha);
    const float held = alpha->vitals.health;
    world.pass(5.5F);
    check(alpha->vitals.health == held, "a character that did damage is in a fight: its health does not come back for 6 s");
    world.pass(30.0F);
    check(alpha->vitals.health == full, "the health comes back to the maximum, and not above it");
    const std::uint64_t rests = health.rests();
    world.pass(5.0F);
    check(health.rests() == rests, "a character with its full health costs no change");
    // A creature that takes the character as its target starts the fight: no rest while it attacks.
    (void)health.hurt(alpha->characterId, full * 0.5F);
    world.pass(5.0F);
    world::Entity* wolf = world.wolf(at(1002.0F, 1002.0F), 180.0F);
    alpha->vitals.healthMax = 1.0e7F; // The character of this part does not die.
    alpha->vitals.health = 5.0e6F;
    world.pass(10.0F);
    check(wolf->creature.mind.attacks >= 3 && alpha->vitals.health < 5.0e6F && health.rests() == rests,
          "while a creature attacks the character, the health does not come back");
    // A dead character gets nothing.
    (void)health.hurt(beta->characterId, 2.0e7F);
    world.pass(20.0F);
    check(beta->life.dead && beta->vitals.health == 0.0F, "the health of a dead character does not come back");
}

gamedata::CreatureSlice fighter_slice() {
    gamedata::CreatureSlice slice = wolf_slice();
    slice.actions.push_back({"Attack_Light2", "Attack_Light2", 8, false});
    slice.actions.push_back({"Attack_Heavy1", "Attack_Heavy1", 9, false});
    slice.actions.push_back({"Attack_Double", "Attack_Double", 10, false});
    slice.actions.push_back({"Attack_Heavy2", "Attack_Heavy2", 11, false});
    const gamedata::CreatureContact base = slice.attacks[0].contacts[0];
    const auto contact = [&](float time, float factor) {
        gamedata::CreatureContact made = base;
        made.time = time;
        made.factor = factor;
        return made;
    };
    // In the sequence of the data: light (0.2), heavy (0.9), light (0.25), two contacts of one swing (0.3), heavy with three hits (0.9).
    slice.attacks = {gamedata::CreatureAttack{kBite, 1.0F, {contact(0.5F, 0.2F)}},
                     gamedata::CreatureAttack{5, 1.0F, {contact(0.5F, 0.9F)}},
                     gamedata::CreatureAttack{4, 1.0F, {contact(0.5F, 0.25F)}},
                     gamedata::CreatureAttack{6, 1.0F, {contact(0.45F, 0.3F), contact(0.4F, 0.3F)}},
                     gamedata::CreatureAttack{7, 1.0F, {contact(0.2F, 0.3F), contact(0.5F, 0.3F), contact(0.8F, 0.3F)}}};
    return slice;
}

/** PL3: the attacks of a slice. */
void attack_selection(const Options& options) {
    PlayHost play(options, "play-attacks", false);
    NavHost& world = play.world;
    check(world.host.creatures.set_full({fighter_slice()}, {wolf_kind()}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}), "the creature data of the case is valid");
    world::Entity* alpha = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    alpha->vitals.healthMax = 1.0e7F;
    alpha->vitals.health = 1.0e7F;
    world::Entity* wolf = world.wolf(at(1001.5F, 1000.0F), 90.0F);
    const gamedata::CreatureSlice& body = *wolf->creature.body;
    check(body.attacks.size() == 5 && body.attacks[3].hits == 1 && near(body.attacks[3].weight, 0.3F) && body.attacks[3].contacts[0].time == 0.4F
              && !body.attacks[3].contacts[0].repeat && body.attacks[3].contacts[1].repeat && body.attacks[4].hits == 3
              && near(body.attacks[4].weight, 0.9F),
          "the load of the data: the contacts of an attack are in the sequence of their times, and two contacts that are 0.15 s or less from each other are one hit");
    check(!body.attacks[0].heavy && body.attacks[1].heavy && !body.attacks[2].heavy && !body.attacks[3].heavy && body.attacks[4].heavy
              && body.lightAttacks == std::vector<std::uint32_t>({0, 2, 3}) && body.heavyAttacks == std::vector<std::uint32_t>({1, 4}),
          "a heavy attack has more than 0.5 of the damage of the creature and more than 1.5 times the lightest attack of its slice");
    // Eight attacks: their sequence, the time between them, and their damage results.
    std::vector<std::uint32_t> sequence;
    std::vector<float> starts;
    std::vector<std::uint64_t> hitsAfter;
    float time = 0.0F;
    for (int tick = 0; tick < 4000 && sequence.size() < 9; ++tick) {
        world.ticks(1);
        time += 0.01F;
        if (wolf->creature.mind.attacks > sequence.size() && wolf->creature.mind.attack) {
            hitsAfter.push_back(play.behavior().counts().contactsHit);
            sequence.push_back(*wolf->creature.mind.attack);
            starts.push_back(time);
        }
    }
    check(sequence == std::vector<std::uint32_t>({0, 2, 3, 1, 0, 2, 3, 4, 0}),
          "the sequence of the attacks: the three light attacks, one heavy attack, the three light attacks, the second heavy attack");
    bool pauses = sequence.size() == 9;
    std::string gaps;
    for (std::size_t index = 1; index < starts.size() && pauses; ++index) {
        const bool heavy = body.attacks[sequence[index - 1]].heavy;
        const float pause = std::chrono::duration<float>(heavy ? world::behavior_rules::kHeavyPause : world::behavior_rules::kAttackPause).count();
        const float gap = starts[index] - starts[index - 1] - body.attacks[sequence[index - 1]].length;
        gaps += (index > 1 ? ", " : "") + std::to_string(gap).substr(0, 4);
        pauses = gap >= pause - 0.011F && gap <= pause + 0.25F;
    }
    note("the pauses after the attacks (s): " + gaps);
    check(pauses, "the pause after a light attack is 1.5 s and the pause after a heavy attack is 2.5 s (with the step of the behavior, 0.2 s)");
    // The damage results of each attack: 1, 1, 1 (two contacts of one swing), 1, 1, 1, 1, 3.
    bool results = hitsAfter.size() == 9;
    const std::uint64_t expected[8] = {1, 1, 1, 1, 1, 1, 1, 3};
    for (std::size_t index = 0; index + 1 < hitsAfter.size() && results; ++index) {
        results = hitsAfter[index + 1] - hitsAfter[index] == expected[index];
    }
    check(results && play.behavior().counts().contactsMissed == 0,
          "each hit of an attack is one damage result: one for the attack with two contacts of one swing, three for the attack with three hits");
}

/** PL4: creatures do not end their way at the same place. */
void crowd(const Options& options) {
    PlayHost play(options, "play-crowd", false);
    NavHost& world = play.world;
    world::Entity* alpha = world.character(at(1000.0F, 1000.0F));
    world.ticks(60);
    alpha->vitals.healthMax = 1.0e7F;
    alpha->vitals.health = 1.0e7F;
    // Three wolves on one line to the east of the character.
    world::Entity* wolves[3] = {world.wolf(at(1008.0F, 1000.0F), 90.0F), world.wolf(at(1010.0F, 1000.0F), 90.0F),
                                world.wolf(at(1012.0F, 1000.0F), 90.0F)};
    world.pass(8.0F);
    const float first = wolves[0]->position.x - 1000.0F;
    const float second = wolves[1]->position.x - 1000.0F;
    const float third = wolves[2]->position.x - 1000.0F;
    std::printf("        three wolves on one line: %.2f m, %.2f m, and %.2f m from the character; attacks %u, %u, %u\n", static_cast<double>(first),
                static_cast<double>(second), static_cast<double>(third), wolves[0]->creature.mind.attacks, wolves[1]->creature.mind.attacks,
                wolves[2]->creature.mind.attacks);
    // The reach of the bite is 2.2 m: a wolf stops 0.6 m in it.
    check(near(first, 1.6F, 0.06F) && wolves[0]->creature.mind.attacks >= 2, "the first wolf stops in the reach of its attack and attacks");
    check(second - first >= world::behavior_rules::kCrowdDistance - 0.01F && second <= 2.2F && wolves[1]->creature.mind.attacks >= 2,
          "the second wolf stops 0.5 m or more from the first one, and it attacks from there: the character is in the reach of its attack");
    check(third - second >= world::behavior_rules::kCrowdDistance - 0.01F && third > 2.2F && wolves[2]->creature.mind.attacks == 0
              && wolves[2]->creature.mind.mode == CreatureMode::fight && play.behavior().counts().crowded > 0,
          "the third wolf stops behind them, outside the reach of its attack: it waits and keeps its target");
    // The first wolf dies: the place is free, and the wolves behind it come nearer.
    (void)world.creatures().kill(wolves[0]->id);
    world.creatures().remove(wolves[0]->id);
    world.pass(4.0F);
    std::printf("        after the first wolf is gone: %.2f m and %.2f m from the character; attacks %u, %u\n",
                static_cast<double>(wolves[1]->position.x - 1000.0F), static_cast<double>(wolves[2]->position.x - 1000.0F),
                wolves[1]->creature.mind.attacks, wolves[2]->creature.mind.attacks);
    check(wolves[2]->position.x - 1000.0F <= 2.2F && wolves[1]->position.x - 1000.0F <= 2.2F && wolves[2]->creature.mind.attacks >= 1
              && std::fabs(wolves[2]->position.x - wolves[1]->position.x) >= world::behavior_rules::kCrowdDistance - 0.01F,
          "when the first wolf is gone, its place is free: the two other wolves are in the reach, 0.5 m from each other, and the third one attacks too");
    // Wolves that come from different directions do not stop for each other.
    world::Entity* north = world.wolf(at(1000.0F, 1008.0F), 180.0F);
    world.pass(3.0F);
    check(near(world::horizontal_distance(north->position, alpha->position), 1.6F, 0.06F) && north->creature.mind.attacks >= 1,
          "a wolf from a different direction goes to its own place in the reach");
}

} // namespace

std::vector<Case> navigation_cases() {
    return {
        {"NV1", "navigation: the reader of the navigation files, and each file and tile that is not correct", navigation_files},
        {"NV2", "navigation: the ground at a place with more than one floor, and which creatures walk", ground_and_floors},
        {"NV3", "navigation: a creature goes around a wall and through its opening, and back", path_around_a_wall},
        {"NV4", "navigation: a creature on a floor above the terrain walks, and it goes up and down stairs", floors_and_stairs},
        {"NV5", "navigation: a goal with no way costs no search, and the creature loses its target", goal_with_no_way},
        {"NV6", "navigation: the steps of the searches of one tick have a limit, and the other searches wait", tick_limit},
        {"NV7", "navigation: the world reads the tiles near a character and removes them again, with a thread of its own", tiles_near_characters},
        {"NV8", "navigation: a wall stops the sight and the attack of a creature, a low object does not", sight_and_walls},
        {"NV9", "navigation: a creature walks in shallow water and not in deep water", water},
        {"NV10", "navigation: the load of 50 characters with fights and path searches, with a time limit, and the memory of the tiles",
         load_with_navigation},
        {"NV12", "navigation: which creatures walk, and a creature whose place has no data yet", footing_when_the_data_comes},
        {"PL1", "play: the shelter of a character that came to a place (a world entry, a travel, a respawn), with two characters", shelter},
        {"PL2", "play: the health of a character that is not in a fight comes back", rest},
        {"PL3", "play: the hits of an attack, light and heavy attacks, their sequence, and the pause after each one", attack_selection},
        {"PL4", "play: creatures do not end their way to a target at the same place", crowd},
    };
}

} // namespace soulwarden::backend::tests

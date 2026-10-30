// The corrections of the code review of stage DG3 (~/Games/soulwarden/work/evidence/dungeons/dg3/code-review/FINDINGS.md), stage QG9e. The
// cases come from the proof cases DG3RV1 to DG3RV4 of the review; each one fails on the tree of stage DG3 (trees/after-dg3) and passes after
// the correction. The ids, the levels, and the places are made up.
//
//   DG3R1  R1: the cooldown of a checkpoint stays over a disconnect and the entry again into the same instance (A; B stays in the main world)
//   DG3R2  R2: the reader of a navigation folder takes a correct tile with 63 floors in each cell (its plain form is above 16 MiB)
//   DG3R3  R3: the reader of the interaction data refuses two checkpoints with one request id in one mode and map
//   DG3R4  R4: a tick of the levels does not look at the levels that the world read and that have no space and no work

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/level_data.h"
#include "gamedata/navigation_data.h"
#include "server/rep/health_messages.h"
#include "support/expedition_fixture.h"
#include "support/test_navigation.h"
#include "world/levels.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;
using namespace expedfix;

/** DG3R1 */
void reconnect_cooldown(const Options&) {
    ExpeditionShop shop;
    shop.enter();
    shop.enter_with(kCharacterA, 0);
    shop.move(kCharacterA, kVestibule);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(instance != nullptr && instance->checkpoints.size() == 2 && instance->checkpoints[1].active, "setup: A activated its vestibule checkpoint");
    if (instance == nullptr) {
        return;
    }
    const std::uint64_t instanceId = instance->id;
    check(shop.host.world.health().hurt(kCharacterA, 1.0e6F).error == world::HealthError::none, "setup: A dies");
    shop.respawn(kCharacterA, "Checkpoint_Vestibule");
    check(near(shop.entity(kCharacterA)->position, kVestibuleRespawn), "setup: A respawned at the checkpoint (its cooldown of 30 s runs)");
    // A disconnects (decision D9) and enters the same instance again before the end of the cooldown.
    shop.host.datagram(shop.clientA.peer, shop.clientA.carrier_close());
    shop.host.steps(3, {&shop.clientB});
    check(shop.entity(kCharacterA) == nullptr && shop.instances().find(instanceId) != nullptr, "setup: the disconnect keeps the instance");
    FakeClient again(27901);
    shop.host.enter(again, shop.a.registration());
    world::Entity* a = shop.entity(kCharacterA);
    check(a != nullptr && a->space == world::kMainSpace, "setup: D9 gives A its return place");
    if (a == nullptr) {
        return;
    }
    const world::InstanceResult joined = shop.instances().enter_mode(kCharacterA, kExpedition);
    check(joined.error == world::InstanceError::none && joined.instance.has_value() && joined.instance->id == instanceId,
          "setup: A enters the same instance again in the cooldown");
    // The home point of the vestibule shows the end of the cooldown, and a respawn there falls back to the start of the map.
    const bool shown = std::any_of(a->homePoints.begin(), a->homePoints.end(), [](const world::HomePoint& point) {
        return point.id == "Checkpoint_Vestibule" && point.cooldownEndNs != 0;
    });
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e6F);
    const auto revived = shop.host.world.health().respawn(a->owner, "Checkpoint_Vestibule");
    note("DG3R1 second respawn=" + std::to_string(a->position.x) + "/" + std::to_string(a->position.y) + " cooldown shown=" + std::to_string(shown));
    check(shown, "the home point list after the entry again shows the cooldown of the vestibule checkpoint");
    check(revived.error == world::HealthError::none && near(a->position, kStoryStart),
          "a checkpoint in its cooldown is not a respawn place after a disconnect and the entry again: the start of the map");
    check(shop.entity(kCharacterB)->space == world::kMainSpace && !shop.entity(kCharacterB)->homePointsKnown,
          "B stays in the main world with no home point list");
}

/** DG3R2 */
void large_tile(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "dg3r2-large-tile";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    region.ground = 5.0F;
    const TestCells floors = [](int, int, std::vector<TestFloor>& out) {
        out[0].sightTo = 2.0F;
        for (int index = 1; index < 63; ++index) {
            out.push_back({5.0F + 3.0F * static_cast<float>(index), 0, 0.0F, 2.0F});
        }
    };
    const std::vector<std::uint8_t> plain = region.tile_bytes(&floors);
    std::string problem;
    const auto decoded = gamedata::NavTile::decode(0, 0, plain, region.heightScale, region.agent, &problem);
    gamedata::NavSpot spots[gamedata::NavTile::kMaxFloors];
    check(decoded != nullptr && decoded->spots(10, 10, spots) == 63 && plain.size() > (16U << 20),
          "setup: the tile with 63 floors in each cell decodes, and its plain form is above 16 MiB: " + problem);
    region.tiles[{0, 0}] = floors;
    region.write(folder);
    gamedata::NavigationData data;
    const bool accepted = data.set_directory(folder.string());
    const auto loaded = accepted ? data.load(0, 0) : nullptr;
    note("DG3R2 plain bytes=" + std::to_string(plain.size()) + " directory accepted=" + std::to_string(accepted) + " cause=" + data.problem());
    check(accepted && loaded != nullptr && loaded->spots(10, 10, spots) == 63,
          "the reader of the folder takes a correct tile at the floor limit (63 floors in each cell) and keeps each floor");
    std::filesystem::remove_all(folder);
}

nlohmann::json checkpoint_json(const std::string& id, float x) {
    return {{"mode", kExpedition},
            {"map", kExpedition},
            {"spawner", id},
            {"slice", "slices/test/checkpoint.dynamicslice"},
            {"template", 0},
            {"position", {x, 300.0, 30.0}},
            {"yaw", 0.0},
            {"respawn", {x, 304.0, 30.0}},
            {"type", 10},
            {"cooldown", 30.0},
            {"name", "@Respawn_ShrineName"},
            {"homeEntity", "0000015411bd66fa"},
            {"homeComponent", "52d0db72600e2508"},
            {"trigger", {{"center", {x, 300.0, 30.0}}, {"radius", 5.0}}}};
}

/** DG3R3 */
void duplicate_checkpoint(const Options& options) {
    using nlohmann::json;
    const std::filesystem::path file = options.workDirectory / "dg3r3-checkpoints.json";
    const json crystal = {{"slice", "slices/test/checkpoint.dynamicslice"},
                          {"asset", "a55e7502000000000000000000000002"},
                          {"sub", 2},
                          {"kind", "other"},
                          {"root", "0000e4c1000000a1"},
                          {"slots", {{"position", 1}, {"buildable", 0}, {"ownership", 0}, {"vitals", 0}}},
                          {"buildableState", -1},
                          {"permissionRule", -1},
                          {"assembly", nullptr},
                          {"bounds", json::array()},
                          {"children", json::array()}};
    json document = {{"format", 2},          {"options", json::array()}, {"achievements", json::array()},
                     {"objects", json::array()}, {"templates", {crystal}}, {"spawned", json::array()},
                     {"fixtures", json::object()},
                     {"checkpoints", {checkpoint_json("Checkpoint_A", 300.0F), checkpoint_json("Checkpoint_B", 340.0F)}}};
    const auto load = [&](const json& value, gamedata::InteractionData& data) {
        std::ofstream(file) << value.dump();
        return data.load(file.string());
    };
    gamedata::InteractionData valid;
    check(load(document, valid) && valid.checkpoints().size() == 2, "setup: two checkpoints with two ids load: " + valid.error());
    json other = document;
    other["checkpoints"][1]["mode"] = "Test_Other_Expedition";
    other["checkpoints"][1]["map"] = "Test_Other_Expedition";
    other["checkpoints"][1]["spawner"] = "Checkpoint_A";
    gamedata::InteractionData otherMode;
    check(load(other, otherMode) && otherMode.checkpoints().size() == 2, "control: one id in two modes loads");
    document["checkpoints"][1]["spawner"] = "Checkpoint_A";
    gamedata::InteractionData duplicate;
    const bool accepted = load(document, duplicate);
    note("DG3R3 duplicate ids accepted=" + std::to_string(accepted) + " count=" + std::to_string(duplicate.checkpoints().size()) + " cause="
         + duplicate.error());
    check(!accepted && duplicate.checkpoints().empty(), "the reader refuses two checkpoints with one request id in one mode and map");
    document["checkpoints"][1]["spawner"] = std::string(32, 'x');
    gamedata::InteractionData longId;
    check(!load(document, longId), "control: an id of 32 bytes is refused");
    std::filesystem::remove(file);
}

/** DG3R4 */
void idle_level_scan(const Options&) {
    ServerHost host;
    const auto a = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kHere);
    const auto b = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kHere);
    FakeClient clientA(27903);
    FakeClient clientB(27904);
    host.enter(clientA, a.registration());
    host.enter(clientB, b.registration());
    for (int index = 0; index < 35; ++index) {
        check(!host.world.levels().available("nw_review_idle_" + std::to_string(index)), "setup: the world reads an own level with no set");
    }
    check(host.world.instances().instances().empty() && host.world.levels().levels().size() == 35,
          "setup: no instance, no tile, no search, and 35 levels in the cache");
    const std::uint64_t before = host.world.levels().counts().visits;
    host.world.levels().tick();
    const std::uint64_t visited = host.world.levels().counts().visits - before;
    note("DG3R4 idle cached levels visited in one tick=" + std::to_string(visited));
    check(visited == 0, "a tick does not look at the levels of the cache that have no space and no work");
}

} // namespace

std::vector<Case> review_dg3_cases() {
    return {{"DG3R1", "review of DG3 (R1): the cooldown of a checkpoint stays over a disconnect and the entry again", reconnect_cooldown},
            {"DG3R2", "review of DG3 (R2): the reader of a navigation folder takes a correct tile with 63 floors in each cell", large_tile},
            {"DG3R3", "review of DG3 (R3): two checkpoints with one request id in one mode and map are refused", duplicate_checkpoint},
            {"DG3R4", "review of DG3 (R4): a tick of the levels looks only at the levels with a space or with work", idle_level_scan}};
}

} // namespace soulwarden::backend::tests

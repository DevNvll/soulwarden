// The cases of the code review of stage QG9d (corrected in stage DG2; the findings:
// ~/Games/soulwarden/work/evidence/quests/qg9d/code-review/FINDINGS.md). Each case fails on the tree of stage QG9d
// (~/Games/soulwarden/work/trees/after-qg9d) and passes now. The base of the cases is the proof cases QG9DRV2, QG9DRV3, and QG9DRV1 of the
// review. The accounts, characters, kinds, and places are made up.
//
//   QG9DR1  finding QG9DR-R1: a setup stage with the end rule "start" makes the creature of a later kill stage at its start; its navigation
//           tile is not in the world yet (A), then it is (B): the end rule waits for the spawn that waits for its tile, and each instance
//           has its creature; the control with the tile in the world gives the same result
//   QG9DR2  finding QG9DR-R2: a roof spawn (floor 30 m, terrain 22 m) whose tile stays on its way after the 50 tries of the floor wait: the
//           spawn waits (no creature on the terrain), and after the tile comes the boss, its anchor, and its home are on the roof (A and B)
//   QG9DR3  finding QG9DR-R3: A gathers the quest object of its mode root, then a reset of the quest makes its task active again: the
//           instance makes the object again; B in its own instance has no task, no object, and its encounter waits

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "support/range_fixture.h"
#include "support/story_fixture.h"
#include "support/test_navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;

// The tiles of the navigation (a private member): QG9DR2 holds one tile on its way, a model of a slow read of the loader thread.
struct TileAccess {
    friend auto tile_member(TileAccess);
};
template <auto Member>
struct ExposeTiles {
    friend auto tile_member(TileAccess) {
        return Member;
    }
};
template struct ExposeTiles<&world::Navigation::m_tiles>;

constexpr const char* kObjectQuest = "Review_Objects";

json setup_document() {
    json document = story_document();
    document["format"] = 3;
    const json spawn = json{{"member", kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(kBossAt)})}};
    const json setup = stage_json("Review setup", 0xB00, 11, false, json{{"type", "none"}, {"end", "start"}}, json::array({spawn}));
    const json fight = stage_json("Review fight", 0xB10, 12, true, destroyed(1, kMemberSlice), json::array());
    document["recipes"][0]["stages"] = json::array({setup, fight});
    document["recipes"][0]["stageOrder"] = {0, 1};
    return document;
}

void cold_setup(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "qg9dr1-cold-setup";
    TestNavRegion region;
    region.ground = 30;
    region.write(folder);
    for (const bool warm : {false, true}) {
        StoryShop shop(":memory:", setup_document());
        shop.host.world.set_creature_behavior(false);
        check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation region loads");
        shop.enter();
        shop.host.clock.advance(std::chrono::milliseconds(500));
        shop.host.world.navigation().tick();
        if (warm) {
            (void)shop.host.world.navigation().find_path(kBossAt, kBossAt);
        }
        for (const char* who : {kCharacterA, kCharacterB}) {
            check(shop.instances().enter_mode(who, kMode).error == world::InstanceError::none, "setup: a character enters its own instance");
            world::Entity& entrant = *shop.entity(who);
            world::PositionTrack track;
            check(shop.host.world.report_position(entrant.owner, world::PositionReport{1, entrant.position, wire_height(entrant.position.z)}, track)
                      == world::PositionVerdict::accepted,
                  "setup: the client confirms the entry position");
        }
        for (int step = 0; step < 100; ++step) {
            shop.host.clock.advance(std::chrono::milliseconds(10));
            shop.host.world.tick();
        }
        for (const char* who : {kCharacterA, kCharacterB}) {
            const world::SpaceId space = shop.entity(who)->space;
            const world::EncounterInstance* run = shop.story_run(space);
            note("QG9DR1 " + std::string(warm ? "warm" : "cold") + " " + who + ": boss " + std::to_string(shop.boss(space) != nullptr) + ", waits "
                 + std::to_string(shop.host.world.encounters().counts().spawnsWaited));
            check(run != nullptr && run->stages[0].ended && run->stages[1].started, "the setup ended and the fight started");
            check(shop.boss(space) != nullptr, "a navigation wait does not cancel the creature of a setup stage (the end rule waits for it)");
        }
    }
    std::filesystem::remove_all(folder);
}

void long_pending_floor(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "qg9dr2-pending-floor";
    TestNavRegion region;
    region.ground = 22;
    region.tiles[{4, 4}] = [](int, int, std::vector<TestFloor>& floors) {
        floors[0].sightTo = 7;
        floors.push_back({30, 0});
    };
    region.write(folder);
    StoryShop shop;
    shop.host.world.set_creature_behavior(false);
    shop.host.terrain.source = [](float, float) { return std::optional<float>(22); };
    check(shop.host.navigation.set_directory(folder.string()), "setup: a roof at 30 m above the terrain at 22 m");
    shop.enter();
    world::Navigation& navigation = shop.host.world.navigation();
    // The test host reads a tile at its request: the request of the tile, then the tile goes back to the state "on its way".
    (void)navigation.line_ready(kBossAt, kBossAt);
    auto& tiles = navigation.*tile_member(TileAccess{});
    const std::uint64_t key = (std::uint64_t{4} << 32) | 4;
    auto found = tiles.find(key);
    check(found != tiles.end() && found->second.tile != nullptr, "setup: the roof tile decodes");
    if (found == tiles.end() || !found->second.tile) {
        return;
    }
    auto& held = found->second;
    auto roof = held.tile;
    held.state = decltype(held.state)::loading;
    held.tile.reset();
    for (const char* who : {kCharacterA, kCharacterB}) {
        check(shop.instances().enter_mode(who, kMode).error == world::InstanceError::none, "setup: each character enters its own instance");
        world::Entity& entrant = *shop.entity(who);
        world::PositionTrack track;
        check(shop.host.world.report_position(entrant.owner, world::PositionReport{1, entrant.position, wire_height(entrant.position.z)}, track)
                  == world::PositionVerdict::accepted,
              "setup: each client confirms the entry position");
    }
    for (int step = 0; step < 600; ++step) {
        shop.host.clock.advance(std::chrono::milliseconds(10));
        shop.host.world.tick();
        if (step == 350) {
            check(shop.boss(shop.entity(kCharacterA)->space) == nullptr, "the tile on its way holds the spawn");
        }
    }
    check(held.state == decltype(held.state)::loading, "setup: the tile is still on its way after the 50 tries");
    check(shop.boss(shop.entity(kCharacterA)->space) == nullptr && shop.boss(shop.entity(kCharacterB)->space) == nullptr,
          "no creature on the terrain below the roof while its tile is on its way");
    held.tile = roof;
    held.state = decltype(held.state)::ready;
    (void)navigation.ground_height(state::WorldPosition{kBossAt.x + 64, kBossAt.y, kBossAt.z});
    for (int step = 0; step < 300; ++step) {
        shop.host.clock.advance(std::chrono::milliseconds(10));
        shop.host.world.tick();
    }
    for (const char* who : {kCharacterA, kCharacterB}) {
        const world::Entity* boss = shop.boss(shop.entity(who)->space);
        check(boss != nullptr, "the creature is there after its tile came");
        note("QG9DR2 " + std::string(who) + ": boss " + std::to_string(boss != nullptr ? boss->position.z : -1.0F) + ", anchor "
             + std::to_string(boss != nullptr ? boss->creature.mind.anchor.z : -1.0F) + ", home "
             + std::to_string(boss != nullptr ? boss->creature.mind.home.z : -1.0F));
        check(boss == nullptr
                  || (std::fabs(boss->position.z - 30.0F) < 0.01F && std::fabs(boss->creature.mind.anchor.z - 30.0F) < 0.01F
                      && std::fabs(boss->creature.mind.home.z - 30.0F) < 0.01F),
              "the limit of the tries does not put the spawn, its anchor, and its home on the terrain below a roof whose tile is on its way");
    }
    std::filesystem::remove_all(folder);
}

json objects_document() {
    json document = story_document();
    document["instanceObjects"].push_back({{"mode", kMode}, {"map", kMode}, {"key", "e4c00000-0000-4000-8000-0000000000c1/19"},
                                           {"slice", kStartSlice}, {"spawner", "ReviewObject"}, {"position", {301, 300, 30}}, {"yaw", 0},
                                           {"onInit", false}});
    return document;
}

json objects_quests() {
    json document = story_quests();
    document["tasks"]["review_place"] = questfix::task("Review_Place", "taskgameevent", gamedata::task_type::kGameEvent,
                                                       {{"KillEnemyType", kStartEvent}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    document["tasks"]["review_kill"] = questfix::task("Review_Kill", "taskkillcontribution", gamedata::task_type::kKillContribution,
                                                      {{"KillEnemyType", "Test_Member_Category"}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    document["tasks"]["review_root"] = questfix::task("Review_Root", "consecutivetaskcontainer", gamedata::task_type::kConsecutiveContainer);
    document["objectives"].push_back(
        {{"id", kObjectQuest},
         {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
         {"tree", json::array({json::array({0, "review_place", json::array()}), json::array({1, "review_kill", json::array()}),
                               json::array({2, "review_root", json::array({0, 1})})})}});
    return document;
}

void object_reset(const Options&) {
    StoryShop shop(":memory:", objects_document(), objects_quests(), kMode, {}, {{kPortalSlice, 1}, {kStartSlice, 0}});
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(a, kObjectQuest, "command", 0).ok(), "setup: A has the quest of the object");
    shop.enter_story(kCharacterA);
    shop.enter_story(kCharacterB);
    shop.wait(0.5F);
    const world::Instance* instanceA = shop.instances().instance_of(kCharacterA);
    const world::Instance* instanceB = shop.instances().instance_of(kCharacterB);
    check(instanceA != nullptr && instanceB != nullptr && instanceA != instanceB && instanceA->questObjects.size() == 1
              && instanceA->questObjects[0].entity != 0,
          "setup: A has a quest object in its own instance");
    if (instanceA == nullptr || instanceB == nullptr || instanceA->questObjects.empty()) {
        return;
    }
    world::Entity* object = shop.host.world.find(instanceA->questObjects[0].entity);
    if (object == nullptr) {
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*object), 1500);
    shop.wait(0.5F);
    const auto* quest = questfix::quest_of(a, kObjectQuest);
    check(quest != nullptr && quest->tasks[0].progress == 1 && instanceA->questObjects[0].entity == 0,
          "control: the first gather completes its task and removes its object");
    if (quest == nullptr) {
        return;
    }
    check(shop.host.world.quests().reset(a, quest->instance).ok(), "setup: the reset of the quest");
    shop.wait(2.0F);
    quest = questfix::quest_of(a, kObjectQuest);
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kActive && quest->tasks[0].progress == 0,
          "setup: the task of the object is active again");
    note("QG9DR3 gathered " + std::to_string(instanceA->questObjects[0].gathered) + ", object " + std::to_string(instanceA->questObjects[0].entity != 0));
    check(instanceA->questObjects[0].entity != 0, "the task of the object after a reset has an object that gives its event again");
    check(instanceB->questObjects[0].entity == 0 && instanceB->held, "B has no task, no quest object, and its encounter waits");
    // The object gives its event again: the task completes again.
    if (world::Entity* again = shop.host.world.find(instanceA->questObjects[0].entity)) {
        shop.gather(shop.clientA, EncounterShop::start_block(*again), 1500);
        shop.wait(0.5F);
        quest = questfix::quest_of(a, kObjectQuest);
        check(quest != nullptr && quest->tasks[0].progress == 1 && instanceA->questObjects[0].entity == 0,
              "the second gather completes the task again and removes the object");
    }
}

} // namespace

std::vector<Case> review_qg9d_cases() {
    return {{"QG9DR1", "code review QG9d R1: the end rule of a setup stage waits for its spawn that waits for a navigation tile", cold_setup},
            {"QG9DR2", "code review QG9d R2: a roof spawn waits for its tile on its way also after the limit of the tries", long_pending_floor},
            {"QG9DR3", "code review QG9d R3: a reset of the quest makes the gathered quest object of the mode root again", object_reset}};
}

} // namespace soulwarden::backend::tests

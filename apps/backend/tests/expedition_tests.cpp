// Stage DG3 (docs/systems/instances.md, "The expeditions (stage DG3)"): the entry at the entrance of an expedition, its checks, the
// checkpoints and the respawn, the objects of the level set (a lore page, the exit stone), the leave, a disconnect, the end of an empty
// instance, and the floors of a cell above 16. The ids, the levels, and the places are made up.
//
//   DG3E1  the entry at the entrance of an expedition with an own level (A) and a character of the main world (B; MP-10): the
//          difficulty of the request (a mutation tier: 14), RequiredLevel (10), EntryAchievementConditional (10),
//          RequiredConsumableItemClasses (3), a level with no data set (10); each refusal changes nothing; then the entry (0): A in the
//          space of the level with its membership and the home point of the entrance checkpoint; B gets the stop of A and no state of
//          the instance; the remote setting of the group size
//   DG3C1  the checkpoints of two instances of one level (A and B): the activation sphere of the data, the home point list of each
//          member, the respawn 1938 at a checkpoint (its place, its cooldown of 30 s, then the start of the map), a checkpoint of the
//          other instance is not a respawn place, the empty list after the leave
//   DG3L1  the objects of the level set in two instances (A and B): the lore page (its achievement; the state of each space; no entity,
//          no state to an other session), the exit stone (the leave to the return place), the end of the empty instances after the
//          time of the mode and the objects of their spaces
//   DG3D1  a disconnect in an instance (decision D9): the return place at the next world entry, the instance waits, the entry at the
//          entrance joins it again (B stays in the main world and sees the return); the end of the instance after the time of its mode
//   DG3N1  the navigation of a cell with 41 floors (stage DG3, part B: the limit 63): a path on the floor of index 21 around a wall

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/level_data.h"
#include "server/http/routes.h"
#include "server/rep/health_messages.h"
#include "server/rep/travel_messages.h"
#include "support/chest_fixture.h"
#include "support/expedition_fixture.h"
#include "support/story_fixture.h"
#include "support/test_navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;
using namespace expedfix;

/** One home point of a PlayerHome fragment. */
struct Point {
    std::uint64_t group{0};
    std::uint64_t facet{0};
    std::string name;
    state::WorldPosition place;
    std::uint64_t cooldown{0};
    std::uint64_t cooldownEnd{0};
    std::uint32_t type{0};
    std::string id;
};

/** @return The home point lists of the PlayerHome fragments of a client (slot 20, type 3652) from a message index on; a list that does not
 *          read to its end is no list. */
std::vector<std::vector<Point>> home_lists(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<Point>> lists;
    for (const Seen& seen : fragments_of(client, server::rep::kPlayerHomeSlot, server::rep::kTypePlayerHomeState, from)) {
        const auto field = seen.fragment.fields.find({0, 0});
        if (field == seen.fragment.fields.end()) {
            continue;
        }
        server::hub::Reader reader(field->second);
        const std::uint32_t changes = reader.size();
        const std::uint8_t version = reader.u8();
        const std::uint32_t count = reader.size();
        std::vector<Point> list;
        for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
            Point point;
            (void)reader.raw(16);
            point.group = reader.u64();
            point.facet = reader.u64();
            point.name = reader.string();
            point.place.x = reader.f32();
            point.place.y = reader.f32();
            point.place.z = reader.f32();
            point.cooldown = reader.u64();
            point.cooldownEnd = reader.u64();
            point.type = reader.u32();
            (void)reader.u8();
            (void)reader.u8();
            point.id = reader.string();
            (void)reader.u32();
            list.push_back(point);
        }
        if (reader.ok() && reader.left() == 0 && changes == 0 && version == 0) {
            lists.push_back(std::move(list));
        }
    }
    return lists;
}

bool has_point(const std::vector<Point>& list, const char* id) {
    return std::any_of(list.begin(), list.end(), [&](const Point& point) { return point.id == id; });
}

/** DG3E1 */
void entry_checks(const Options&) {
    ExpeditionShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const auto refused = [&](std::uint32_t difficulty, std::uint32_t wanted, const char* what) {
        const std::size_t from = shop.clientA.got.size();
        shop.enter_with(kCharacterA, difficulty);
        const auto results = dungeon_results(shop.clientA, from);
        note(std::string(what) + ": 3094 " + (results.empty() ? std::string("none") : std::to_string(results.back().result)));
        check(results.size() == 1 && results[0].result == wanted && a.space == world::kMainSpace && shop.instances().instances().empty()
                  && shop.host.store.list_instance_members().empty(),
              std::string("a refused entry changes nothing: ") + what);
    };
    // The difficulty of the request: a mutation tier of a mode that is not mutable, then of a mutable mode (the backend has no mutation).
    refused(1, world::dungeon_result::kMutationDifficultyReq, "a mutation tier of a mode that is not mutable (14)");
    gamedata::GameMap map = expedition_map();
    map.mutableMode = true;
    shop.set_map(map);
    refused(2, world::dungeon_result::kMutationDifficultyReq, "a mutation tier of a mutable mode (14)");
    map = expedition_map();
    map.requiredLevel = 99;
    shop.set_map(map);
    refused(0, world::dungeon_result::kEntranceActivationFailure, "a level below RequiredLevel (10)");
    map = expedition_map();
    map.entryConditional = "Test_Expedition_Never_Unlocked";
    shop.set_map(map);
    refused(0, world::dungeon_result::kEntranceActivationFailure, "an EntryAchievementConditional that the character does not have (10)");
    map = expedition_map();
    map.requiredItemClasses = "DungeonKey";
    shop.set_map(map);
    refused(0, world::dungeon_result::kMissingReqItem, "RequiredConsumableItemClasses (3)");
    map = expedition_map();
    map.level = "nw_test_noset_00";
    map.terrain = "coatlicue/nw_test_noset_00";
    shop.set_map(map);
    refused(0, world::dungeon_result::kEntranceActivationFailure, "a level with no data set (10)");
    check(shop.instances().counts().entranceRefused == 6 && !shop.instances().mode_runs(kExpedition),
          "the six refusals are counted; an expedition with no data set of its level does not run (the rule of the support manifest too)");
    // The entry: the group of one character (decision D7: the minimum 1; MinGroupSize 3 of the data), the normal mode.
    shop.set_map(expedition_map());
    check(shop.instances().mode_runs(kExpedition), "an expedition with an accepted data set of its level runs (Instances::mode_runs)");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.enter_with(kCharacterA, 0);
    const auto results = dungeon_results(shop.clientA, fromA);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(results.size() == 1 && results[0].result == world::dungeon_result::kSuccess && instance != nullptr && a.space == instance->space
              && instance->level.level == kExpeditionLevel && instance->capacity == 5 && a.gameModeMembership.has_value(),
          "the entry at the entrance: 3094 with 0, A in the space of the own level with its membership (one member of five places)");
    if (instance == nullptr) {
        note("3094: " + (results.empty() ? std::string("none") : std::to_string(results.back().result)));
        return;
    }
    const auto lists = home_lists(shop.clientA, fromA);
    if (lists.empty() || lists.back().empty()) {
        check(false, "the home point list of A after the entry");
        return;
    }
    check(!lists.empty() && lists.back().size() == 1 && lists.back()[0].id == "Checkpoint_Entrance" && lists.back()[0].type == 10
              && lists.back()[0].cooldown == 30'000'000'000ULL && lists.back()[0].name == "@Respawn_ShrineName"
              && near(lists.back()[0].place, kEntrancePoint) && instance->checkpoints.size() == 2,
          "the start of the map is in the sphere of the entrance checkpoint (30 m): the home point list of A has it (type 10, 30 s)");
    const world::Entity* crystal = instance != nullptr ? shop.host.world.find(instance->checkpoints[0].entity) : nullptr;
    check(crystal != nullptr && crystal->space == instance->space && lists.back()[0].group == crystal->id
              && lists.back()[0].facet == (kHomeEntity ^ (kCheckpointRoot ^ crystal->id)) + kHomeComponent,
          "the checkpoint is a server child of the space of the instance; the reference of its home point (G, the facet target by the XOR rule)");
    // B in the main world (MP-10): the stop of A, no level change, no state of the instance.
    const auto& known = shop.host.session(2).observer().known();
    check(known.find(a.id) == known.end() && home_lists(shop.clientB, fromB).empty()
              && fragments_of(shop.clientB, server::rep::kGameModeParticipantSlot, server::rep::kTypeGameModeParticipantState, fromB).empty()
              && shop.entity(kCharacterB)->space == world::kMainSpace && !shop.entity(kCharacterB)->homePointsKnown,
          "B in the main world: no record of A (the stop), no home point list, no membership");
    check(std::string(server::http::kMeleeReportDocument).find(R"("javelin.enable-dungeon-group-restriction":false)") != std::string::npos,
          "the remote settings turn the group size check of the entry screen off (decision D7)");
}

/** DG3C1 */
void checkpoints(const Options&) {
    ExpeditionShop shop;
    shop.enter();
    shop.enter_with(kCharacterA, 0);
    shop.enter_with(kCharacterB, 0);
    const world::Instance* first = shop.instances().instance_of(kCharacterA);
    const world::Instance* second = shop.instances().instance_of(kCharacterB);
    check(first != nullptr && second != nullptr && first->space != second->space, "setup: A and B in two instances of one level");
    // A comes into the sphere of the vestibule checkpoint (5 m).
    std::size_t fromA = shop.clientA.got.size();
    std::size_t fromB = shop.clientB.got.size();
    shop.move(kCharacterA, kVestibule);
    auto listsA = home_lists(shop.clientA, fromA);
    check(!listsA.empty() && listsA.back().size() == 2 && has_point(listsA.back(), "Checkpoint_Vestibule")
              && first->checkpoints[1].active && !second->checkpoints[1].active && home_lists(shop.clientB, fromB).empty(),
          "A activates the vestibule checkpoint of its instance: its list has two points; the instance of B and the list of B do not change");
    // The death of A and the respawn at the vestibule checkpoint (1938 with its id).
    check(shop.host.world.health().hurt(kCharacterA, 1.0e6F).error == world::HealthError::none && shop.entity(kCharacterA)->life.dead,
          "setup: A dies");
    fromA = shop.clientA.got.size();
    shop.respawn(kCharacterA, "Checkpoint_Vestibule");
    world::Entity& a = *shop.entity(kCharacterA);
    listsA = home_lists(shop.clientA, fromA);
    const auto used = !listsA.empty() ? std::find_if(listsA.back().begin(), listsA.back().end(),
                                                     [](const Point& point) { return point.id == "Checkpoint_Vestibule"; })
                                      : std::vector<Point>::iterator{};
    const std::uint64_t wallNow = shop.host.clock.wall_time_ns(shop.host.clock.now());
    note("A at " + std::to_string(a.position.x) + " " + std::to_string(a.position.y) + " " + std::to_string(a.position.z) + "; lists "
         + std::to_string(listsA.size()) + "; cooldown end - now "
         + std::to_string(!listsA.empty() && used != listsA.back().end() ? static_cast<double>(used->cooldownEnd) - static_cast<double>(wallNow)
                                                                          : -1.0));
    check(!a.life.dead && a.space == first->space && near(a.position, kVestibuleRespawn) && !listsA.empty() && used != listsA.back().end()
              && used->cooldownEnd > wallNow + 29'000'000'000ULL && used->cooldownEnd <= wallNow + 30'000'000'000ULL,
          "the respawn at the checkpoint: its respawn place in the space of A; the list shows the end of its cooldown (30 s)");
    // A dies again in the cooldown: the start of the map (the random rule of a dead character).
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e6F);
    shop.respawn(kCharacterA, "Checkpoint_Vestibule");
    check(!a.life.dead && near(a.position, kStoryStart) && shop.instances().counts().checkpointRespawns == 1,
          "a respawn at a checkpoint in its cooldown is at the start of the map");
    // B: the vestibule checkpoint of the instance of A is not a respawn place of B; the entrance checkpoint of B is.
    (void)shop.host.world.health().hurt(kCharacterB, 1.0e6F);
    shop.respawn(kCharacterB, "Checkpoint_Vestibule");
    world::Entity& b = *shop.entity(kCharacterB);
    check(!b.life.dead && b.space == second->space && near(b.position, kStoryStart),
          "B names the checkpoint of the other instance: the start of the map of its own instance");
    (void)shop.host.world.health().hurt(kCharacterB, 1.0e6F);
    shop.respawn(kCharacterB, "Checkpoint_Entrance");
    check(!b.life.dead && b.space == second->space && near(b.position, kEntrancePoint) && shop.instances().counts().checkpointRespawns == 2,
          "B at the entrance checkpoint of its instance");
    // The leave: the empty list.
    fromA = shop.clientA.got.size();
    check(shop.instances().leave(kCharacterA).error == world::InstanceError::none, "setup: A leaves");
    shop.follow(kCharacterA);
    listsA = home_lists(shop.clientA, fromA);
    check(!listsA.empty() && listsA.back().empty() && a.homePoints.empty(), "after the leave the list of A is empty");
}

/** DG3L1 */
void level_objects(const Options&) {
    ExpeditionShop shop;
    shop.enter();
    shop.enter_with(kCharacterA, 0);
    shop.enter_with(kCharacterB, 0);
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    const world::SpaceId spaceA = a.space;
    const world::SpaceId spaceB = b.space;
    const std::size_t entities = shop.host.world.interactions().object_entities();
    shop.move(kCharacterA, kPagePlace);
    shop.move(kCharacterB, kPagePlace);
    const gamedata::InteractionOption read = option_row("ReadLore", 47, true);
    std::size_t fromA = shop.clientA.got.size();
    std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, ExpeditionShop::level_block(page_object(), 0, read));
    const bool readA = !replies(shop.clientA, fromA).empty() && replies(shop.clientA, fromA).back().second == 1;
    shop.request(shop.clientB, ExpeditionShop::level_block(page_object(), 0, read));
    const bool readB = !replies(shop.clientB, fromB).empty() && replies(shop.clientB, fromB).back().second == 1;
    const world::InteractionCounts& counts = shop.host.world.interactions().counts();
    check(readA && readB && a.interaction && b.interaction && a.interaction->instance != b.interaction->instance
              && std::binary_search(a.achievements.begin(), a.achievements.end(), kPageAchievement)
              && std::binary_search(b.achievements.begin(), b.achievements.end(), kPageAchievement) && counts.levelObjects == 2
              && counts.levelObjectUses == 2 && shop.host.world.interactions().level_object_count() == 2,
          "the lore page of the level set in two instances: each use is accepted with the achievement of the page, one object for each space");
    check(shop.host.world.interactions().object_entities() == entities && object_states(shop.clientA, fromA).empty()
              && object_states(shop.clientB, fromB).empty(),
          "the object of a space has no entity: no Interact state to a session (the state of each space stays in its space)");
    shop.end_block(shop.clientA);
    shop.end_block(shop.clientB);
    // The exit stone of the level set: A leaves to its return place; the instance waits (its checkpoints are content).
    shop.move(kCharacterA, kStonePlace);
    const gamedata::InteractionOption exit = option_row("ExitDungeon", 74, false);
    shop.request(shop.clientA, ExpeditionShop::level_block(stone_object(), 0, exit));
    shop.end_block(shop.clientA);
    shop.follow(kCharacterA);
    const world::Instance* waiting = shop.instances().instance_of_space(spaceA);
    check(a.space == world::kMainSpace && near(a.position, kHere, 1.0F) && shop.instances().counts().portalLeaves == 1 && waiting != nullptr
              && waiting->members.empty() && waiting->emptyUntil.has_value() && b.space == spaceB,
          "the exit stone: A at its return place in the main world; its instance waits for the time of its mode; B stays in its instance");
    shop.move(kCharacterB, kStonePlace);
    shop.request(shop.clientB, ExpeditionShop::level_block(stone_object(), 0, exit));
    shop.end_block(shop.clientB);
    shop.follow(kCharacterB);
    check(b.space == world::kMainSpace && shop.instances().counts().portalLeaves == 2, "the exit stone of the instance of B");
    shop.host.advance(std::chrono::seconds(601));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(shop.instances().instances().empty() && shop.instances().counts().emptyEnded == 2
              && shop.host.world.interactions().level_object_count() == 0 && counts.levelObjectsEnded == 4,
          "the empty instances end after the time of their mode, with the objects of their spaces (the page and the stone of each one)");
}

/** DG3D1 */
void disconnect_rule(const Options&) {
    ExpeditionShop shop;
    shop.enter();
    shop.enter_with(kCharacterA, 0);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(instance != nullptr, "setup: A in the expedition");
    if (instance == nullptr) {
        return;
    }
    const std::uint64_t id = instance->id;
    shop.host.datagram(shop.clientA.peer, shop.clientA.carrier_close());
    shop.host.steps(3, {&shop.clientB});
    const world::Instance* waiting = shop.instances().find(id);
    check(shop.entity(kCharacterA) == nullptr && shop.host.store.list_instance_members().empty() && waiting != nullptr
              && waiting->members.empty() && waiting->emptyUntil.has_value() && shop.instances().counts().departures == 1,
          "D9: the session of A ends in the instance: its exit rows; the instance waits for the time of its mode");
    // The next world entry of A: the main world at its return place; then the entrance joins the instance again.
    FakeClient again(27011);
    shop.host.enter(again, shop.a.registration());
    std::uint16_t sequence = 1;
    const auto send = [&](const std::optional<InteractionBlock>& block) {
        const world::Entity* own = shop.entity(kCharacterA);
        const state::WorldPosition place = own != nullptr ? own->position : kHere;
        InputEntry entry = report_entry(sequence++, place.x, place.y, place.z);
        entry.interaction = block;
        shop.host.datagram(again.peer, again.input_report(entry));
        shop.host.steps(3, {&again, &shop.clientB});
    };
    send(std::nullopt);
    send(std::nullopt);
    world::Entity* a = shop.entity(kCharacterA);
    check(a != nullptr && a->space == world::kMainSpace && near(a->position, kHere, 1.0F), "the next world entry: the return place");
    const std::size_t from = again.got.size();
    send(shop.entrance_block(kOpenMenuIndex));
    send(shop.entrance_block(kEnterIndex));
    send(std::nullopt);
    a = shop.entity(kCharacterA);
    check(a != nullptr && !dungeon_results(again, from).empty() && dungeon_results(again, from).back().result == 0
              && shop.instances().instance_of(kCharacterA) != nullptr && shop.instances().instance_of(kCharacterA)->id == id
              && shop.instances().counts().rejoined == 1 && a->space == waiting->space,
          "the entry at the entrance in the time of the mode joins the instance of A again");
    check(shop.entity(kCharacterB)->space == world::kMainSpace && !shop.entity(kCharacterB)->homePointsKnown,
          "B stays in the main world with no state of the instance");
    check(shop.instances().leave(kCharacterA).error == world::InstanceError::none, "setup: A leaves again");
    shop.host.advance(std::chrono::seconds(599));
    shop.host.steps(2, {&again, &shop.clientB});
    const bool kept = shop.instances().find(id) != nullptr;
    shop.host.advance(std::chrono::seconds(2));
    shop.host.steps(2, {&again, &shop.clientB});
    check(kept && shop.instances().find(id) == nullptr, "the empty instance ends at the time of its mode (600 s), not before");
}

/** DG3N1 */
void many_floors(const Options& options) {
    const auto folder = options.workDirectory / "dg3n1-navigation";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    region.ground = 5.0F;
    // Tile (4, 4) (256 m to 320 m): each cell has the terrain and 40 floors 3 m apart (50 m to 167 m): 41 floors, the floor of 110 m has
    // the index 21. A wall of the cells x 150 to 200, y 190 to 193 has no floor above 107 m: the path at 110 m goes around it.
    region.tiles[{4, 4}] = [](int x, int y, std::vector<TestFloor>& floors) {
        const bool wall = x >= 150 && x <= 200 && y >= 190 && y <= 193;
        floors[0].sightTo = 50.0F - 5.0F - 0.5F;
        for (int n = 0; n < 40; ++n) {
            if (wall && n >= 20) {
                break;
            }
            floors.push_back({50.0F + static_cast<float>(n) * 3.0F, 0, 0.0F, n + 1 < 40 ? 2.5F : -1.0F});
        }
    };
    region.write(folder);
    ServerHost host;
    check(host.navigation.set_directory(folder.string()), "setup: the navigation file of one region");
    world::Navigation& navigation = host.world.navigation();
    // The path command reads the tiles of the two places in this thread.
    const world::PathReport report = navigation.find_path(state::WorldPosition{300.125F, 300.125F, 110.0F},
                                                          state::WorldPosition{300.125F, 310.125F, 110.0F});
    const std::optional<float> floor = navigation.ground_height(state::WorldPosition{300.1F, 300.1F, 110.2F});
    const bool level = std::all_of(report.corners.begin(), report.corners.end(),
                                   [](const state::WorldPosition& corner) { return std::fabs(corner.z - 110.0F) < 0.2F; });
    std::string heights;
    for (const state::WorldPosition& corner : report.corners) {
        heights += " " + std::to_string(corner.x) + "/" + std::to_string(corner.y) + "/" + std::to_string(corner.z);
    }
    note("floor " + std::to_string(floor.value_or(-1.0F)) + "; path found " + std::to_string(report.found) + ", corners "
         + std::to_string(report.corners.size()) + heights + ", searches " + std::to_string(navigation.counts().searches) + ", cause: "
         + report.cause);
    check(floor && std::fabs(*floor - 110.0F) < 0.05F && navigation.counts().tilesMissing == 0,
          "a tile with 41 floors in a cell is kept (the limit 63): the floor of index 21 at 110 m");
    check(report.found && report.corners.size() >= 3 && level,
          "the path search on the floor of index 21 goes around the wall on that floor (the key of a search has 6 bits for the floor)");
    std::filesystem::remove_all(folder);
}

} // namespace

std::vector<Case> expedition_cases() {
    return {{"DG3E1", "expeditions DG3 (I1, I2, MP-10): the entry at the entrance of an expedition and its checks", entry_checks},
            {"DG3C1", "expeditions DG3 (I1, MP-10): the checkpoints of two instances, the respawn at a checkpoint and its cooldown", checkpoints},
            {"DG3L1", "expeditions DG3 (I1, MP-10): the lore page and the exit stone of the level set in two instances, the end", level_objects},
            {"DG3D1", "expeditions DG3 (D9, MP-10): a disconnect in an instance, the entry again, the end of the empty instance", disconnect_rule},
            {"DG3N1", "expeditions DG3 (part B): a cell with 41 floors, a path on the floor of index 21", many_floors}};
}

} // namespace soulwarden::backend::tests

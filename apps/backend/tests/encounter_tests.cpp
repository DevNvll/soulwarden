// The cases of stage QG5c2 (docs/systems/encounters.md, world/encounters.h): the encounter runner. Made-up slices, kinds, places, recipes,
// quests, and accounts. The recipe of the cases has the form of the Obelisk recipe of the game data (04A_Side02_02_ObeliskDefenders): a
// timed setup (3 s), a start object (the GatherOption of a quest gatherable), a kill stage with two members (delays 0 and 5 s), a timed
// cleanup (3 s), the success event, a 30 m detector, a grace of 10 s, and a parent respawn of 10 to 15 s.
//
//   QG5C2A  (I1) the recipe file: a valid file loads with the user-wave indexes; the loader refuses a file of a wrong format, a stage order
//           with an index that is not a stage, a spawn count above the bound, no members; the runner refuses a recipe with a cause of the
//           tool and a member with no kind of the creature data, each with its cause
//   QG5C2B  (I2, I3, I8 two participants) the run of the recipe with two sessions: the record of the root for each observer (the metadata,
//           the place, the manager, the objectives), the stage states 2133, the start object with its node state and its VariationData state
//           (type 14: only the u16 field, the variant index), the gather of A (the start
//           event to A only), the members at their delays on the world clock, the deaths, the kill counter of the root task element (index
//           3), the cleanup time, one success event for each participant, the links of field 5, the end, the respawn of a new generation
//   QG5C2C  (I8) a nearby character that does not take part (it observes the root, no link, no event), a late observer (the current states,
//           no replay), a late participant (its link, its event), a participant in its grace
//   QG5C2D  (I2) the fail and the reset: a fight with no participant fails (the members go, the links end, the parent respawn); an old
//           generation changes nothing (a late death, an old gather, an old timer)
//   QG5C2E  (I2) a store failure at the credit: the recipient stays in the ledger (no plan retry of the quest system), the retry credits it
//           one time, no second credit
//   QG5C2F  (I8) two encounters at the same time: their own generations and members; a death counts only for its own instance
//   QG5C2G  (I9) the load of the runner: 4,001 placements; a scan looks only at the cells near the characters, with a time limit
//   QG5C2H  (I7) the support manifest: the success event of a taken recipe and the event of its start object are producers; the event of a
//           refused recipe names the cause; a kill task of the category of a member of a taken recipe is supported
//   QG5C2I  (I1) the check of the encounter tool (tools/gamedata/check_encounters_tool.py): the refusal rules with made-up recipes

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using namespace encfix;
using questfix::json;

/** QG5C2A: the recipe file and the refusals of the runner. */
void encounter_file(const Options&) {
    gamedata::EncounterData data;
    check(data.load_text(encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)}).dump()) && data.recipes().size() == 1
              && data.recipes()[0].stages.size() == 4 && data.recipes()[0].userWaves == 2 && data.recipes()[0].stages[1].userIndex == 0
              && data.recipes()[0].stages[2].userIndex == 1 && data.recipes()[0].stages[0].userIndex == -1
              && data.recipes()[0].stages[2].spawns[1].delay == std::chrono::milliseconds(5000),
          "a valid file loads: 4 stages, 2 user waves (the interaction 0, the kill 1), the spawn delay of 5 s");
    json wrong = encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    wrong["format"] = 2;
    json order = encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    order["recipes"][0]["stageOrder"] = {0, 1, 7};
    json count = encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    count["recipes"][0]["stages"][2]["spawns"][0]["count"] = gamedata::EncounterData::kMaxCount + 1;
    json noMembers = encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    noMembers.erase("members");
    for (const auto& [name, document] : {std::pair{"a wrong format", wrong}, std::pair{"a stage order with an index that is not a stage", order},
                                         std::pair{"a spawn count above the bound", count}, std::pair{"no members", noMembers}}) {
        gamedata::EncounterData broken;
        check(!broken.load_text(document.dump()) && !broken.loaded() && broken.recipes().empty() && !broken.error().empty(),
              std::string("the loader refuses a file with ") + name + " (" + broken.error() + ")");
    }
    // The runner: a recipe of the tool with a cause stays refused; a member with no kind of the creature data is refused with its cause.
    json noKind = recipe_json("e4c00000-0000-4000-8000-000000000002/17", {400.0F, 205.0F, 30.0F}, {400.0F, 201.0F, 30.0F}, {403.0F, 206.0F, 30.0F});
    noKind["stages"][2]["spawns"][0]["member"] = "slices/test/hidden.dynamicslice";
    json members = members_json();
    members["slices/test/hidden.dynamicslice"] = {{"kind", "creature"}, {"vitals", "Test_Hidden"}, {"body", "slices/test/hidden_body.dynamicslice"}};
    json document = encounter_document({recipe_json(kKey, kRootAt, kStartAt, kMemberAt),
                                        recipe_json("e4c00000-0000-4000-8000-000000000003/17", {700.0F, 205.0F, 30.0F}, {700.0F, 201.0F, 30.0F},
                                                    {703.0F, 206.0F, 30.0F}, {"an ambush: stage QG5c3"}),
                                        noKind});
    document["members"] = members;
    EncounterShop shop({});
    check(shop.host.encounters.load_text(document.dump()), "control: the file of three recipes loads");
    shop.host.creatures.set_members({{kMemberSlice, {1, ""}}, {"slices/test/hidden.dynamicslice", {-1, "the client does not show its slice"}}});
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    // Stage QG5c3 (finding QG5C2R-R3): the third recipe also has the cause of its count (the kill stage needs 2 deaths; the runner makes 1).
    check(placements.size() == 3 && placements[0].refused.empty() && placements[1].refused == std::vector<std::string>{"an ambush: stage QG5c3"}
              && !placements[2].refused.empty()
              && placements[2].refused[0] == "the creature member hidden.dynamicslice: the client does not show its slice"
              && shop.encounters().counts().taken == 1,
          "the runner takes the first recipe; it refuses the recipe with the cause of the tool and the member with no kind (its cause)");
}

/** QG5C2B: the run with two participants. */
void encounter_run(const Options&) {
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    begin(shop);
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->generation == 1 && run->phase == world::EncounterPhase::running && run->participants.size() == 2,
          "the instance of the placement near the characters runs (generation 1), with A and B as participants (5 m from the root)");
    if (run == nullptr) {
        return;
    }
    const world::EntityId root = run->root;
    const RootSeen firstA = root_seen(shop.clientA);
    const RootSeen firstB = root_seen(shop.clientB);
    for (const RootSeen* view : {&firstA, &firstB}) {
        check(view->interest != 0 && has(*view, 0, kTypeMetadata) && has(*view, kPlaceSlot, kTypePlace) && has(*view, kManagerSlot, kTypeManager)
                  && has(*view, kObjectivesSlot, kTypeObjectives) && has(*view, kSetupSlot, kTypeStage),
              std::string("the first record of the root for ") + (view == &firstA ? "A" : "B")
                  + ": the metadata, the place (13 at slot 3), the manager (6786 at slot 2), the objectives (3857 at slot 4), the setup stage");
        check(!view->managers.empty() && view->managers.back() == world::encounter_rules::kInProgress
                  && statuses(*view, kSetupSlot) == std::vector<std::uint32_t>{2},
              "the manager is InProgress (2) and the setup stage has the status 2");
    }
    {
        // The admin readers (the routes of the tool window): the encounters near A in the state of A, and the recipe catalog; no token: 401.
        server::http::AdminAccess access;
        server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
        server::http::Router router;
        check(access.add(kAdminToken, kSteamIdA), "setup: the admin token of the account of A");
        server::http::register_admin_routes(router, services, access);
        const auto send = [&router](const std::string& path, const std::string& query, const char* token) {
            service::HttpRequest request;
            request.method = "GET";
            request.path = path;
            request.query = query;
            request.target = query.empty() ? path : path + "?" + query;
            if (token != nullptr) {
                request.headers.emplace_back(server::http::kAdminTokenHeader, token);
            }
            return server::http::handle_request(router, request);
        };
        const service::HttpResponse stateReply = send("/admin/v1/state", std::string("character=") + kCharacterA, kAdminToken);
        const json state = json::parse(stateReply.body, nullptr, false);
        const service::HttpResponse catalog = send("/admin/v1/catalog/encounters", {}, kAdminToken);
        const json recipes = json::parse(catalog.body, nullptr, false);
        check(stateReply.status == 200 && state.contains("encounters") && state["encounters"].dump().find(kKey) != std::string::npos,
              "the admin state of A has the member encounters with the instance near A (its key)");
        check(catalog.status == 200 && recipes.contains("recipes") && recipes["recipes"].size() == 1 && recipes["taken"] == 1
                  && send("/admin/v1/catalog/encounters", {}, nullptr).status == 401,
              "the admin catalog of the encounters has the one recipe (taken); with no token the reply is 401");
    }
    const auto linksA = links_seen(shop.clientA);
    const auto linksB = links_seen(shop.clientB);
    check(!linksA.empty() && linksA.back() == std::vector<std::uint64_t>{root} && !linksB.empty() && linksB.back() == std::vector<std::uint64_t>{root},
          "the player Objectives state of A and of B has the link of the root (field 5: G of the root)");
    // The setup ends at 3 s (the world clock): the interaction stage makes the start object.
    shop.wait(3.2F);
    const std::vector<world::Entity*> objects = shop.members(true);
    check(objects.size() == 1 && objects.front()->object.nodeSlot == kNodeSlot && objects.front()->object.nodeState == kHealthy
              && world::horizontal_distance(objects.front()->position, kStartAt) < 0.01F,
          "the setup ends after its 3 s: the start object is at its spawn location with the node state Healthy");
    for (const FakeClient* client : {&shop.clientA, &shop.clientB}) {
        const auto variation = variation_seen(*client);
        check(variation && !variation->contains({0, 0}) && variation->contains({0, 1})
                  && variation->at({0, 1}) == tests::Bytes{0x02, 0xDD},
              std::string("the record of the start object for ") + (client == &shop.clientA ? "A" : "B")
                  + " has its VariationData state (type 14 at the slot of the slice): only field 1, the u16 variant index 733 (02 dd)");
    }
    const RootSeen setupA = root_seen(shop.clientA);
    check(statuses(setupA, kSetupSlot) == std::vector<std::uint32_t>{2, 3} && statuses(setupA, kInteractSlot) == std::vector<std::uint32_t>{2},
          "the stage states: the setup 2 then 3, the interaction 2 (one history for each stage, increasing sequences)");
    const auto interactLeaf = root_task(setupA, 1);
    check(interactLeaf && interactLeaf->first == 0 && interactLeaf->second == world::quest_state::kActive && root_task(setupA, 5).has_value()
              && !root_task(setupA, 3).has_value(),
          "the root task elements: the interaction leaf (index 1) Active, the outer container (index 5), no kill leaf yet");
    if (objects.size() != 1) {
        return;
    }
    // A gathers the start object (1 s): the start event goes to A only; the object goes; the kill stage makes its first member.
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(0.3F);
    check(shop.progress(kCharacterA, "Test_Examine") == 1 && shop.progress(kCharacterB, "Test_Examine") == 0,
          "the event of the start object goes to its gatherer A only (B has the same task and gets nothing)");
    check(shop.members(true).empty() && shop.members(false).size() == 1 && shop.encounters().counts().objectsGathered == 1,
          "the start object goes, and the kill stage makes its first member (delay 0)");
    shop.wait(3.8F);
    check(shop.members(false).size() == 1, "the second member is not there before its delay of 5 s");
    shop.wait(0.8F);
    std::vector<world::Entity*> members = shop.members(false);
    check(members.size() == 2 && members.front()->creature.encounter && members.front()->creature.encounter->second == 1
              && !members.front()->creature.spawnPoint,
          "the second member comes at 5 s of the world clock; a member has its placement and generation and no spawn point");
    if (members.size() != 2) {
        return;
    }
    shop.kill(*members[0], kCharacterA);
    shop.wait(0.2F);
    const RootSeen oneA = root_seen(shop.clientA);
    const auto oneLeaf = root_task(oneA, 3);
    check(oneLeaf && oneLeaf->first == 1 && oneLeaf->second == world::quest_state::kActive && root_task(oneA, 1)
              && root_task(oneA, 1)->second == world::quest_state::kComplete,
          "one death: the kill leaf (generated index 3) has the progress 1 (the N of 1 / 2); the interaction leaf is Complete");
    shop.kill(*members[1], kCharacterA);
    shop.wait(0.2F);
    const RootSeen twoA = root_seen(shop.clientA);
    check(statuses(twoA, kKillSlot) == std::vector<std::uint32_t>{2, 3} && statuses(twoA, kCleanupSlot) == std::vector<std::uint32_t>{2}
              && root_task(twoA, 3) && root_task(twoA, 3)->first == 2,
          "two deaths: the kill stage ends (2, 3) with the progress 2; the cleanup starts");
    check(shop.progress(kCharacterA, "Test_Defend") == 0 && shop.progress(kCharacterB, "Test_Defend") == 0,
          "no success event before the end of the cleanup (3 s)");
    const std::size_t beforeA = shop.clientA.got.size();
    shop.wait(3.3F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.progress(kCharacterB, "Test_Defend") == 1
              && shop.encounters().counts().credits == 2 && shop.encounters().counts().succeeded == 1,
          "the success: one event for each participant (A and B: the task of each one 1 / 1)");
    const RootSeen endA = root_seen(shop.clientA);
    const auto endLinks = links_seen(shop.clientA, beforeA);
    check(endA.stops == 1 && shop.host.world.find(root) == nullptr && shop.instance() == nullptr && !endLinks.empty() && endLinks.back().empty()
              && a.encounterLinks.empty() && b.encounterLinks.empty(),
          "the end: the root goes (the stop for A), the members go, the links of A and B end (an empty field 5)");
    // The placement makes a new instance after its respawn (10 to 15 s); no second credit.
    shop.wait(9.0F);
    check(shop.instance() == nullptr, "no new instance before the respawn of the parent (10 s at least)");
    shop.wait(7.0F);
    const world::EncounterInstance* next = shop.instance();
    check(next != nullptr && next->generation == 2 && next->root != root && shop.progress(kCharacterA, "Test_Defend") == 1,
          "after the respawn of the parent a new instance runs (generation 2, a new root); no second credit");
}

/** QG5C2C: a nearby character that does not take part, a late observer, a late participant, the grace. */
void encounter_observers(const Options&) {
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    // B enters 50 m from the root: it observes the root (100 m) and is no participant (the sphere has 30 m).
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 255.0F, 30.0F}), "control: B is 50 m from the root");
    shop.placeB = {100.0F, 255.0F, 30.0F};
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    shop.wait(0.5F);
    (void)shop.host.world.quests().grant(*shop.entity(kCharacterA), "Test_Defend", "command", 0);
    (void)shop.host.world.quests().grant(*shop.entity(kCharacterB), "Test_Defend", "command", 0);
    shop.wait(3.2F);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.size() == 1 && run->participants.count(kCharacterA) == 1,
          "A (5 m) is a participant; B (50 m) is not");
    check(root_seen(shop.clientB).interest != 0 && links_seen(shop.clientB).empty() && shop.entity(kCharacterB)->encounterLinks.empty(),
          "B observes the root (its record) and has no link (no field 5)");
    const std::vector<world::Entity*> objects = shop.members(true);
    if (run == nullptr || objects.size() != 1) {
        check(false, "control: the start object");
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(5.6F);
    check(shop.members(false).size() == 2, "control: two members");
    // A late observer: B goes away (out of the interest) and comes back in the kill stage.
    shop.move(kCharacterB, {100.0F, 400.0F, 30.0F});
    shop.wait(1.0F);
    const std::size_t from = shop.clientB.got.size();
    shop.move(kCharacterB, {100.0F, 255.0F, 30.0F});
    shop.wait(1.0F);
    const RootSeen late = root_seen(shop.clientB, from);
    check(late.interest != 0 && statuses(late, kSetupSlot) == std::vector<std::uint32_t>{2, 3}
              && statuses(late, kInteractSlot) == std::vector<std::uint32_t>{2, 3} && statuses(late, kKillSlot) == std::vector<std::uint32_t>{2}
              && statuses(late, kCleanupSlot).empty() && !late.managers.empty() && late.managers.back() == world::encounter_rules::kInProgress,
          "a late observer gets the current states: the history of each started stage, the manager InProgress, no stage that did not start");
    check(shop.instance()->participants.count(kCharacterB) == 0 && shop.progress(kCharacterB, "Test_Defend") == 0,
          "B near the fight (50 m) is no participant and has no event");
    // B comes into the sphere: a late participant with its link.
    shop.move(kCharacterB, {101.0F, 210.0F, 30.0F});
    shop.wait(0.6F);
    check(shop.instance()->participants.count(kCharacterB) == 1 && shop.entity(kCharacterB)->encounterLinks.size() == 1,
          "a late participant: B in the sphere is a participant with its link");
    // A goes out of the sphere for less than its grace (10 s) and comes back: A stays a participant.
    shop.move(kCharacterA, {100.0F, 245.0F, 30.0F});
    shop.wait(4.0F);
    check(shop.instance()->participants.count(kCharacterA) == 1 && shop.instance()->participants.at(kCharacterA).graceUntil.has_value(),
          "A out of the sphere for 4 s: still a participant in its grace");
    shop.move(kCharacterA, {100.0F, 200.0F, 30.0F});
    shop.wait(0.6F);
    check(shop.instance()->participants.count(kCharacterA) == 1 && !shop.instance()->participants.at(kCharacterA).graceUntil.has_value(),
          "A back in the sphere: its grace ends");
    for (world::Entity* member : shop.members(false)) {
        shop.kill(*member, kCharacterA);
    }
    shop.wait(3.5F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.progress(kCharacterB, "Test_Defend") == 1,
          "the late participant B gets the event at the success, as A");
}

/** QG5C2D: the fail, the reset, the old generation. */
void encounter_reset(const Options&) {
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    begin(shop, true, false);
    shop.move(kCharacterB, {100.0F, 600.0F, 30.0F});
    shop.wait(3.2F);
    const std::vector<world::Entity*> objects = shop.members(true);
    if (objects.size() != 1) {
        check(false, "control: the start object");
        return;
    }
    const world::EntityId objectId = objects.front()->id;
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(0.3F);
    std::vector<world::Entity*> members = shop.members(false);
    check(members.size() == 1, "control: the first member");
    if (members.empty()) {
        return;
    }
    // A copy of the member of generation 1 for an old death later.
    const world::Entity oldMember = *members.front();
    const world::EntityId root = shop.instance()->root;
    // A goes away (60 m: no participant after its grace of 10 s), with a member alive: the encounter fails.
    shop.move(kCharacterA, {100.0F, 265.0F, 30.0F});
    shop.wait(8.0F);
    check(shop.instance() != nullptr, "in the grace of A the encounter runs");
    shop.wait(3.0F);
    check(shop.instance() == nullptr && shop.host.world.find(root) == nullptr && shop.host.world.find(oldMember.id) == nullptr
              && shop.encounters().counts().failed == 1 && shop.entity(kCharacterA)->encounterLinks.empty(),
          "a fight with no participant fails: the root and its members go, the link of A ends");
    const auto index = shop.encounters().placement_of(kKey);
    const world::EncounterPlacement& placement = shop.encounters().placements()[*index];
    check(placement.respawnAt.has_value() && !placement.live, "the placement waits the respawn of its parent");
    // An old death: the member of generation 1 dies after the reset.
    const std::uint64_t old = shop.encounters().counts().oldGeneration;
    shop.encounters().member_died(oldMember);
    shop.wait(0.2F);
    check(shop.encounters().counts().oldGeneration == old + 1 && shop.progress(kCharacterA, "Test_Defend") == 0,
          "a late death of generation 1 changes nothing (counted as an old generation)");
    // An old gather: the start object of generation 1 (it went away with its gather).
    shop.encounters().object_gathered(objectId, *shop.entity(kCharacterA));
    check(shop.encounters().counts().oldGeneration == old + 2, "an old gather (the object of an instance that ended) changes nothing");
    // An old timer: a new instance (generation 2) ends by a command in its setup; its setup timer comes later and changes nothing.
    shop.move(kCharacterA, {100.0F, 200.0F, 30.0F});
    for (int step = 0; step < 200 && shop.instance() == nullptr; ++step) {
        shop.wait(0.1F);
    }
    const world::EncounterInstance* second = shop.instance();
    check(second != nullptr && second->generation == 2, "control: the new instance of generation 2");
    shop.wait(1.0F);
    check(shop.encounters().reset(kKey), "the command ends the instance in its setup");
    const std::uint64_t timers = shop.encounters().counts().oldGeneration;
    shop.wait(0.6F);
    const world::EncounterInstance* third = shop.instance();
    const bool thirdRuns = third != nullptr && third->generation == 3;
    shop.wait(2.0F);
    check(thirdRuns && shop.encounters().counts().oldGeneration > timers && shop.instance() != nullptr && shop.instance()->generation == 3
              && shop.members(true).empty(),
          "the setup timer of generation 2 comes later and changes nothing: the instance of generation 3 has no start object yet");
}

/** QG5C2E: a store failure at the credit and its retry. */
void encounter_store(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "encounter-store.sqlite3";
    std::filesystem::remove(path);
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)}, path.string());
    begin(shop, true, false);
    shop.move(kCharacterB, {100.0F, 600.0F, 30.0F});
    const auto sql = [&](const char* text) {
        sqlite3* connection = nullptr;
        const bool opened = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK;
        const bool ok = opened && sqlite3_exec(connection, text, nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(connection);
        return ok;
    };
    shop.wait(3.2F);
    const std::vector<world::Entity*> objects = shop.members(true);
    if (objects.size() != 1) {
        check(false, "control: the start object");
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(5.6F);
    std::vector<world::Entity*> members = shop.members(false);
    check(members.size() == 2, "control: two members");
    for (world::Entity* member : members) {
        shop.kill(*member, kCharacterA);
    }
    check(sql("CREATE TRIGGER e5i BEFORE INSERT ON character_quest_tasks BEGIN SELECT RAISE(FAIL, 'e5'); END")
              && sql("CREATE TRIGGER e5u BEFORE UPDATE ON character_quest_tasks BEGIN SELECT RAISE(FAIL, 'e5'); END"),
          "control: the store refuses each row of the quest tasks");
    const std::uint64_t retries = shop.host.world.quests().counts().eventRetries;
    shop.wait(3.5F);
    check(shop.progress(kCharacterA, "Test_Defend") == 0 && shop.encounters().payouts().size() == 1
              && shop.encounters().payouts()[0].pending == std::vector<std::string>{kCharacterA} && shop.encounters().counts().creditRetries == 1
              && shop.host.world.quests().counts().eventRetries == retries,
          "a store failure at the success: A is not paid (its task 0), it stays in the ledger, and the quest system plans no retry of its own");
    check(sql("DROP TRIGGER e5i") && sql("DROP TRIGGER e5u"), "control: the store takes the rows again");
    shop.wait(2.5F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.encounters().payouts()[0].pending.empty() && shop.encounters().counts().credits == 1,
          "the retry of the ledger (2 s later) credits A one time");
    shop.wait(4.0F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.encounters().counts().credits == 1, "no second credit");
}

/** QG5C2F: two encounters at the same time. */
void encounter_two(const Options&) {
    const char* second = "e4c00000-0000-4000-8000-000000000009/17";
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt),
                        recipe_json(second, {100.0F, 150.0F, 30.0F}, {101.0F, 149.0F, 30.0F}, {103.0F, 151.0F, 30.0F})});
    begin(shop);
    const world::EncounterInstance* one = shop.instance(kKey);
    const world::EncounterInstance* two = shop.instance(second);
    check(one != nullptr && two != nullptr && one->root != two->root && one->generation == 1 && two->generation == 1
              && one->participants.size() == 2 && two->participants.empty(),
          "two instances run at one time (their own roots and generations); the characters are in the sphere of the first only");
    if (one == nullptr || two == nullptr) {
        return;
    }
    // B goes to the second encounter: after its grace it is a participant of the second only.
    shop.move(kCharacterB, {100.0F, 152.0F, 30.0F});
    shop.placeB = {100.0F, 152.0F, 30.0F};
    shop.wait(11.0F);
    check(shop.instance(kKey)->participants.count(kCharacterB) == 0 && shop.instance(second)->participants.count(kCharacterB) == 1
              && shop.entity(kCharacterB)->encounterLinks == std::vector<world::EntityId>{shop.instance(second)->root},
          "B is a participant of the second encounter only, with its link");
    const std::vector<world::Entity*> objectsOne = shop.members(true, kKey);
    const std::vector<world::Entity*> objectsTwo = shop.members(true, second);
    check(objectsOne.size() == 1 && objectsTwo.size() == 1, "control: each has its start object");
    if (objectsOne.size() != 1 || objectsTwo.size() != 1) {
        return;
    }
    shop.move(kCharacterB, {101.0F, 150.0F, 30.0F});
    shop.gather(shop.clientA, EncounterShop::start_block(*objectsOne.front()), 1500);
    shop.gather(shop.clientB, EncounterShop::start_block(*objectsTwo.front()), 1500);
    shop.wait(0.3F);
    const std::vector<world::Entity*> membersOne = shop.members(false, kKey);
    const std::vector<world::Entity*> membersTwo = shop.members(false, second);
    check(membersOne.size() == 1 && membersTwo.size() == 1, "control: each has its first member");
    if (membersOne.empty() || membersTwo.empty()) {
        return;
    }
    shop.kill(*membersOne.front(), kCharacterA);
    shop.wait(0.2F);
    constexpr std::uint32_t kKillStage = 2;
    check(shop.instance(kKey)->stages[kKillStage].destroyed == 1 && shop.instance(second)->stages[kKillStage].destroyed == 0,
          "a death counts only for the instance of its member");
}

/** QG5C2G: the load of the runner. */
void encounter_load(const Options&) {
    std::vector<json> recipes = {recipe_json(kKey, kRootAt, kStartAt, kMemberAt)};
    for (int index = 0; index < 4000; ++index) {
        const float x = 2000.0F + static_cast<float>(index % 80) * 150.0F;
        const float y = 2000.0F + static_cast<float>(index / 80) * 150.0F;
        char key[64];
        std::snprintf(key, sizeof key, "e4c01000-0000-4000-8000-%012d/17", index);
        recipes.push_back(recipe_json(key, {x, y, 30.0F}, {x, y - 4.0F, 30.0F}, {x + 3.0F, y + 1.0F, 30.0F}));
    }
    EncounterShop shop(recipes);
    begin(shop);
    const std::uint64_t scans = shop.encounters().counts().scans;
    const std::uint64_t looked = shop.encounters().counts().placementsLooked;
    const auto started = std::chrono::steady_clock::now();
    shop.wait(20.0F);
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    const std::uint64_t scanCount = shop.encounters().counts().scans - scans;
    const std::uint64_t perScan = scanCount > 0 ? (shop.encounters().counts().placementsLooked - looked) / scanCount : 0;
    check(shop.encounters().counts().taken == 4001 && scanCount >= 35 && perScan <= 4 && shop.encounters().instances().size() == 1,
          "4,001 placements: each scan looks only at the placements of the cells near the two characters (" + std::to_string(perScan)
              + " of 4,001 a scan, " + std::to_string(scanCount) + " scans); one instance runs");
    const double longest = std::chrono::duration<double, std::milli>(shop.encounters().scan_load().largest).count();
    check(shop.encounters().scan_load().count > 0 && longest < 20.0,
          "the longest scan of the runner is below 20 ms (" + std::to_string(longest) + " ms; 20 s of the world in " + std::to_string(spent.count())
              + " ms)");
}

/** QG5C2H: the support manifest. */
void encounter_manifest(const Options&) {
    json refused = recipe_json("e4c00000-0000-4000-8000-000000000004/17", {900.0F, 205.0F, 30.0F}, {900.0F, 201.0F, 30.0F},
                               {903.0F, 206.0F, 30.0F}, {"an ambush: stage QG5c3"});
    refused["successEvent"] = "Test_Refused_Success";
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt), refused});
    shop.enter();
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const auto row = [&](const char* id) -> json {
        for (const json& entry : manifest["objectives"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        return json::object();
    };
    const json defend = row("Test_Defend");
    const json examine = row("Test_Examine");
    const json hunt = row("Test_Member_Hunt");
    const json other = row("Test_Refused_Defend");
    check(!defend.empty() && defend["tasks"][0]["supported"] == true && !examine.empty() && examine["tasks"][0]["supported"] == true,
          "the success event of a taken recipe and the event of its start object are producers (supported tasks)");
    check(!hunt.empty() && hunt["tasks"][0]["supported"] == true, "a kill task of the category of a member of a taken recipe is supported");
    check(!other.empty() && other["tasks"][0]["supported"] == false
              && other["tasks"][0]["reason"].get<std::string>().find("is refused: an ambush: stage QG5c3") != std::string::npos,
          "the event of a refused recipe stays blocked, and its reason names the cause of the refusal");
}

/** QG5C2I: the check of the encounter tool (tools/gamedata/check_encounters_tool.py: the refusal rules with made-up recipes). */
void encounter_tool(const Options& options) {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const std::filesystem::path log = options.workDirectory / "check_encounters_tool.py.log";
    const std::filesystem::path folder = options.workDirectory / "check_encounters_tool.py.work";
    std::filesystem::create_directories(folder);
    const std::string command = "python3 -B " + (root / "tools/gamedata/check_encounters_tool.py").string() + " " + folder.string() + " > "
                                + log.string() + " 2>&1";
    check(std::system(command.c_str()) == 0, "the check of the encounter tool: status 0 (log: work folder " + log.filename().string() + ")");
}

} // namespace

std::vector<Case> encounter_cases() {
    return {{"QG5C2A", "encounters QG5c2 (I1): the recipe file and its refusals; the refusals of the runner with their causes", encounter_file},
            {"QG5C2B",
             "encounters QG5c2 (I2, I3, I8): the run with two participants: the records, the stages, the start object, the members at their "
             "delays, the kill counter, the success event of each participant, the links, the respawn",
             encounter_run},
            {"QG5C2C", "encounters QG5c2 (I8): a nearby non-participant, a late observer, a late participant, the grace", encounter_observers},
            {"QG5C2D", "encounters QG5c2 (I2): the fail of a fight with no participant, the old generation (a death, a gather, a timer)",
             encounter_reset},
            {"QG5C2E", "encounters QG5c2 (I2): a store failure at the credit, the retry of the ledger, no second credit", encounter_store},
            {"QG5C2F", "encounters QG5c2 (I8): two encounters at the same time", encounter_two},
            {"QG5C2G", "encounters QG5c2 (I9): 4,001 placements; a scan looks only at the cells near the characters (time limit)", encounter_load},
            {"QG5C2H", "encounters QG5c2 (I7): the support manifest takes the events and the members of the taken recipes", encounter_manifest},
            {"QG5C2I", "encounters QG5c2 (I1): the check of the encounter tool (check_encounters_tool.py: the refusal rules)", encounter_tool}};
}

} // namespace soulwarden::backend::tests

// The cases of the code review of stage QG6a (evidence/quests/qg6a/code-review/FINDINGS.md), corrected in stage QG6b. Each case fails on the
// tree of stage QG6a (trees/after-qg6a) and passes now. Made-up ids, names, keys, and places only (the fixture of the mission board cases).
//
//   QG6AR1 finding QG6AR-R1: the offer filter takes the level of the screen (the client getter 0x3CEC5C0 adds 1 to the raw level)
//   QG6AR2 finding QG6AR-R2: a refused hand-in (a store failure of its town entry row) keeps the return task pending, in the entity and in
//          the store; a retry after the failure pays
//   QG6AR3 finding QG6AR-R3: a reset (5600, and a TaskDie with ResetOnCondition) keeps the mission, the provider, the destination, and the
//          community flag of the instance
//   QG6AR4 finding QG6AR-R4: the loader refuses town buckets with members of another goal, a member in two buckets, and a town file with no
//          town buckets
//   QG6AR5 finding QG6AR-R5: 3819 and 2461 on the actor route of another character change nothing
//   QG6AR6 finding QG6AR-R6: the mission board diagnostic of the admin path needs the harness gate

#include <cstdlib>
#include <optional>
#include <string>

#include "server/http/admin_access.h"
#include "server/http/character_commands.h"
#include "server/http/request_handler.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/mission_fixture.h"
#include "world/progress.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;
using namespace missionfix;
using nlohmann::json;

/** QG6AR1: the level of the offer filter. */
void offer_level(const Options&) {
    Boards shop;
    json document = mission_document();
    document["missions"][1]["cells"]["MinLevel"] = "5";
    check(shop.host.missions.load_text(document.dump()), "setup: the boar mission needs the level 5");
    shop.host.quests.bind_missions(shop.host.missions);
    shop.start();
    check(shop.host.world.progress().set_level(kCharacterA, 5).ok(), "setup: A is at the level 5 of the screen");
    check(shop.seek([&] { return reference_index(crc(kProject), reference_seed(kCharacterA, 6, shop.server_ns()), 2) == 1; }),
          "setup: a seed window in which the client (two Kill candidates) selects the boar");
    const world::Entity* a = shop.entity(kCharacterA);
    const world::TownOffers offers = shop.missions().offers_of(*a, 6);
    check(!offers.offers.empty() && offers.offers[0].candidates == 2 && offers.offers[0].mission == crc(kBoar),
          "the offer filter of the backend has the two candidates of the client at the level 5 (MinLevel 5 passes the level of the screen)");
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kBoar));
    check(shop.mission_of(kCharacterA, crc(kBoar)) != nullptr, "3819 accepts the boar that the client shows at its MinLevel");
}

/** QG6AR2: a refused hand-in is one transaction with its return task. */
void atomic_hand_in(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "review-qg6a-return.db";
    std::filesystem::remove(path);
    Boards shop(path.string());
    const std::uint64_t instance = accepted(shop);
    shop.kill(kCharacterA, "wolf", 5);
    world::Entity* a = shop.entity(kCharacterA);
    const std::uint32_t experience = a->progression.experience;
    const std::uint64_t coin = a->wallet.coin;
    const std::vector<world::QuestTaskState> before = shop.mission_of(kCharacterA, crc(kWolf))->tasks;
    check(before[1].state == world::quest_state::kActive && before[2].progress == 1, "setup: the return task is pending (the container has 1 of 2)");
    check(run_sql(path, "CREATE TRIGGER review_no_offer BEFORE INSERT ON character_mission_offers BEGIN SELECT RAISE(ABORT, 'review'); END"),
          "setup: the store refuses the town entry row");
    shop.open(shop.clientA);
    const world::MissionResult refused = shop.missions().complete(*a, instance);
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kWolf));
    const std::vector<state::QuestRow> rows = shop.host.store.list_quests(kCharacterA);
    check(refused.error == world::MissionError::storeFailed && a->progression.experience == experience && a->wallet.coin == coin,
          "the hand-in fails at its town entry row and pays nothing");
    check(quest != nullptr && quest->tasks[1].state == before[1].state && quest->tasks[2].progress == before[2].progress && rows.size() == 1
              && rows[0].tasks[1].state == before[1].state,
          "the return task stays pending in the entity and in the store (the client still offers the completion)");
    const std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    const auto* back = state ? task_of(*state, instance, 1) : nullptr;
    check(back != nullptr && back->state == world::quest_state::kActive, "the client has the pending return task");
    check(run_sql(path, "DROP TRIGGER review_no_offer"), "setup: the store takes the row again");
    check(shop.missions().complete(*a, instance).ok() && a->progression.experience - experience == 110,
          "a hand-in after the failure completes the return and pays one time");
    check(shop.entity(kCharacterB)->quests.active.empty(), "B does not change");
}

/** QG6AR3: a reset keeps the mission of the instance. */
void reset_keeps_mission(const Options&) {
    Boards shop;
    const std::uint64_t instance = accepted(shop);
    shop.kill(kCharacterA, "wolf", 2);
    world::Entity* a = shop.entity(kCharacterA);
    const std::array<std::uint8_t, 16> destination = shop.mission_of(kCharacterA, crc(kWolf))->destination;
    server::hub::Writer body;
    body.u64(0);
    body.u64(a->id);
    body.u64(instance);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeResetObjective, a->actor.bytes, body.data()));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(a->quests.active.size() == 1 && a->quests.active[0].instance != instance, "setup: 5600 made a new instance");
    const world::QuestInstance& reset = a->quests.active[0];
    check(reset.mission == crc(kWolf) && reset.communityGoal && reset.provider == kBoardKey && reset.destination == destination,
          "the new instance has the mission, the provider, the destination, and the community flag of the old one");
    const std::vector<state::QuestRow> rows = shop.host.store.list_quests(kCharacterA);
    check(rows.size() == 1 && rows[0].mission == crc(kWolf) && rows[0].communityGoal && rows[0].provider == kBoardKey,
          "the store has the mission of the new instance");
    const std::optional<Fragment> fragment = last_objectives_fragment(shop.clientA);
    const std::vector<MissionElement> elements = fragment ? mission_elements(*fragment) : std::vector<MissionElement>{};
    check(elements.size() == 1 && elements[0].mission == crc(kWolf) && elements[0].community && elements[0].destination == destination,
          "the client gets the mission element of the new instance");
    shop.kill(kCharacterA, "wolf", 5);
    shop.open(shop.clientA);
    const std::uint32_t experience = a->progression.experience;
    shop.hand_in(shop.clientA, kCharacterA, a->quests.active[0].instance);
    check(a->quests.active.empty() && a->progression.experience - experience == 110,
          "the new instance has the kill override of the mission (5 wolves) and its hand-in at the board pays the event of the row");
    check(shop.entity(kCharacterB)->quests.active.empty(), "B does not change");
}

/** QG6AR3 (the death): a TaskDie with ResetOnCondition keeps the mission of the instance too. */
void death_reset_keeps_mission(const Options&) {
    Boards shop;
    json quests = mission_quests();
    quests["tasks"]["test_die_reset"] = questfix::task("Test_Die_Reset", "taskdie", 65, {{"ResetOnCondition", "1"}});
    quests["tasks"]["test_die_first"] = questfix::task("Test_Die_First", "simpletaskcontainer", 0);
    quests["tasks"]["test_die_container"] = questfix::task("Test_Die_Container", "consecutivetaskcontainer", 1);
    quests["objectives"].push_back({{"id", "Test_CM_DieReset"},
                                    {"cells", {{"Type", "CommunityGoal"}, {"CanBeAbandoned", "1"}}},
                                    {"tree", json::array({json::array({0, "test_m_kill", json::array()}),
                                                          json::array({1, "test_die_reset", json::array()}),
                                                          json::array({2, "test_die_first", json::array({0, 1})}),
                                                          json::array({3, "test_cm_return", json::array()}),
                                                          json::array({4, "test_die_container", json::array({2, 3})})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: the quest data has a mission template with a TaskDie that resets");
    json document = mission_document();
    json cells = document["missions"][0]["cells"];
    document["missions"].push_back(mission_row("TestProject_1_DieWolf_3", 3, "Test_CM_DieReset", cells));
    document["missions"].back()["cells"]["TaskKillContributionQtyOverride"] = "3";
    document["townBuckets"][0]["missions"].push_back(crc("TestProject_1_DieWolf_3"));
    check(shop.host.missions.load_text(document.dump()), "setup: the mission data has the row " + shop.host.missions.error());
    shop.host.quests.bind_missions(shop.host.missions);
    shop.start();
    world::Entity* a = shop.entity(kCharacterA);
    const gamedata::MissionBinding* binding = shop.host.quests.mission(crc("TestProject_1_DieWolf_3"));
    world::MissionGrant grant{crc("TestProject_1_DieWolf_3"), true, kBoardKey, shop.board()->object.providerIdentity};
    const world::QuestResult granted = binding != nullptr ? shop.host.world.quests().grant_mission(*a, *binding, 6, grant) : world::QuestResult{};
    check(granted.ok(), "setup: A has the mission with the TaskDie");
    shop.host.world.quests().died(*a);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(a->quests.active.size() == 1 && a->quests.active[0].instance != granted.instance, "setup: the death reset the instance");
    const world::QuestInstance& reset = a->quests.active[0];
    check(reset.mission == crc("TestProject_1_DieWolf_3") && reset.communityGoal && reset.provider == kBoardKey
              && reset.destination == shop.board()->object.providerIdentity,
          "the instance of the death reset keeps the mission, the provider, the destination, and the community flag");
}

/** QG6AR4: the town buckets agree with the mission rows. */
void bucket_validation(const Options&) {
    gamedata::MissionData data;
    json swapped = mission_document();
    swapped["townBuckets"][0]["missions"] = json::array({crc(kPoi)});
    swapped["townBuckets"][1]["missions"] = json::array({crc(kWolf), crc(kBoar)});
    check(!data.load_text(swapped.dump()) && !data.loaded(), "a Kill bucket with a Gather row and a Gather bucket with Kill rows: refused");
    json twice = mission_document();
    twice["townBuckets"][0]["missions"] = json::array({crc(kWolf), crc(kWolf), crc(kBoar)});
    check(!data.load_text(twice.dump()), "a mission two times in a bucket: refused");
    json missing = mission_document();
    missing.erase("townBuckets");
    check(!data.load_text(missing.dump()), "a file with town missions and no town buckets: refused");
    check(data.load_text(mission_document().dump()), "control: the file of the fixture loads: " + data.error());
}

/** QG6AR5: the board requests on the actor route of another character. */
void foreign_route(const Options&) {
    {
        Boards shop;
        shop.start();
        check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf); }), "setup: A has the wolf offer");
        shop.open(shop.clientA);
        world::Entity* a = shop.entity(kCharacterA);
        world::Entity* b = shop.entity(kCharacterB);
        server::hub::Writer body;
        body.u64(0);
        body.u64(a->id);
        body.u32(crc(kWolf));
        body.u32(0);
        shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeCommunityGoalSelection, b->actor.bytes, body.data()));
        shop.host.steps(3, {&shop.clientA, &shop.clientB});
        check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && b->quests.active.empty(), "3819 on the actor route of B: no accept");
    }
    Boards shop;
    const std::uint64_t instance = accepted(shop);
    shop.kill(kCharacterA, "wolf", 5);
    shop.open(shop.clientA);
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    const std::uint64_t coin = a->wallet.coin;
    server::hub::Writer body;
    body.u64(0);
    body.u64(a->id);
    body.u64(instance);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeObjectiveCompletion, b->actor.bytes, body.data()));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(shop.mission_of(kCharacterA, crc(kWolf)) != nullptr && a->wallet.coin == coin && b->quests.active.empty(),
          "2461 on the actor route of B: no hand-in, no payment");
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && a->wallet.coin - coin == 135, "control: 2461 on the own route hands in");
}

/** The harness gate of the process for a block of a case (the old value comes back at the end). */
struct HarnessGate {
    std::optional<std::string> old;
    explicit HarnessGate(bool on) {
        if (const char* value = std::getenv("SOULWARDEN_HARNESS")) {
            old = value;
        }
        if (on) {
            setenv("SOULWARDEN_HARNESS", "1", 1);
        } else {
            unsetenv("SOULWARDEN_HARNESS");
        }
    }
    ~HarnessGate() {
        if (old) {
            setenv("SOULWARDEN_HARNESS", old->c_str(), 1);
        } else {
            unsetenv("SOULWARDEN_HARNESS");
        }
    }
    HarnessGate(const HarnessGate&) = delete;
    HarnessGate& operator=(const HarnessGate&) = delete;
};

/** QG6AR6: the diagnostic of the admin path needs the harness gate. */
void diagnostic_gate(const Options&) {
    Boards shop;
    shop.start();
    server::http::Router router;
    server::http::AdminAccess access;
    constexpr const char* token = "qg6ar6-made-up-token-0123456789abcdef";
    check(access.add(token, shop.a.steamId), "setup: the admin token of the account of A");
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_admin_routes(router, services, access);
    shop.host.tick();
    const auto call = [&]() {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/admin/v1/commands";
        request.target = request.path;
        request.body = json{{"command", "mission_board"}, {"character", kCharacterA}, {"args", {{"mission", kWolf}, {"windows", 2}}}}.dump();
        request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        return server::http::handle_request(router, request);
    };
    {
        const HarnessGate off(false);
        check(call().status == 403, "the mission board diagnostic of the admin path with no SOULWARDEN_HARNESS=1: 403");
    }
    {
        const HarnessGate on(true);
        check(call().status == 200, "control: with the harness gate the diagnostic answers");
    }
}

} // namespace

std::vector<Case> review_qg6a_cases() {
    return {{"QG6AR1", "review of QG6a, R1: the offer filter takes the level of the screen", offer_level},
            {"QG6AR2", "review of QG6a, R2: a refused mission hand-in keeps its return task pending (one transaction)", atomic_hand_in},
            {"QG6AR3", "review of QG6a, R3: a reset keeps the mission of the instance", reset_keeps_mission},
            {"QG6AR3B", "review of QG6a, R3: a death reset keeps the mission of the instance", death_reset_keeps_mission},
            {"QG6AR4", "review of QG6a, R4: the town buckets agree with the mission rows", bucket_validation},
            {"QG6AR5", "review of QG6a, R5: the board requests check the actor route", foreign_route},
            {"QG6AR6", "review of QG6a, R6: the mission diagnostic needs the harness gate", diagnostic_gate}};
}

} // namespace soulwarden::backend::tests

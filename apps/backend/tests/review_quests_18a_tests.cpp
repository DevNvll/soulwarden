// The corrections of the code review of stage 1.8a (findings QR8A-R1 to QR8A-R6; the review cases QR8A1 to QR8A6 of the evidence folder of
// the review, adapted). Each case fails on the tree of stage 1.8a and passes after its correction (stage 1.8c). Made-up accounts, ids, names,
// and data only. QR8A-R7 (the scan of the achievement events) is case QRV7 of quest_event_tests.cpp: it needs the index of stage 1.8c.
//
//   QRV1  an empty 1458 before the first Objectives state of the session keeps the saved pin (QR8A-R1)
//   QRV2  a failure timer is satisfied while it runs and permits the next task; its end fails the objective (QR8A-R2)
//   QRV3  a CompleteActive TaskAchievement and its container follow a conditional that becomes false (QR8A-R3)
//   QRV4  the lock of a bit removes each spelling of its row, and the bit stays locked after a reconnect (QR8A-R4)
//   QRV5  a timer whose completion write the store refused tries again, with no reconnect (QR8A-R5)
//   QRV6  the loader refuses a tree with a task that no container reaches (QR8A-R6)

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "support/cases.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace questfix;

void early_pins(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_new_character(host, kQuestNew, "Reviewnew", kSteamIdA);
    FakeClient client(27100);
    host.enter(client, player.registration());
    world::Entity* entity = host.world.find_character(kQuestNew);
    const auto pins = entity->quests.tracked;
    check(pins.size() == 1, "setup: one saved start pin before 2709");
    host.datagram(client.peer, client.component_message(server::rep::kTypeUpdateTrackedObjectives, entity->actor.bytes,
                                                       tracked_body(entity->id, {})));
    host.steps(3, {&client});
    send_2709(host, client, kQuestNew);
    host.steps(5, {&client});
    const auto states = own_fragments(client, kObjectivesSlot, kObjectivesType);
    note("pins after the early 1458: entity " + std::to_string(entity->quests.tracked.size()) + ", store "
         + std::to_string(host.store.list_quest_pins(kQuestNew).size()) + ", wire "
         + std::to_string(states.empty() ? 999 : decode_objectives(states.back()).tracked.size()));
    check(entity->quests.tracked == pins && host.store.list_quest_pins(kQuestNew) == pins && !states.empty()
              && decode_objectives(states.back()).tracked == pins,
          "an empty 1458 before the first Objectives state keeps the saved pin in the entity, the rows, and the first state");
    // After the first state the list of the client is the truth again.
    host.datagram(client.peer, client.component_message(server::rep::kTypeUpdateTrackedObjectives, entity->actor.bytes,
                                                       tracked_body(entity->id, {})));
    host.steps(3, {&client});
    check(entity->quests.tracked.empty() && host.store.list_quest_pins(kQuestNew).empty(),
          "a 1458 after the first state sets the list (the unpin of the player)");
}

void failure_timer(const Options&) {
    ServerHost host;
    json document = quest_document();
    document["tasks"]["test_timer_wait"]["cells"]["FailOnCondition"] = "1";
    check(host.quests.load_text(document.dump()), "setup: a failure timer in a consecutive container");
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Reviewa", state::WorldPosition{100, 200, 30});
    FakeClient client(27100);
    host.enter(client, player.registration());
    auto* entity = host.world.find_character(kQuestA);
    check(host.world.quests().grant(*entity, "Test_TimerQuest", "command", 0).ok(), "setup: grant the quest");
    const auto* quest = quest_of(*entity, "Test_TimerQuest");
    note("the failure timer at the grant: " + states_text(*quest));
    check(quest->tasks[1].state == world::quest_state::kCompleteActive && quest->tasks[1].progress == 1 && quest->tasks[1].startNs != 0
              && has_bit(*entity, 21),
          "a failure timer is satisfied (CompleteActive, progress 1) while it runs, keeps its start, and permits the next task");
    host.steps(2600, {&client});
    check(quest_of(*entity, "Test_TimerQuest") == nullptr && host.store.list_quests(kQuestA).empty() && !has_bit(*entity, 21)
              && host.world.quests().counts().failures == 1,
          "its end fails the objective: the instance goes (no completed entry), and the undo of its unlock runs");
}

void stay_active_regression(const Options&) {
    ServerHost host;
    json document = quest_document();
    document["tasks"]["test_wait_flag"]["cells"]["StayActive"] = "1";
    check(host.quests.load_text(document.dump()), "setup: a StayActive TaskAchievement");
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Reviewa", state::WorldPosition{100, 200, 30});
    FakeClient client(27100);
    host.enter(client, player.registration());
    auto* entity = host.world.find_character(kQuestA);
    check(host.world.quests().grant(*entity, "Test_WaitQuest", "command", 0).ok(), "setup: grant the wait quest");
    check(host.world.ownership().unlock_achievement(*entity, "Test_Flag", 22), "setup: the conditional is true");
    check(quest_of(*entity, "Test_WaitQuest")->tasks[0].state == world::quest_state::kCompleteActive, "setup: the task is CompleteActive");
    check(host.world.ownership().lock_achievement(*entity, "Test_Flag"), "the lock of the bit of the conditional");
    const auto* quest = quest_of(*entity, "Test_WaitQuest");
    note("after the conditional becomes false: " + states_text(*quest));
    check(quest->tasks[0].state == world::quest_state::kActive && quest->tasks[0].progress == 0 && quest->tasks[2].progress < 2
              && quest->tasks[2].state == world::quest_state::kActive,
          "the task returns to Active with progress 0, and its container counts it no more and is Active again");
    const auto rows = host.store.list_quests(kQuestA);
    check(rows.size() == 1 && rows[0].tasks[0].state == world::quest_state::kActive, "the rows have the regression");
    check(host.world.ownership().unlock_achievement(*entity, "Test_Flag", 22)
              && quest_of(*entity, "Test_WaitQuest")->tasks[0].state == world::quest_state::kCompleteActive,
          "the conditional true again: CompleteActive again");
}

void lock_case(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Reviewa", state::WorldPosition{100, 200, 30});
    check(host.store.grant_achievements(kQuestA, {"test_last"}), "setup: the row has the lower-case spelling");
    FakeClient client(27100);
    host.enter(client, player.registration());
    auto* entity = host.world.find_character(kQuestA);
    check(has_bit(*entity, 25), "setup: the lookup takes this spelling");
    check(host.world.quests().grant(*entity, "Test_LockQuest", "command", 0).ok(), "the lock action with the spelling Test_Last");
    check(!has_bit(*entity, 25), "the lock clears the bit");
    note("achievement rows after the lock: " + std::to_string(host.store.list_achievements(kQuestA).size()));
    host.disconnect(client.peer);
    host.steps(2, {});
    FakeClient next(27101);
    host.enter(next, player.registration());
    const bool restored = has_bit(*host.world.find_character(kQuestA), 25);
    check(host.store.list_achievements(kQuestA).empty() && !restored,
          "the lock removes each spelling of the row, and the bit stays locked after a reconnect");
    check(host.store.grant_achievements(kQuestA, {"Test_Other"}) && host.store.grant_achievements(kQuestA, {"TEST_OTHER"})
              && host.store.list_achievements(kQuestA).size() == 1,
          "a grant of a second spelling of a stored id writes no second row");
}

void timer_write_failure(const Options& options) {
    const auto database = (options.workDirectory / "review-quests-timer.sqlite").string();
    std::filesystem::remove(database);
    ServerHost host(service::RepServerOptions{}, database);
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Reviewa", state::WorldPosition{100, 200, 30});
    FakeClient client(27100);
    host.enter(client, player.registration());
    auto* entity = host.world.find_character(kQuestA);
    check(host.world.quests().grant(*entity, "Test_TimerQuest", "command", 0).ok(), "setup: grant the timer quest");
    sqlite3* sql = nullptr;
    check(sqlite3_open(database.c_str(), &sql) == SQLITE_OK, "setup: open the made-up database for a write fault");
    check(sqlite3_exec(sql, "CREATE TRIGGER fail_quest BEFORE UPDATE ON character_quests BEGIN SELECT RAISE(FAIL,'test failure'); END;",
                       nullptr, nullptr, nullptr) == SQLITE_OK,
          "setup: the store refuses the completion write of the timer");
    host.steps(2600, {&client});
    check(host.world.quests().counts().storeFailures >= 1
              && quest_of(*entity, "Test_TimerQuest")->tasks[1].state == world::quest_state::kActive,
          "the refused completion write changes no task");
    check(sqlite3_exec(sql, "DROP TRIGGER fail_quest", nullptr, nullptr, nullptr) == SQLITE_OK, "the store takes writes again");
    sqlite3_close(sql);
    host.steps(3000, {&client});
    note("after the store takes writes again: " + states_text(*quest_of(*entity, "Test_TimerQuest")));
    check(quest_of(*entity, "Test_TimerQuest")->tasks[1].state == world::quest_state::kComplete && has_bit(*entity, 21),
          "the due timer tries again and completes, with no reconnect");
}

void disconnected_tree(const Options&) {
    json document = quest_document();
    document["objectives"][1]["tree"][3][2] = json::array({1, 2});
    gamedata::QuestData data;
    const bool accepted = data.load_text(document.dump());
    note("a tree with the task 0 outside its root: accepted " + std::to_string(accepted) + (accepted ? "" : " (" + data.error() + ")"));
    check(!accepted && !data.loaded(), "the loader refuses a compiled tree with a task that no container reaches");
    check(data.load_text(quest_document().dump()), "the document of the cases (each task under its root) loads");
}

} // namespace

std::vector<Case> review_quests_18a_cases() {
    return {{"QRV1", "review of 1.8a, QR8A-R1: an early 1458 before the first quest state keeps the saved pin", early_pins},
            {"QRV2", "review of 1.8a, QR8A-R2: a failure timer is satisfied while it runs; its end fails the objective", failure_timer},
            {"QRV3", "review of 1.8a, QR8A-R3: a CompleteActive TaskAchievement follows a conditional that becomes false",
             stay_active_regression},
            {"QRV4", "review of 1.8a, QR8A-R4: the lock removes each spelling of the achievement row", lock_case},
            {"QRV5", "review of 1.8a, QR8A-R5: a timer tries again after a refused completion write", timer_write_failure},
            {"QRV6", "review of 1.8a, QR8A-R6: the loader refuses a task outside the root tree", disconnected_tree}};
}

} // namespace soulwarden::backend::tests

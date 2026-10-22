// Cases of the corrections of the code review of stage 2.5c (~/Games/soulwarden/work/evidence/persons/25c/code-review/FINDINGS.md), made in
// stage 1.8b. Each case is the proof case of its finding (PR5C1 to PR5C8 of the review), with a free case id of the repository; each fails on
// the tree of stage 2.5c and passes after the correction. Made-up data only.
//
//   QRP1  PR5C-R1: a late 5397 after the loss of the range of the source, and after an accepted use of a different source (a chair), does
//         not finish a topic (the closed conversation is kept only after a close of the client)
//   QRP2  PR5C-R2: a topic request between the loss of the presence and the tick ends the use and the service grant too
//   QRP3  PR5C-R3: a service that the state of now does not offer does not pass the grant API; a new conversation with no service ends the
//         grant of the earlier one
//   QRP4  PR5C-R4: the reset of a parent stores the new parent of its active child; after a new world entry the completion of the child
//         completes the task of the new parent
//   QRP5  PR5C-R5: the retry of an event whose write the store refused does not credit the new instance of a reset quest
//   QRP6  PR5C-R6: the ids of a person child at the borders of the rule (two axes at one time, the float of a place above the border):
//         check_interactions_tool.py --persons
//   QRP7  PR5C-R7: an achievement expression that the reader cannot read to its end does not pass a requirement

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/person_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;

/** QRP1: the range and a new source end the closed topic context (finding PR5C-R1). */
void source_ends_topics(const Options&) {
    {
        PersonShop shop;
        shop.enter();
        world::Entity* a = shop.entity(kCharacterA);
        world::Entity* b = shop.entity(kCharacterB);
        check(shop.host.world.quests().grant(*a, "Test_TopicQuest", "command", 0).ok()
                  && shop.host.world.quests().grant(*b, "Test_TopicQuest", "command", 0).ok(),
              "control: A and B have the task of the topic");
        shop.request(shop.clientA, shop.talk(kArtificer));
        shop.request(shop.clientB, shop.talk(kArtificer));
        check(a->conversation && a->conversation->topics.size() == 1 && a->interaction, "control: A has an open conversation with its topic");
        shop.placeA = {100.0F, 210.0F, 30.0F};
        for (int step = 0; step < 120; ++step) {
            shop.report(shop.clientA, shop.talk(kArtificer));
            shop.host.steps(1, {&shop.clientA, &shop.clientB});
        }
        check(a->position.y > 209.0F && !a->interaction, "control: an accepted position out of the range of the source ends the use");
        const std::uint64_t before = shop.topic_count(world::TopicResult::accepted);
        shop.topic(kCharacterA, {crc("Test_Topic_Ask")}, true);
        note("QRP1 after the range loss: accepted " + std::to_string(shop.topic_count(world::TopicResult::accepted) - before) + ", context "
             + std::to_string(a->conversation.has_value()));
        check(shop.topic_count(world::TopicResult::accepted) == before && !a->conversation,
              "a late 5397 after the loss of the range finishes no topic (the conversation is forgotten)");
        check(b->conversation && b->conversation->open && b->conversation->finished.empty() && b->interaction,
              "B keeps its open conversation");
    }
    PersonShop shop;
    std::vector<gamedata::InteractionObject> objects = shop.host.interactions.objects();
    std::vector<gamedata::InteractionOption> options = shop.host.interactions.options();
    options.push_back(option_row("ChairInteractionAnimation", 64, true));
    objects.push_back(object_of(5, InteractionKind::other, {101.0F, 198.0F, 30.0F}, {1}));
    check(shop.host.interactions.set(options, {}, objects), "control: a chair as a different source");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Test_TopicQuest", "command", 0).ok(), "control: A has the task of the topic");
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.close(kCharacterA, kArtificer);
    const InteractionBlock chair = block_for(shop.host.interactions, 5, 1);
    shop.request(shop.clientA, chair);
    check(a->interaction && a->interaction->reference.group == chair.group, "control: the chair is the source of A now");
    const std::uint64_t before = shop.topic_count(world::TopicResult::accepted);
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")}, true);
    check(shop.topic_count(world::TopicResult::accepted) == before && !a->conversation,
          "after an accepted use of a different source the 5397 of the old person finishes no topic");
}

/** QRP2: a topic before the presence tick (finding PR5C-R2). */
void presence_before_tick(const Options&) {
    PersonShop shop;
    json document = conversation_document();
    document["states"][3]["topics"] = json::array({"Test_Topic_Pirate"});
    check(shop.host.conversations.load_text(document.dump()), "control: the pirate offers a topic and Open_Shop");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit)
              && shop.host.world.ownership().unlock_achievement(*b, "Test_Swap_Pirate", kSwapPirateBit),
          "control: the pirate is there for A and B");
    shop.request(shop.clientA, shop.talk(kPirate));
    shop.request(shop.clientB, shop.talk(kPirate));
    check(a->interaction && a->serviceGrant && b->interaction && b->serviceGrant, "control: A and B have a use and a grant");
    check(shop.host.world.ownership().lock_achievement(*a, "Test_Swap_Pirate"), "control: the pirate goes for A");
    // No tick between the change of the bit and the message of the client.
    shop.topic(kCharacterA, {crc("Test_Topic_Pirate")});
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    const world::ServiceError service = shop.interactions().require_service(kCharacterA, world::kConversationServiceKind, 16);
    check(shop.topic_count(world::TopicResult::notPresent) == 1, "control: the topic sees the lost presence");
    check(!a->interaction && !a->serviceGrant && service != world::ServiceError::none,
          "the topic before the presence tick ends the use and the grant of the person that is not there");
    check(b->interaction && b->conversation && b->serviceGrant, "B keeps its use and its grant");
}

/** QRP3: the grant follows the state of now (finding PR5C-R3). */
void grant_follows_state(const Options&) {
    PersonShop shop;
    json document = conversation_document();
    document["npcs"][2]["states"] = json::array({"Inn_Default", "Inn_NoService"});
    document["states"].push_back({{"id", "Inn_NoService"}, {"requires", {{"RequiredAchievement", "Test_Topic_Done"}}}});
    check(shop.host.conversations.load_text(document.dump()), "control: a later state of the innkeeper has no service");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    shop.request(shop.clientB, shop.talk(kInnkeeper));
    check(a->serviceGrant && b->serviceGrant, "control: A and B have the services of the default state");
    shop.close(kCharacterA, kInnkeeper);
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Topic_Done", 62), "control: the bit of the later state for A only");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const world::ServiceError stale = shop.interactions().require_service(kCharacterA, world::kConversationServiceKind, 16);
    note("QRP3 the closed offer after the change of the state: " + std::string(world::service_error_text(stale)));
    check(stale != world::ServiceError::none, "a service that the state of now does not offer does not pass the grant API");
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->conversation && a->conversation->services.empty() && !a->serviceGrant
              && shop.interactions().require_service(kCharacterA, world::kConversationServiceKind, 16) != world::ServiceError::none,
          "a new conversation with no service ends the grant of the earlier conversation");
    check(b->serviceGrant && shop.interactions().require_service(kCharacterB, world::kConversationServiceKind, 16) == world::ServiceError::none,
          "B keeps its service");
}

/** QRP4: the reset of a parent stores the new parent of the child (finding PR5C-R4). */
void parent_reset_stored(const Options&) {
    PersonShop shop;
    json document = person_quest_document();
    document["tasks"]["qrp_child_wait"] = questfix::task("Qrp_Child_Wait", "taskgameevent", 14, {{"KillEnemyType", "Qrp_Child_Done"}});
    document["tasks"]["qrp_child_task"] = questfix::task("Qrp_Child_Task", "taskobjective", 52, {{"ItemName", "Qrp_Child"}});
    document["objectives"].push_back(
        {{"id", "Qrp_Child"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qrp_child_wait", json::array()})})}});
    document["objectives"].push_back(
        {{"id", "Qrp_Parent"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qrp_child_task", json::array()})})}});
    check(shop.host.quests.load_text(document.dump()), "control: a parent task grants a child objective");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const world::QuestResult original = shop.host.world.quests().grant(*a, "Qrp_Parent", "command", 0);
    const world::QuestResult reset = shop.host.world.quests().reset(*a, original.instance);
    check(original.ok() && reset.ok() && questfix::quest_of(*a, "Qrp_Child")->parent == reset.instance,
          "control: the child names the new parent in the entity");
    std::uint64_t stored = 0;
    for (const state::QuestRow& row : shop.host.store.list_quests(kCharacterA)) {
        stored = row.objective == "Qrp_Child" ? row.parent : stored;
    }
    note("QRP4 the parent of the child: entity " + std::to_string(questfix::quest_of(*a, "Qrp_Child")->parent) + ", row " + std::to_string(stored));
    check(stored == reset.instance, "the transaction of the reset stores the new parent of the child");
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(60, {&shop.clientB});
    FakeClient again{27003};
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(30, {&again, &shop.clientB});
    a = shop.entity(kCharacterA);
    check(a != nullptr && questfix::quest_of(*a, "Qrp_Child") != nullptr, "control: the child loads in a new world entry");
    world::QuestEvent done;
    done.kind = world::QuestEventKind::gameEvent;
    done.keys = {"Qrp_Child_Done"};
    shop.host.world.quests().event(*a, done);
    check(questfix::quest_of(*a, "Qrp_Parent") != nullptr && questfix::quest_of(*a, "Qrp_Parent")->tasks[0].state == world::quest_state::kComplete,
          "after the new entry the completion of the child completes the task of its new parent");
}

/** QRP5: an old retry does not credit a new instance (finding PR5C-R5). */
void retry_keeps_its_tasks(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "qrp5.sqlite3").string();
    std::filesystem::remove(path);
    PersonShop shop(true, 0, path);
    json document = person_quest_document();
    document["tasks"]["qrp_retry_counter"] =
        questfix::task("Qrp_Retry_Counter", "taskgameevent", 14, {{"KillEnemyType", "Qrp_Result"}, {"TargetQty", "2"}});
    document["objectives"].push_back({{"id", "Qrp_Retry_Quest"},
                                      {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "qrp_retry_counter", json::array()})})}});
    check(shop.host.quests.load_text(document.dump()), "control: a quest with a counter of a result");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const world::QuestResult first = shop.host.world.quests().grant(*a, "Qrp_Retry_Quest", "command", 0);
    const auto sql = [&](const char* text) {
        sqlite3* database = nullptr;
        const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(database);
        return ok;
    };
    check(first.ok() && sql("CREATE TRIGGER qrp5_refuse BEFORE UPDATE ON character_quests BEGIN SELECT RAISE(FAIL, 'qrp5'); END"),
          "control: the store refuses the update of the event");
    world::QuestEvent event;
    event.kind = world::QuestEventKind::gameEvent;
    event.keys = {"Qrp_Result"};
    event.result = 812;
    shop.host.world.quests().event(*a, event);
    check(shop.host.world.quests().counts().eventRetries == 1 && questfix::quest_of(*a, "Qrp_Retry_Quest")->tasks[0].progress == 0,
          "control: the refused event has one retry and no progress");
    check(sql("DROP TRIGGER qrp5_refuse"), "control: the store takes the writes again");
    const world::QuestResult replacement = shop.host.world.quests().reset(*a, first.instance);
    check(replacement.ok() && replacement.instance != first.instance && questfix::quest_of(*a, "Qrp_Retry_Quest")->tasks[0].progress == 0,
          "control: the reset gives a new instance before the retry");
    shop.host.advance(std::chrono::milliseconds(2100));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    note("QRP5 the progress of the new instance after the retry: " + std::to_string(questfix::quest_of(*a, "Qrp_Retry_Quest")->tasks[0].progress));
    check(questfix::quest_of(*a, "Qrp_Retry_Quest")->tasks[0].progress == 0, "the retry of the event of the old instance does not credit the new instance");
    std::filesystem::remove(path);
}

/** QRP6: the ids at the borders of the rule (finding PR5C-R6): the check of the interaction tool. */
void child_ids_at_borders(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata/check_interactions_tool.py";
    const std::filesystem::path output = options.workDirectory / "qrp6.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + script.lexically_normal().string()
                                + "\" --persons > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    bool border = false;
    std::ifstream log(output);
    for (std::string line; std::getline(log, line);) {
        if (line.find("the ids of a slot at the borders of the rule") != std::string::npos) {
            note(line.substr(0, 160));
            border = line.rfind("ok", 0) == 0;
        }
    }
    std::filesystem::remove(output);
    check(status == 0 && border, "the ids of a slot at the borders of the rule have the id of the client (two axes, the float of a place)");
}

/** QRP7: a malformed achievement expression does not pass (finding PR5C-R7). */
void malformed_expression(const Options&) {
    PersonShop shop;
    json document = conversation_document();
    document["states"][1]["requires"] = {{"RequiredAchievement", "(!Test_Never"}};
    check(shop.host.conversations.load_text(document.dump()), "control: a state requirement with no closing parenthesis");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const world::PersonSelection selection = shop.persons().select(*a, *shop.host.interactions.objects()[0].person);
    const std::string state = selection.state >= 0 ? shop.host.conversations.state(static_cast<std::uint32_t>(selection.state)).id : "(none)";
    note("QRP7 the state for the malformed requirement: " + state);
    check(state == "Art_Default", "a malformed requirement does not select the gated state");
    check(!shop.host.world.quests().conditional(*a, "!(Test_Never") && !shop.host.world.quests().conditional(*a, "!Test_Never &&")
              && !shop.host.world.quests().conditional(*a, "!Test_Never )") && shop.host.world.quests().conditional(*a, "!(Test_Never)"),
          "a missing parenthesis, a missing operand, and text after the end are false, also under a negation; a correct expression passes");
}

} // namespace

std::vector<Case> review_persons_25c_cases() {
    return {{"QRP1", "review of 2.5c, PR5C-R1: the range and a new source end the closed topic context", source_ends_topics},
            {"QRP2", "review of 2.5c, PR5C-R2: a topic before the presence tick ends the use and the grant", presence_before_tick},
            {"QRP3", "review of 2.5c, PR5C-R3: the grant follows the state of now; a new conversation with no service", grant_follows_state},
            {"QRP4", "review of 2.5c, PR5C-R4: the reset of a parent stores the new parent of the child", parent_reset_stored},
            {"QRP5", "review of 2.5c, PR5C-R5: an old retry does not credit a new instance", retry_keeps_its_tasks},
            {"QRP6", "review of 2.5c, PR5C-R6: the ids of a person child at the borders of the rule", child_ids_at_borders},
            {"QRP7", "review of 2.5c, PR5C-R7: a malformed achievement expression does not pass", malformed_expression}};
}

} // namespace soulwarden::backend::tests

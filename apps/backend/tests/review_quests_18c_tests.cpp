// The corrections of the code review of stage 1.8c (findings QR8C-R1 to QR8C-R14; the review cases QR8C1 to QR8C14 of the evidence folder
// of the review, adapted). Each case fails on the tree of stage 1.8c and passes after its correction (stage 2.5c). Made-up accounts, ids,
// names, and data only.
//
//   QRW1   a refused event write does not use its result id: a second delivery counts one time; the world tries the event again (QR8C-R1)
//   QRW2   the item actions of a plan apply in the order of the tasks: a give, then a remove of the same item (QR8C-R2)
//   QRW3   a reset of a child keeps its parent; a reset of a parent gives the active child to the new parent (QR8C-R3)
//   QRW4   a return removes the equipped item that it counted (QR8C-R4)
//   QRW5   an abandon updates the achievement waits of the other quests (QR8C-R5)
//   QRW6   EnforceLocationRequirement applies to a possession task (QR8C-R6)
//   QRW7   a place with no shape in the POI data is no proof of an exit (QR8C-R7)
//   QRW8   a timed gather completes no interaction at its start, and one at its success (QR8C-R8)
//   QRW9   a kill drop that the full bag lost does not use AwardOnlyOnce (QR8C-R9)
//   QRW10  the craft event has the gear score of a fixed output (QR8C-R10)
//   QRW11  one indexed event visits no unrelated quest of its character (QR8C-R11)
//   QRW12  a gather that the full bag refused completes no interaction (QR8C-R12)
//   QRW13  one gather of a quest object completes its interaction one time (QR8C-R13)
//   QRW14  a give of a new quest reaches its new possession task (QR8C-R14)

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "support/cases.h"
#include "support/crafting_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::quest_of;
using questfix::states_text;
using world::quest_state::kActive;
using world::quest_state::kComplete;
using world::quest_state::kCompleteActive;

/** A quest document of made-up objectives: each objective has a container (simple or consecutive) with its leaves. */
struct ReviewQuests {
    json tasks = json::object();
    json objectives = json::array();
    json achievements = json::array();

    void task(const std::string& id, const char* type, unsigned code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id.c_str(), type, code, cells);
    }
    void objective(const std::string& id, const std::vector<std::string>& leaves, bool consecutive = false) {
        const std::string root = id + "_root";
        task(root, consecutive ? "consecutivetaskcontainer" : "simpletaskcontainer", consecutive ? 1 : 0);
        json nodes = json::array();
        json children = json::array();
        for (unsigned index = 0; index < leaves.size(); ++index) {
            nodes.push_back(json::array({index, gamedata::lower_text(leaves[index]), json::array()}));
            children.push_back(index);
        }
        nodes.push_back(json::array({leaves.size(), gamedata::lower_text(root), children}));
        objectives.push_back({{"id", id}, {"cells", {{"Type", "Journey"}}}, {"tree", nodes}});
    }
    template <class Fixture> void load(Fixture& shop) const {
        check(shop.host.quests.load_text(json{{"format", 1},
                                              {"tasks", tasks},
                                              {"objectives", objectives},
                                              {"achievements", achievements},
                                              {"npa", json::array()},
                                              {"territories", json::array()},
                                              {"backstories", json::array()}}
                                             .dump()),
              "setup: the made-up quest data loads");
    }
};

world::QuestResult grant(GatherShop& shop, const char* objective) {
    return shop.host.world.quests().grant(*shop.entity(kCharacterA), objective, "command", 0);
}

const world::QuestInstance* quest(GatherShop& shop, const char* objective) {
    return quest_of(*shop.entity(kCharacterA), objective);
}

void game_event(GatherShop& shop, const char* key, std::uint64_t result = 0) {
    world::QuestEvent event;
    event.kind = world::QuestEventKind::gameEvent;
    event.keys = {key};
    event.result = result;
    shop.host.world.quests().event(*shop.entity(kCharacterA), event);
}

std::uint64_t count(GatherShop& shop, const char* item) {
    return world::Inventory::count(*shop.entity(kCharacterA), item);
}

void steps(GatherShop& shop, int count = 5) {
    shop.host.steps(count, {&shop.clientA, &shop.clientB});
}

bool sql(const std::string& path, const char* text) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

/** A counter of the game event Result with a target of `target`. */
void counter_quest(ReviewQuests& quests, unsigned target) {
    quests.task("Counter", "taskgameevent", 14, {{"KillEnemyType", "Result"}, {"TargetQty", std::to_string(target)}});
    quests.objective("CounterQuest", {"Counter"});
}

void child_quests(ReviewQuests& quests) {
    quests.task("ChildWait", "taskgameevent", 14, {{"KillEnemyType", "ChildDone"}});
    quests.objective("ChildQuest", {"ChildWait"});
    quests.task("Child", "taskobjective", 52, {{"ItemName", "ChildQuest"}});
    quests.objective("ParentQuest", {"Child"});
}

/** A capital gather node whose interaction bound has the tag review_tag; `delay`: the Delay Time of its option. */
void tagged_node(GatherShop& shop, float delay) {
    gamedata::InteractionObject object = object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex});
    object.bounds[0].tag = "review_tag";
    gamedata::InteractionOption option = option_row("GatherOption", 14, true);
    option.delay = delay;
    check(shop.host.interactions.set({option}, {}, {object}), "setup: a gather node with the tag review_tag at its bound");
}

void interact_quest(ReviewQuests& quests, unsigned target) {
    quests.task("Interact", "taskinteract", 23, {{"InteractTag", "review_tag"}, {"TargetQty", std::to_string(target)}});
    quests.objective("InteractQuest", {"Interact"});
}

void event_write_refused(const Options& options) {
    const std::string path = (options.workDirectory / "review-18c-event.sqlite").string();
    std::filesystem::remove(path);
    GatherShop shop(path);
    ReviewQuests quests;
    counter_quest(quests, 3);
    quests.load(shop);
    shop.enter();
    check(grant(shop, "CounterQuest").ok(), "setup: the counter quest");
    const char* refuse = "CREATE TRIGGER refuse_quest BEFORE UPDATE ON character_quests BEGIN SELECT RAISE(FAIL,'test fault'); END";
    check(sql(path, refuse), "setup: the store refuses the write of a quest");
    game_event(shop, "Result", 771);
    check(quest(shop, "CounterQuest")->tasks[0].progress == 0 && shop.host.world.quests().counts().storeFailures == 1,
          "a refused event write changes no task");
    check(sql(path, "DROP TRIGGER refuse_quest"), "setup: the store writes again");
    game_event(shop, "Result", 771);
    note("progress after the refused write and a second delivery: " + std::to_string(quest(shop, "CounterQuest")->tasks[0].progress));
    check(quest(shop, "CounterQuest")->tasks[0].progress == 1, "a second delivery of the result of a refused write counts one time");
    shop.host.advance(std::chrono::milliseconds(2100));
    steps(shop);
    check(quest(shop, "CounterQuest")->tasks[0].progress == 1, "the retry of the world after 2 s sees the result that counted: no second count");
    check(sql(path, refuse), "setup: the store refuses the write of a quest again");
    game_event(shop, "Result", 773);
    check(sql(path, "DROP TRIGGER refuse_quest"), "setup: the store writes again");
    shop.host.advance(std::chrono::milliseconds(2100));
    steps(shop);
    note("progress after the retry of the world: " + std::to_string(quest(shop, "CounterQuest")->tasks[0].progress));
    check(quest(shop, "CounterQuest")->tasks[0].progress == 2, "with no second delivery, the world tries the refused event again after 2 s");
}

void item_action_order(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Give", "taskgiveitem", 33, {{"ItemName", "TestGemA"}});
    quests.task("Remove", "taskremoveitem", 36, {{"ItemName", "TestGemA"}});
    quests.objective("OrderQuest", {"Give", "Remove"}, true);
    quests.load(shop);
    shop.enter();
    check(count(shop, "TestGemA") == 0, "setup: no gem");
    check(grant(shop, "OrderQuest").ok(), "the grant applies the give, then the remove");
    note("after the give and the remove: gems " + std::to_string(count(shop, "TestGemA")) + "; " + states_text(*quest(shop, "OrderQuest")));
    check(count(shop, "TestGemA") == 0 && quest(shop, "OrderQuest")->tasks[1].state == kComplete,
          "the later remove takes the gem that the earlier give of the same plan gave");
}

void reset_links(const Options&) {
    {
        GatherShop shop;
        ReviewQuests quests;
        child_quests(quests);
        quests.load(shop);
        shop.enter();
        check(grant(shop, "ParentQuest").ok(), "setup: the parent gives its child");
        check(shop.host.world.quests().reset(*shop.entity(kCharacterA), quest(shop, "ChildQuest")->instance).ok(), "a reset of the child");
        game_event(shop, "ChildDone");
        check(quest(shop, "ChildQuest")->tasks[0].state == kComplete, "control: the new child completes");
        note("parent after the reset of its child: " + states_text(*quest(shop, "ParentQuest")));
        check(quest(shop, "ParentQuest")->tasks[0].state == kComplete, "the completion of the reset child completes the task of its parent");
    }
    GatherShop shop;
    ReviewQuests quests;
    child_quests(quests);
    quests.load(shop);
    shop.enter();
    const world::QuestResult parent = grant(shop, "ParentQuest");
    check(parent.ok(), "setup: the parent gives its child");
    const world::QuestResult reset = shop.host.world.quests().reset(*shop.entity(kCharacterA), parent.instance);
    check(reset.ok(), "a reset of the parent");
    check(quest(shop, "ChildQuest")->parent == reset.instance, "the active child is the child of the new parent");
    game_event(shop, "ChildDone");
    note("new parent after the completion of the child: " + states_text(*quest(shop, "ParentQuest")));
    check(quest(shop, "ParentQuest")->tasks[0].state == kComplete, "the completion of the child completes the task of the new parent");
}

void equipped_return(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Return", "taskhaveandreturnitems", 26, {{"ItemName", "TestKnife"}});
    quests.objective("ReturnQuest", {"Return"});
    quests.load(shop);
    shop.enter();
    check(shop.equip(kCharacterA, "TestKnife", kKnifeSlot), "setup: one equipped knife, none in the bag");
    const world::QuestResult granted = grant(shop, "ReturnQuest");
    check(granted.ok() && quest(shop, "ReturnQuest")->tasks[0].state == kComplete, "the equipped knife satisfies the return task");
    check(shop.host.world.quests().abandon(*shop.entity(kCharacterA), granted.instance).ok(), "the abandon commits");
    note("knives after the return: " + std::to_string(count(shop, "TestKnife")));
    check(count(shop, "TestKnife") == 0, "the return removes the equipped knife that it counted");
}

void abandon_waits(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.achievements.push_back(json::array({"Flag", 200}));
    quests.task("Unlock", "taskunlockachievement", 20, {{"ItemName", "Flag"}});
    quests.task("Never", "taskgameevent", 14, {{"KillEnemyType", "Never"}});
    quests.objective("SourceQuest", {"Unlock", "Never"}, true);
    quests.task("Wait", "taskachievement", 18, {{"ItemName", "Flag"}, {"StayActive", "1"}});
    // Stage 1.8d: a second child that never completes: the wait quest is not ready (a ready quest holds its StayActive tasks, case QD11).
    quests.task("WaitNever", "taskgameevent", 14, {{"KillEnemyType", "Never"}});
    quests.objective("WaitQuest", {"Wait", "WaitNever"});
    quests.load(shop);
    shop.enter();
    check(grant(shop, "WaitQuest").ok(), "setup: a wait for the flag");
    const world::QuestResult source = grant(shop, "SourceQuest");
    check(source.ok() && quest(shop, "WaitQuest")->tasks[0].state == kCompleteActive, "setup: the source quest satisfies the StayActive wait");
    check(shop.host.world.quests().abandon(*shop.entity(kCharacterA), source.instance).ok(), "the abandon of the source quest");
    steps(shop);
    note("after the undo: bit " + std::to_string(questfix::has_bit(*shop.entity(kCharacterA), 200)) + "; wait "
         + states_text(*quest(shop, "WaitQuest")));
    check(quest(shop, "WaitQuest")->tasks[0].state == kActive && quest(shop, "WaitQuest")->tasks[0].progress == 0,
          "the undo of the abandon makes the indexed achievement wait of the other quest active again");
}

void location_possession(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Have", "taskhaveitems", 25, {{"ItemName", "TestWoodT1"}, {"POITag", "required"}, {"EnforceLocationRequirement", "1"}});
    quests.objective("LocationQuest", {"Have"});
    quests.load(shop);
    check(shop.host.pois.load_text(json{{"format", 1},
                                        {"pois", json::array({{{"id", 1},
                                                               {"center", {10, 10, 0}},
                                                               {"tags", {"required"}},
                                                               {"shape", {{0, 0}, {20, 0}, {20, 20}, {0, 20}}}}})}}
                                       .dump()),
          "setup: the place of the task is far from the character");
    shop.enter();
    check(shop.host.world.inventory().give(kCharacterA, "TestWoodT1", 1).ok(), "setup: one wood out of the place");
    check(grant(shop, "LocationQuest").ok(), "the grant of the location task");
    note("out of the place: " + states_text(*quest(shop, "LocationQuest")));
    check(quest(shop, "LocationQuest")->tasks[0].state == kActive, "a possession out of the enforced place does not complete the task");
}

void exit_without_shape(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Exit", "taskexitpoitag", 9, {{"POITag", "missing_shape"}});
    quests.objective("ExitQuest", {"Exit"});
    quests.load(shop);
    check(shop.host.pois.load_text(R"({"format":1,"pois":[]})"), "setup: the POI data has no shape for the tag of the task");
    shop.enter();
    check(grant(shop, "ExitQuest").ok(), "the grant of the exit task");
    note("no shape: " + states_text(*quest(shop, "ExitQuest")));
    check(quest(shop, "ExitQuest")->tasks[0].state == kActive, "a place with no shape leaves the exit task active");
}

void timed_gather(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    interact_quest(quests, 2);
    quests.load(shop);
    tagged_node(shop, 0.0F);
    shop.enter();
    check(grant(shop, "InteractQuest").ok(), "setup: the interaction task");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: a pick");
    shop.request(shop.clientA, shop.capital_block());
    note("at the start of the gather: " + states_text(*quest(shop, "InteractQuest")));
    check(quest(shop, "InteractQuest")->tasks[0].progress == 0, "a gather with a work time completes no interaction at its start");
    shop.hold(shop.clientA, shop.capital_block(), 4500);
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().completions == 1 && count(shop, "TestOreT1") == 2, "control: one gather committed");
    note("after one gather: " + states_text(*quest(shop, "InteractQuest")));
    check(quest(shop, "InteractQuest")->tasks[0].progress == 1, "one gather completes one interaction");
}

void lost_kill_drop(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Drop", "taskhaveitems", 25,
                {{"ItemName", "TestFiberT1"}, {"ItemDropVC", "Test_BagWolf"}, {"ItemDropProbability", "100"}, {"AwardOnlyOnce", "1"}});
    quests.objective("DropQuest", {"Drop"});
    quests.load(shop);
    shop.enter();
    check(grant(shop, "DropQuest").ok(), "setup: the drop quest");
    check(shop.host.world.inventory().give(kCharacterA, "TestFiller", 500).ok(), "setup: the 500 slots of the bag are full");
    shop.dead_wolf();
    check(count(shop, "TestFiberT1") == 0 && shop.host.store.list_quest_awards(kCharacterA).empty(),
          "the full bag gets no quest drop, and the store has no award row");
    check(shop.host.world.inventory().remove(kCharacterA, 0, 1).ok(), "setup: one free slot");
    shop.dead_wolf();
    steps(shop);
    const auto awards = shop.host.store.list_quest_awards(kCharacterA);
    note("after a kill with a free slot: fiber " + std::to_string(count(shop, "TestFiberT1")) + "; award "
         + std::to_string(awards.empty() ? 0 : awards[0].amount));
    check(count(shop, "TestFiberT1") == 1 && awards.size() == 1 && awards[0].amount == 1,
          "a lost drop does not use AwardOnlyOnce: the next kill gives the drop and writes its award");
}

void craft_score(const Options&) {
    crafting::Crafter shop;
    ReviewQuests quests;
    quests.task("Craft", "taskcraftrecipe", 38, {{"CraftingRecipeId", "TestKnifeT1"}, {"MinGearScore", "100"}});
    quests.objective("CraftQuest", {"Craft"});
    quests.load(shop);
    shop.enter();
    world::Entity* character = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*character, "CraftQuest", "command", 0).ok(), "setup: a craft task with MinGearScore 100");
    check(shop.host.world.inventory().set_coin(kCharacterA, 1000).ok(), "setup: the coin of the fee");
    shop.give(kCharacterA, "TestFlintT1", 1);
    shop.give(kCharacterA, "TestWoodT1", 1);
    shop.use_station(shop.clientA);
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {crafting::entry("TestFlintT1"), crafting::entry("TestWoodT1")});
    shop.finish();
    std::uint32_t score = 0;
    for (const auto& slot : character->bag.slots) {
        if (slot.item && slot.item->itemId == "TestKnifeT1") {
            score = slot.item->gearScore;
        }
    }
    check(score == 100 && shop.crafting().counts().jobs == 1, "setup: a committed craft gives a knife of gear score 100 (its fixed output)");
    note("crafted gear score " + std::to_string(score) + "; task " + states_text(*quest_of(*character, "CraftQuest")));
    check(quest_of(*character, "CraftQuest")->tasks[0].state == kComplete, "the craft event has the gear score of the fixed output");
}

void event_cost(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    counter_quest(quests, 2);
    quests.task("Idle", "taskgameevent", 14, {{"KillEnemyType", "Unrelated"}});
    for (int index = 0; index < 30; ++index) {
        quests.objective("IdleQuest" + std::to_string(index), {"Idle"});
    }
    quests.load(shop);
    shop.enter();
    check(grant(shop, "CounterQuest").ok(), "setup: one subscriber of the event");
    for (int index = 0; index < 30; ++index) {
        check(grant(shop, ("IdleQuest" + std::to_string(index)).c_str()).ok(), "setup: an unrelated active quest");
    }
    const world::QuestCounts before = shop.host.world.quests().counts();
    game_event(shop, "Result");
    const world::QuestCounts after = shop.host.world.quests().counts();
    const std::uint64_t subscribers = after.subscribersVisited - before.subscribersVisited;
    const std::uint64_t visits = after.questVisits - before.questVisits;
    const std::uint64_t lookups = after.instanceLookups - before.instanceLookups;
    note("one event with 31 active quests: subscribers " + std::to_string(subscribers) + ", quest visits " + std::to_string(visits)
         + ", instance lookups " + std::to_string(lookups));
    check(quest(shop, "CounterQuest")->tasks[0].progress == 1 && subscribers == 1, "control: the event visits its one indexed subscriber");
    check(visits == 0 && lookups <= 4, "the event visits no quest of the list: only the lookups of its one instance by its key");
}

void refused_gather(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    interact_quest(quests, 1);
    quests.load(shop);
    tagged_node(shop, 0.1F);
    shop.enter();
    check(grant(shop, "InteractQuest").ok(), "setup: the interaction task");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: a pick");
    check(shop.host.world.inventory().give(kCharacterA, "TestFiller", 500).ok(), "setup: the 500 slots of the bag are full");
    const std::uint64_t fullBags = shop.gathering().counts().fullBags;
    shop.request(shop.clientA, shop.capital_block());
    check(quest(shop, "InteractQuest")->tasks[0].progress == 0, "control: no completion at the start of a timed use");
    shop.hold(shop.clientA, shop.capital_block(), 4500);
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().fullBags == fullBags + 1 && count(shop, "TestOreT1") == 0, "setup: the full bag refuses the gather");
    note("after the refused gather: " + states_text(*quest(shop, "InteractQuest")));
    check(quest(shop, "InteractQuest")->tasks[0].progress == 0, "a refused gather completes no interaction");
}

void quest_object_once(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    interact_quest(quests, 3);
    quests.load(shop);
    tagged_node(shop, 0.1F);
    json document = gathering_document();
    document["rows"].push_back(
        row_json("ReviewQuestObject", "QuestGatherables", "None", "NoToolNecessary", "", 0, 1, 100, 100, "Empty", "", true));
    document["rows"].back()["quest"] = true;
    const gamedata::InteractionObject object = object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex});
    document["capitals"][object.key] = json::array({kOreSlice, static_cast<int>(kRowCount), "review_tag", true});
    check(shop.host.gathering.set(document.dump(), {{kRegion, region_records()}}),
          "setup: a quest object whose tag is the tag of its interaction bound");
    shop.enter();
    check(grant(shop, "InteractQuest").ok(), "setup: the task of the tag opens the object");
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    check(shop.gathering().counts().completions == 1, "control: one gather of the quest object committed");
    note("one gather of the quest object: " + states_text(*quest(shop, "InteractQuest")));
    check(quest(shop, "InteractQuest")->tasks[0].progress == 1, "one gather completes its interaction one time (one path)");
}

void give_reaches_new_task(const Options&) {
    GatherShop shop;
    ReviewQuests quests;
    quests.task("Have", "taskhaveitems", 25, {{"ItemName", "TestGemA"}});
    quests.task("Give", "taskgiveitem", 33, {{"ItemName", "TestGemA"}});
    quests.objective("ParallelQuest", {"Have", "Give"});
    quests.load(shop);
    shop.enter();
    check(grant(shop, "ParallelQuest").ok(), "setup: a possession task and a give of its item in one container");
    steps(shop);
    note("after the give: gems " + std::to_string(count(shop, "TestGemA")) + "; " + states_text(*quest(shop, "ParallelQuest")));
    check(count(shop, "TestGemA") == 1 && quest(shop, "ParallelQuest")->tasks[0].state == kComplete,
          "the committed give reaches the possession task of the same grant");
}

} // namespace

std::vector<Case> review_quests_18c_cases() {
    return {
        {"QRW1", "review of 1.8c, QR8C-R1: a refused event write: a second delivery counts one time; the retry of the world", event_write_refused},
        {"QRW2", "review of 1.8c, QR8C-R2: the item actions of a plan apply in the order of the tasks", item_action_order},
        {"QRW3", "review of 1.8c, QR8C-R3: a reset of a child or of a parent keeps the link of the child to its parent", reset_links},
        {"QRW4", "review of 1.8c, QR8C-R4: a return removes the equipped item that it counted", equipped_return},
        {"QRW5", "review of 1.8c, QR8C-R5: an abandon updates the achievement waits of the other quests", abandon_waits},
        {"QRW6", "review of 1.8c, QR8C-R6: EnforceLocationRequirement applies to a possession task", location_possession},
        {"QRW7", "review of 1.8c, QR8C-R7: a place with no shape is no proof of an exit", exit_without_shape},
        {"QRW8", "review of 1.8c, QR8C-R8: a timed gather: no interaction at its start, one at its success", timed_gather},
        {"QRW9", "review of 1.8c, QR8C-R9: a kill drop that the full bag lost does not use AwardOnlyOnce", lost_kill_drop},
        {"QRW10", "review of 1.8c, QR8C-R10: the craft event has the gear score of a fixed output", craft_score},
        {"QRW11", "review of 1.8c, QR8C-R11: one indexed event visits no unrelated quest (the counted visits and lookups)", event_cost},
        {"QRW12", "review of 1.8c, QR8C-R12: a gather that the full bag refused completes no interaction", refused_gather},
        {"QRW13", "review of 1.8c, QR8C-R13: one gather of a quest object completes its interaction one time", quest_object_once},
        {"QRW14", "review of 1.8c, QR8C-R14: a give of a new quest reaches its new possession task", give_reaches_new_task},
    };
}

} // namespace soulwarden::backend::tests

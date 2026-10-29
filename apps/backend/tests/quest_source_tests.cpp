// The cases of stage QG8: the quest item sources (docs/systems/quests.md, the item sources of stage QG8). The quest data has the form of the
// tool (quest-data.json, format 1) with made-up quests (Test_QG8_*); the fixture of the gathering cases gives the world (a capital node that is
// a quest object, the wolf, the items, the loot tables). All accounts, characters, and names are made up.
//
//   QG8A  a possession task drops at the gather of a quest object of its ItemDropVC (client text 0x825F310, 0x825F470; the drop predicate
//         0x3B03890, source type 4): the item of A only (MP-10: B gets nothing, then B gathers its own object); a possession task that names
//         an ordinary node row gets no drop at its gather (the gather-mode gate of the source types 2 and 7)
//   QG8B  the planner of the drops: the key of a quest object (questObject) reaches a possession task; the key of an ordinary gather and of a
//         kill does not (the destruction of a destructible quest object uses the same key: QuestObjects::destroy)
//   QG8C  an ItemReward "[LTID]NAME" of the game event of a TaskTriggerGameEvent rolls the loot table (one of two tickets, an OR table with
//         equal thresholds); B gets nothing (MP-10); a full bag makes the task wait, and a change of the bag rolls again; a table that the
//         loot data does not have gives no item, and the task completes
//   QG8D  the support manifest: the quest object source of a possession task, the items of a loot table of a game event (the history
//         decides), the reason and the class of an item with no source (a task drop of an ordinary row, a chest drop, a loot table with no
//         root, none), the count itemTasksWithNoSource
//
// The cases of the code review of stage QG8 (in stage DG1; the findings: ~/Games/soulwarden/work/evidence/quests/qg8/code-review/FINDINGS.md).
// Each one fails on the tree of stage QG8 (trees/after-qg8):
//   QG8R4  finding QG8R-R4: an ItemRewardQty of a loot table above the roll limit (kRewardTableRolls, 100) is refused, not cut: no ticket, the
//          task waits, and the manifest blocks it; the limit itself (100) gives 100 tickets; B gets nothing (MP-10)
//   QG8R5  finding QG8R-R5: an event with ItemRewardQty 0 completes and gives no ticket, and the manifest names it as no source of the tickets

#include <string>
#include <tuple>
#include <vector>

#include "gamedata/game_event.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;
using world::quest_state::kActive;
using world::quest_state::kComplete;

/** The quest data of the cases in the form of the tool (one objective for each source). */
json source_quests() {
    json tasks = json::object();
    json objectives = json::array();
    const auto task = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = {{"id", id}, {"crc", 0}, {"type", type}, {"code", code}, {"cells", cells}};
    };
    const auto objective = [&](const char* id, const std::vector<std::tuple<int, const char*, std::vector<int>>>& tree) {
        json nodes = json::array();
        for (const auto& [index, taskId, children] : tree) {
            nodes.push_back(json::array({index, gamedata::lower_text(taskId), children}));
        }
        objectives.push_back({{"id", id}, {"cells", {{"Type", "Journey"}}}, {"tree", nodes}});
    };
    // A possession task whose ItemDropVC names the row of a quest object (the coffin of 02A_Side08_02_LostReturned).
    task("Test_QG8_Coffin", "taskhaveandreturnitems", 26,
         {{"ItemName", "TestGemB"}, {"TargetQty", "1"}, {"ItemDropVC", "Test_QG8_CoffinRow"}, {"ItemDropProbability", "100"},
          {"InteractTag", "IO_Test_QG8_Coffin"}, {"PauseAndClearLootOnTaskComplete", "1"}});
    objective("Test_QG8_CoffinQuest", {{0, "Test_QG8_Coffin", {}}});
    // A possession task whose ItemDropVC names an ordinary node row.
    task("Test_QG8_HaveOre", "taskhaveitems", 25,
         {{"ItemName", "TestQG8Token"}, {"TargetQty", "1"}, {"ItemDropVC", "Test_OreSmall"}, {"ItemDropProbability", "100"}});
    objective("Test_QG8_OreQuest", {{0, "Test_QG8_HaveOre", {}}});
    // The lottery: the game event gives one ticket of a loot table; one ticket completes the partial container.
    task("Test_QG8_Lottery", "tasktriggergameevent", 15, {{"KillEnemyType", "Test_QG8_Lottery_Event"}});
    task("Test_QG8_TicketA", "taskhaveandreturnitems", 26, {{"ItemName", "TestQG8TicketA"}, {"TargetQty", "1"}});
    task("Test_QG8_TicketB", "taskhaveandreturnitems", 26, {{"ItemName", "TestQG8TicketB"}, {"TargetQty", "1"}});
    task("Test_QG8_Tickets", "partialtaskcontainer", 2);
    task("Test_QG8_LotteryRoot", "consecutivetaskcontainer", 1);
    objective("Test_QG8_LotteryQuest",
              {{0, "Test_QG8_Lottery", {}}, {1, "Test_QG8_TicketA", {}}, {2, "Test_QG8_TicketB", {}}, {3, "Test_QG8_Tickets", {1, 2}},
               {4, "Test_QG8_LotteryRoot", {0, 3}}});
    // A game event whose table the loot data does not have.
    task("Test_QG8_NoTable", "tasktriggergameevent", 15, {{"KillEnemyType", "Test_QG8_NoTable_Event"}});
    objective("Test_QG8_NoTableQuest", {{0, "Test_QG8_NoTable", {}}});
    // The items with no source of the manifest: a chest drop of a tag with no point of interest, an item of a table with no root, none.
    task("Test_QG8_ChestOnly", "taskhaveitems", 25,
         {{"ItemName", "TestPick"}, {"TargetQty", "1"}, {"ChestDropProbability", "100"}, {"POITag", "test_qg8_nowhere"}});
    objective("Test_QG8_ChestQuest", {{0, "Test_QG8_ChestOnly", {}}});
    task("Test_QG8_Orphan", "taskhaveitems", 25, {{"ItemName", "TestKnife"}, {"TargetQty", "1"}});
    objective("Test_QG8_OrphanQuest", {{0, "Test_QG8_Orphan", {}}});
    task("Test_QG8_Nothing", "taskhaveitems", 25, {{"ItemName", "TestAxe"}, {"TargetQty", "1"}});
    objective("Test_QG8_NothingQuest", {{0, "Test_QG8_Nothing", {}}});
    json events = json::object();
    events["test_qg8_lottery_event"] = {{"id", "Test_QG8_Lottery_Event"}, {"ItemReward", "[LTID]Test_QG8_Tickets"}};
    events["test_qg8_notable_event"] = {{"id", "Test_QG8_NoTable_Event"}, {"ItemReward", "[LTID]Test_QG8_Absent"}};
    return json{{"format", 1},          {"objectives", objectives},     {"tasks", tasks},
                {"achievements", json::array()}, {"npa", json::array()}, {"territories", json::array()},
                {"backstories", json::array()}, {"events", events}};
}

/** The gathering data: the capital node is the quest object Test_QG8_CoffinRow (the form of a quest row: no loot, a game event). */
json source_gathering(bool questObject) {
    json document = gathering_document();
    if (questObject) {
        document["rows"].push_back(row_json("Test_QG8_CoffinRow", "QuestGatherables", "None", "NoToolNecessary", "", 0, 1.0, 200.0, 300.0, "Empty",
                                            "Test_QG8_Coffin_interact", true));
        document["rows"].back()["quest"] = true;
        const std::string key = object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex}).key;
        document["capitals"][key] = json::array({kOreSlice, static_cast<int>(kRowCount), "IO_Test_QG8_Coffin", true});
    }
    return document;
}

/** A gathering shop with the quests and the loot tables of the cases (the tickets, a table with no root). */
struct SourceShop : GatherShop {
    explicit SourceShop(bool questObject = true) {
        std::vector<gamedata::ItemDefinition> items = gathering_items();
        items.push_back(chests::chest_item("TestAxe", "Weapon", "Weapon", 0, 1));
        for (const char* id : {"TestQG8TicketA", "TestQG8TicketB", "TestQG8Token"}) {
            items.push_back(chests::chest_item(id, "Resource", "Resource+Quest", 0, 100));
        }
        host.catalog.set(items);
        gamedata::LootData loot = gathering_loot();
        loot.tables.push_back(chests::loot_table("Test_QG8_Tickets", false, 1, {},
                                                 {chests::loot_entry(gamedata::LootEntryKind::item, "TestQG8TicketA", 0),
                                                  chests::loot_entry(gamedata::LootEntryKind::item, "TestQG8TicketB", 0)}));
        loot.tables.back().bonus = gamedata::RollBonus::ignoreBonus;
        loot.tables.push_back(chests::loot_table("Test_QG8_OrphanTable", true, 0, {}, {chests::loot_entry(gamedata::LootEntryKind::item, "TestKnife", 0)}));
        if (!host.loot.set(loot)) {
            throw std::runtime_error("QG8 test: the loot tables were not set: " + host.loot.error());
        }
        if (!host.gathering.set(source_gathering(questObject).dump(), {{kRegion, region_records()}})) {
            throw std::runtime_error("QG8 test: the gathering data was not set: " + host.gathering.error());
        }
        if (!host.quests.load_text(source_quests().dump())) {
            throw std::runtime_error("QG8 test: the quest data did not load: " + host.quests.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    bool grant(const char* character, const char* objective) {
        return quests().grant(*entity(character), objective, "command", 0).ok();
    }
    const world::QuestInstance* quest(const char* character, const char* objective) {
        return questfix::quest_of(*entity(character), objective);
    }
    void settle(int steps = 3) {
        host.steps(steps, {&clientA, &clientB});
    }
};

std::uint32_t count_of(const world::Entity& entity, const char* item) {
    return static_cast<std::uint32_t>(world::Inventory::count(entity, item));
}

std::uint8_t state_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].state : 255;
}

void quest_object_drop(const Options&) {
    SourceShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.grant(kCharacterA, "Test_QG8_CoffinQuest") && shop.grant(kCharacterB, "Test_QG8_CoffinQuest"), "setup: A and B have the coffin task");
    check(shop.quests().quest_object_open(a, "IO_Test_QG8_Coffin"), "the quest object is open for A (the InteractTag of the possession task)");
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    shop.settle();
    check(count_of(a, "TestGemB") == 1 && state_of(shop.quest(kCharacterA, "Test_QG8_CoffinQuest"), 0) == kComplete,
          "A gathers the quest object: the item of the possession task (ItemDropVC names the row) is in its bag, and the task completes");
    check(count_of(b, "TestGemB") == 0 && state_of(shop.quest(kCharacterB, "Test_QG8_CoffinQuest"), 0) == kActive,
          "B gets nothing of the gather of A (the drop of A only, MP-10)");
    shop.gather(shop.clientB, shop.capital_block(), 1500);
    shop.settle();
    check(count_of(b, "TestGemB") == 1 && state_of(shop.quest(kCharacterB, "Test_QG8_CoffinQuest"), 0) == kComplete && count_of(a, "TestGemB") == 1,
          "B gathers its own object and gets its own item; A does not change");
    check(shop.quests().counts().drops == 2, "two quest drops in all");
    // A possession task that names an ordinary node row: no drop at its gather.
    SourceShop ore(false);
    ore.enter();
    world::Entity& c = *ore.entity(kCharacterA);
    check(ore.grant(kCharacterA, "Test_QG8_OreQuest"), "setup: A has the possession task of the ore row");
    check(ore.equip(kCharacterA, "TestPick", 36), "setup: a pick");
    ore.gather(ore.clientA, ore.capital_block(), 4500);
    ore.settle();
    check(count_of(c, "TestOreT1") == 2 && count_of(c, "TestQG8Token") == 0 && ore.quests().counts().drops == 0,
          "the gather of the ordinary node gives its loot and no drop of the possession task (the gather-mode gate of the source types 2, 7)");
}

void drop_planner(const Options&) {
    SourceShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "Test_QG8_CoffinQuest"), "setup: A has the coffin task");
    check(shop.quests().plan_drops(a, true, {"Test_QG8_CoffinRow"}).empty(), "the key of an ordinary gather does not reach the possession task");
    check(shop.quests().plan_drops(a, false, {"Test_QG8_CoffinRow"}).size() == 1, "control: the key of a kill reaches it (a category of ItemDropVC)");
    const std::vector<world::QuestDrop> drops = shop.quests().plan_drops(a, true, {"Test_QG8_CoffinRow"}, true);
    check(drops.size() == 1 && drops[0].itemId == "TestGemB" && drops[0].quantity == 1,
          "the key of a quest object reaches the possession task: one item (the gather and the destruction of a quest object)");
    check(shop.quests().plan_drops(*shop.entity(kCharacterB), true, {"Test_QG8_CoffinRow"}, true).empty(), "B with no task: no drop");
}

void event_table(const Options&) {
    SourceShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.grant(kCharacterA, "Test_QG8_LotteryQuest"), "the grant of the lottery quest");
    shop.settle();
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test_QG8_LotteryQuest");
    const std::uint32_t tickets = count_of(a, "TestQG8TicketA") + count_of(a, "TestQG8TicketB");
    note("QG8C: ticket A " + std::to_string(count_of(a, "TestQG8TicketA")) + ", ticket B " + std::to_string(count_of(a, "TestQG8TicketB")));
    check(tickets == 1 && state_of(quest, 0) == kComplete, "the game event of the task rolls its loot table [LTID]Test_QG8_Tickets: one ticket");
    check(quest == nullptr || state_of(quest, 3) == kComplete, "one ticket completes the partial container");
    check(count_of(b, "TestQG8TicketA") + count_of(b, "TestQG8TicketB") == 0, "B gets nothing (MP-10)");
    // Equal thresholds: one of the two entries at random; many rolls give both.
    std::uint32_t gemA = 0;
    std::uint32_t gemB = 0;
    for (int roll = 0; roll < 40; ++roll) {
        const std::optional<std::uint32_t> table = shop.host.loot.table_index("Test_QG8_Tickets");
        world::LootContext context;
        for (const world::LootDrop& drop : shop.host.world.rewards().roll_root(*table, context)) {
            gemA += drop.itemId == "TestQG8TicketA" ? 1 : 0;
            gemB += drop.itemId == "TestQG8TicketB" ? 1 : 0;
        }
    }
    check(gemA > 0 && gemB > 0 && gemA + gemB == 40, "the OR table of equal thresholds gives one entry for each roll, each one at random");
    // A full bag: the task waits, and a change of the bag rolls again.
    std::uint32_t axes = 0;
    while (axes < 1000 && shop.host.world.inventory().give(kCharacterB, "TestAxe", 1).ok()) {
        ++axes;
    }
    check(axes > 0, "setup: the bag of B is full (" + std::to_string(axes) + " axes)");
    check(shop.grant(kCharacterB, "Test_QG8_LotteryQuest"), "setup: B has the lottery quest with a full bag");
    shop.settle();
    check(state_of(shop.quest(kCharacterB, "Test_QG8_LotteryQuest"), 0) == kActive && count_of(b, "TestQG8TicketA") + count_of(b, "TestQG8TicketB") == 0,
          "a full bag: no ticket, the task waits");
    bool removed = false;
    for (std::uint32_t slot = 0; !removed && slot < b.bag.slots.size(); ++slot) {
        if (b.bag.slots[slot].item && b.bag.slots[slot].item->itemId == "TestAxe") {
            removed = shop.host.world.inventory().remove(kCharacterB, slot, 1).ok();
        }
    }
    check(removed, "setup: one axe leaves the bag of B (a change of the bag)");
    shop.settle();
    check(state_of(shop.quest(kCharacterB, "Test_QG8_LotteryQuest"), 0) == kComplete && count_of(b, "TestQG8TicketA") + count_of(b, "TestQG8TicketB") == 1,
          "the change of the bag tries the event again: a new roll, one ticket");
    // A table that the loot data does not have.
    check(shop.grant(kCharacterA, "Test_QG8_NoTableQuest"), "setup: the quest of an event with no table");
    shop.settle();
    check(state_of(shop.quest(kCharacterA, "Test_QG8_NoTableQuest"), 0) == kComplete && count_of(a, "TestQG8TicketA") + count_of(a, "TestQG8TicketB") == 1,
          "a table that the loot data does not have: no item, the event applies, and the task completes");
}

void manifest(const Options&) {
    SourceShop shop;
    world::QuestSupport support(shop.host.world);
    const json document = support.manifest({});
    const auto task_of = [&](const char* objective) -> json {
        for (const json& row : document["objectives"]) {
            if (row["id"] == objective) {
                return row["tasks"][0];
            }
        }
        return json();
    };
    const json coffin = task_of("Test_QG8_CoffinQuest");
    note("QG8D coffin: " + coffin.dump());
    check(coffin.value("supported", false) && coffin.value("supplier", "") == "the quest drop of test_qg8_coffinrow",
          "a possession task with the row of a quest object: supported, the quest drop of the row");
    const json ore = task_of("Test_QG8_OreQuest");
    note("QG8D ore: " + ore.dump());
    check(!ore.value("supported", true) && ore.value("itemSource", "") == "task drop"
              && ore.value("reason", "").find("a gatherable row that is not a quest object") != std::string::npos,
          "a possession task with an ordinary row: no source; the class task drop and its cause");
    json lottery;
    for (const json& row : document["objectives"]) {
        if (row["id"] == "Test_QG8_LotteryQuest") {
            lottery = row;
        }
    }
    check(!lottery.is_null() && lottery["tasks"][1].value("questItem", "") == "testqg8ticketa" && lottery["tasks"][2].value("questItem", "") == "testqg8ticketb"
              && lottery["tasks"][1].value("supplier", "").find("the loot table Test_QG8_Tickets of the game event Test_QG8_Lottery_Event")
                     != std::string::npos,
          "the tickets come from the loot table of the game event of the tree (the history decides)");
    const json chest = task_of("Test_QG8_ChestQuest");
    const json orphan = task_of("Test_QG8_OrphanQuest");
    const json nothing = task_of("Test_QG8_NothingQuest");
    note("QG8D chest: " + chest.value("reason", "") + "; orphan: " + orphan.value("reason", "") + "; none: " + nothing.value("reason", ""));
    check(chest.value("itemSource", "") == "chest drop" && chest.value("reason", "").find("the quest drop of the chests") != std::string::npos,
          "a chest drop of a tag with no chest: the class chest drop");
    check(orphan.value("itemSource", "") == "loot table"
              && orphan.value("reason", "").find("the loot table Test_QG8_OrphanTable: no source of the world reaches it (no root") != std::string::npos,
          "an item of a table that no root reaches: the class loot table, with the table");
    check(nothing.value("itemSource", "") == "none", "an item with no declared source: the class none");
    const json counts = document["counts"]["itemTasksWithNoSource"];
    note("QG8D counts: " + counts.dump());
    check(counts.value("task drop", 0) == 1 && counts.value("chest drop", 0) == 1 && counts.value("loot table", 0) == 1 && counts.value("none", 0) == 1,
          "the count of the item tasks with no source by class");
}

} // namespace

/** The lottery data with an ItemRewardQty, and a tickets table that gives ticket A at each roll (an AND table with one entry). */
void lottery_with_quantity(SourceShop& shop, const std::string& quantity) {
    json quests = source_quests();
    quests["events"]["test_qg8_lottery_event"]["ItemRewardQty"] = quantity;
    check(shop.host.quests.load_text(quests.dump()), "setup: the quest data with the ItemRewardQty " + quantity + " loads");
    gamedata::LootData loot = shop.host.loot.data();
    for (gamedata::LootTable& table : loot.tables) {
        if (table.id == "Test_QG8_Tickets") {
            table.all = true;
            table.maxRoll = 0;
            table.entries.resize(1);
            table.entries[0].threshold = 0;
        }
    }
    check(shop.host.loot.set(loot), "setup: the tickets table gives one ticket A at each roll");
}

const json* manifest_row(const json& manifest, const char* id) {
    for (const json& row : manifest["objectives"]) {
        if (row["id"] == id) {
            return &row;
        }
    }
    return nullptr;
}

void review_event_limit(const Options&) {
    {
        SourceShop shop;
        lottery_with_quantity(shop, "100");
        shop.enter();
        check(shop.grant(kCharacterA, "Test_QG8_LotteryQuest"), "setup: A starts the lottery");
        shop.settle();
        check(count_of(*shop.entity(kCharacterA), "TestQG8TicketA") == 100 && state_of(shop.quest(kCharacterA, "Test_QG8_LotteryQuest"), 0) == kComplete,
              "the limit itself: ItemRewardQty 100 rolls the table 100 times (100 tickets), and the event completes");
    }
    SourceShop shop;
    lottery_with_quantity(shop, "101");
    shop.enter();
    check(shop.grant(kCharacterA, "Test_QG8_LotteryQuest"), "setup: A starts the lottery");
    shop.settle();
    const std::uint32_t tickets = count_of(*shop.entity(kCharacterA), "TestQG8TicketA");
    note("QG8R4: ItemRewardQty 101 gives " + std::to_string(tickets) + " ticket(s)");
    check(tickets == 0 && state_of(shop.quest(kCharacterA, "Test_QG8_LotteryQuest"), 0) == kActive,
          "ItemRewardQty 101 (above the limit of 100 rolls) is refused, not cut: no ticket, and the event task waits (no part of its reward)");
    check(count_of(*shop.entity(kCharacterB), "TestQG8TicketA") == 0, "B gets nothing (MP-10)");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const json* row = manifest_row(manifest, "Test_QG8_LotteryQuest");
    check(row != nullptr && !(*row)["tasks"][0].value("supported", true)
              && (*row)["tasks"][0].value("reason", "").find("above the limit of 100 rolls") != std::string::npos,
          "the manifest blocks the event task with the cause (the limit of the rolls)");
    check(row != nullptr && (*row)["tasks"][1].value("questItem", "").empty(), "the manifest names no ticket of the refused event");
}

void review_event_zero(const Options&) {
    SourceShop shop;
    lottery_with_quantity(shop, "0");
    shop.enter();
    check(shop.grant(kCharacterA, "Test_QG8_LotteryQuest"), "setup: A starts the lottery");
    shop.settle();
    const std::uint32_t tickets = count_of(*shop.entity(kCharacterA), "TestQG8TicketA") + count_of(*shop.entity(kCharacterA), "TestQG8TicketB");
    check(tickets == 0 && state_of(shop.quest(kCharacterA, "Test_QG8_LotteryQuest"), 0) == kComplete,
          "setup: ItemRewardQty 0 completes the event and gives no ticket (the rule of the world)");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const json* row = manifest_row(manifest, "Test_QG8_LotteryQuest");
    check(row != nullptr, "setup: the lottery row is in the manifest");
    note("QG8R5: the ticket task: " + (row != nullptr ? (*row)["tasks"][1].dump() : std::string("no row")));
    check(row != nullptr && !(*row)["tasks"][1].value("supported", true) && (*row)["tasks"][1].value("questItem", "").empty(),
          "an event with ItemRewardQty 0 supplies no ticket in the manifest (the ticket task has no source)");
}

std::vector<Case> quest_source_cases() {
    return {{"QG8A", "quest sources QG8 (I2, I6, MP-10): a possession task drops at the gather of a quest object of its ItemDropVC", quest_object_drop},
            {"QG8B", "quest sources QG8 (I2): the key of a quest object in the drop planner", drop_planner},
            {"QG8C", "quest sources QG8 (I2, I6, MP-10): the loot table [LTID] of the game event of a task", event_table},
            {"QG8D", "quest sources QG8 (I2, I3): the support manifest of the new sources and the reasons of the others", manifest},
            {"QG8R4", "review of QG8, finding QG8R-R4: an event quantity above the roll limit is refused, not cut", review_event_limit},
            {"QG8R5", "review of QG8, finding QG8R-R5: an event with ItemRewardQty 0 supplies no item in the manifest", review_event_zero}};
}

} // namespace soulwarden::backend::tests

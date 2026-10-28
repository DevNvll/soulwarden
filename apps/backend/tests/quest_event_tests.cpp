// Cases of stage 1.8c: the events of the quests (world/quests.h, docs/systems/quests.md): the kills, the items, the equipment, the gathers and
// the quest objects, the crafts, the places, the screens, the child objectives, the interactions, the deaths, the abandon, the reset, the
// failure, two sessions, the load, and the loaders. The quest data has the form of the tool (quest-data.json, format 1) with made-up quests
// (Test18c_*) and the tree of 06_skinningknife of the game data; the fixture of the gathering cases gives the world (the wolf with its
// credit, the skinning, a capital ore node, the items). All accounts, characters, and names are made up.
//
//   QE1   a credited kill (G10): the vitals categories of the victim, the kill event, the credit list (one receiver: the hit of B gives B
//         nothing), the quest drop of a kill into the bag, a repeated result counts one time
//   QE2   the items (G11): TaskGiveItem with AwardOnlyOnce (no second item after a reset), TaskHaveItems with StayActive and its regression,
//         TaskEquipItem with MinGearScore, TaskRemoveItem (the take by item id)
//   QE3   the gathers (G12): the quest drop of a skinning (ItemDropVC = the gatherable row, AwardOnlyOnce, PauseAndClearLoot), the count of
//         TaskRequireGatherItems (not the drops)
//   QE4   the quest objects (G14): the gate of InteractOnlyOnQuest (refused with no task, open with the task), the quest drop, the game event
//         of the row (TaskGameEvent), no shared depletion (B gathers its own object)
//   QE5   the crafts and the refines: TaskCraftRecipe (MinGearScore), CraftAnyRecipe, TaskCraftMultipleRecipes, TaskRefineRecipe
//   QE6   the places (G7): TaskExitPOITag and TaskGoToPOITag at the activation and after a move (the tick), the territory of a tag, the
//         location rule of a counter (EnforceLocationRequirement), the regression of a StayActive place task, the cue 2297, no POI data
//   QE7   the screen (2617), TaskObjective (a child objective and its completion), TaskInteract
//   QE8   two sessions (MP-10): overlapping tasks and different item counts; one credited kill changes only its credit list; the drops and
//         the returns of each character; the Objectives state of one never reaches the other
//   QE9   the load: 50 characters, 10,000 events, 2,000 creatures with no quest: only the indexed subscribers; a repeated result; the bound
//         store of the results; the time
//   QE10  the trees: the leaves 3 and 9 of 06_skinningknife (a repeated leaf with two states), a partial container, a reused container
//         (a hole of the tree), the concurrent and consecutive graphs
//   QE11  the abandon (3836) and the reset (5600): CanBeAbandoned, the undo of the achievement actions and of a temporary unlock, the
//         returns, the quest item of TaskGiveAndTakeItem, the award rows, the pins, a reconnect
//   QE12  the failures: a failure timer, TaskDie with FailOnCondition and with ResetOnCondition
//   QE13  the loaders: the cells of the event tasks, the POI data, the quest objects of the gathering data
//   QRV7  the review of 1.8a, QR8A-R7: an achievement event visits only the indexed waits of its character and counts each visit
//   QE-TOOL the checks of the data tools (check_pois_tool.py, check_gathering_tool.py, check_loot_tool.py)

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "gamedata/poi_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::decode_objectives;
using questfix::json;
using questfix::own_fragments;
using questfix::quest_of;
using questfix::states_text;

constexpr std::uint32_t kPickSlot = 36;

/** The quest data of the cases of stage 1.8c: made-up quests, one for each event, in the form of the tool. */
struct QuestDoc {
    json tasks = json::object();
    json objectives = json::array();
    json achievements = json::array();
    std::uint32_t nextBit = 200;

    void task(const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = {{"id", id}, {"crc", 0}, {"type", type}, {"code", code}, {"cells", cells}};
    }
    /** @param tree [index, task id, [children]] in index order. */
    void objective(const char* id, json cells, const std::vector<std::tuple<int, const char*, std::vector<int>>>& tree) {
        json nodes = json::array();
        for (const auto& [index, taskId, children] : tree) {
            nodes.push_back(json::array({index, gamedata::lower_text(taskId), children}));
        }
        if (!cells.contains("Type")) {
            cells["Type"] = "Journey";
        }
        objectives.push_back({{"id", id}, {"cells", cells}, {"tree", nodes}});
    }
    void achievement(const char* id) {
        achievements.push_back(json::array({id, nextBit++}));
    }
    [[nodiscard]] std::string text() const {
        return json{{"format", 1},          {"objectives", objectives},     {"tasks", tasks},
                    {"achievements", achievements}, {"npa", json::array()}, {"territories", json::array()},
                    {"backstories", json::array()}}
            .dump();
    }
};

QuestDoc event_quests() {
    QuestDoc doc;
    for (const char* id : {"Test18c_Before", "Test18c_After", "Test18c_Flag", "Test18c_Stay", "Test18c_Temp"}) {
        doc.achievement(id);
    }
    // A kill, its kill event, and a quest drop of a kill.
    doc.task("Test18c_Kill", "taskkillcontribution", 13, {{"KillEnemyType", "Test_BagWolf_Category"}, {"TargetQty", "2"}});
    doc.task("Test18c_KillEvent", "taskgameevent", 14, {{"KillEnemyType", "KillEvent"}, {"TargetQty", "1"}});
    doc.task("Test18c_KillDrop", "taskhaveandreturnitems", 26,
             {{"ItemName", "TestFiberT1"}, {"TargetQty", "1"}, {"ItemDropVC", "Test_BagWolf, Other_Category"}, {"ItemDropProbability", "100"},
              {"PauseAndClearLootOnTaskComplete", "1"}});
    doc.task("Test18c_Kill_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_KillQuest", json::object(),
                  {{0, "Test18c_Kill", {}}, {1, "Test18c_KillEvent", {}}, {2, "Test18c_KillDrop", {}}, {3, "Test18c_Kill_Root", {0, 1, 2}}});
    // The items: a give (AwardOnlyOnce), a possession with StayActive, an equip, a removal.
    doc.task("Test18c_Give", "taskgiveitem", 33, {{"ItemName", "TestGemB"}, {"TargetQty", "1"}, {"AwardOnlyOnce", "1"}});
    doc.task("Test18c_HaveWood", "taskhaveitems", 25, {{"ItemName", "TestWoodT1"}, {"TargetQty", "3"}, {"StayActive", "1"}});
    doc.task("Test18c_EquipPick", "taskequipitem", 45, {{"ItemName", "TestPick"}, {"TargetQty", "1"}, {"MinGearScore", "150"}});
    doc.task("Test18c_RemoveStone", "taskremoveitem", 36, {{"ItemName", "TestStoneT1"}, {"TargetQty", "2"}});
    // Stage 1.8d: a last child that waits (an achievement that the data does not know): the quest is not ready, so its StayActive
    // possession regresses (a quest whose root is satisfied holds its StayActive tasks: case QD11).
    doc.task("Test18c_WaitNever", "taskachievement", 18, {{"ItemName", "Test18c_Never_Known"}});
    doc.task("Test18c_Items_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_ItemQuest", json::object(),
                  {{0, "Test18c_Give", {}},
                   {1, "Test18c_HaveWood", {}},
                   {2, "Test18c_EquipPick", {}},
                   {3, "Test18c_RemoveStone", {}},
                   {4, "Test18c_WaitNever", {}},
                   {5, "Test18c_Items_Root", {0, 1, 2, 3, 4}}});
    // The gathers: a quest drop of the skinning, the count of the ore of a gather.
    doc.task("Test18c_SkinDrop", "taskgatheritems", 29,
             {{"ItemName", "TestGemA"}, {"TargetQty", "2"}, {"ItemDropVC", "Test_WolfSkin"}, {"ItemDropProbability", "100"},
              {"AwardOnlyOnce", "1"}, {"PauseAndClearLootOnTaskComplete", "1"}});
    doc.task("Test18c_GatherOre", "taskrequiregatheritems", 31, {{"ItemName", "TestOreT1"}, {"TargetQty", "4"}});
    doc.task("Test18c_Gather_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_GatherQuest", json::object(),
                  {{0, "Test18c_SkinDrop", {}}, {1, "Test18c_GatherOre", {}}, {2, "Test18c_Gather_Root", {0, 1}}});
    // A quest object: its drop and its game event.
    doc.task("Test18c_Chest", "taskgatherandreturnitems", 30,
             {{"ItemName", "TestHideT1"}, {"TargetQty", "1"}, {"ItemDropVC", "Test18c_ChestRow"}, {"ItemDropProbability", "100"},
              {"InteractTag", "IO_Test18c_Chest"}});
    doc.task("Test18c_ChestEvent", "taskgameevent", 14, {{"KillEnemyType", "Test18c_ChestRow_interact"}, {"TargetQty", "1"}});
    doc.task("Test18c_Chest_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_ChestQuest", json::object(),
                  {{0, "Test18c_Chest", {}}, {1, "Test18c_ChestEvent", {}}, {2, "Test18c_Chest_Root", {0, 1}}});
    // Stage QG5a: a task of two events of the quest object (the object stays open for the character while the task is Active).
    doc.task("Test18c_ChestTwice", "taskgameevent", 14, {{"KillEnemyType", "Test18c_ChestRow_interact"}, {"TargetQty", "2"}, {"InteractTag", "IO_Test18c_Chest"}});
    doc.objective("Test18c_ChestTwiceQuest", json::object(), {{0, "Test18c_ChestTwice", {}}});
    // The crafts and the refines.
    doc.task("Test18c_Craft", "taskcraftrecipe", 38, {{"CraftingRecipeId", "Test_Recipe"}, {"TargetQty", "2"}, {"MinGearScore", "250"}});
    doc.task("Test18c_CraftAny", "taskcraftrecipe", 38, {{"CraftAnyRecipe", "1"}, {"TargetQty", "3"}});
    doc.task("Test18c_CraftMany", "taskcraftmultiplerecipes", 86, {{"ItemDropVC", "Test_RecipeA,Test_RecipeB"}, {"TargetQty", "2"}});
    doc.task("Test18c_Refine", "taskrefinerecipe", 42, {{"CraftingRecipeId", "Test_Refine"}, {"TargetQty", "5"}});
    doc.task("Test18c_Craft_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_CraftQuest", json::object(),
                  {{0, "Test18c_Craft", {}},
                   {1, "Test18c_CraftAny", {}},
                   {2, "Test18c_CraftMany", {}},
                   {3, "Test18c_Refine", {}},
                   {4, "Test18c_Craft_Root", {0, 1, 2, 3}}});
    // The places.
    doc.task("Test18c_LeaveA", "taskexitpoitag", 9, {{"POITag", "test_poi_a"}, {"TerritoryID", "6"}, {"StayActive", "1"}});
    doc.task("Test18c_GoToB", "taskgotopoitag", 6, {{"POITag", "test_poi_b"}});
    doc.task("Test18c_EventInB", "taskgameevent", 14,
             {{"KillEnemyType", "Test18c_PlaceEvent"}, {"TargetQty", "1"}, {"EnforceLocationRequirement", "1"}, {"POITag", "test_poi_b"}});
    doc.task("Test18c_Place_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_PlaceQuest", json::object(),
                  {{0, "Test18c_LeaveA", {}}, {1, "Test18c_GoToB", {}}, {2, "Test18c_EventInB", {}}, {3, "Test18c_Place_Root", {0, 1, 2}}});
    // Stage 1.8d: the same places with a last child that waits: the quest is not ready, so its StayActive exit task regresses (a ready
    // quest holds it: case QD11).
    doc.task("Test18c_PlaceNever", "taskgameevent", 14, {{"KillEnemyType", "Test18c_Never"}, {"TargetQty", "1"}});
    doc.task("Test18c_PlaceLong_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_PlaceLong", json::object(),
                  {{0, "Test18c_LeaveA", {}}, {1, "Test18c_GoToB", {}}, {2, "Test18c_PlaceNever", {}}, {3, "Test18c_PlaceLong_Root", {0, 1, 2}}});
    // A screen, then a child objective with an interaction.
    doc.task("Test18c_OpenInventory", "taskopenuiscreen", 62, {{"UIScreenTag", "Inventory"}, {"TargetQty", "1"}});
    doc.task("Test18c_Child", "taskobjective", 52, {{"ItemName", "Test18c_ChildQuest"}});
    doc.task("Test18c_Screen_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_ScreenQuest", json::object(),
                  {{0, "Test18c_OpenInventory", {}}, {1, "Test18c_Child", {}}, {2, "Test18c_Screen_Root", {0, 1}}});
    doc.task("Test18c_Interact", "taskinteract", 23, {{"InteractTag", "Test18c_Tag"}, {"TargetQty", "1"}, {"OnInteractStart", "0"}});
    doc.task("Test18c_ChildRoot", "simpletaskcontainer", 0);
    doc.objective("Test18c_ChildQuest", json::object(), {{0, "Test18c_Interact", {}}, {1, "Test18c_ChildRoot", {0}}});
    // The deaths.
    doc.task("Test18c_DieFail", "taskdie", 65, {{"FailOnCondition", "1"}});
    doc.task("Test18c_DieReset", "taskdie", 65, {{"ResetOnCondition", "1"}});
    doc.task("Test18c_Never", "taskgameevent", 14, {{"KillEnemyType", "Test18c_Never"}, {"TargetQty", "1"}});
    doc.task("Test18c_DieFail_Root", "simpletaskcontainer", 0);
    doc.task("Test18c_DieReset_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_DieFailQuest", json::object(), {{0, "Test18c_DieFail", {}}, {1, "Test18c_Never", {}}, {2, "Test18c_DieFail_Root", {0, 1}}});
    doc.objective("Test18c_DieResetQuest", json::object(),
                  {{0, "Test18c_DieReset", {}}, {1, "Test18c_Never", {}}, {2, "Test18c_DieReset_Root", {0, 1}}});
    // The abandon: a quest item of TaskGiveAndTakeItem, an unlock and a temporary unlock (their undo), a return task.
    doc.task("Test18c_GiveQuestItem", "taskgiveandtakeitem", 34, {{"ItemName", "Test18c_QuestItem"}, {"TargetQty", "1"}});
    doc.task("Test18c_Unlock", "taskunlockachievement", 20, {{"ItemName", "Test18c_Before"}});
    doc.task("Test18c_UnlockStay", "taskunlockachievement", 20, {{"ItemName", "Test18c_Stay"}, {"StayActive", "1"}});
    doc.task("Test18c_TempUnlock", "tasktemporaryunlockachievement", 22, {{"ItemName", "Test18c_Temp"}});
    doc.task("Test18c_ReturnHides", "taskhaveandreturnitems", 26, {{"ItemName", "TestHideT1"}, {"TargetQty", "2"}});
    doc.task("Test18c_Return_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_ReturnQuest", json::object(),
                  {{0, "Test18c_GiveQuestItem", {}},
                   {1, "Test18c_ReturnHides", {}},
                   {2, "Test18c_Unlock", {}},
                   {3, "Test18c_UnlockStay", {}},
                   {4, "Test18c_TempUnlock", {}},
                   {5, "Test18c_Return_Root", {0, 2, 3, 4, 1}}});
    // A main story quest: no abandon, a reset (AwardOnlyOnce keeps its row).
    doc.objective("Test18c_StoryQuest", json{{"Type", "MainStoryQuest"}, {"CanBeAbandoned", "0"}},
                  {{0, "Test18c_Give", {}},
                   {1, "Test18c_HaveWood", {}},
                   {2, "Test18c_EquipPick", {}},
                   {3, "Test18c_RemoveStone", {}},
                   {4, "Test18c_Items_Root", {0, 1, 2, 3}}});
    // A failure timer.
    doc.task("Test18c_FailTimer", "tasktimer", 64, {{"Duration", "3.5"}, {"FailOnCondition", "1"}});
    doc.task("Test18c_Timer_Root", "simpletaskcontainer", 0);
    doc.objective("Test18c_TimerQuest", json::object(),
                  {{0, "Test18c_FailTimer", {}}, {1, "Test18c_Unlock", {}}, {2, "Test18c_Timer_Root", {0, 1}}});
    // A tree with a reused container (a hole at index 3: the second use of the container 2 builds no node: client 0x4466350).
    doc.task("Test18c_Leaf", "taskgameevent", 14, {{"KillEnemyType", "Test18c_LeafEvent"}, {"TargetQty", "1"}});
    doc.task("Test18c_Reused", "consecutivetaskcontainer", 1);
    doc.task("Test18c_Hole_Root", "consecutivetaskcontainer", 1);
    doc.objective("Test18c_HoleQuest", json::object(),
                  {{0, "Test18c_Leaf", {}}, {1, "Test18c_Unlock", {}}, {2, "Test18c_Reused", {0, 1}}, {4, "Test18c_Hole_Root", {2}}});
    return doc;
}

/** The tree of 06_skinningknife of the game data (c01 objective tasks; its achievement ids get made-up bits, its items test items). */
void skinning_knife(QuestDoc& doc) {
    for (const char* id : {"SwapActive_01_AelstromWW_01", "SwapActive_01_UrdaWW_01", "tutorial_gathering_bushflint", "SwapActive_01_AelstromWW_03"}) {
        doc.achievement(id);
    }
    doc.task("06_skinningknife_01_Walkaway_01_Aelstrom_p01_Off", "taskunlockachievement", 20, {{"ItemName", "SwapActive_01_AelstromWW_01"}});
    doc.task("06_skinningknife_01_Walkaway_01_UrdaWW_p01_On", "taskunlockachievement", 20, {{"ItemName", "SwapActive_01_UrdaWW_01"}});
    doc.task("06_skinningknife_02_TutorialUnlock", "taskunlockachievement", 20, {{"ItemName", "tutorial_gathering_bushflint"}});
    doc.task("06_skinningknife_03_HaveKnife", "taskhaveitems", 25, {{"ItemName", "TestKnife"}, {"TargetQty", "1"}});
    doc.task("06_skinningknife_03_SliceSwap_01_Aelstrom_p03_On", "taskunlockachievement", 20, {{"ItemName", "SwapActive_01_AelstromWW_03"}});
    doc.task("06_skinningknife_03_HaveKnife_container", "consecutivetaskcontainer", 1);
    doc.task("06_skinningknife_03_TaskHaveItems_1_FlintT1", "taskhaveitems", 25, {{"ItemName", "TestStoneT1"}, {"TargetQty", "1"}});
    doc.task("06_skinningknife_03_TaskHaveItems_1_WoodT1", "taskhaveitems", 25, {{"ItemName", "TestWoodT1"}, {"TargetQty", "1"}});
    doc.task("06_skinningknife_03_gather_container", "simpletaskcontainer", 0);
    doc.task("06_skinningknife_03_gather_container_partial", "partialtaskcontainer", 2);
    doc.task("06_skinningknife_03_TaskHaveItems_1_1hSkinningKnifeT1", "taskhaveitems", 25,
             {{"ItemName", "TestKnife"}, {"TargetQty", "1"}, {"TerritoryID", "6"}, {"POITag", "watchtower"}});
    doc.task("06_skinningknife_03_gatherThenCraft_container", "consecutivetaskcontainer", 1);
    doc.task("06_skinningknife_03_swapDelay", "tasktimer", 64, {{"FailOnCondition", "0"}, {"Duration", "25.4"}});
    doc.task("06_skinningknife_03_NeverTrue_task", "taskgameevent", 14, {{"TargetQty", "1"}, {"UIScreenTag", "neverbetrue"}});
    doc.task("06_skinningknife_03_SwapTimerNeverComplete_container", "consecutivetaskcontainer", 1);
    doc.task("06_skinningknife_03_timerAndGather_container_partial", "partialtaskcontainer", 2);
    doc.task("06_skinningknife_container", "consecutivetaskcontainer", 1);
    doc.objective("06_skinningknife", json{{"Type", "MainStoryQuest"}, {"CanBeAbandoned", "0"}},
                  {{0, "06_skinningknife_01_Walkaway_01_Aelstrom_p01_Off", {}},
                   {1, "06_skinningknife_01_Walkaway_01_UrdaWW_p01_On", {}},
                   {2, "06_skinningknife_02_TutorialUnlock", {}},
                   {3, "06_skinningknife_03_HaveKnife", {}},
                   {4, "06_skinningknife_03_SliceSwap_01_Aelstrom_p03_On", {}},
                   {5, "06_skinningknife_03_HaveKnife_container", {3, 4}},
                   {6, "06_skinningknife_03_TaskHaveItems_1_FlintT1", {}},
                   {7, "06_skinningknife_03_TaskHaveItems_1_WoodT1", {}},
                   {8, "06_skinningknife_03_gather_container", {6, 7}},
                   {9, "06_skinningknife_03_HaveKnife", {}},
                   {10, "06_skinningknife_03_gather_container_partial", {8, 9}},
                   {11, "06_skinningknife_03_TaskHaveItems_1_1hSkinningKnifeT1", {}},
                   {12, "06_skinningknife_03_SliceSwap_01_Aelstrom_p03_On", {}},
                   {13, "06_skinningknife_03_gatherThenCraft_container", {10, 11, 12}},
                   {14, "06_skinningknife_03_swapDelay", {}},
                   {15, "06_skinningknife_03_SliceSwap_01_Aelstrom_p03_On", {}},
                   {16, "06_skinningknife_03_NeverTrue_task", {}},
                   {17, "06_skinningknife_03_SwapTimerNeverComplete_container", {14, 15, 16}},
                   {18, "06_skinningknife_03_timerAndGather_container_partial", {5, 13, 17}},
                   {19, "06_skinningknife_container", {0, 1, 2, 18}}});
}

/** A gathering shop with the quests of the cases (loaded before the entry of the characters). */
struct QuestShop : GatherShop {
    explicit QuestShop(bool withSkinningKnife = false) {
        std::vector<gamedata::ItemDefinition> items = gathering_items();
        gamedata::ItemDefinition questItem = chests::chest_item("Test18c_QuestItem", "Resource", "Resource+Quest", 0, 100);
        questItem.nonRemovable = true;
        items.push_back(questItem);
        host.catalog.set(items);
        QuestDoc doc = event_quests();
        if (withSkinningKnife) {
            skinning_knife(doc);
        }
        if (!host.quests.load_text(doc.text())) {
            throw std::runtime_error("quest event test: the quest data did not load: " + host.quests.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    world::QuestResult grant(const char* character, const char* objective) {
        return quests().grant(*entity(character), objective, "command", 0);
    }
    const world::QuestInstance* quest(const char* character, const char* objective) {
        return quest_of(*entity(character), objective);
    }
    /** The world goes on by some ticks (the items and the places of the quests look at each tick). */
    void settle(int steps = 3) {
        host.steps(steps, {&clientA, &clientB});
    }
    bool give(const char* character, const char* item, std::uint32_t quantity) {
        return host.world.inventory().give(character, item, quantity).ok();
    }
    /** Removes a quantity of an item from the bag of a character. */
    bool take(const char* character, const char* item, std::uint32_t quantity) {
        const world::Entity* own = entity(character);
        for (std::uint32_t slot = 0; own != nullptr && slot < own->bag.slots.size(); ++slot) {
            if (own->bag.slots[slot].item && own->bag.slots[slot].item->itemId == item) {
                return host.world.inventory().remove(character, slot, quantity).ok();
            }
        }
        return false;
    }
};

std::uint32_t count_of(const world::Entity& entity, const char* item) {
    return static_cast<std::uint32_t>(world::Inventory::count(entity, item));
}

std::uint8_t state_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].state : 255;
}

std::uint32_t progress_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].progress : 9999;
}

Bytes body_of(std::uint64_t entity, std::uint64_t value, bool wide) {
    server::hub::Writer body;
    body.u64(0);
    body.u64(entity);
    if (wide) {
        body.u64(value);
    } else {
        body.u32(static_cast<std::uint32_t>(value));
    }
    return body.data();
}

using world::quest_state::kActive;
using world::quest_state::kComplete;
using world::quest_state::kCompleteActive;
using world::quest_state::kInactive;

void kills(const Options&) {
    QuestShop shop;
    shop.enter();
    check(shop.grant(kCharacterA, "Test18c_KillQuest").ok() && shop.grant(kCharacterB, "Test18c_KillQuest").ok(), "setup: A and B have the kill quest");
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    // A kills a wolf (the victim categories of the kind: the vitals row and the category row Test_BagWolf_Category: the loot tables of the
    // cases have no VitalsCategories, as a file of the loot tool of before stage 1.8c).
    (void)shop.dead_wolf();
    shop.settle();
    const world::QuestInstance* questA = shop.quest(kCharacterA, "Test18c_KillQuest");
    const world::QuestInstance* questB = shop.quest(kCharacterB, "Test18c_KillQuest");
    note("A after one kill: " + states_text(*questA) + "; B: " + states_text(*questB));
    check(progress_of(questA, 0) == 1 && state_of(questA, 0) == kActive && state_of(questA, 1) == kComplete,
          "the kill counts 1 of 2 for A, and its kill event (KillEvent) completes TaskGameEvent");
    check(count_of(a, "TestFiberT1") == 1 && state_of(questA, 2) == kComplete,
          "the quest drop of the kill (ItemDropVC names the vitals row of the victim) is in the bag of A, and its possession task completes");
    check(progress_of(questB, 0) == 0 && state_of(questB, 1) == kActive && count_of(b, "TestFiberT1") == 0, "B, not in the credit list, gets nothing");
    // B hits a second wolf a little, A kills it: one receiver of the credit data: A only.
    world::Entity* wolf = shop.host.world.creatures().spawn("Test_BagWolf", bags::kKillPlace, 0.0F).entity;
    const world::EntityId id = wolf->id;
    (void)shop.host.world.creatures().hurt(id, bags::kWolfHealth * 0.2F, b.id);
    (void)shop.host.world.creatures().hurt(id, bags::kWolfHealth * 2.0F, a.id);
    shop.settle();
    questA = shop.quest(kCharacterA, "Test18c_KillQuest");
    questB = shop.quest(kCharacterB, "Test18c_KillQuest");
    check(questA != nullptr && state_of(questA, 0) == kComplete && state_of(questA, 3) == kComplete && progress_of(questB, 0) == 0,
          "the second kill completes the count of A (its root completes); B hit the wolf but is not in the credit list: no change");
    check(count_of(a, "TestFiberT1") == 1, "PauseAndClearLootOnTaskComplete: no second quest drop while A has the target count");
    // A repeated delivery of one result counts one time.
    const std::uint64_t repeated = shop.quests().counts().repeatedResults;
    std::vector<world::Entity*> credited{&b};
    shop.quests().killed(*shop.host.world.find(id), credited, {"test_bagwolf_category"}, "killevent", 0xC0FFEE);
    shop.quests().killed(*shop.host.world.find(id), credited, {"test_bagwolf_category"}, "killevent", 0xC0FFEE);
    questB = shop.quest(kCharacterB, "Test18c_KillQuest");
    check(progress_of(questB, 0) == 1 && shop.quests().counts().repeatedResults == repeated + 2,
          "a kill with the same result id a second time counts one time (the kill and its event: two repeats)");
}

void items(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.give(kCharacterA, "TestStoneT1", 5), "setup: A has 5 stones");
    check(shop.grant(kCharacterA, "Test18c_ItemQuest").ok(), "the grant of the item quest");
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(count_of(a, "TestGemB") == 1 && state_of(quest, 0) == kComplete && state_of(quest, 1) == kActive && progress_of(quest, 1) == 0,
          "TaskGiveItem gives its item at the activation and completes; the possession task waits (0 of 3)");
    check(shop.host.store.list_quest_awards(kCharacterA).size() == 1 && shop.host.store.list_quest_awards(kCharacterA)[0].amount == 1,
          "AwardOnlyOnce: the award row of the give");
    check(shop.give(kCharacterA, "TestWoodT1", 2), "setup: 2 wood");
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(progress_of(quest, 1) == 2 && state_of(quest, 1) == kActive, "a change of the bag: the count is 2 of 3 at the next tick");
    check(shop.give(kCharacterA, "TestWoodT1", 2), "setup: 2 more wood");
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(progress_of(quest, 1) == 3 && state_of(quest, 1) == kCompleteActive && state_of(quest, 2) == kActive,
          "4 wood: the StayActive task is CompleteActive at its target (3), and the equip task is active");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot, 120), "setup: a pick of gear score 120 in its slot");
    shop.settle();
    check(state_of(shop.quest(kCharacterA, "Test18c_ItemQuest"), 2) == kActive, "MinGearScore 150: a pick of gear score 120 does not count");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot, 160), "setup: a pick of gear score 160 in its slot");
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(state_of(quest, 2) == kComplete && state_of(quest, 3) == kComplete && count_of(a, "TestStoneT1") == 3 && state_of(quest, 4) == kActive
              && state_of(quest, 5) == kActive,
          "the pick of 160 completes the equip; TaskRemoveItem takes 2 stones by item id (5 to 3); the root waits for its last child");
    // The regression: the wood leaves the bag.
    check(shop.take(kCharacterA, "TestWoodT1", 3), "setup: 3 wood leave the bag (1 stays)");
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    note("after the wood left: " + states_text(*quest));
    check(state_of(quest, 1) == kActive && progress_of(quest, 1) == 1 && state_of(quest, 5) == kActive && progress_of(quest, 5) == 3,
          "the StayActive task regresses to Active (1 of 3); the consecutive root counts its 3 satisfied children "
          "(client 0x3AD6C60: the satisfied direct children, in any place)");
    check(state_of(quest, 2) == kComplete && state_of(quest, 3) == kComplete && state_of(quest, 4) == kInactive,
          "the completed later tasks stay Complete (terminal); the later active task is Inactive (the rewind)");
    check(shop.give(kCharacterA, "TestWoodT1", 2), "setup: the wood comes back");
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(state_of(quest, 1) == kCompleteActive && state_of(quest, 4) == kActive && state_of(quest, 5) == kActive && count_of(a, "TestStoneT1") == 3,
          "satisfied again: the next task that is not done is active again; no second removal");
    // The reset: a new instance; AwardOnlyOnce gives no second gem.
    const std::uint64_t instance = quest->instance;
    const world::QuestResult reset = shop.quests().reset(a, instance);
    quest = shop.quest(kCharacterA, "Test18c_ItemQuest");
    check(reset.ok() && quest != nullptr && quest->instance != instance && count_of(a, "TestGemB") == 1 && state_of(quest, 0) == kComplete,
          "the reset makes a new instance; the give completes with no second item (AwardOnlyOnce)");
    check(state_of(quest, 1) == kCompleteActive && state_of(quest, 2) == kComplete && count_of(a, "TestStoneT1") == 1,
          "the new instance evaluates its predicates at the activation (3 wood, the pick) and removes 2 stones again (3 to 1)");
}

void gathers(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "Test18c_GatherQuest").ok(), "setup: the gather quest");
    check(shop.equip(kCharacterA, "TestKnife", kKnifeSlot) && shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: a knife and a pick");
    world::Entity* body = shop.dead_wolf();
    shop.gather(shop.clientA, GatherShop::body_block(*body), 1600);
    shop.settle();
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_GatherQuest");
    check(count_of(a, "TestGemA") == 1 && count_of(a, "TestHideT1") == 2 && progress_of(quest, 0) == 1,
          "the skinning gives its loot and the quest drop of the task (ItemDropVC names the gatherable row Test_WolfSkin): 1 of 2");
    check(shop.host.store.list_quest_awards(kCharacterA).size() == 1 && shop.host.store.list_quest_awards(kCharacterA)[0].amount == 1,
          "AwardOnlyOnce: the award row counts the drop");
    check(progress_of(quest, 1) == 0, "TaskRequireGatherItems counts no hide and no quest drop");
    shop.gather(shop.clientA, shop.capital_block(), 4500);
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_GatherQuest");
    check(progress_of(quest, 1) == 2 && count_of(a, "TestOreT1") == 2, "the ore of the gather counts (2 of 4)");
    world::Entity* second = shop.dead_wolf();
    shop.gather(shop.clientA, GatherShop::body_block(*second), 1600);
    shop.settle();
    quest = shop.quest(kCharacterA, "Test18c_GatherQuest");
    check(count_of(a, "TestGemA") == 2 && state_of(quest, 0) == kComplete, "the second skinning: 2 of 2, complete");
    check(shop.take(kCharacterA, "TestGemA", 2), "setup: the gems leave the bag");
    world::Entity* third = shop.dead_wolf();
    shop.gather(shop.clientA, GatherShop::body_block(*third), 1600);
    check(count_of(a, "TestGemA") == 0, "a complete task drops nothing more");
    check(shop.quests().counts().drops == 2, "two quest drops in all");
}

json chest_gathering_document() {
    json document = gathering_document();
    document["rows"].push_back(row_json("Test18c_ChestRow", "QuestGatherables", "None", "NoToolNecessary", "", 0, 1.0, 200.0, 300.0, "Empty",
                                        "Test18c_ChestRow_interact", true));
    document["rows"].back()["quest"] = true;
    const std::string key = object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex}).key;
    document["capitals"][key] = json::array({kOreSlice, static_cast<int>(kRowCount), "IO_Test18c_Chest", true});
    return document;
}

void quest_objects(const Options&) {
    QuestShop shop;
    check(shop.host.gathering.set(chest_gathering_document().dump(), {{kRegion, region_records()}}),
          "setup: the capital node is a quest object (" + shop.host.gathering.error() + ")");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    shop.request(shop.clientA, shop.capital_block());
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().refusals[static_cast<std::size_t>(world::GatherRefusal::notOnQuest)] == 1 && count_of(a, "TestHideT1") == 0,
          "with no task of its tag the quest object is refused (InteractOnlyOnQuest; live check G1: no prompt)");
    check(shop.grant(kCharacterA, "Test18c_ChestQuest").ok() && shop.grant(kCharacterB, "Test18c_ChestQuest").ok(), "setup: A and B have the task");
    check(shop.quests().quest_object_open(a, "IO_Test18c_Chest") && !shop.quests().quest_object_open(a, "IO_Other"),
          "the object is open for A while a task has its tag");
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    shop.settle();
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_ChestQuest");
    check(count_of(a, "TestHideT1") == 1 && state_of(quest, 0) == kComplete && state_of(quest, 1) == kComplete,
          "the gather gives the quest drop and applies the game event of the row: both tasks complete");
    check(shop.host.store.list_gathering_nodes().empty() && shop.gathering().node_count() == 0,
          "no shared depletion: no node row and no node state (each character gathers its own object)");
    check(!shop.quests().quest_object_open(a, "IO_Test18c_Chest"), "the task is complete: the object is closed for A");
    shop.gather(shop.clientB, shop.capital_block(), 1500);
    shop.settle();
    check(count_of(b, "TestHideT1") == 1 && state_of(shop.quest(kCharacterB, "Test18c_ChestQuest"), 0) == kComplete, "B gathers its own object after A");
}

/**
 * QG5F (stage QG5a): the per-character state of a gathered quest object (the rule of the destructible objects of stage 1.8e): its
 * LootTracker entry Looted for its character only, no second gather by the character until the respawn of its row, the gather of B;
 * and the requirement conditional of a bound (a variation requirement that is an expression).
 */
void gathered_quest_objects(const Options&) {
    QuestShop shop;
    check(shop.host.gathering.set(chest_gathering_document().dump(), {{kRegion, region_records()}}), "setup: the capital node is a quest object");
    gamedata::InteractionObject object = object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex});
    object.bounds[0].conditional = "Test18c_Flag || Test18c_Stay";
    check(shop.host.interactions.set({option_row("GatherOption", 14, true)}, {}, {object}), "setup: the bound of the object has a conditional");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.grant(kCharacterA, "Test18c_ChestTwiceQuest").ok() && shop.grant(kCharacterB, "Test18c_ChestTwiceQuest").ok(), "setup: A and B have the task");
    shop.request(shop.clientA, shop.capital_block());
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().begins == 0, "the conditional of the bound is false for A (no bit): the use is refused");
    const auto flag = shop.host.world.ownership().achievement_bit("Test18c_Flag");
    check(flag && shop.host.world.ownership().unlock_achievement(a, "Test18c_Flag", *flag) && flag
              && shop.host.world.ownership().unlock_achievement(b, "Test18c_Flag", *flag),
          "setup: A and B get a bit of the conditional");
    const std::array<std::uint8_t, 16> actor = object.actor;
    const std::size_t fromB = shop.clientB.got.size();
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    shop.settle();
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_ChestTwiceQuest");
    check(progress_of(quest, 0) == 1 && a.questGathers.size() == 1 && chests::tracker_state(shop.clientA, actor) == world::quest_object_rules::kLooted,
          "A gathers the quest object: its event (1 of 2) and its LootTracker entry Looted");
    const std::uint64_t completions = shop.gathering().counts().completions;
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    shop.settle();
    check(progress_of(shop.quest(kCharacterA, "Test18c_ChestTwiceQuest"), 0) == 1 && shop.gathering().counts().completions == completions
              && shop.gathering().counts().refusals[static_cast<std::size_t>(world::GatherRefusal::gatheredQuestObject)] == 1,
          "a second gather of A is refused until the respawn: no second event from one object");
    check(!chests::tracker_state(shop.clientB, actor, fromB) && b.questGathers.empty(), "the entry of A does not reach B (owner only, MP-10)");
    shop.gather(shop.clientB, shop.capital_block(), 1500);
    shop.settle();
    check(progress_of(shop.quest(kCharacterB, "Test18c_ChestTwiceQuest"), 0) == 1, "B gathers its own object (each character its own)");
    // The clients go on with their reports (a session with no datagram for long ends) while the world clock passes 301 s.
    for (int second = 0; second < 301; ++second) {
        shop.report(shop.clientA, std::nullopt);
        shop.report(shop.clientB, std::nullopt);
        shop.host.advance(std::chrono::seconds(1));
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
    shop.settle();
    check(shop.entity(kCharacterA) == &a && a.questGathers.empty() && shop.gathering().counts().questObjectRespawns >= 1, "after the respawn bounds of the row (200 to 300 s) the entry of A goes");
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    shop.settle();
    check(state_of(shop.quest(kCharacterA, "Test18c_ChestTwiceQuest"), 0) == kComplete, "after the respawn A gathers it again: 2 of 2");
}

void crafts(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "Test18c_CraftQuest").ok(), "setup: the craft quest");
    const auto craft = [&](const char* recipe, std::uint32_t amount, std::uint32_t gearScore, bool refine) {
        world::QuestEvent event;
        event.kind = refine ? world::QuestEventKind::refine : world::QuestEventKind::craft;
        event.keys = {recipe};
        event.amounts = {amount};
        event.gearScore = gearScore;
        shop.quests().event(a, event);
    };
    craft("Test_Recipe", 1, 200, false);
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_CraftQuest");
    check(progress_of(quest, 0) == 0 && progress_of(quest, 1) == 1, "a craft of gear score 200 is below MinGearScore 250; CraftAnyRecipe counts it");
    craft("test_recipe", 2, 300, false);
    quest = shop.quest(kCharacterA, "Test18c_CraftQuest");
    check(state_of(quest, 0) == kComplete && progress_of(quest, 1) == 3 && state_of(quest, 1) == kComplete,
          "2 crafted items of 300: the recipe task completes (the id in any case); any recipe: 3 of 3");
    craft("Test_RecipeB", 1, 0, false);
    craft("Test_Refine", 1, 0, false);
    quest = shop.quest(kCharacterA, "Test18c_CraftQuest");
    check(progress_of(quest, 2) == 1 && progress_of(quest, 3) == 0, "TaskCraftMultipleRecipes counts a recipe of its list; a craft is not a refine");
    craft("Test_Refine", 5, 0, true);
    craft("Test_RecipeA", 1, 0, false);
    quest = shop.quest(kCharacterA, "Test18c_CraftQuest");
    check(state_of(quest, 3) == kComplete && state_of(quest, 2) == kComplete && state_of(quest, 4) == kComplete,
          "the refine of 5 completes TaskRefineRecipe; the second recipe of the list completes the multiple task; the root completes");
}

json poi_document() {
    return json{{"format", 1},
                {"pois",
                 json::array({{{"id", 40901}, {"tags", {"test_poi_a"}}, {"territory", 6}, {"center", {100.0, 200.0, 30.0}}, {"radius", 150.0},
                               {"shape", json::array({{80.0, 180.0}, {120.0, 180.0}, {120.0, 220.0}, {80.0, 220.0}})}},
                              {{"id", 40902}, {"tags", {"test_poi_b"}}, {"territory", 6}, {"center", {300.0, 200.0, 30.0}}, {"radius", 150.0},
                               {"shape", json::array({{280.0, 180.0}, {320.0, 180.0}, {320.0, 220.0}, {280.0, 220.0}})}},
                              {{"id", 40903}, {"tags", {"test_poi_a"}}, {"territory", 12}, {"center", {500.0, 200.0, 30.0}}, {"radius", 150.0},
                               {"shape", json::array({{480.0, 180.0}, {520.0, 180.0}, {520.0, 220.0}, {480.0, 220.0}})}}})}};
}

void places(const Options&) {
    QuestShop shop;
    check(shop.host.pois.load_text(poi_document().dump()), "setup: three points of interest");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    // A move: a travel (an estimated place), then the report of the client at the place (the accepted position: World::place).
    const auto move = [&](float x, float y) {
        (void)shop.host.world.travel(kCharacterA, state::WorldPosition{x, y, 30.0F}, world::TravelScreen::off);
        shop.placeA = state::WorldPosition{x, y, 30.0F};
        shop.report(shop.clientA, std::nullopt);
        shop.settle();
    };
    move(100.0F, 200.0F);
    check(shop.grant(kCharacterA, "Test18c_PlaceQuest").ok(), "setup: the place quest, A in the point of interest A");
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_PlaceQuest");
    check(state_of(quest, 0) == kActive && progress_of(quest, 0) == 0, "TaskExitPOITag: in the place at the activation: active");
    move(500.0F, 200.0F);
    quest = shop.quest(kCharacterA, "Test18c_PlaceQuest");
    check(state_of(quest, 0) == kCompleteActive && state_of(quest, 1) == kActive,
          "in a place of the tag in a different territory (12; TerritoryID 6): outside the places of the task: complete (StayActive)");
    world::QuestEvent event;
    event.kind = world::QuestEventKind::gameEvent;
    event.keys = {"Test18c_PlaceEvent"};
    move(300.0F, 200.0F);
    quest = shop.quest(kCharacterA, "Test18c_PlaceQuest");
    check(state_of(quest, 1) == kComplete && state_of(quest, 2) == kActive, "TaskGoToPOITag: the move into B completes it");
    move(200.0F, 200.0F);
    shop.quests().event(a, event);
    check(progress_of(shop.quest(kCharacterA, "Test18c_PlaceQuest"), 2) == 0, "EnforceLocationRequirement: the game event outside the place B does not count");
    move(300.0F, 200.0F);
    shop.quests().event(a, event);
    quest = shop.quest(kCharacterA, "Test18c_PlaceQuest");
    check(state_of(quest, 2) == kComplete && state_of(quest, 3) == kCompleteActive, "in the place B it counts; the root is CompleteActive");
    move(100.0F, 200.0F);
    quest = shop.quest(kCharacterA, "Test18c_PlaceQuest");
    check(state_of(quest, 0) == kCompleteActive && state_of(quest, 3) == kCompleteActive,
          "back into A: the quest is ready, and its StayActive exit task holds (stage 1.8d, case QD11: the walk back to the person)");
    // The regression of a quest that is not ready: the same places with a last child that waits.
    check(shop.grant(kCharacterA, "Test18c_PlaceLong").ok(), "setup: the long place quest, A in the point of interest A");
    move(500.0F, 200.0F);
    move(300.0F, 200.0F);
    quest = shop.quest(kCharacterA, "Test18c_PlaceLong");
    check(state_of(quest, 0) == kCompleteActive && state_of(quest, 1) == kComplete && state_of(quest, 2) == kActive, "setup: out of A, into B");
    move(100.0F, 200.0F);
    quest = shop.quest(kCharacterA, "Test18c_PlaceLong");
    check(state_of(quest, 0) == kActive && state_of(quest, 2) == kInactive && state_of(quest, 3) == kActive,
          "back into A: the StayActive exit task of the quest that is not ready regresses, and its root rewinds");
    const std::uint64_t looks = shop.quests().counts().placeLooks;
    (void)shop.host.world.travel(kCharacterA, state::WorldPosition{101.0F, 200.0F, 30.0F}, world::TravelScreen::off);
    (void)shop.host.world.travel(kCharacterA, state::WorldPosition{102.0F, 200.0F, 30.0F}, world::TravelScreen::off);
    shop.host.world.tick();
    check(shop.quests().counts().placeLooks == looks + 1, "two moves in one tick: one look");
    // The cue of the client: 2297 with the territory of a place (the server tests the shape itself).
    server::hub::Writer body;
    body.u64(0);
    body.u64(a.id);
    body.zeros(16);
    body.u64(0);
    body.u64(0);
    body.u16(40902);
    body.u32(0x39EEC8C7);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeTerritoryEntered, a.actor.bytes, body.data()));
    shop.settle();
    check(state_of(shop.quest(kCharacterA, "Test18c_PlaceLong"), 0) == kActive, "a 2297 for B while A is in A changes nothing (a cue only)");
    // No POI data: a place task waits.
    QuestShop empty;
    empty.enter();
    check(empty.grant(kCharacterA, "Test18c_PlaceQuest").ok(), "setup: the place quest with no POI data");
    (void)empty.host.world.travel(kCharacterA, state::WorldPosition{900.0F, 900.0F, 30.0F}, world::TravelScreen::off);
    empty.settle();
    check(state_of(empty.quest(kCharacterA, "Test18c_PlaceQuest"), 0) == kActive, "no POI data: the exit task waits (no knowledge of a place)");
}

void screens_and_children(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "Test18c_ScreenQuest").ok(), "setup: the screen quest");
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeUiScreenOpened, a.actor.bytes,
                                                                         body_of(a.id, gamedata::quest_crc("hud"), false)));
    shop.settle();
    check(state_of(shop.quest(kCharacterA, "Test18c_ScreenQuest"), 0) == kActive, "2617 for the screen HUD does not open Inventory");
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeUiScreenOpened, a.actor.bytes,
                                                                         body_of(a.id, gamedata::quest_crc("inventory"), false)));
    shop.settle();
    const world::QuestInstance* parent = shop.quest(kCharacterA, "Test18c_ScreenQuest");
    const world::QuestInstance* child = shop.quest(kCharacterA, "Test18c_ChildQuest");
    check(state_of(parent, 0) == kComplete && state_of(parent, 1) == kActive && child != nullptr && child->parent == parent->instance,
          "2617 with the CRC of the lower-case UIScreenTag completes TaskOpenUiScreen; TaskObjective grants the child (its parent instance)");
    check(shop.quests().quest_object_open(a, "test18c_tag"), "a task of the child with an InteractTag opens its objects");
    world::QuestEvent start;
    start.kind = world::QuestEventKind::interact;
    start.keys = {"Test18c_Tag"};
    start.started = true;
    shop.quests().event(a, start);
    check(state_of(shop.quest(kCharacterA, "Test18c_ChildQuest"), 0) == kActive, "OnInteractStart 0: the start of the interaction does not count");
    world::QuestEvent end = start;
    end.started = false;
    shop.quests().event(a, end);
    parent = shop.quest(kCharacterA, "Test18c_ScreenQuest");
    check(state_of(shop.quest(kCharacterA, "Test18c_ChildQuest"), 1) == kComplete && state_of(parent, 1) == kComplete && state_of(parent, 2) == kComplete,
          "the end of the interaction completes the child; its root completes the TaskObjective of the parent and the parent");
}

void two_sessions(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.give(kCharacterA, "TestHideT1", 1) && shop.give(kCharacterB, "TestHideT1", 5), "setup: A has 1 hide, B 5 hides");
    for (const char* character : {kCharacterA, kCharacterB}) {
        check(shop.grant(character, "Test18c_KillQuest").ok() && shop.grant(character, "Test18c_ReturnQuest").ok(),
              "setup: the kill quest and the return quest");
    }
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    shop.settle(5);
    const world::QuestInstance* returnA = shop.quest(kCharacterA, "Test18c_ReturnQuest");
    const world::QuestInstance* returnB = shop.quest(kCharacterB, "Test18c_ReturnQuest");
    check(progress_of(returnA, 1) == 1 && state_of(returnA, 1) == kActive && state_of(returnB, 1) == kComplete,
          "the same task, different counts: A has 1 of 2 hides, B has its 2 (complete)");
    (void)shop.dead_wolf();
    shop.settle();
    check(progress_of(shop.quest(kCharacterA, "Test18c_KillQuest"), 0) == 1 && progress_of(shop.quest(kCharacterB, "Test18c_KillQuest"), 0) == 0,
          "one credited kill (A) changes only A");
    check(count_of(a, "TestFiberT1") == 1 && count_of(b, "TestFiberT1") == 0, "the quest drop of the kill goes to A only");
    check(shop.quests().abandon(a, shop.quest(kCharacterA, "Test18c_ReturnQuest")->instance).ok() && count_of(a, "TestHideT1") == 0
              && count_of(b, "TestHideT1") == 5 && count_of(a, "Test18c_QuestItem") == 0 && count_of(b, "Test18c_QuestItem") == 1,
          "the abandon of A takes its hide (its count, at most the target) and its quest item; B keeps its items");
    shop.settle(5);
    // The Objectives state of a character goes to its own client only.
    check(questfix::foreign_fragments(shop.clientA, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && questfix::foreign_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && !own_fragments(shop.clientA, questfix::kObjectivesSlot, questfix::kObjectivesType).empty(),
          "the quest state of A never reaches B, and that of B never reaches A");
    const auto lastA = decode_objectives(own_fragments(shop.clientA, questfix::kObjectivesSlot, questfix::kObjectivesType).back());
    const auto lastB = decode_objectives(own_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType).back());
    check(lastA.active.size() == 1 && lastB.active.size() == 2, "the last state of A has its kill quest only; B has its two quests");
}

void load(const Options&) {
    QuestShop shop;
    constexpr int kCharacters = 50;
    constexpr int kCreatures = 2000;
    constexpr int kEvents = 10000;
    std::vector<std::string> ids;
    for (int character = 0; character < kCharacters; ++character) {
        char id[48];
        std::snprintf(id, sizeof(id), "0e18c000-0000-4000-8000-%012d", 100 + character);
        char name[16];
        std::snprintf(name, sizeof(name), "Loadc%d", character);
        (void)add_player(shop.host.store, std::to_string(76561202255400000ULL + character), "Tester", id, name,
                         state::WorldPosition{100.0F + character * 50.0F, 200.0F, 30.0F});
        ids.push_back(id);
    }
    for (const std::string& id : ids) {
        check(shop.host.world.spawn_character(id, *shop.host.store.find_character(id)->position) == world::SpawnError::none, "");
        world::Entity* entity = shop.host.world.find_character(id);
        (void)shop.quests().grant(*entity, "Test18c_KillQuest", "command", 0);
        (void)shop.quests().grant(*entity, "Test18c_CraftQuest", "command", 0);
        (void)shop.quests().grant(*entity, "Test18c_ItemQuest", "command", 0);
    }
    for (int creature = 0; creature < kCreatures; ++creature) {
        (void)shop.host.world.creatures().spawn("Test_BagWolf",
                                                state::WorldPosition{5000.0F + static_cast<float>(creature % 50) * 10.0F,
                                                                     5000.0F + static_cast<float>(creature / 50) * 10.0F, 30.0F},
                                                0.0F);
    }
    const world::QuestCounts before = shop.quests().counts();
    std::size_t subscribers = 0;
    for (const std::string& id : ids) {
        subscribers += shop.quests().subscriber_count(*shop.host.world.find_character(id));
    }
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t expected = 0;
    std::map<std::size_t, std::uint32_t> crafts;
    for (int event = 0; event < kEvents; ++event) {
        world::Entity& character = *shop.host.world.find_character(ids[static_cast<std::size_t>(event) % ids.size()]);
        world::QuestEvent quest;
        switch (event % 4) {
        case 0: // A kill of a creature with no quest category: no subscriber.
            quest.kind = world::QuestEventKind::kill;
            quest.keys = {"no_quest_category", "hostile"};
            quest.result = 1000000 + static_cast<std::uint64_t>(event);
            break;
        case 1: // A craft of a recipe that no task names: only the CraftAnyRecipe task of the character ("c:*"), until it completes (3).
            quest.kind = world::QuestEventKind::craft;
            quest.keys = {"no_quest_recipe"};
            expected += crafts[static_cast<std::size_t>(event) % ids.size()]++ < 3 ? 1 : 0;
            break;
        case 2: // A game event that no task names.
            quest.kind = world::QuestEventKind::gameEvent;
            quest.keys = {"no_quest_event"};
            break;
        default: // A refine that no task names.
            quest.kind = world::QuestEventKind::refine;
            quest.keys = {"no_quest_refine"};
            break;
        }
        shop.quests().event(character, quest);
    }
    // A repeated result: the same kill result again counts one time.
    world::QuestEvent again;
    again.kind = world::QuestEventKind::kill;
    again.keys = {"test_bagwolf_category"};
    again.result = 1000000 + static_cast<std::uint64_t>(kEvents - 4);
    world::Entity& last = *shop.host.world.find_character(ids[static_cast<std::size_t>(kEvents - 4) % ids.size()]);
    shop.quests().event(last, again);
    const auto ended = std::chrono::steady_clock::now();
    const world::QuestCounts after = shop.quests().counts();
    const double seconds = std::chrono::duration<double>(ended - start).count();
    const std::uint64_t visits = after.subscribersVisited - before.subscribersVisited;
    std::printf("    QE9: %d characters (%zu indexed tasks), %d creatures with no quest, %d events: %llu subscribers visited, %llu progress "
                "changes, %llu plans, %.3f s\n",
                kCharacters, subscribers, kCreatures, kEvents, static_cast<unsigned long long>(visits),
                static_cast<unsigned long long>(after.progressChanges - before.progressChanges),
                static_cast<unsigned long long>(after.plans - before.plans), seconds);
    check(visits == expected, "the events visit only the indexed subscribers of their keys (" + std::to_string(visits) + " visits, " +
                                  std::to_string(expected) + " expected: the CraftAnyRecipe task of a character until its 3 crafts)");
    check(after.repeatedResults - before.repeatedResults == 1 && progress_of(quest_of(last, "Test18c_KillQuest"), 0) == 0,
          "the repeated kill result counts one time (no progress: its first delivery had no subscriber)");
    check(seconds < 5.0, "the time limit: 10,000 events in less than 5 s");
    // The store of the results has a bound: after more than its window of new results an old result is not known any more.
    for (std::uint64_t result = 1; result <= 5000; ++result) {
        world::QuestEvent kill;
        kill.kind = world::QuestEventKind::kill;
        kill.keys = {"no_quest_category"};
        kill.result = 9000000 + result;
        shop.quests().event(last, kill);
    }
    const std::uint64_t repeats = shop.quests().counts().repeatedResults;
    shop.quests().event(last, again);
    check(shop.quests().counts().repeatedResults == repeats, "the store of the results keeps the last 4,096 results only (an old one is new again)");
}

void trees(const Options&) {
    QuestShop shop(true);
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "06_skinningknife").ok(), "setup: the tree of 06_skinningknife");
    const world::QuestInstance* quest = shop.quest(kCharacterA, "06_skinningknife");
    note("06_skinningknife at the grant: " + states_text(*quest));
    check(state_of(quest, 3) == kActive && state_of(quest, 9) == kActive && state_of(quest, 6) == kActive && state_of(quest, 14) == kActive
              && state_of(quest, 18) == kActive,
          "the concurrent branches of the partial root: the two leaves 3 and 9 of one task row have their own states");
    check(shop.give(kCharacterA, "TestStoneT1", 1) && shop.give(kCharacterA, "TestWoodT1", 1), "setup: the flint and the wood of the tree");
    shop.settle();
    quest = shop.quest(kCharacterA, "06_skinningknife");
    note("after the flint and the wood: " + states_text(*quest));
    check(state_of(quest, 8) == kComplete && state_of(quest, 10) == kComplete && state_of(quest, 9) == kInactive && state_of(quest, 3) == kActive
              && state_of(quest, 11) == kActive,
          "the simple container 8 completes the partial 10, which ends its other child 9 (Inactive); the leaf 3 of the same row stays "
          "active; the consecutive 13 goes to 11");
    check(shop.give(kCharacterA, "TestKnife", 1), "setup: the knife");
    shop.settle();
    quest = shop.quest(kCharacterA, "06_skinningknife");
    note("after the knife: " + states_text(*quest));
    check(state_of(quest, 3) == kComplete && state_of(quest, 5) == kComplete && state_of(quest, 18) == kComplete && state_of(quest, 19) == kComplete
              && state_of(quest, 9) == kInactive,
          "the knife completes the leaf 3 (not 9: inactive), its container 5, the partial root 18, and the root 19");
    // The reused container: the hole of the tree has no node and no element on the wire.
    check(shop.grant(kCharacterA, "Test18c_HoleQuest").ok(), "setup: the tree with a hole");
    quest = shop.quest(kCharacterA, "Test18c_HoleQuest");
    check(quest->tasks.size() == 5 && state_of(quest, 0) == kActive && state_of(quest, 3) == 0 && state_of(quest, 4) == kActive,
          "the indexes of the client: the hole 3 has no state, the root has the index 4");
    world::QuestEvent leaf;
    leaf.kind = world::QuestEventKind::gameEvent;
    leaf.keys = {"Test18c_LeafEvent"};
    shop.quests().event(a, leaf);
    quest = shop.quest(kCharacterA, "Test18c_HoleQuest");
    check(state_of(quest, 2) == kComplete && state_of(quest, 4) == kComplete, "the consecutive chain completes over the hole");
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    shop.settle(5);
    const auto states = own_fragments(shop.clientA, questfix::kObjectivesSlot, questfix::kObjectivesType);
    std::size_t holeElements = 0;
    for (const auto& task : decode_objectives(states.back()).tasks) {
        holeElements += task.instance == quest->instance && task.index == 3 ? 1 : 0;
    }
    check(!states.empty() && holeElements == 0, "the state has no element for the hole");
}

void abandon_and_reset(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.give(kCharacterA, "TestHideT1", 3), "setup: 3 hides");
    check(shop.grant(kCharacterA, "Test18c_ReturnQuest").ok(), "setup: the return quest");
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    shop.settle(5);
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_ReturnQuest");
    const std::uint64_t instance = quest->instance;
    const auto bit = [&](const char* id) { return questfix::has_bit(a, *shop.host.world.ownership().achievement_bit(id)); };
    check(count_of(a, "Test18c_QuestItem") == 1 && bit("Test18c_Before") && bit("Test18c_Stay") && bit("Test18c_Temp") && state_of(quest, 1) == kComplete,
          "the give of the quest item, the three unlocks, and the return task (3 hides: complete)");
    check(std::find(a.quests.tracked.begin(), a.quests.tracked.end(), instance) != a.quests.tracked.end(), "setup: the quest is pinned");
    // The abandon (3836) from the client.
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeAbandonObjective, a.actor.bytes, body_of(a.id, instance, true)));
    shop.settle(5);
    check(shop.quest(kCharacterA, "Test18c_ReturnQuest") == nullptr && shop.host.store.list_quests(kCharacterA).empty()
              && shop.host.store.list_quest_undo(kCharacterA).empty() && shop.host.store.list_quest_pins(kCharacterA).empty() && a.quests.tracked.empty(),
          "3836: the instance, its rows, its undo rows, and its pin go");
    check(!bit("Test18c_Before") && bit("Test18c_Stay") && !bit("Test18c_Temp"),
          "the undo: the unlock reverts, the StayActive unlock stays (the prevention), the temporary unlock locks");
    check(count_of(a, "TestHideT1") == 1 && count_of(a, "Test18c_QuestItem") == 0,
          "the returns: 2 hides (the target) go, 1 stays; the quest item of TaskGiveAndTakeItem goes");
    const auto states = own_fragments(shop.clientA, questfix::kObjectivesSlot, questfix::kObjectivesType);
    check(!states.empty() && decode_objectives(states.back()).active.empty(), "the client gets the state with no active objective");
    // A main story quest: no abandon; the reset keeps the award row.
    check(shop.give(kCharacterA, "TestStoneT1", 4), "setup: 4 stones");
    check(shop.grant(kCharacterA, "Test18c_StoryQuest").ok(), "setup: the story quest");
    const std::uint64_t story = shop.quest(kCharacterA, "Test18c_StoryQuest")->instance;
    check(shop.quests().abandon(a, story).error == world::QuestError::notAbandonable && shop.quest(kCharacterA, "Test18c_StoryQuest") != nullptr,
          "CanBeAbandoned 0: the abandon is refused, nothing changes");
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeResetObjective, a.actor.bytes, body_of(a.id, story, true)));
    shop.settle(5);
    const world::QuestInstance* next = shop.quest(kCharacterA, "Test18c_StoryQuest");
    check(next != nullptr && next->instance != story && count_of(a, "TestGemB") == 1 && shop.host.store.list_quest_awards(kCharacterA).size() == 1,
          "5600: a new instance; the award row stays (no second gem)");
    check(std::find(a.quests.tracked.begin(), a.quests.tracked.end(), next->instance) != a.quests.tracked.end(), "the pin goes to the new instance");
    const std::uint64_t nextInstance = next->instance;
    // A disconnect keeps the quest; the next entry evaluates the predicates again.
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    check(shop.host.world.inventory().give(kCharacterA, "TestWoodT1", 3).ok(), "setup: 3 wood while A is away (the rows)");
    FakeClient again(27050);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(5, {&again});
    const world::QuestInstance* back = quest_of(*shop.host.world.find_character(kCharacterA), "Test18c_StoryQuest");
    check(back != nullptr && back->instance == nextInstance && state_of(back, 1) == kCompleteActive,
          "a reconnect keeps the instance; the entry evaluates the possession task with the wood of the rows");
}

void failures(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.grant(kCharacterA, "Test18c_TimerQuest").ok(), "setup: the failure timer quest");
    const world::QuestInstance* quest = shop.quest(kCharacterA, "Test18c_TimerQuest");
    check(state_of(quest, 0) == kCompleteActive && state_of(quest, 2) == kCompleteActive, "a failure timer is satisfied while it runs");
    shop.host.steps(400, {&shop.clientA, &shop.clientB});
    check(shop.quest(kCharacterA, "Test18c_TimerQuest") == nullptr && shop.quests().counts().failures == 1, "its end fails the objective (3.5 s)");
    check(shop.grant(kCharacterA, "Test18c_DieFailQuest").ok() && shop.grant(kCharacterA, "Test18c_DieResetQuest").ok(), "setup: the two TaskDie quests");
    const std::uint64_t resetBefore = shop.quest(kCharacterA, "Test18c_DieResetQuest")->instance;
    check(state_of(shop.quest(kCharacterA, "Test18c_DieFailQuest"), 0) == kCompleteActive, "TaskDie is satisfied while the character lives");
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e9F);
    shop.settle();
    const world::QuestInstance* reset = shop.quest(kCharacterA, "Test18c_DieResetQuest");
    check(shop.quest(kCharacterA, "Test18c_DieFailQuest") == nullptr && reset != nullptr && reset->instance != resetBefore,
          "the death: FailOnCondition fails its objective; ResetOnCondition resets its objective (a new instance)");
    check(shop.quests().counts().failures == 2 && shop.quests().counts().resets == 1 && a.life.dead, "the counts");
}

void loaders(const Options&) {
    gamedata::QuestData data;
    check(data.load_text(event_quests().text()), "the quest data of the cases loads (" + data.error() + ")");
    const gamedata::QuestObjective* kill = data.objective("Test18c_KillQuest");
    const gamedata::QuestTask& drop = data.task(kill->nodes[2].task);
    check(drop.dropSources == std::vector<std::string>{"test_bagwolf", "other_category"} && drop.dropChance == 100.0 && drop.pauseLoot,
          "a list cell: each part trimmed and in lower case; the drop chance; the flag");
    const gamedata::QuestObjective* place = data.objective("Test18c_PlaceQuest");
    const gamedata::QuestTask& leave = data.task(place->nodes[0].task);
    check(leave.territory == 6 && leave.poiTags == std::vector<std::string>{"test_poi_a"} && leave.stayActive, "TerritoryID, POITag");
    const gamedata::QuestTask& screen = data.task(data.objective("Test18c_ScreenQuest")->nodes[0].task);
    check(screen.screenCrc == gamedata::quest_crc("inventory"), "UIScreenTag: the CRC-32 of the lower-case text");
    gamedata::PoiData pois;
    check(pois.load_text(poi_document().dump()) && pois.pois().size() == 3 && pois.inside({"test_poi_a"}, 6, 100.0F, 200.0F)
              && !pois.inside({"test_poi_a"}, 6, 500.0F, 200.0F) && pois.inside({"test_poi_a"}, 0, 500.0F, 200.0F),
          "the POI data: the shapes, the tags, the territory of a tag");
    json bad = poi_document();
    bad["pois"][0]["shape"] = json::array({{1.0, 2.0}, {3.0, 4.0}});
    check(!pois.load_text(bad.dump()) && !pois.loaded(), "a shape with fewer than 3 points refuses the file");
    gamedata::GatheringData gathering;
    check(gathering.set(chest_gathering_document().dump(), {{kRegion, region_records()}}), "the gathering data with a quest object loads");
    const auto link = gathering.capital(object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex}).key);
    check(link && link->interactTag == "io_test18c_chest" && link->onlyOnQuest && gathering.rows()[link->row].quest,
          "the capital link of a quest object: its tag, InteractOnlyOnQuest, and its row marked quest (no event row needed)");
}

void scan_count(const Options&) {
    ServerHost host;
    json document = questfix::quest_document();
    for (int index = 0; index < 20; ++index) {
        json idle = document["objectives"][3];
        idle["id"] = "Test18c_Idle" + std::to_string(index);
        document["objectives"].push_back(idle);
    }
    check(host.quests.load_text(document.dump()), "setup: 20 idle quests and one quest with an active achievement wait");
    const Player player = add_player(host.store, kSteamIdA, "Tester", questfix::kQuestA, "Reviewa", state::WorldPosition{100, 200, 30});
    for (int index = 0; index < 20; ++index) {
        state::QuestRow row;
        row.instance = static_cast<std::uint64_t>(index + 1);
        row.objective = "Test18c_Idle" + std::to_string(index);
        row.objectiveCrc = gamedata::quest_crc(row.objective);
        row.tasks = {{0, "Test_Lock_Last", gamedata::quest_crc("Test_Lock_Last"), 1, 2, 0},
                     {1, "Test_Lock_Root", gamedata::quest_crc("Test_Lock_Root"), 1, 2, 0}};
        check(host.store.write_quest(questfix::kQuestA, row), "setup: an idle completed tree");
    }
    FakeClient client(27100);
    host.enter(client, player.registration());
    auto* entity = host.world.find_character(questfix::kQuestA);
    check(host.world.quests().grant(*entity, "Test_WaitQuest", "command", 0).ok(), "setup: the active wait");
    check(host.world.quests().subscriber_count(*entity) == 1, "the index of the character has one task: the wait (the 20 idle trees: none)");
    const auto before = host.world.quests().counts().waitsVisited;
    check(host.world.ownership().unlock_achievement(*entity, "Test_Other", 29), "one achievement event that no task names");
    const auto visits = host.world.quests().counts().waitsVisited - before;
    note("the event visited " + std::to_string(visits) + " task(s)");
    check(visits == 1, "the event visits only the one indexed wait, and the counter counts each visit before any filter");
}

void tool_checks(const Options& options) {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    for (const char* tool : {"check_pois_tool.py", "check_gathering_tool.py", "check_loot_tool.py"}) {
        const std::filesystem::path log = options.workDirectory / (std::string(tool) + ".log");
        const std::filesystem::path folder = options.workDirectory / (std::string(tool) + ".work");
        std::filesystem::create_directories(folder);
        const std::string command =
            "python3 -B " + (root / "tools/gamedata" / tool).string() + " " + folder.string() + " > " + log.string() + " 2>&1";
        check(std::system(command.c_str()) == 0, std::string("the check ") + tool + ": status 0 (log: work folder " + log.filename().string() + ")");
    }
}

} // namespace

std::vector<Case> quest_event_cases() {
    return {{"QG5F", "quests QG5a (I4, MP-10): a gathered quest object for its character only, its respawn; the conditional of a bound",
             gathered_quest_objects},
            {"QE1", "quests (1.8c, G10): a credited kill with its vitals categories, the kill event, the credit list, a kill drop", kills},
            {"QE2", "quests (1.8c, G11): give with AwardOnlyOnce, a StayActive possession and its regression, equip, take by item id, reset", items},
            {"QE3", "quests (1.8c, G12): the quest drop of a skinning, TaskRequireGatherItems", gathers},
            {"QE4", "quests (1.8c, G14): a quest object: InteractOnlyOnQuest, its drop, its game event, no shared depletion", quest_objects},
            {"QE5", "quests (1.8c): the crafts and the refines (MinGearScore, any recipe, a recipe list)", crafts},
            {"QE6", "quests (1.8c, G7): the places at the activation and after a move, the territory, the location rule, 2297", places},
            {"QE7", "quests (1.8c): 2617, TaskObjective and its child, TaskInteract", screens_and_children},
            {"QE8", "quests (1.8c, MP-10): two sessions, one credited kill, the drops and the returns of each character", two_sessions},
            {"QE9", "quests (1.8c, I5): 50 characters, 10,000 events, 2,000 creatures: only the indexed subscribers; the results", load},
            {"QE10", "quests (1.8c, I3): the repeated leaves 3 and 9 of 06_skinningknife, a partial container, a reused container", trees},
            {"QE11", "quests (1.8c): the abandon 3836 and the reset 5600: the undo, the returns, the awards, the pins, a reconnect",
             abandon_and_reset},
            {"QE12", "quests (1.8c): a failure timer, TaskDie with FailOnCondition and ResetOnCondition", failures},
            {"QE13", "quests (1.8c): the loaders of the task cells, the POI data, the quest objects of the gathering data", loaders},
            {"QRV7", "review of 1.8a, QR8A-R7: an achievement event visits only the indexed waits and counts each visit", scan_count},
            {"QE-TOOL", "quests (1.8c): the checks of the data tools (POI, gathering, loot)", tool_checks}};
}

} // namespace soulwarden::backend::tests

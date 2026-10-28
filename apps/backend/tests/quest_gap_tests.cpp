// The cases of stage QG2a (docs/systems/quests.md, the small quest rules; research quest-gaps.md and quest-gaps-qg2b.md): the category
// rule of the kills, TaskHaveLevel, the salvage events of the loot containers, the Azoth amount, and the push of PushObjectiveOnReqMet.
// The world is the fixture of the gathering cases (the wolf with its credit, two characters A and B with their clients); the quest data has
// made-up quests in the form of the tool. Made-up accounts, characters, ids, and names only.
//
//   QG2A  the category rule (LootTables::kill_categories): the base row, the spawn category, the direct parent of each (not the parent of a
//         parent), the id of the base row for a kill only; a credited kill counts for a task of the parent category, not of the parent of the
//         parent, the quest drop of the parent category; B (not credited) gets nothing; the Objectives state of A does not reach B (MP-10)
//   QG2B  TaskHaveLevel: the level form at the activation (TargetQty at or below the level: done; above: waits), a level change completes
//         it at the next tick, the progression form waits, the target 1 of the client; two sessions with different levels (MP-10)
//   QG2C  the salvage event of a loot container (the coin purse): one payment of CurrencyReward with the take, a table and an event, a skin
//         box and an event with no row refused with no change, a refused write changes nothing, the reward join of a purse and of a skin
//         box; the currency state of A only to A (MP-10)
//   QG2E  the Azoth amount: the truncation and the 16 bits of the client (37.5 gives 37), a blank and a zero chance pay the amount, the
//         hand-in pays 37; the categorical state of A only to A (MP-10)
//   QG2F  the push of PushObjectiveOnReqMet: the grant at the entry, after a change of a bit and of the level, the `_pushedonce` bit of the
//         first task, no second grant, the excluded rows (decision B), ExclusiveTerritory, the completion, the abandon (the undo of the bit
//         and a new grant), the cost (the rows of the changed bits only); two sessions with different bits (MP-10)
//   QG2G  the loaders: the salvage events, the container events, and the category groups of the loot data; a file of before; the reader of
//         a game event (CurrencyReward as a whole number, LootTags, the Azoth amount)

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "gamedata/game_event.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/procedural_fixture.h"
#include "support/quest_fixture.h"
#include "world/item_containers.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;
using questfix::quest_of;
using questfix::states_text;
using world::quest_state::kActive;
using world::quest_state::kComplete;
using world::quest_state::kCompleteActive;

constexpr std::uint32_t kObjectivesSlot = 22;
constexpr std::uint32_t kObjectivesType = 3857;

/** The quest data of the cases, in the form of the tool. */
struct GapDoc {
    json tasks = json::object();
    json objectives = json::array();
    json achievements = json::array();
    json events = json::object();
    std::uint32_t nextBit = 400;

    void task(const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = {{"id", id}, {"crc", 0}, {"type", type}, {"code", code}, {"cells", cells}};
    }
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
        return json{{"format", 1},           {"objectives", objectives},     {"tasks", tasks},          {"achievements", achievements},
                    {"npa", json::array()}, {"territories", json::array()}, {"backstories", json::array()}, {"events", events}}
            .dump();
    }
};

GapDoc gap_quests() {
    GapDoc doc;
    for (const char* id : {"Gap_PushReq", "Gap_Push_pushedonce", "Gap_Push_Done", "Gap_Old_Done", "Gap_Free_Bit", "Gap_Free_Done",
                           "Gap_Lvl_Done", "Gap_Territory_Done", "Gap_Other_Bit", "Gap_Never"}) {
        doc.achievement(id);
    }
    // QG2A: the kills of a parent category (Test_BagWolf_Category -> Test_WolfPack -> Test_Beasts) and its quest drop.
    doc.task("Gap_KillParent", "taskkillcontribution", 13, {{"KillEnemyType", "Test_WolfPack"}, {"TargetQty", "2"}});
    doc.task("Gap_KillRoot", "taskkillcontribution", 13, {{"KillEnemyType", "Test_Beasts"}, {"TargetQty", "1"}});
    doc.task("Gap_KillOther", "taskkillcontribution", 13, {{"KillEnemyType", "Test_Other_Group"}, {"TargetQty", "1"}});
    doc.task("Gap_DropParent", "taskhaveandreturnitems", 26,
             {{"ItemName", "TestFiberT1"}, {"TargetQty", "1"}, {"ItemDropVC", "Test_WolfPack"}, {"ItemDropProbability", "100"},
              {"PauseAndClearLootOnTaskComplete", "1"}});
    doc.task("Gap_Kill_Root", "simpletaskcontainer", 0);
    doc.objective("Gap_KillQuest", json::object(),
                  {{0, "Gap_KillParent", {}}, {1, "Gap_KillRoot", {}}, {2, "Gap_KillOther", {}}, {3, "Gap_DropParent", {}}, {4, "Gap_Kill_Root", {0, 1, 2, 3}}});
    // QG2B: TaskHaveLevel (a blank ItemName: the level of the character; an ItemName: a faction progression).
    doc.task("Gap_Level4", "taskhavelevel", 48, {{"TargetQty", "4"}, {"ShouldCheckPoints", "1"}});
    doc.task("Gap_Level6", "taskhavelevel", 48, {{"TargetQty", "6"}, {"StayActive", "1"}});
    doc.task("Gap_Level_Unlock", "taskunlockachievement", 20, {{"ItemName", "Gap_Lvl_Done"}});
    doc.task("Gap_Level_Root", "consecutivetaskcontainer", 1);
    doc.objective("Gap_LevelQuest", json::object(),
                  {{0, "Gap_Level4", {}}, {1, "Gap_Level6", {}}, {2, "Gap_Level_Unlock", {}}, {3, "Gap_Level_Root", {0, 1, 2}}});
    doc.task("Gap_LevelCovenant", "taskhavelevel", 48, {{"TargetQty", "3000"}, {"ItemName", "Covenant"}, {"ShouldCheckPoints", "1"}});
    doc.task("Gap_Faction_Root", "simpletaskcontainer", 0);
    doc.objective("Gap_FactionLevel", json::object(), {{0, "Gap_LevelCovenant", {}}, {1, "Gap_Faction_Root", {0}}});
    // QG2C: the rewards of loot containers with a salvage event.
    doc.task("Gap_Ready", "taskunlockachievement", 20, {{"ItemName", "Gap_Other_Bit"}});
    doc.events["gap_purse_success"] = {{"id", "Gap_Purse_Success"}, {"ItemReward", "Test_Purse"}};
    doc.events["gap_skin_success"] = {{"id", "Gap_Skin_Success"}, {"ItemReward", "Test_SkinBox"}};
    doc.objective("Gap_PurseQuest", {{"SuccessGameEventId", "Gap_Purse_Success"}}, {{0, "Gap_Ready", {}}});
    doc.objective("Gap_SkinQuest", {{"SuccessGameEventId", "Gap_Skin_Success"}}, {{0, "Gap_Ready", {}}});
    // QG2E: the Azoth amount (37.5 with a chance of 0, 12.5 with a blank chance).
    doc.events["gap_azoth_success"] = {{"id", "Gap_Azoth_Success"}, {"AzothReward", "37.5"}, {"AzothRewardChance", "0"}};
    doc.events["gap_azoth_blank"] = {{"id", "Gap_Azoth_Blank"}, {"AzothReward", "12.5"}};
    doc.objective("Gap_AzothQuest", {{"SuccessGameEventId", "Gap_Azoth_Success"}}, {{0, "Gap_Ready", {}}});
    doc.objective("Gap_AzothBlank", {{"SuccessGameEventId", "Gap_Azoth_Blank"}}, {{0, "Gap_Ready", {}}});
    // QG2F: the push rows. Gap_Push: a bit, the level 35, and its own `_pushedonce` bit (the first task unlocks it, StayActive 0).
    doc.task("Gap_Push_Once", "taskunlockachievement", 20, {{"ItemName", "Gap_Push_pushedonce"}});
    doc.task("Gap_Push_Wait", "taskachievement", 18, {{"ItemName", "Gap_Never"}});
    doc.task("Gap_Push_Root", "consecutivetaskcontainer", 1);
    doc.objective("Gap_Push",
                  {{"PushObjectiveOnReqMet", "true"}, {"PushObjectiveOnReqRetrigger", "false"}, {"RequiredAchievementId", "Gap_PushReq && !Gap_Push_pushedonce"},
                   {"RequiredLevel", "5"}, {"AchievementId", "Gap_Push_Done"}},
                  {{0, "Gap_Push_Once", {}}, {1, "Gap_Push_Wait", {}}, {2, "Gap_Push_Root", {0, 1}}});
    // Gap_PushFree: no requirement (Vector_Kiln_PushQuest has none), Retrigger TRUE: granted at the entry, one time; its completion stops it.
    doc.task("Gap_Free_Unlock", "taskunlockachievement", 20, {{"ItemName", "Gap_Free_Bit"}, {"StayActive", "1"}});
    doc.objective("Gap_PushFree", {{"PushObjectiveOnReqMet", "1"}, {"PushObjectiveOnReqRetrigger", "TRUE"}, {"AchievementId", "Gap_Free_Done"}},
                  {{0, "Gap_Free_Unlock", {}}});
    // The excluded rows of decision B (their conditions are true here) and a row with ExclusiveTerritory.
    doc.objective("01_SoleSurvivor2", {{"PushObjectiveOnReqMet", "1"}, {"RequiredAchievementId", "!Gap_Old_Done"}}, {{0, "Gap_Push_Wait", {}}});
    doc.objective("Quest_03189", {{"PushObjectiveOnReqMet", "true"}, {"AchievementId", "Gap_Old_Done"}}, {{0, "Gap_Push_Wait", {}}});
    doc.objective("Gap_PushTerritory", {{"PushObjectiveOnReqMet", "true"}, {"ExclusiveTerritory", "12"}, {"AchievementId", "Gap_Territory_Done"}},
                  {{0, "Gap_Push_Wait", {}}});
    return doc;
}

std::vector<gamedata::ItemDefinition> gap_items() {
    std::vector<gamedata::ItemDefinition> items = gathering_items();
    items.push_back(chests::chest_item("Test_Purse", "Resource", "Resource+LootContainer", 2, 10000));
    items.push_back(chests::chest_item("Test_SkinBox", "Armor", "Resource+LootContainer", 5, 1));
    items.push_back(chests::chest_item("Test_NoRowBox", "Resource", "Resource+LootContainer", 2, 10));
    items.push_back(chests::chest_item("Test_BothBox", "Resource", "Resource+LootContainer", 2, 10));
    return items;
}

gamedata::LootData gap_loot() {
    gamedata::LootData data = gathering_loot();
    // QG2A: the parents of the categories (VitalsCategoryData.GroupVitalsCategoryId, in lower case).
    data.categoryGroups = {{"test_bagwolf_category", "test_wolfpack"}, {"test_wolfpack", "test_beasts"}};
    // QG2C: the salvage events of the containers (the cells of the loot tool).
    data.containerEvents = {{"test_purse", "Test_Purse_Event"}, {"test_skinbox", "Test_Skin_Event"}, {"test_norowbox", "Test_Missing_Event"},
                            {"test_bothbox", "Test_Purse_Event"}};
    data.containerRoots = {{"test_bothbox", "Test_GatherFiber"}};
    data.salvageEvents = {
        {"test_purse_event", gamedata::read_game_event("Test_Purse_Event", {{"EventID", "Test_Purse_Event"}, {"CurrencyReward", "2500"}, {"EmitTelemetry", "1"}})},
        {"test_skin_event",
         gamedata::read_game_event("Test_Skin_Event", {{"EventID", "Test_Skin_Event"}, {"EntitlementId", "EntitlementSkin_Test"}, {"EmitTelemetry", "1"}})}};
    return data;
}

/** The gathering world with the quests, the items, and the loot of the cases (loaded before the entry of the characters). */
struct GapShop : GatherShop {
    explicit GapShop(const std::string& database = ":memory:") : GatherShop(database) {
        host.catalog.set(gap_items());
        if (!host.loot.set(gap_loot())) {
            throw std::runtime_error("quest gap test: the loot data was not set: " + host.loot.error());
        }
        if (!host.quests.load_text(gap_quests().text())) {
            throw std::runtime_error("quest gap test: the quest data did not load: " + host.quests.error());
        }
        if (!host.crafting.set(procedural::procedural_document())) {
            throw std::runtime_error("quest gap test: the crafting data was not set: " + host.crafting.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    world::Entity& own(const char* character) {
        return *entity(character);
    }
    const world::QuestInstance* quest(const char* character, const char* objective) {
        return quest_of(own(character), objective);
    }
    void settle(int steps = 3) {
        host.steps(steps, {&clientA, &clientB});
    }
    std::uint64_t ready(const char* character, const char* objective) {
        const world::QuestResult result = quests().grant(own(character), objective, "command", 0);
        check(result.ok() && quests().ready(*quest(character, objective)), std::string("control: ") + objective + " is ready");
        return result.instance;
    }
    bool bit(const char* character, const char* id) {
        const std::optional<std::uint32_t> index = host.world.ownership().achievement_bit(id);
        return index && questfix::has_bit(own(character), *index);
    }
    std::uint32_t count(const char* character, const char* item) {
        return static_cast<std::uint32_t>(world::Inventory::count(own(character), item));
    }
    std::uint32_t slot_of(const char* character, const char* item) {
        const world::Entity& entity = own(character);
        for (std::uint32_t slot = 0; slot < entity.bag.slots.size(); ++slot) {
            if (entity.bag.slots[slot].item && entity.bag.slots[slot].item->itemId == item) {
                return slot;
            }
        }
        return 0xFFFFFFFFU;
    }
    world::OpenResult open(const char* character, const char* item) {
        world::Entity& entity = own(character);
        const std::uint32_t slot = slot_of(character, item);
        const world::InstanceId instance = slot < entity.bag.slots.size() ? entity.bag.slots[slot].item->instance : world::InstanceId{};
        return host.world.item_containers().open(entity, slot, instance, 1);
    }
};

/** @return The number of fragments of a slot of an entity that is not the own one (interest 1) that a client got from `from` on. */
std::size_t foreign_slot(const FakeClient& client, std::uint32_t slot, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const tests::Record& record : read_bundle(client.got[index].body).records) {
            for (const tests::Fragment& fragment : record.fragments) {
                count += record.interest != 1 && fragment.slot == slot ? 1 : 0;
            }
        }
    }
    return count;
}

std::uint8_t state_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].state : 255;
}

std::uint32_t progress_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].progress : 9999;
}

/** QG2A: the category rule of the kills. */
void category_kills(const Options&) {
    GapShop shop;
    const gamedata::LootTables& loot = shop.host.loot;
    gamedata::LootVitals base;
    base.categories = {"test_bagwolf_7", "hostile", "test_bagwolf_category"};
    const std::vector<std::string> withBase = loot.kill_categories(&base, "Test_BagWolf_7", "Test_BagWolf_Category");
    const std::vector<std::string> noBase = loot.kill_categories(nullptr, "Test_BagWolf", "Test_BagWolf_Category");
    const std::vector<std::string> spawn = loot.kill_categories(&base, "Test_BagWolf_7", "Test_Spawn_Only");
    gamedata::LootVitals named;
    named.id = "Test_Base_Row";
    named.categories = {"hostile", "test_bagwolf_category"};
    const std::vector<std::string> killNamed = loot.kill_categories(&named, "Test_BagWolf_7", "Test_Spawn_Only", true);
    const std::vector<std::string> dropNamed = loot.kill_categories(&named, "Test_BagWolf_7", "Test_Spawn_Only", false);
    const auto text = [](const std::vector<std::string>& list) {
        std::string joined;
        for (const std::string& entry : list) {
            joined += (joined.empty() ? "" : ",") + entry;
        }
        return joined;
    };
    note("QG2A categories: base row " + text(withBase) + "; no base row " + text(noBase) + "; a spawn category " + text(spawn) + "; kill "
         + text(killNamed) + "; drop " + text(dropNamed));
    check(text(withBase) == "test_bagwolf_7,hostile,test_bagwolf_category,test_wolfpack" && text(noBase) == "test_bagwolf,test_bagwolf_category,test_wolfpack"
              && text(spawn) == "test_bagwolf_7,hostile,test_bagwolf_category,test_spawn_only,test_wolfpack",
          "the kill categories: the base row (none: the vitals row), the spawn category also with a base row, then the direct parent of each "
          "(one level: test_beasts, the parent of the parent, is not there; client 0x3B19F40)");
    check(text(killNamed) == "test_base_row,hostile,test_bagwolf_category,test_spawn_only,test_wolfpack"
              && text(dropNamed) == "hostile,test_bagwolf_category,test_spawn_only,test_wolfpack",
          "the kill matches the id of the base row (0x3B1A057); the quest drops do not (0x5C75830)");
    shop.enter();
    check(shop.quests().grant(shop.own(kCharacterA), "Gap_KillQuest", "command", 0).ok()
              && shop.quests().grant(shop.own(kCharacterB), "Gap_KillQuest", "command", 0).ok(),
          "setup: A and B have the kill quest of the parent category");
    const std::size_t fromB = shop.clientB.got.size();
    (void)shop.dead_wolf();
    shop.settle();
    const world::QuestInstance* questA = shop.quest(kCharacterA, "Gap_KillQuest");
    const world::QuestInstance* questB = shop.quest(kCharacterB, "Gap_KillQuest");
    note("QG2A A after one kill: " + states_text(*questA) + "; B: " + states_text(*questB));
    check(progress_of(questA, 0) == 1 && state_of(questA, 0) == kActive, "the kill of a wolf of Test_BagWolf_Category counts 1 of 2 for Test_WolfPack (its parent)");
    check(progress_of(questA, 1) == 0 && state_of(questA, 1) == kActive,
          "the kill does not count for Test_Beasts (the parent of the parent: one level, client 0x3B19F40)");
    check(progress_of(questA, 2) == 0, "the kill does not count for an unrelated category");
    check(shop.count(kCharacterA, "TestFiberT1") == 1 && state_of(questA, 3) == kComplete, "the quest drop of ItemDropVC Test_WolfPack is in the bag of A");
    check(progress_of(questB, 0) == 0 && progress_of(questB, 1) == 0 && shop.count(kCharacterB, "TestFiberT1") == 0, "B, not in the credit list, gets nothing");
    (void)shop.dead_wolf();
    shop.settle();
    questA = shop.quest(kCharacterA, "Gap_KillQuest");
    check(state_of(questA, 0) == kComplete && state_of(questA, 1) == kActive && state_of(questA, 4) == kActive && shop.count(kCharacterA, "TestFiberT1") == 1,
          "the second kill completes the parent task (PauseAndClearLootOnTaskComplete: no second drop)");
    check(questfix::foreign_fragments(shop.clientB, kObjectivesSlot, kObjectivesType) == 0 && foreign_slot(shop.clientB, kObjectivesSlot, fromB) == 0,
          "the Objectives state of A does not reach the session of B (owner only, MP-10)");
}

/** QG2B: TaskHaveLevel. */
void have_level(const Options&) {
    GapShop shop;
    shop.enter();
    // The level cap of the test tables is 6.
    check(shop.host.world.progress().set_level(kCharacterA, 5).ok() && shop.host.world.progress().set_level(kCharacterB, 2).ok(),
          "setup: A has the level 5, B the level 2");
    shop.settle();
    const gamedata::QuestObjective* row = shop.host.quests.objective("Gap_LevelQuest");
    check(row != nullptr && row->nodes[0].target == 1 && row->nodes[1].target == 1, "the target of TaskHaveLevel is 1 (constructor 0x3B01C80), not TargetQty");
    check(shop.quests().grant(shop.own(kCharacterA), "Gap_LevelQuest", "command", 0).ok()
              && shop.quests().grant(shop.own(kCharacterB), "Gap_LevelQuest", "command", 0).ok(),
          "setup: the grant of the level quest to A and B");
    const world::QuestInstance* questA = shop.quest(kCharacterA, "Gap_LevelQuest");
    const world::QuestInstance* questB = shop.quest(kCharacterB, "Gap_LevelQuest");
    note("QG2B at the activation: A " + states_text(*questA) + "; B " + states_text(*questB));
    check(state_of(questA, 0) == kComplete && progress_of(questA, 0) == 1 && state_of(questA, 1) == kActive && progress_of(questA, 1) == 0,
          "the activation at the level 5: TargetQty 4 is done (progress 1), TargetQty 6 waits");
    check(state_of(questB, 0) == kActive && progress_of(questB, 0) == 0, "B at the level 2: TargetQty 4 waits");
    const std::uint64_t looks = shop.quests().counts().levelLooks;
    const std::size_t fromB = shop.clientB.got.size();
    check(shop.host.world.progress().set_level(kCharacterA, 6).ok(), "control: A gets the level 6");
    shop.settle();
    questA = shop.quest(kCharacterA, "Gap_LevelQuest");
    questB = shop.quest(kCharacterB, "Gap_LevelQuest");
    check(state_of(questA, 1) == kCompleteActive && state_of(questA, 2) == kComplete && shop.bit(kCharacterA, "Gap_Lvl_Done")
              && shop.quests().counts().levelLooks == looks + 1,
          "the level change completes TaskHaveLevel 6 at the next tick (one look), and the tree goes on");
    check(state_of(questB, 0) == kActive && !shop.bit(kCharacterB, "Gap_Lvl_Done") && foreign_slot(shop.clientB, kObjectivesSlot, fromB) == 0,
          "B does not change, and the Objectives state of A does not reach B (MP-10)");
    check(shop.quests().grant(shop.own(kCharacterA), "Gap_FactionLevel", "command", 0).ok(), "control: the grant of the faction level quest");
    check(state_of(shop.quest(kCharacterA, "Gap_FactionLevel"), 0) == kActive && progress_of(shop.quest(kCharacterA, "Gap_FactionLevel"), 0) == 0,
          "the progression form (ItemName Covenant) waits: the faction progression has no owner (roadmap 4.5)");
}

bool sql(const std::string& path, const char* command) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, command, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

/** QG2C: the salvage event of a coin purse. */
void purse_event(const Options& options) {
    const auto path = options.workDirectory / "quest-gap-purse.sqlite";
    std::filesystem::create_directories(options.workDirectory);
    std::filesystem::remove(path);
    GapShop shop(path.string());
    shop.enter();
    world::ItemContainers& containers = shop.host.world.item_containers();
    check(containers.openable("Test_Purse") && containers.openable("Test_BothBox") && !containers.openable("Test_SkinBox")
              && !containers.openable("Test_NoRowBox"),
          "the purse (a coin event, no table) and a box with a table and a coin event open; a skin box and an event with no row do not");
    check(containers.open_refusal("Test_SkinBox").find("EntitlementId EntitlementSkin_Test") != std::string::npos
              && containers.open_refusal("Test_NoRowBox").find("has no row") != std::string::npos,
          "the cause of each refusal names the reward with no owner, or the event with no row");
    check(shop.host.world.inventory().give(kCharacterA, "Test_Purse", 2).ok(), "setup: A has 2 purses");
    const std::uint64_t coinA = shop.own(kCharacterA).wallet.coin;
    const std::uint64_t coinB = shop.own(kCharacterB).wallet.coin;
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    const world::OpenResult first = shop.open(kCharacterA, "Test_Purse");
    shop.settle();
    note("QG2C the open of a purse: " + std::string(world::open_error_text(first.error)) + ", coin " + std::to_string(first.coin));
    check(first.ok() && first.coin == 2500 && shop.own(kCharacterA).wallet.coin == coinA + 2500 && shop.count(kCharacterA, "Test_Purse") == 1
              && shop.host.store.currency(kCharacterA, state::kCurrencyCoin) == coinA + 2500,
          "one open pays CurrencyReward 2500 (25.00 coin) one time and takes one purse: the entity and the row");
    check(!slot_updates(shop.clientA, kCurrencySlot, fromA).fragments.empty() && foreign_slot(shop.clientB, kCurrencySlot, fromB) == 0
              && shop.own(kCharacterB).wallet.coin == coinB,
          "the currency state goes to A only (owner only); B does not change (MP-10)");
    // A refused write: the take and the coin are one transaction.
    check(sql(path.string(), "CREATE TRIGGER gap_fail BEFORE UPDATE ON currencies BEGIN SELECT RAISE(FAIL,'gap'); END;"
                             "CREATE TRIGGER gap_fail_insert BEFORE INSERT ON currencies BEGIN SELECT RAISE(FAIL,'gap'); END"),
          "control: the writes of the coin fail now");
    const world::OpenResult refusedWrite = shop.open(kCharacterA, "Test_Purse");
    check(!refusedWrite.ok() && shop.count(kCharacterA, "Test_Purse") == 1 && shop.own(kCharacterA).wallet.coin == coinA + 2500
              && shop.host.store.currency(kCharacterA, state::kCurrencyCoin) == coinA + 2500,
          "a refused write of the coin changes nothing: the purse stays, the coin stays");
    check(sql(path.string(), "DROP TRIGGER gap_fail; DROP TRIGGER gap_fail_insert"), "control: the store takes the writes again");
    // The refusals of an event that the open does not pay.
    check(shop.host.world.inventory().give(kCharacterA, "Test_SkinBox", 1).ok() && shop.host.world.inventory().give(kCharacterA, "Test_NoRowBox", 1).ok(),
          "setup: a skin box and a box with no event row");
    const world::OpenResult skin = shop.open(kCharacterA, "Test_SkinBox");
    const world::OpenResult noRow = shop.open(kCharacterA, "Test_NoRowBox");
    check(skin.error == world::OpenError::eventNotPayable && noRow.error == world::OpenError::eventNotPayable
              && shop.count(kCharacterA, "Test_SkinBox") == 1 && shop.count(kCharacterA, "Test_NoRowBox") == 1
              && shop.own(kCharacterA).wallet.coin == coinA + 2500,
          "a skin box (EntitlementId) and an event with no row are refused: the item and the coin stay");
    // A box with a table and an event: the roll and the coin.
    check(shop.host.world.inventory().give(kCharacterA, "Test_BothBox", 1).ok(), "setup: a box with a table and a coin event");
    const std::uint32_t fiber = shop.count(kCharacterA, "TestFiberT1");
    const world::OpenResult both = shop.open(kCharacterA, "Test_BothBox");
    check(both.ok() && both.coin == 2500 && shop.count(kCharacterA, "TestFiberT1") == fiber + 3 && shop.count(kCharacterA, "Test_BothBox") == 0,
          "a box with a table and a salvage event rolls the table (3 TestFiberT1) and pays the event (2500)");
    // The reward join of a quest (the hand-in and the manifest): a purse is a payable reward, a skin box is not.
    world::RewardJoin join;
    std::string detail;
    check(shop.quests().reward_join(shop.own(kCharacterA), *shop.host.quests.objective("Gap_PurseQuest"), 0, join, detail) == world::QuestError::none
              && join.items.size() == 1 && join.items[0].itemId == "Test_Purse",
          "the reward join: a coin purse is a reward that the backend pays");
    const world::QuestError skinJoin = shop.quests().reward_join(shop.own(kCharacterA), *shop.host.quests.objective("Gap_SkinQuest"), 0, join, detail);
    note("QG2C the join of the skin box: " + detail);
    check(skinJoin == world::QuestError::cannotPay && detail.find("loot container that cannot open") != std::string::npos
              && detail.find("EntitlementId") != std::string::npos,
          "the reward join of a skin box is refused with its cause (EntitlementId)");
}

/** QG2E: the Azoth amount. */
void azoth_amount(const Options&) {
    check(gamedata::azoth_amount("37.5") == 37U && gamedata::azoth_amount("12.5") == 12U && gamedata::azoth_amount("0.5") == 0U
              && gamedata::azoth_amount("50") == 50U && gamedata::azoth_amount("70000") == 70000U - 65536U && !gamedata::azoth_amount("-1")
              && !gamedata::azoth_amount("x"),
          "the client amount: truncation toward zero (37.5: 37, 12.5: 12, 0.5: 0), 16 bits (70000: 4464); a negative or a text: no value");
    GapShop shop;
    const gamedata::QuestGameEvent* zero = shop.host.quests.game_event("Gap_Azoth_Success");
    const gamedata::QuestGameEvent* blank = shop.host.quests.game_event("Gap_Azoth_Blank");
    check(zero != nullptr && zero->azoth == 37 && zero->unpaid.empty() && blank != nullptr && blank->azoth == 12 && blank->unpaid.empty(),
          "the loader: 37.5 with the chance 0 is 37, 12.5 with a blank chance is 12; no unpaid reward (the chance has no effect)");
    shop.enter();
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    world::RewardJoin join;
    std::string detail;
    check(shop.quests().reward_join(shop.own(kCharacterA), *shop.host.quests.objective("Gap_AzothQuest"), 0, join, detail) == world::QuestError::none
              && join.azoth == 37,
          "the join (the preview of the command) has the amount 37");
    const world::HandInResult paid = shop.quests().hand_in(shop.own(kCharacterA), shop.ready(kCharacterA, "Gap_AzothQuest"));
    shop.settle();
    check(paid.ok() && paid.azoth == 37 && world::Skills::balance(shop.own(kCharacterA).skills, world::kAzothCurrency) == 37,
          "the hand-in pays 37 Azoth (the amount that the client shows; a project rule)");
    check(world::Skills::balance(shop.own(kCharacterB).skills, world::kAzothCurrency) == 0 && !slot_updates(shop.clientA, kCategoricalSlot, fromA).fragments.empty()
              && foreign_slot(shop.clientB, kCategoricalSlot, fromB) == 0,
          "B gets no Azoth, and the categorical state of A goes to A only (MP-10)");
}

/** QG2F: the push of PushObjectiveOnReqMet. */
void push_grants(const Options&) {
    GapShop shop;
    check(!world::Quests::pushed(*shop.host.quests.objective("01_SoleSurvivor2")) && !world::Quests::pushed(*shop.host.quests.objective("Quest_03189"))
              && world::Quests::pushed(*shop.host.quests.objective("Gap_Push")) && !world::Quests::pushed(*shop.host.quests.objective("Gap_KillQuest")),
          "decision B: 01_SoleSurvivor2 and Quest_03189 are not pushed; a push row is; a row with no flag is not");
    shop.enter();
    world::Quests& quests = shop.quests();
    const world::QuestInstance* freeA = shop.quest(kCharacterA, "Gap_PushFree");
    note("QG2F after the entry: A has " + std::to_string(shop.own(kCharacterA).quests.active.size()) + " quest(s), push grants "
         + std::to_string(quests.counts().pushGrants));
    check(freeA != nullptr && freeA->source == "push" && shop.quest(kCharacterB, "Gap_PushFree") != nullptr,
          "the entry grants the push row with no requirement to A and to B (source push)");
    check(shop.quest(kCharacterA, "01_SoleSurvivor2") == nullptr && shop.quest(kCharacterA, "Quest_03189") == nullptr
              && shop.quest(kCharacterA, "Gap_PushTerritory") == nullptr && shop.quest(kCharacterA, "Gap_Push") == nullptr,
          "no grant of the excluded rows (their conditions are true), of a row whose ExclusiveTerritory is not the territory of the character, "
          "and of a row whose requirement is false");
    // A change of a bit: the level is below RequiredLevel 5 (the level cap of the test tables is 6).
    check(shop.host.world.progress().set_level(kCharacterA, 3).ok() && shop.host.world.progress().set_level(kCharacterB, 6).ok(),
          "setup: A has the level 3, B the level 6");
    shop.settle();
    const std::optional<std::uint32_t> required = shop.host.world.ownership().achievement_bit("Gap_PushReq");
    check(required && shop.host.world.ownership().unlock_achievement(shop.own(kCharacterA), "Gap_PushReq", *required), "control: A gets Gap_PushReq");
    const std::uint64_t visits = quests.counts().pushVisits;
    shop.settle();
    check(shop.quest(kCharacterA, "Gap_Push") == nullptr && quests.counts().pushVisits == visits + 1,
          "after the change of the bit, the level 3 is below 5: no grant; the check visited 1 row (the row of the changed bit)");
    // A change of the level: the grant, with the `_pushedonce` bit of its first task.
    const std::size_t fromB = shop.clientB.got.size();
    check(shop.host.world.progress().set_level(kCharacterA, 5).ok(), "control: A gets the level 5");
    shop.settle();
    const world::QuestInstance* push = shop.quest(kCharacterA, "Gap_Push");
    note("QG2F after the level change: " + (push != nullptr ? states_text(*push) : std::string("no push")));
    check(push != nullptr && push->source == "push" && state_of(push, 0) == kComplete && shop.bit(kCharacterA, "Gap_Push_pushedonce"),
          "the level change grants the push row; its first task sets the bit Gap_Push_pushedonce");
    check(shop.quest(kCharacterB, "Gap_Push") == nullptr && foreign_slot(shop.clientB, kObjectivesSlot, fromB) == 0,
          "B (level 6, no Gap_PushReq) gets no push, and the Objectives state of A does not reach B (MP-10)");
    if (push == nullptr) {
        return;
    }
    // No second grant: a later change finds the row active and its bit set.
    const std::uint64_t instance = push->instance;
    const std::uint64_t grants = quests.counts().pushGrants;
    const std::optional<std::uint32_t> other = shop.host.world.ownership().achievement_bit("Gap_Other_Bit");
    check(shop.host.world.ownership().unlock_achievement(shop.own(kCharacterA), "Gap_Other_Bit", *other), "control: a different bit");
    check(shop.host.world.progress().set_level(kCharacterA, 6).ok(), "control: one more level");
    shop.settle();
    check(quests.counts().pushGrants == grants && shop.quest(kCharacterA, "Gap_Push")->instance == instance,
          "no second grant: the row is active and its `_pushedonce` bit is set");
    // The abandon: the undo of the bit makes the row available, and the change grants it again (a project rule).
    check(quests.abandon(shop.own(kCharacterA), instance).ok(), "control: the abandon of the push row");
    const bool bitAfterAbandon = shop.bit(kCharacterA, "Gap_Push_pushedonce");
    shop.settle();
    const world::QuestInstance* again = shop.quest(kCharacterA, "Gap_Push");
    note(std::string("QG2F after the abandon: the bit ") + (bitAfterAbandon ? "stayed" : "went") + ", a new instance: "
         + (again != nullptr ? std::to_string(again->instance != instance) : std::string("none")));
    check(!bitAfterAbandon && again != nullptr && again->instance != instance && shop.bit(kCharacterA, "Gap_Push_pushedonce"),
          "the abandon undoes Gap_Push_pushedonce (StayActive 0); the change of the bit grants the row again, and its task sets the bit again");
    // The completion of a push row: its completion achievement stops a new grant (also with Retrigger TRUE).
    const world::QuestInstance* free = shop.quest(kCharacterA, "Gap_PushFree");
    check(free != nullptr && quests.ready(*free), "control: the free push row is ready");
    if (free == nullptr) {
        return;
    }
    const world::HandInResult done = quests.hand_in(shop.own(kCharacterA), free->instance);
    shop.settle();
    check(done.ok() && shop.bit(kCharacterA, "Gap_Free_Done") && shop.quest(kCharacterA, "Gap_PushFree") == nullptr,
          "after the hand-in the completion achievement Gap_Free_Done stops the push (Retrigger is a grant of one time)");
    // A new entry: the push rows that are not available stay away.
    const std::uint64_t before = quests.counts().pushGrants;
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    FakeClient returning(27060);
    shop.host.enter(returning, shop.a.registration());
    shop.host.steps(5, {&returning, &shop.clientB});
    const world::Entity* back = shop.host.world.find_character(kCharacterA);
    check(back != nullptr && quests.counts().pushGrants == before && quest_of(*back, "Gap_PushFree") == nullptr && quest_of(*back, "Gap_Push") != nullptr,
          "a new entry grants nothing again (the completed row and the active row)");
}

/** QG2G: the loaders. */
void loaders(const Options& options) {
    const gamedata::QuestGameEvent range = gamedata::read_game_event("Gift", {{"EventID", "GiftGold1"}, {"CurrencyReward", "5000-10000"}});
    const gamedata::QuestGameEvent limited =
        gamedata::read_game_event("Cache", {{"EventID", "Cache"}, {"CurrencyReward", "5000"}, {"LootTags", "PUGFinder_DailyLimit_Vanilla"}});
    const gamedata::QuestGameEvent coin = gamedata::read_game_event("purse", {{"CurrencyReward", " 2500 "}});
    check(range.coin == 0 && !range.unpaid.empty() && range.unpaid.front() == "CurrencyReward 5000-10000" && range.id == "GiftGold1",
          "CurrencyReward is a whole number: a range (5000-10000) is a reward with no rule, not its first number");
    check(limited.coin == 5000 && limited.unpaid.size() == 1 && limited.unpaid.front().rfind("LootTags", 0) == 0,
          "LootTags (a loot limit) is a reward with no rule");
    check(coin.coin == 2500 && coin.unpaid.empty() && coin.id == "purse", "a coin event with the key as its id");
    // The members of the loot file (format 2): a file of before has none; the new members; a member with a wrong form refuses the file.
    const std::string base =
        R"({"format":2,"rewards":[[1,10,300,1]],"modifiers":{"Solo":[1,1,0,1,null]},"disparity":[[0,0,0]],"events":{"killevent":0.05},)"
        R"("credit":{"timeLimit":120,"healthShare":0.01,"range":200,"receivers":1,"modifiers":[[1,1,1]]},)"
        R"("gearScore":[[100,120,110,130,120,140,130,150,140,160]],"lootCeiling":[[1,150]],"containerFloor":[[1,100]],"chestRoots":[],)"
        R"("limits":{},"bases":[],"vitals":{},"tables":{},"buckets":{})";
    const std::string members = R"(,"containerEvents":{"Test_Purse":"Test_Purse_Event"},)"
                                R"("salvageEvents":{"test_purse_event":{"CurrencyReward":"2500","EventID":"Test_Purse_Event"}},)"
                                R"("categoryGroups":{"Dryad_Test":"Bronzeroot_Test"})";
    const auto load = [&](gamedata::LootTables& tables, const std::string& text) {
        const std::filesystem::path path = options.workDirectory / "quest-gap-loot.json";
        std::filesystem::create_directories(options.workDirectory);
        std::ofstream(path) << text;
        return tables.load(path.string());
    };
    gamedata::LootTables before;
    gamedata::LootTables after;
    gamedata::LootTables bad;
    const bool oldLoaded = load(before, base + "}");
    const bool newLoaded = load(after, base + members + "}");
    note("QG2G loads: before " + std::to_string(oldLoaded) + " (" + before.error() + "), after " + std::to_string(newLoaded) + " (" + after.error() + ")");
    check(oldLoaded && before.container_event("Test_Purse").empty() && before.kill_categories(nullptr, "Dryad_Test", "").size() == 1,
          "a file of before: no container event and no parent of a category");
    check(newLoaded && after.container_event("test_purse") == "Test_Purse_Event" && after.salvage_event("Test_Purse_Event") != nullptr
              && after.salvage_event("Test_Purse_Event")->coin == 2500 && after.kill_categories(nullptr, "Dryad_Test", "").size() == 2
              && after.kill_categories(nullptr, "Dryad_Test", "")[1] == "bronzeroot_test",
          "the new members: the event of a container (any case), the coin of its event, the parent of a category (lower case)");
    check(!load(bad, base + R"(,"categoryGroups":{"Dryad_Test":7}})") && !load(bad, base + R"(,"salvageEvents":{"x":{"CurrencyReward":5}}})"),
          "a group or a cell that is not a text refuses the file");
    gamedata::LootData data = gap_loot();
    check(after.set(data) && after.container_event("TEST_PURSE") == "Test_Purse_Event" && after.salvage_event("test_purse_event")->coin == 2500
              && after.salvage_event("Test_Missing_Event") == nullptr && after.container_event("QbPouch").empty(),
          "the loot data of the cases: the event of a container, an event with no row, a container with no event");
}

} // namespace

std::vector<Case> quest_gap_cases() {
    return {{"QG2A", "quests QG2a (I1): the category rule of a kill (the spawn category, the parents), the parent credit and drop, two sessions",
             category_kills},
            {"QG2B", "quests QG2a (I2): TaskHaveLevel at the activation and at a level change, the progression form waits, two sessions", have_level},
            {"QG2C", "quests QG2a (I3): the salvage event of a coin purse, one payment with the take, the refusals, a refused write, two sessions",
             purse_event},
            {"QG2E", "quests QG2a (I6): the Azoth amount of the client (37.5 gives 37), the chance has no effect, two sessions", azoth_amount},
            {"QG2F", "quests QG2a (I7): the push of PushObjectiveOnReqMet at the entry and at a change, _pushedonce, the excluded rows, the "
                     "abandon, two sessions",
             push_grants},
            {"QG2G", "quests QG2a: the loaders of the salvage events and the category groups, the reader of a game event", loaders}};
}

} // namespace soulwarden::backend::tests

// The cases of stage QG5a (docs/systems/quests.md, the quest chest drops and the quest objects of stage QG5a): the quest drop of a chest
// (ChestDropProbability), the proximity quest objects, the loaders of the new members, and the support manifest. Made-up accounts,
// characters, ids, items, chests, places, and objects only.
//
//   QG5A  the quest drop of a chest: no item from a chest outside the point of interest of the task; one item for each open of a chest in
//         the place (with the loot of the chest, one grant), up to the target count (2), then none; the second open of a looted chest is
//         refused (no second award); B with no task gets no quest item from the same chest (MP-10)
//   QG5B  one transaction: a refused write of the award row (AwardOnlyOnce) gives no quest item, no item of the chest, and no claim; the next
//         open after the fault gives both
//   QG5C  the proximity objects: A with the task in the sphere of the trail (0.5 m of 0.75 m) finds it at the next tick (the game event, the
//         task, the LootTracker entry Looted of its actor), A at 2 m does not; B with no task in the sphere: no event and no entry, and the
//         entry of A does not reach B (MP-10); a strike does not hit a proximity object; after the respawn of the row the entry goes
//   QG5D  the loaders: the member proximity of quest-objects.json (a radius of 0 or above 64 m refuses the file; a file with no member
//         loads), the member children of gathering-data.json (a key that is not UUID/ENTITY refuses; a child key finds its link), and the
//         requirement conditional of the interaction data
//   QG5E  the manifest: the item of a chest task has the source "the quest drop of the chests of TAG" when an ordinary chest is in the
//         point of interest of its tag, and no source for a tag with no chest; the event of a proximity object has a producer

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "gamedata/gathering_data.h"
#include "gamedata/interaction_data.h"
#include "gamedata/poi_data.h"
#include "gamedata/quest_object_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "support/quest_person_fixture.h"
#include "world/quest_objects.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using questfix::json;
using interaction::kCharacterA;
using interaction::kCharacterB;

constexpr const char* kToken = "Qg5_Token";
constexpr const char* kOther = "Qg5_Other";
constexpr const char* kTrailTag = "IO_Qg5_Trail";
constexpr const char* kTrailEvent = "Qg5_Trail_Interact";

/** A point of interest of 6 m around the supply and the elite chest (not the provision chest at 102, 202) and one far away. */
json chest_pois() {
    return json{{"format", 1},
                {"pois", json::array({{{"id", 49501}, {"tags", {"qg5_ruins"}}, {"territory", 6}, {"center", {98.0, 201.0, 30.0}}, {"radius", 50.0},
                                       {"shape", json::array({{95.0, 198.0}, {101.0, 198.0}, {101.0, 204.0}, {95.0, 204.0}})}},
                                      {{"id", 49502}, {"tags", {"qg5_far"}}, {"territory", 6}, {"center", {900.0, 900.0, 30.0}}, {"radius", 50.0},
                                       {"shape", json::array({{890.0, 890.0}, {910.0, 890.0}, {910.0, 910.0}, {890.0, 910.0}})}}})}};
}

/** The quests of the chest cases: the fetch of 2 tokens from the chests of the ruins, a fetch from a far place, a fetch of chance 40. */
json chest_quests(bool awardOnce = false) {
    json document = questfix::quest_document();
    json fetch = {{"ItemName", kToken}, {"TargetQty", "2"}, {"ChestDropProbability", "100"}, {"POITag", "qg5_ruins"},
                  {"PauseAndClearLootOnTaskComplete", "1"}, {"EnforceLocationRequirement", "1"}};
    if (awardOnce) {
        fetch["AwardOnlyOnce"] = "1";
    }
    document["tasks"]["qg5_fetch"] = questfix::task("Qg5_Fetch", "taskhaveandreturnitems", 26, fetch);
    document["tasks"]["qg5_wait"] = questfix::task("Qg5_Wait", "taskachievement", 18, {{"ItemName", "Test_Never"}});
    document["tasks"]["qg5_root"] = questfix::task("Qg5_Root", "simpletaskcontainer", 0);
    document["objectives"].push_back({{"id", "Qg5_ChestQuest"}, {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "qg5_fetch", json::array()}), json::array({1, "qg5_wait", json::array()}),
                                                            json::array({2, "qg5_root", json::array({0, 1})})})}});
    document["tasks"]["qg5_far_fetch"] =
        questfix::task("Qg5_Far_Fetch", "taskhaveitems", 25, {{"ItemName", kOther}, {"TargetQty", "1"}, {"ChestDropProbability", "100"}, {"POITag", "qg5_far"}});
    document["objectives"].push_back({{"id", "Qg5_FarQuest"}, {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "qg5_far_fetch", json::array()})})}});
    return document;
}

/** The chest shop with the quests, the points of interest, and the two items of the tasks. */
struct ChestQuestShop : chests::ChestShop {
    explicit ChestQuestShop(const std::string& database = ":memory:", bool awardOnce = false) : chests::ChestShop(database) {
        std::vector<gamedata::ItemDefinition> items = chests::chest_items();
        items.push_back(chests::chest_item(kToken, "Resource", "Resource+Quest", 0, 100));
        items.push_back(chests::chest_item(kOther, "Resource", "Resource+Quest", 0, 100));
        host.catalog.set(items);
        if (!host.pois.load_text(chest_pois().dump())) {
            throw std::runtime_error("QG5a test: the POI data did not load: " + host.pois.error());
        }
        if (!host.quests.load_text(chest_quests(awardOnce).dump())) {
            throw std::runtime_error("QG5a test: the quest data did not load: " + host.quests.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    /** One complete open of a chest by a character: the request, the hold to its time, the release. */
    void open(FakeClient& client, std::uint8_t tag, int milliseconds = 5300) {
        request(client, open_block(tag));
        hold(client, open_block(tag), milliseconds);
        end_block(client);
    }
};

std::uint64_t bag(const world::Entity& entity, const char* item) {
    return world::Inventory::count(entity, item);
}

/** QG5A: the quest drop of a chest. */
void chest_drops(const Options&) {
    ChestQuestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.quests().grant(a, "Qg5_ChestQuest", "command", 0).ok() && shop.quests().grant(a, "Qg5_FarQuest", "command", 0).ok(),
          "setup: A has the fetch of the ruins and the fetch of the far place");
    check(shop.host.pois.inside({"qg5_ruins"}, 0, chests::kSupplyPlace.x, chests::kSupplyPlace.y)
              && !shop.host.pois.inside({"qg5_ruins"}, 0, chests::kProvisionPlace.x, chests::kProvisionPlace.y)
              && shop.host.pois.inside({"qg5_ruins"}, 0, a.position.x, a.position.y),
          "setup: the supply chest and A are in the ruins, the provision chest is not");
    shop.open(shop.clientA, chests::kProvision);
    note("QG5A after the provision chest: " + std::to_string(bag(a, kToken)) + " token(s), " + std::to_string(shop.chests().counts().completions)
         + " open(s)");
    check(shop.chests().counts().completions == 1 && bag(a, kToken) == 0 && bag(a, kOther) == 0,
          "a chest outside the point of interest of the task gives no quest item (and none of a far place)");
    shop.open(shop.clientA, chests::kSupply);
    check(shop.chests().counts().completions == 2 && bag(a, kToken) == 1 && bag(a, "TestOre") >= 3 && shop.chests().counts().questDrops == 1,
          "a chest in the ruins gives one quest item with its own loot (one grant)");
    const world::QuestInstance* quest = questfix::quest_of(a, "Qg5_ChestQuest");
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kActive, "one of two: the task stays Active");
    shop.open(shop.clientA, chests::kSupply);
    check(bag(a, kToken) == 1 && shop.chests().counts().completions == 2, "the second open of the looted chest is refused: no second award");
    shop.open(shop.clientA, chests::kElite, 5300);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    quest = questfix::quest_of(a, "Qg5_ChestQuest");
    check(bag(a, kToken) == 2 && quest != nullptr && quest->tasks[0].state == world::quest_state::kComplete,
          "the second chest of the ruins gives the second item: the task has its target count and completes");
    const std::uint64_t drops = shop.chests().counts().questDrops;
    // B (no task) opens the supply chest: its own claim, its own loot, no quest item; the state of A does not reach B (MP-10).
    shop.open(shop.clientB, chests::kSupply);
    check(bag(b, kToken) == 0 && bag(b, "TestOre") >= 3 && shop.chests().counts().questDrops == drops && questfix::quest_of(b, "Qg5_ChestQuest") == nullptr,
          "B with no task opens the same chest: its loot and no quest item (MP-10)");
}

/** QG5B: one transaction of the open and the quest award. */
void chest_drop_transaction(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "qg5a-chest-award.sqlite3";
    std::filesystem::remove(path);
    ChestQuestShop shop(path.string(), true);
    sqlite3* connection = nullptr;
    check(sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK
              && sqlite3_exec(connection, "CREATE TRIGGER refuse_award BEFORE INSERT ON character_quest_awards BEGIN SELECT RAISE(FAIL, 'award'); END",
                              nullptr, nullptr, nullptr) == SQLITE_OK,
          "setup: the database refuses the award rows");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.quests().grant(a, "Qg5_ChestQuest", "command", 0).ok(), "setup: A has the fetch (AwardOnlyOnce)");
    shop.open(shop.clientA, chests::kSupply);
    check(bag(a, kToken) == 0 && bag(a, "TestOre") == 0 && shop.host.store.list_loot_claims(kCharacterA).empty() && shop.chests().counts().storeFailures == 1,
          "a refused award row: no quest item, no item of the chest, and no claim (one transaction)");
    check(sqlite3_exec(connection, "DROP TRIGGER refuse_award", nullptr, nullptr, nullptr) == SQLITE_OK, "setup: the fault goes");
    sqlite3_close(connection);
    shop.open(shop.clientA, chests::kSupply);
    check(bag(a, kToken) == 1 && bag(a, "TestOre") >= 3 && shop.host.store.list_loot_claims(kCharacterA).size() == 1,
          "the next open gives the quest item and the loot, and the chest is looted");
}

/** The proximity trail 2 m north of the place of A, and an object far away (the cost: not visited). */
json proximity_objects(double radius = 0.75) {
    const auto object = [](const char* key, const char* actor, double x, double y, double r) {
        return json{{"key", key}, {"actor", actor}, {"asset", "00112233445566778899aabbccddeeff"}, {"slot", 3}, {"row", 0}, {"tag", kTrailTag},
                    {"onlyOnQuest", true}, {"position", {x, y, 30.0}}, {"radius", r}, {"variant", "Qg5_Trail1"}};
    };
    return {{"format", 1},
            {"rows", json::array({{{"id", "Qg5_Trail"}, {"time", 1.0}, {"respawn", {200.0, 200.0}}, {"event", kTrailEvent}}})},
            {"objects", json::array()},
            {"proximity", json::array({object("55555555-0000-4000-8000-000000000001", "0c000000000000550000000000000000", 100.0, 202.0, radius),
                                       object("55555555-0000-4000-8000-000000000002", "0c000000000000560000000000000000", 900.0, 900.0, radius)})}};
}

/** QG5C: the proximity objects. */
void proximity_objects_case(const Options&) {
    questperson::QuestShop shop;
    json quests = questfix::quest_document();
    quests["tasks"]["qg5_trail_task"] = questfix::task("Qg5_Trail_Task", "taskgameevent", 14, {{"KillEnemyType", kTrailEvent}, {"TargetQty", "1"}, {"InteractTag", kTrailTag}});
    quests["tasks"]["qg5_wait"] = questfix::task("Qg5_Wait", "taskachievement", 18, {{"ItemName", "Test_Never"}});
    quests["tasks"]["qg5_root"] = questfix::task("Qg5_Root", "simpletaskcontainer", 0);
    quests["objectives"].push_back({{"id", "Qg5_TrailQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qg5_trail_task", json::array()}), json::array({1, "qg5_wait", json::array()}),
                                                          json::array({2, "qg5_root", json::array({0, 1})})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: the trail quest loads");
    check(shop.host.questObjects.load_text(proximity_objects().dump()) && shop.host.questObjects.proximity_count() == 2,
          "setup: two proximity objects load (" + shop.host.questObjects.error() + ")");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(a, "Qg5_TrailQuest", "command", 0).ok(), "setup: A has the trail task");
    const std::array<std::uint8_t, 16> actor = shop.host.questObjects.objects()[0].actor;
    // B (no task) walks into the sphere first: nothing.
    shop.walk(kCharacterB, state::WorldPosition{100.0F, 201.6F, 30.0F});
    check(shop.host.world.quest_objects().counts().proximityFound == 0 && b.questObjects.empty(),
          "B with no task in the sphere of the trail: no event and no entry (InteractOnlyOnQuest)");
    shop.walk(kCharacterA, state::WorldPosition{100.0F, 200.0F, 30.0F});
    const world::QuestInstance* quest = questfix::quest_of(a, "Qg5_TrailQuest");
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kActive && shop.host.world.quest_objects().counts().proximityFound == 0,
          "A 2 m from the trail (outside its sphere of 0.75 m): the task stays Active");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.walk(kCharacterA, state::WorldPosition{100.0F, 201.5F, 30.0F});
    quest = questfix::quest_of(a, "Qg5_TrailQuest");
    note("QG5C A 0.5 m from the trail: found " + std::to_string(shop.host.world.quest_objects().counts().proximityFound) + ", task state "
         + std::to_string(quest != nullptr ? quest->tasks[0].state : 255));
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kComplete && shop.host.world.quest_objects().counts().proximityFound == 1,
          "A with the task in the sphere: the game event of the row completes its task at the next tick (one find)");
    check(chests::tracker_state(shop.clientA, actor, fromA) == world::quest_object_rules::kLooted, "A gets the LootTracker entry Looted of the trail");
    check(!chests::tracker_state(shop.clientB, actor, fromB), "the entry of A does not reach B (owner only, MP-10)");
    shop.walk(kCharacterA, state::WorldPosition{100.0F, 201.6F, 30.0F});
    check(shop.host.world.quest_objects().counts().proximityFound == 1, "a second look in the sphere: no second find (the object is found for A)");
    shop.host.world.quest_objects().strike(a, 1, 1);
    check(shop.host.world.quest_objects().counts().hits == 0, "a strike does not hit a proximity object");
    const world::QuestObjectCounts& counts = shop.host.world.quest_objects().counts();
    check(counts.proximityLooks > 0 && counts.proximityVisits == counts.proximityLooks,
          "each look visits only the objects of the cells near the character: one object (the far object: never)");
    // The clients go on with their reports (a session with no datagram for long ends) while the world clock passes 201 s.
    for (int second = 0; second < 201; ++second) {
        shop.report(shop.clientA, std::nullopt);
        shop.report(shop.clientB, std::nullopt);
        shop.host.advance(std::chrono::seconds(1));
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
    check(shop.entity(kCharacterA) == &a && a.questObjects.empty() && shop.host.world.quest_objects().counts().respawns == 1, "after the respawn of the row (200 s) the entry of A goes");
}

/** QG5D: the loaders of the new members. */
void loaders(const Options&) {
    gamedata::QuestObjectData objects;
    check(objects.load_text(proximity_objects().dump()) && objects.objects().size() == 2 && objects.objects()[0].proximity
              && objects.objects()[0].radius == 0.75F && objects.proximity_reach() == 0.75F,
          "quest-objects.json: the member proximity loads its objects with their radius");
    check(!objects.load_text(proximity_objects(0.0).dump()) && !objects.load_text(proximity_objects(65.0).dump()),
          "a proximity object with a radius of 0 or above 64 m refuses the file");
    json old = proximity_objects();
    old.erase("proximity");
    check(objects.load_text(old.dump()) && objects.proximity_count() == 0, "a file of before (no member proximity) loads with no proximity object");
    json gathering = gather::gathering_document();
    gathering["children"] = json::object();
    gathering["children"][std::string(36, 'a') + "/0000000000000001"] = gathering["capitals"].begin().value();
    gamedata::GatheringData data;
    const std::string child = std::string(36, 'a') + "/0000000000000001";
    check(data.set(gathering.dump(), {{gather::kRegion, gather::region_records()}}) && data.capital(child).has_value(),
          "gathering-data.json: a child of the member children (UUID/ENTITY) finds its link (" + data.error() + ")");
    gathering["children"]["not-a-child"] = gathering["capitals"].begin().value();
    check(!data.set(gathering.dump(), {{gather::kRegion, gather::region_records()}}), "a key of children that is not UUID/ENTITY refuses the file");
    gamedata::InteractionData interactions;
    gamedata::InteractionObject object = interaction::object_of(1, gamedata::InteractionKind::gathering, interaction::kHere, {0});
    object.bounds[0].conditional = "Qg5_A || Qg5_B";
    check(interactions.set({interaction::option_row("GatherOption", 14, true)}, {}, {object})
              && interactions.objects()[0].bounds[0].conditional == "Qg5_A || Qg5_B",
          "the interaction data keeps the conditional of a bound (the expression of a variation requirement)");
}

/** QG5E: the manifest. */
void manifest(const Options&) {
    ChestQuestShop shop;
    check(shop.host.questObjects.load_text(proximity_objects().dump()), "setup: the proximity objects load");
    json quests = chest_quests();
    quests["tasks"]["qg5_trail_task"] = questfix::task("Qg5_Trail_Task", "taskgameevent", 14, {{"KillEnemyType", kTrailEvent}, {"TargetQty", "1"}, {"InteractTag", kTrailTag}});
    quests["objectives"].push_back({{"id", "Qg5_TrailQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qg5_trail_task", json::array()})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: the quests of the manifest load");
    world::QuestSupport support(shop.host.world);
    const json result = support.manifest({});
    const auto task_of = [&](const char* objective, std::size_t index) {
        for (const json& row : result["objectives"]) {
            if (row["id"] == objective) {
                return row["tasks"][index];
            }
        }
        return json();
    };
    const json fetch = task_of("Qg5_ChestQuest", 0);
    const json far = task_of("Qg5_FarQuest", 0);
    const json trail = task_of("Qg5_TrailQuest", 0);
    note("QG5E the fetch: " + fetch.value("supplier", std::string{}) + fetch.value("reason", std::string{}) + "; the far fetch: "
         + far.value("reason", std::string{}).substr(0, 60) + "; the trail: " + trail.value("supplier", std::string{}));
    check(fetch.value("supported", false) && fetch.value("supplier", std::string{}) == "the quest drop of the chests of qg5_ruins",
          "the item of a chest task has a source: an ordinary chest is in the point of interest of its tag");
    check(!far.value("supported", true) && far.value("reason", std::string{}).find("no supported source gives the item") == 0,
          "a chest task of a place with no chest has no source of its item");
    check(trail.value("supported", false) && trail.value("supplier", std::string{}).find("a proximity quest object") == 0,
          "the event of a proximity object has a producer (a proximity quest object)");
}

} // namespace

std::vector<Case> quest_object_gap_cases() {
    return {{"QG5A", "quests QG5a (I1, MP-10): the quest drop of a chest: the place, the count, no second award, B gets nothing", chest_drops},
            {"QG5B", "quests QG5a (I1): one transaction of the open of a chest and the quest award", chest_drop_transaction},
            {"QG5C", "quests QG5a (I2, MP-10): the proximity objects: the sphere, the task, the LootTracker of A only, the respawn", proximity_objects_case},
            {"QG5D", "quests QG5a: the loaders of the members proximity, children, and conditional", loaders},
            {"QG5E", "quests QG5a (I1, I2): the manifest: the chest source of an item, the producer of a proximity event", manifest}};
}

} // namespace soulwarden::backend::tests

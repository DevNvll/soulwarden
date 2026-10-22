#include <sqlite3.h>

#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/quest_person_fixture.h"
#include "support/test_navigation.h"
#include "world/quest_objects.h"
#include "world/quest_support.h"

// Stage 1.8f: the corrections of the code review of stage 1.8e (QRE1 to QRE8 for the findings QR8E-R1 to R8; R9 is the observer check of
// QF1) and the validity of a conversation state record (NC9: the offer 06A_MSQ_01_Quartermaster of Marcel Dupont). Each case fails on the
// tree of stage 1.8e (QRE8 there does not end). The data and the identities are made up.

namespace soulwarden::backend::tests {
namespace {

using namespace questperson;

/** One destructible object (a nodule of the work `work`, respawn 33 s, only on a quest) at a place. */
json object_document(const state::WorldPosition& at, float work = 4.0F) {
    return {{"format", 1},
            {"rows", json::array({{{"id", "Review_Node"}, {"time", work}, {"respawn", {33.0, 33.0}}, {"event", "Review_Destroyed"}}})},
            {"objects", json::array({{{"key", "22222222-0000-4000-8000-000000000001"},
                                      {"actor", "0b0000000000000a0000000000000000"},
                                      {"asset", "00112233445566778899aabbccddeeff"},
                                      {"slot", 4},
                                      {"row", 0},
                                      {"tag", "Review_Tag"},
                                      {"onlyOnQuest", true},
                                      {"position", {at.x, at.y, at.z}}}})}};
}

/** The quest of the object: a TaskGameEvent of 4 with the event and the tag of the object. */
void add_object_quest(json& quests) {
    quests["tasks"]["review_counter"] =
        questfix::task("Review_Counter", "taskgameevent", 14, {{"KillEnemyType", "Review_Destroyed"}, {"TargetQty", "4"}, {"InteractTag", "Review_Tag"}});
    quests["objectives"].push_back({{"id", "Review_Quest"},
                                    {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "review_counter", json::array()})})}});
}

std::uint64_t refused(const world::QuestObjects& objects, world::QuestObjectVerdict verdict) {
    const auto& counts = objects.counts().refused;
    const auto index = static_cast<std::size_t>(verdict);
    return index < std::size(counts) ? counts[index] : 0;
}

/** An object 2 m north of the place of the attack range, and A with its task. */
void arm_object(attack_fixture::Range& range, float work, const state::WorldPosition& at) {
    json quests = questfix::quest_document();
    add_object_quest(quests);
    check(range.host.quests.load_text(quests.dump()), "control: the quest data loads");
    check(range.host.questObjects.load_text(object_document(at, work).dump()), "control: one object loads");
    world::Entity* a = range.host.world.find_character(attack_fixture::kCharacterA);
    check(range.host.world.quests().grant(*a, "Review_Quest", "command", 0).ok(), "control: A has its active object task");
}

/** QRE1 (QR8E-R1): one partial heavy swing of the great axe with no learned node: one unit of work, not two. */
void usable_strikes(const Options&) {
    attack_fixture::Range range;
    const auto place = attack_fixture::kPlace;
    arm_object(range, 4.0F, {place.x, place.y + 2.0F, place.z});
    world::Entity* a = range.host.world.find_character(attack_fixture::kCharacterA);
    check(a->skills.pools.empty(), "control: A has no learned node: of the two strike entries of Attack_Heavy only the normal one is for A");
    attack_fixture::Attacker attacker{range.host, range.a, place};
    attacker.hold(12);
    attacker.idle(50);
    const world::QuestObjectCounts& counts = range.host.world.quest_objects().counts();
    const unsigned hits = a->questObjects.empty() ? 0 : a->questObjects.begin()->second.hits;
    note("QRE1 one partial heavy swing: visits " + attack_fixture::joined(range.visits()) + ", work units " + std::to_string(hits)
         + ", strike events " + std::to_string(counts.strikes));
    check(hits == 1 && counts.hits == 1 && counts.strikes == 1,
          "one swing gives one unit of work: the strike entry with no usable row for A (it needs the heavy pull node) is no strike");
}

/** QRE2 (QR8E-R2): a solid wall between the character and the object stops the strike; a wall behind the object does not. */
void wall_strikes(const Options& options) {
    const auto place = attack_fixture::kPlace;
    const auto run = [&](float wallFrom, float wallWidth, const char* folderName, bool directFirst) {
        attack_fixture::Range range;
        arm_object(range, 1.0F, {place.x, place.y + 2.0F, place.z});
        TestNavRegion region;
        region.ground = place.z;
        region.tiles[{15, 15}] = [place, wallFrom, wallWidth](int, int cy, std::vector<TestFloor>& floors) {
            const float y = 15.0F * 64.0F + static_cast<float>(cy) * 0.25F;
            if (y >= place.y + wallFrom && y < place.y + wallFrom + wallWidth) {
                floors = {{place.z + 3.0F, gamedata::NavFloor::kBlocked}};
            }
        };
        const auto folder = options.workDirectory / folderName;
        region.write(folder);
        check(range.host.navigation.set_directory(folder.string()), "control: the wall data loads");
        world::Entity* a = range.host.world.find_character(attack_fixture::kCharacterA);
        world::QuestObjects& objects = range.host.world.quest_objects();
        if (directFirst) {
            a->look = 0.0F;
            objects.strike(*a, 900, 0);
        }
        attack_fixture::Attacker attacker{range.host, range.a, place};
        attacker.idle(20);
        attacker.click();
        attacker.idle(45);
        const bool destroyed = !a->questObjectTracker.empty();
        return std::make_tuple(destroyed, questfix::quest_of(*a, "Review_Quest")->tasks[0].progress, refused(objects, world::QuestObjectVerdict::wall),
                               refused(objects, world::QuestObjectVerdict::wallDataPending), a->questObjects.empty() ? 0U : a->questObjects.begin()->second.hits);
    };
    const auto [throughDestroyed, throughProgress, throughWall, pending, throughHits] = run(1.0F, 0.5F, "review-18e-wall-front", true);
    note("QRE2 wall in front: destroyed " + std::to_string(throughDestroyed) + ", progress " + std::to_string(throughProgress) + ", wall refusals "
         + std::to_string(throughWall) + ", data pending " + std::to_string(pending));
    check(pending == 1, "a strike before the tiles of the line are there: no hit (the data of the line is pending)");
    check(!throughDestroyed && throughProgress == 0 && throughHits == 0 && throughWall >= 1,
          "a swing through a solid wall does not destroy the object and does not count its event");
    const auto [behindDestroyed, behindProgress, behindWall, behindPending, behindHits] = run(3.0F, 0.5F, "review-18e-wall-behind", false);
    (void)behindPending;
    (void)behindHits;
    note("QRE2 wall behind the object: destroyed " + std::to_string(behindDestroyed) + ", progress " + std::to_string(behindProgress));
    check(behindDestroyed && behindProgress == 1 && behindWall == 0, "control: a wall behind the object: the swing destroys it and counts its event");
    // The solid of the object itself (the data has the body of a nodule: a solid of 1 m around its place) is not a wall: the line ends at its
    // near side.
    const auto [bodyDestroyed, bodyProgress, bodyWall, bodyPending, bodyHits] = run(1.5F, 1.0F, "review-18e-wall-body", false);
    (void)bodyPending;
    (void)bodyHits;
    note("QRE2 a solid around the object: destroyed " + std::to_string(bodyDestroyed) + ", progress " + std::to_string(bodyProgress));
    check(bodyDestroyed && bodyProgress == 1 && bodyWall == 0, "the solid of the object itself is not a wall: the swing destroys it");
}

/** QRE3 (QR8E-R3): late attack steps while a granted cutscene plays strike no object; after its end a strike does. */
void cutscene_strikes(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    add_object_quest(quests);
    check(shop.host.quests.load_text(quests.dump()), "control: the cutscene and object quests load");
    check(shop.host.combat.set(attack_fixture::test_combat()) && shop.host.weaponActions.set(attack_fixture::test_actions()),
          "control: the attack graph loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    a->equipment.slots[world::paperdoll::kMainHand1].item = attack_fixture::test_item("TestAxe");
    check(shop.host.world.quests().grant(*a, kStart, "command", 0).ok(), "control: the cutscene quest starts");
    shop.walk_to(kCharacterA, kTriggerPlace);
    const state::WorldPosition at = a->position;
    a->look = 0.0F;
    check(shop.host.questObjects.load_text(object_document({at.x, at.y + 2.0F, at.z}, 1.0F).dump()), "control: the one-strike object loads");
    check(shop.host.world.quests().grant(*a, "Review_Quest", "command", 0).ok(), "control: the object task is active");
    check(shop.host.world.quests().cutscene_requested(*a, qcrc(kCutscene)) && shop.host.world.quests().cutscene_started(*a, qcrc(kCutscene)),
          "control: the server granted the cutscene and accepted its start");
    attack_fixture::Attacker attacker{shop.host, shop.clientA, at};
    attacker.input = static_cast<std::uint16_t>(shop.sequenceA - 1);
    attacker.others = {&shop.clientB};
    attacker.click();
    attacker.idle(45);
    world::QuestObjects& objects = shop.host.world.quest_objects();
    note("QRE3 started cutscene: still playing " + std::to_string(a->quests.cutsceneStarted) + ", destroyed " + std::to_string(objects.counts().destroyed)
         + ", cutscene refusals " + std::to_string(refused(objects, world::QuestObjectVerdict::cutscene)));
    check(a->quests.cutsceneStarted && a->questObjectTracker.empty() && questfix::quest_of(*a, "Review_Quest")->tasks[0].progress == 0
              && refused(objects, world::QuestObjectVerdict::cutscene) >= 1,
          "late attack steps during a started cutscene strike no quest object");
    check(shop.host.world.quests().cutscene_ended(*a) && !a->quests.cutsceneStarted, "control: the cutscene ends");
    objects.strike(*a, 950, 0);
    check(a->questObjectTracker.size() == 1 && questfix::quest_of(*a, "Review_Quest")->tasks[0].progress == 1,
          "control: after the end of the cutscene a strike destroys the object");
}

bool sql(const std::string& path, const char* command) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, command, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

/** QRE4 (QR8E-R4): a refused write of the quest rows keeps the object (no Looted entry, no respawn); the next strike after the fault works. */
void refused_write(const Options& options) {
    const auto path = options.workDirectory / "review-18e-store.sqlite";
    std::filesystem::remove(path);
    QuestShop shop(0, path.string());
    json quests = quest_document_18b();
    add_object_quest(quests);
    check(shop.host.quests.load_text(quests.dump()), "control: the object task loads");
    shop.enter();
    shop.walk_to(kCharacterA, {101.0F, 200.0F, 30.0F});
    world::Entity* a = shop.entity(kCharacterA);
    const state::WorldPosition at = a->position;
    a->look = 0.0F;
    check(shop.host.questObjects.load_text(object_document({at.x, at.y + 2.0F, at.z}, 1.0F).dump()), "control: the object loads");
    check(shop.host.world.quests().grant(*a, "Review_Quest", "command", 0).ok(), "control: the active task is saved");
    check(sql(path.string(), "CREATE TRIGGER review_fail BEFORE INSERT ON character_quest_tasks BEGIN SELECT RAISE(FAIL,'review'); END"),
          "control: the writes of the quest task rows fail now");
    world::QuestObjects& objects = shop.host.world.quest_objects();
    objects.strike(*a, 1, 0);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const auto progress = questfix::quest_of(*a, "Review_Quest")->tasks[0].progress;
    const bool kept = !a->questObjects.empty() && !a->questObjects.begin()->second.destroyed && a->questObjects.begin()->second.hits == 0;
    note("QRE4 refused quest write: progress " + std::to_string(progress) + ", destroyed " + std::to_string(objects.counts().destroyed)
         + ", tracker entries " + std::to_string(a->questObjectTracker.size()) + ", store failures " + std::to_string(objects.counts().storeFailures));
    check(progress == 0 && kept && a->questObjectTracker.empty() && objects.counts().destroyed == 0 && objects.counts().storeFailures == 1,
          "a refused write of the quest rows: the object stays with its last unit of work, no Looted entry, no progress");
    check(sql(path.string(), "DROP TRIGGER review_fail"), "control: the store takes the writes again");
    objects.strike(*a, 2, 0);
    check(a->questObjectTracker.size() == 1 && questfix::quest_of(*a, "Review_Quest")->tasks[0].progress == 1 && objects.counts().destroyed == 1,
          "control: the next strike destroys the object and counts its event in one change");
}

/** A quest document for the manifest: the territory start of Windsward (6) is `start`, and the Soldier archetype row. */
json manifest_document(const char* start) {
    json document = {{"format", 1}, {"tasks", json::object()}, {"objectives", json::array()}, {"achievements", json::array({json::array({"Review_Bit", 950})})},
                     {"territories", json::array({{{"territory", 6}, {"objective", start}, {"table", "Review_Territory"}}})},
                     {"backstories", json::array({{{"id", "Archetype_Soldier"}, {"archetype", true}, {"objectives", json::array()}, {"achievements", json::array()}}})}};
    document["tasks"]["review_have"] = questfix::task("Review_Have", "taskhaveitems", 25, {{"ItemName", "QbRelic"}});
    document["tasks"]["review_give"] = questfix::task("Review_Give", "taskgiveitem", 33, {{"ItemName", "QbRelic"}, {"TargetQty", "1"}});
    document["tasks"]["review_unlock"] = questfix::task("Review_Unlock", "taskunlockachievement", 20, {{"ItemName", "Review_Bit"}});
    document["tasks"]["review_root"] = questfix::task("Review_Root", "consecutivetaskcontainer", 1);
    return document;
}

void add_objective(json& document, const char* id, json cells, json tree) {
    cells["Type"] = "Journey";
    if (!cells.contains("NpcDestinationId")) {
        cells["NpcDestinationId"] = "Test_Artificer";
    }
    document["objectives"].push_back({{"id", id}, {"cells", cells}, {"tree", tree}});
}

/** @return The played objectives of the first profile (the new Soldier) and its count. */
std::vector<std::string> played_of(QuestShop& shop, const json& quests) {
    check(shop.host.quests.load_text(quests.dump()), "control: the quest data of the manifest loads");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest(support.default_profiles());
    std::vector<std::string> played;
    for (const json& id : manifest["profiles"][0]["played"]) {
        played.push_back(id.get<std::string>());
    }
    return played;
}

/** QRE5 (QR8E-R5): the history has an item of a quest only from a played reward or from a give in the tree that runs. */
void quest_items(const Options&) {
    QuestShop shop;
    const json one = json::array({json::array({0, "review_have", json::array()})});
    // A quest whose only source of its item is its own reward.
    json circular = manifest_document("Review_Source");
    add_objective(circular, "Review_Source", {{"ItemRewardName", "QbRelic"}, {"ItemRewardQty", "1"}}, one);
    const std::vector<std::string> circularPlayed = played_of(shop, circular);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const auto granted = shop.host.world.quests().grant(*a, "Review_Source", "command", 0);
    const world::QuestInstance* active = questfix::quest_of(*a, "Review_Source");
    check(granted.ok() && active != nullptr && !shop.host.world.quests().ready(*active) && shop.count(kCharacterA, "QbRelic") == 0,
          "control: the real quest has no relic and cannot complete before its own hand-in");
    note("QRE5 circular source: played " + std::to_string(circularPlayed.size()));
    check(circularPlayed.empty(), "the history does not take the item of a task from the reward of the same unfinished quest");
    // The reward of a played quest, then the quest that needs it.
    json chain = manifest_document("Review_Giver");
    add_objective(chain, "Review_Giver", {{"ItemRewardName", "QbRelic"}, {"ItemRewardQty", "1"}, {"ObjectiveReward", "Review_Taker"}},
                  json::array({json::array({0, "review_unlock", json::array()})}));
    add_objective(chain, "Review_Taker", json::object(), one);
    const std::vector<std::string> chainPlayed = played_of(shop, chain);
    check(chainPlayed == std::vector<std::string>{"Review_Giver", "Review_Taker"}, "control: the reward of a played quest is an item of the history");
    // A give of the tree before the possession task.
    json give = manifest_document("Review_Give");
    add_objective(give, "Review_Give", json::object(),
                  json::array({json::array({0, "review_give", json::array()}), json::array({1, "review_have", json::array()}),
                               json::array({2, "review_root", json::array({0, 1})})}));
    check(played_of(shop, give) == std::vector<std::string>{"Review_Give"}, "control: a give of the tree that runs is an item of the history");
    // The loot of a table that no source of the world reaches is no source; the loot of the root of a chest is one.
    gamedata::LootData loot = chests::chest_loot();
    loot.tables.push_back(chests::loot_table("Review_Unreached", false, 100, {}, {chests::loot_entry(gamedata::LootEntryKind::item, "QbPants", 0)}));
    check(shop.host.loot.set(loot) && chests::set_chest_data(shop.host.chests), "control: the loot tables and the chests load");
    const auto loot_quest = [&](const char* item) {
        json quests = manifest_document("Review_Loot");
        quests["tasks"]["review_have"]["cells"]["ItemName"] = item;
        add_objective(quests, "Review_Loot", json::object(), one);
        return played_of(shop, quests);
    };
    const std::vector<std::string> unreached = loot_quest("QbPants");
    note("QRE5 an item of a table that no source reaches: played " + std::to_string(unreached.size()));
    check(unreached.empty(), "an item only in a loot table that no source of the world reaches: no source");
    check(loot_quest("TestHide") == std::vector<std::string>{"Review_Loot"}, "control: an item of a table that the root of a chest reaches: a source");
}

/** QRE6 (QR8E-R6): the history hands in a quest only at its destination person, with the bits after its tasks. */
void hand_in_person(const Options&) {
    QuestShop shop;
    const json ready = json::array({json::array({0, "qb_ready_unlock", json::array()})});
    const auto played = [&](const char* destination, const json& tree) {
        json quests = quest_document_18b();
        quests["objectives"].push_back({{"id", "Review_HandIn"}, {"cells", {{"Type", "Journey"}, {"NpcDestinationId", destination}}}, {"tree", tree}});
        quests["territories"] = json::array({{{"territory", 6}, {"objective", "Review_HandIn"}, {"table", "Review_Territory"}}});
        quests["backstories"] =
            json::array({{{"id", "Archetype_Soldier"}, {"archetype", true}, {"objectives", json::array()}, {"achievements", json::array()}}});
        check(shop.host.quests.load_text(quests.dump()), "control: the quest with its destination loads");
        world::QuestSupport support(shop.host.world);
        const json manifest = support.manifest(support.default_profiles());
        bool found = false;
        for (const json& id : manifest["profiles"][0]["played"]) {
            found = found || id == "Review_HandIn";
        }
        return found;
    };
    const bool absent = played("Review_AbsentPerson", ready);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Review_HandIn", "command", 0).ok() && shop.host.world.quests().ready(*questfix::quest_of(*a, "Review_HandIn")),
          "control: the real quest is ready");
    shop.walk_to(kCharacterA, kNearArtificer);
    shop.request(shop.clientA, shop.talk(kArtificer));
    const world::QuestRequest result = shop.host.world.persons().hand_in(*a, qcrc("Review_HandIn"));
    check(result.result == world::QuestRequestResult::wrongPerson, "control: the real hand-in needs the named destination");
    note("QRE6 absent hand-in person: played " + std::to_string(absent));
    check(!absent, "the history does not hand in a quest whose destination has no person slot");
    // The artificer is gone while the bit Test_Swap_Artificer is set (its presence rule): a tree that sets it cannot hand in there.
    json hides = json::array({json::array({0, "review_swap", json::array()})});
    json quests = quest_document_18b();
    quests["tasks"]["review_swap"] = questfix::task("Review_Swap", "taskunlockachievement", 20, {{"ItemName", "Test_Swap_Artificer"}});
    quests["objectives"].push_back({{"id", "Review_HandIn"}, {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}}, {"tree", hides}});
    quests["territories"] = json::array({{{"territory", 6}, {"objective", "Review_HandIn"}, {"table", "Review_Territory"}}});
    quests["backstories"] = json::array({{{"id", "Archetype_Soldier"}, {"archetype", true}, {"objectives", json::array()}, {"achievements", json::array()}}});
    check(shop.host.quests.load_text(quests.dump()), "control: the quest that hides its destination loads");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest(support.default_profiles());
    check(manifest["profiles"][0]["played"].empty(), "the destination that the tasks of the quest hide (its presence rule): no hand-in");
    check(played("Test_Artificer", ready), "control: a quest whose destination person is there: the history hands it in");
}

/** QRE7 (QR8E-R7): two objects with one LootTracker actor, or with one placement key: the file is refused. */
void duplicate_objects(const Options&) {
    QuestShop shop;
    json objects = object_document({100.0F, 202.0F, 30.0F}, 1.0F);
    json second = objects["objects"][0];
    second["key"] = "22222222-0000-4000-8000-000000000002";
    objects["objects"].push_back(second);
    const bool actor = shop.host.questObjects.load_text(objects.dump());
    const std::string actorError = shop.host.questObjects.error();
    objects["objects"][1]["actor"] = "1b0000000000000a0000000000000000";
    objects["objects"][1]["key"] = objects["objects"][0]["key"];
    const bool key = shop.host.questObjects.load_text(objects.dump());
    note("QRE7 duplicate actor: loaded " + std::to_string(actor) + " (" + actorError + "); duplicate key: loaded " + std::to_string(key));
    check(!actor && actorError.find("actor") != std::string::npos, "two placement keys with one actor: the file is refused");
    check(!key, "two objects with one placement key: the file is refused");
    objects["objects"][1]["key"] = "22222222-0000-4000-8000-000000000002";
    check(shop.host.questObjects.load_text(objects.dump()) && shop.host.questObjects.objects().size() == 2,
          "control: two objects with their own keys and actors load");
}

/** QRE8 (QR8E-R8): the manifest ends for a tree with the largest task index (255). */
void largest_index(const Options&) {
    ServerHost host;
    json quests = {{"format", 1}, {"tasks", json::object()}, {"objectives", json::array()}, {"achievements", json::array()}, {"territories", json::array()},
                   {"backstories", json::array()}};
    json tree = json::array();
    json children = json::array();
    for (unsigned index = 0; index < 255; ++index) {
        const std::string name = "Review_Leaf_" + std::to_string(index);
        const std::string key = gamedata::lower_text(name);
        quests["tasks"][key] = questfix::task(name.c_str(), "tasktimer", 64, {{"Duration", "1"}});
        tree.push_back(json::array({index, key, json::array()}));
        children.push_back(index);
    }
    quests["tasks"]["review_root"] = questfix::task("Review_Root", "simpletaskcontainer", 0);
    tree.push_back(json::array({255, "review_root", children}));
    quests["objectives"].push_back({{"id", "Review_Max_Quest"}, {"cells", {{"Type", "Journey"}}}, {"tree", tree}});
    const bool loaded = host.quests.load_text(quests.dump());
    check(loaded && host.quests.objectives()[0].nodes.size() == 256, "control: the task index 255 loads as a tree of 256 nodes");
    if (!loaded) {
        return;
    }
    world::QuestSupport support(host.world);
    const json manifest = support.manifest({});
    check(manifest["objectives"].size() == 1 && manifest["objectives"][0]["tasks"].size() == 256,
          "the manifest ends for the largest task index and has each of the 256 tasks");
}

/** NC9 (stage 1.8f, MP-10): the validity of a state record (IsInitialState XOR a requirement): the offer of an NPC of the Marcel form. */
void state_validity(const Options&) {
    QuestShop shop;
    json conversations = conversation_document_18b();
    // The form of 06_MarcelDupont01: the initial state, the offer, a task state, a state with no requirement, a later state.
    conversations["npcs"][0]["states"] = json::array({"Art_Default", "Art_Offer", "Art_TaskTopic", "Art_Empty", "Art_Later"});
    conversations["states"].push_back({{"id", "Art_Empty"}, {"type", "Linear_Objective"}});
    conversations["states"].push_back({{"id", "Art_Later"}, {"type", "Linear_Objective"}, {"requires", {{"RequiredAchievement", "Qb_Next_Done"}}}});
    // An NPC with an initial state that has a requirement, and the form of 06_PriyaThakur (a task state before a state with no requirement).
    conversations["npcs"].push_back({{"id", "Test_InitialRule"}, {"states", json::array({"Init_Rule"})}});
    conversations["states"].push_back({{"id", "Init_Rule"}, {"type", "Linear_Objective"}, {"initial", true}, {"requires", {{"RequiredLevel", "1"}}}});
    conversations["npcs"].push_back({{"id", "Test_Priya"}, {"states", json::array({"Art_Default", "Art_TaskTopic", "Art_Empty"})}});
    check(shop.host.conversations.load_text(conversations.dump()), "control: the conversation data of the Marcel form loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.ownership().unlock_achievement(*a, "Qb_Start_Done", *shop.host.world.ownership().achievement_bit("Qb_Start_Done")),
          "control: A has the bit of the offer state (the form of S_introtosettlement); B has none");
    const gamedata::ConversationData& data = shop.host.conversations;
    const auto state_of = [&](world::Entity& character, const char* npc) {
        const world::PersonSelection selection = shop.persons().select(character, binding(npc));
        return selection.state >= 0 ? data.state(static_cast<std::uint32_t>(selection.state)).id : std::string("none");
    };
    note("NC9 states: A " + state_of(*a, "Test_Artificer") + ", B " + state_of(*b, "Test_Artificer") + ", the initial state with a rule "
         + state_of(*a, "Test_InitialRule") + ", the Priya form " + state_of(*a, "Test_Priya"));
    check(state_of(*a, "Test_Artificer") == "Art_Offer", "A: the reverse scan skips the state with no requirement (not initial) and selects the offer");
    check(state_of(*b, "Test_Artificer") == "Art_Default", "B (no bit): the initial state, not the state with no requirement");
    check(state_of(*a, "Test_InitialRule") == "none", "an initial state with a requirement is an invalid record: no state");
    check(state_of(*a, "Test_Priya") == "Art_Default", "the Priya form with no active task: the initial state");
    // The offer at the person of each session (3772): A accepts, B is refused.
    shop.walk_to(kCharacterA, kNearArtificer);
    shop.walk_to(kCharacterB, kNearArtificer);
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.request(shop.clientB, shop.talk(kArtificer));
    shop.accept(kCharacterA, "Qb_Paid");
    shop.accept(kCharacterB, "Qb_Paid");
    check(questfix::quest_of(*a, "Qb_Paid") != nullptr && questfix::quest_of(*b, "Qb_Paid") == nullptr,
          "the offer of the selected state: A accepts it; B (its initial state has no offer) is refused, no change for B");
    check(shop.host.world.ownership().unlock_achievement(*a, "Qb_Next_Done", *shop.host.world.ownership().achievement_bit("Qb_Next_Done"))
              && state_of(*a, "Test_Artificer") == "Art_Later",
          "control: with the later bit the later state passes first");
}

} // namespace

std::vector<Case> review_quests_18e_cases() {
    return {{"QRE1", "review 1.8e (1.8f, R1): one strike of a swing for each usable strike entry", usable_strikes},
            {"QRE2", "review 1.8e (1.8f, R2): a wall between the character and the object; the data of the line", wall_strikes},
            {"QRE3", "review 1.8e (1.8f, R3, MP-10): late attack steps during a started cutscene", cutscene_strikes},
            {"QRE4", "review 1.8e (1.8f, R4): a refused write of the quest rows keeps the object", refused_write},
            {"QRE5", "review 1.8e (1.8f, R5): the items of a quest in the history of the manifest", quest_items},
            {"QRE6", "review 1.8e (1.8f, R6): the hand-in person in the history of the manifest", hand_in_person},
            {"QRE7", "review 1.8e (1.8f, R7): two objects with one actor or one placement key", duplicate_objects},
            {"QRE8", "review 1.8e (1.8f, R8): the manifest ends for the task index 255", largest_index},
            {"NC9", "persons (1.8f, MP-10): the validity of a state record (the offer of 06A_MSQ_01_Quartermaster)", state_validity}};
}

} // namespace soulwarden::backend::tests

// The corrections of the code review of stage QG5c3 (evidence/quests/qg5c3/code-review/FINDINGS.md), stage QG5c4. Each case fails on the
// tree of stage QG5c3 (trees/after-qg5c3; QGR5C3A with AddressSanitizer) and passes with the correction. Made-up recipes, rows, quests, and
// accounts (the encounter fixtures of tests/support/encounter_fixture.h and encounter_pattern_fixture.h).
//
//   QGR5C3A  QG5C3R-R1: the first and last stage is a spawned objective that its spawn meets at once: the encounter succeeds in its
//            activation, and the activation does not read the instance after its end (a use after free that AddressSanitizer finds)
//   QGR5C3B  QG5C3R-R2: two finders of one proximity object; the store refuses the quest write of A only: B has its event at once, the
//            object stays, and A has its event at a later look after the fault ends; the object goes and counts one time
//   QGR5C3C  QG5C3R-R3: a destroyed parent (1, no time limit) whose only matching spawn is in a timed child (1 s) at the delay 2 s is
//            refused (the end of the child cancels the spawn)
//   QGR5C3D  QG5C3R-R4: two producers of one event (a key that a quest gives and no rank; no key and the rank 5 of AzothStaff): a history
//            with the key and the rank 0 takes the task with the first one

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/encounter_fixture.h"
#include "support/encounter_pattern_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using namespace encfix;
using questfix::json;

/** QGR5C3A (QG5C3R-R1): a spawned objective that ends the encounter in its activation. */
void spawned_terminal(const Options&) {
    json recipe = summon_recipe();
    recipe["stageOrder"] = {2};
    recipe["stages"][2]["objective"]["type"] = "SliceSpawnedObjective";
    recipe["stages"][2]["spawns"][0]["delay"] = 0;
    PatternShop shop({recipe});
    // The characters get their quests far from the placement, then come near: the first scan near them makes the instance, and its
    // activation ends it.
    for (const char* character : {kCharacterA, kCharacterB}) {
        check(shop.host.store.set_character_position(character, {100.0F, 600.0F, 30.0F}), "control: the character is 400 m away");
    }
    shop.placeA = {100.0F, 600.0F, 30.0F};
    shop.placeB = {101.0F, 600.0F, 30.0F};
    begin(shop);
    check(shop.instance() == nullptr && shop.encounters().counts().made == 0, "control: no instance with no character near");
    (void)shop.host.world.travel(kCharacterA, {100.0F, 210.0F, 30.0F}, world::TravelScreen::off);
    (void)shop.host.world.travel(kCharacterB, {101.0F, 210.0F, 30.0F}, world::TravelScreen::off);
    shop.placeA = {100.0F, 210.0F, 30.0F};
    shop.placeB = {101.0F, 210.0F, 30.0F};
    shop.wait(0.6F);
    check(shop.encounters().counts().succeeded == 1 && shop.instance() == nullptr,
          "the spawn of the boss meets the only stage at once: the encounter succeeds in its activation, and the instance ends");
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.progress(kCharacterB, "Test_Defend") == 1,
          "the participants of the activation (A and B near the root) get the success event one time each");
    check(shop.entity(kCharacterA)->encounterLinks.empty() && shop.entity(kCharacterB)->encounterLinks.empty(),
          "no link to the root that ended stays on a character");
    shop.wait(16.0F);
    check(shop.encounters().counts().succeeded >= 2 && shop.progress(kCharacterA, "Test_Defend") == 1,
          "the next instance after the respawn of the parent runs the same way (no second credit of the completed quest)");
}

/** QGR5C3B (QG5C3R-R2): a store failure of one finder keeps the object until that finder has its write. */
void partial_finders(const Options& options) {
    const auto path = options.workDirectory / "qgr5c3-partial-finders.sqlite3";
    std::filesystem::remove(path);
    PatternShop shop({ambush_recipe()}, pattern_members(), path.string());
    begin(shop);
    shop.grant(kCharacterA, "Test_Ambush");
    shop.grant(kCharacterB, "Test_Ambush");
    const auto sql = [&](const std::string& text) {
        sqlite3* db = nullptr;
        const bool opened = sqlite3_open(path.string().c_str(), &db) == SQLITE_OK;
        const bool ok = opened && sqlite3_exec(db, text.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(db);
        return ok;
    };
    // A and B wait in the detector, out of the sphere of the object, for the end of the setup (3 s); then the store refuses the writes of A,
    // and both come into the sphere.
    shop.move(kCharacterA, {100.0F, 212.0F, 30.0F});
    shop.move(kCharacterB, {101.0F, 212.0F, 30.0F});
    shop.wait(3.0F);
    check(shop.members(true).size() == 1 && shop.encounters().counts().objectsFound == 0, "control: the proximity object waits");
    check(sql(std::string("CREATE TRIGGER qgr5c3_refuse_a BEFORE INSERT ON character_quest_tasks WHEN NEW.character_id='") + kCharacterA
              + "' BEGIN SELECT RAISE(FAIL, 'test fault'); END"),
          "control: the store refuses the quest task writes of A only");
    shop.move(kCharacterA, {100.0F, 201.0F, 30.0F});
    shop.move(kCharacterB, {101.0F, 201.0F, 30.0F});
    check(shop.progress(kCharacterB, "Test_Ambush") == 1 && shop.progress(kCharacterA, "Test_Ambush") == 0,
          "B has its event; the write of A was refused");
    check(shop.members(true).size() == 1 && shop.encounters().counts().objectsFound == 0,
          "the object stays while the write of A waits (no count of its stage yet)");
    check(sql("DROP TRIGGER qgr5c3_refuse_a"), "control: the fault of the store ends");
    shop.move(kCharacterA, {100.0F, 260.0F, 30.0F}); // A leaves the sphere: it found the object, and its write is tried again.
    shop.wait(1.0F);
    check(shop.progress(kCharacterA, "Test_Ambush") == 1 && shop.progress(kCharacterB, "Test_Ambush") == 1,
          "A has its event at a later look (out of the sphere), and B has its event one time");
    check(shop.members(true).empty() && shop.encounters().counts().objectsFound == 1,
          "the object goes when each finder has its write, and its stage counts it one time");
}

/** QGR5C3C (QG5C3R-R3): a child spawn after the time of its child stage gives no count. */
void late_child_spawn(const Options&) {
    json recipe = ambush_recipe();
    recipe["stageOrder"] = {2, 3};
    recipe["stages"][2]["children"] = {4};
    recipe["stages"][2]["objective"]["requiredCount"] = 1;
    recipe["stages"][4]["objective"] = timed(1);
    recipe["stages"][4]["spawns"] = json::array({spawn_json(kMemberSlice, 2, 1, kMemberAt)});
    json early = recipe;
    early["key"] = "e4c00000-0000-4000-8000-000000000041/17";
    early["position"] = {400.0, 205.0, 30.0};
    early["stages"][4]["spawns"][0]["delay"] = 0.5;
    PatternShop shop({recipe, early});
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    check(placements.size() == 2 && !placements[0].refused.empty()
              && placements[0].refused[0].find("needs 1 destroyed spawns, and the members that the runner makes can give only 0")
                     != std::string::npos,
          "the parent whose only matching spawn is due at 2 s in a child of 1 s is refused (" +
              (placements.size() == 2 && !placements[0].refused.empty() ? placements[0].refused[0] : std::string()) + ")");
    check(placements.size() == 2 && placements[1].refused.empty(), "control: the same spawn at 0.5 s is taken");
}

/** QGR5C3D (QG5C3R-R4): a history with the key of the unranked producer takes the task (not only the free rank 5 producer). */
void alternative_history(const Options&) {
    GatherShop shop;
    json data = gathering_document();
    for (const auto index : {kOreRow, kStoneRow}) {
        json& row = data["rows"][index];
        row["quest"] = true;
        row["type"] = "None";
        row["tradeskill"] = index == kStoneRow ? "AzothStaff" : "";
        row["level"] = index == kStoneRow ? 5 : 0;
        row["time"] = 1.0;
        row["loot"] = "Empty";
        row["event"] = "Review_Alternative_Event";
        row["restriction"] = index == kOreRow ? "ItemRequired" : "NoToolNecessary";
        row["item"] = index == kOreRow ? "Review_Alternative_Key" : "";
        row["itemCount"] = index == kOreRow ? 1 : 0;
    }
    check(shop.host.gathering.set(data.dump(), {{kRegion, region_records()}}), "control: the two producers of the event load");
    auto catalog = gathering_items();
    catalog.push_back(chests::chest_item("Review_Alternative_Key", "Resource", "Resource", 1, 1));
    shop.host.catalog.set(catalog);
    json quests = encounter_quests();
    quests["tasks"]["review_alt_task"] = questfix::task("Review_Alt_Task", "taskgameevent", gamedata::task_type::kGameEvent,
                                                        {{"KillEnemyType", "Review_Alternative_Event"}, {"TargetQty", "1"}});
    quests["tasks"]["review_key_task"] = questfix::task("Review_Key_Task", "taskgiveitem", 33,
                                                        {{"ItemName", "Review_Alternative_Key"}, {"TargetQty", "1"}});
    for (const auto& [id, task] : {std::pair{"Review_Alt", "review_alt_task"}, std::pair{"Review_Key_Source", "review_key_task"}}) {
        quests["objectives"].push_back({{"id", id},
                                        {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                        {"tree", json::array({json::array({0, task, json::array()})})}});
    }
    check(shop.host.quests.load_text(quests.dump()), "control: the event task and a quest source of the key load");
    world::SupportProfile keyed;
    keyed.id = "review:key";
    keyed.group = "review";
    keyed.starts = {"Review_Alt"};
    keyed.items = {"review_alternative_key"};
    keyed.level = 30;
    world::SupportProfile keyless = keyed;
    keyless.id = "review:nokey";
    keyless.items.clear();
    world::QuestSupport support(shop.host.world);
    // The primary history of a manifest is its first profile.
    const auto task_of = [&](const json& manifest, std::string& cause) {
        json fact;
        for (const json& row : manifest["objectives"]) {
            if (row["id"] == "Review_Alt") {
                fact = row["tasks"][0];
                cause = row["primary"].value("cause", std::string{});
            }
        }
        return fact;
    };
    std::string withKey;
    std::string withoutKey;
    const json fact = task_of(support.manifest({keyed}), withKey);
    (void)task_of(support.manifest({keyless}), withoutKey);
    check(fact.contains("alternatives") && fact["alternatives"].size() == 1
              && fact["alternatives"][0].value("questItem", std::string{}) == "review_alternative_key",
          "the task keeps the unranked producer with its key as an alternative (" + fact.dump() + ")");
    check(withKey.find("rank 0") == std::string::npos && withKey.find("waits") == std::string::npos,
          "a history with the key and the rank 0 takes the task with the unranked producer (" + withKey + ")");
    check(withoutKey.find("rank 5 of the trade skill AzothStaff") != std::string::npos,
          "control (finding QG5C2R-R6): a history with no key still needs the own rank 5 of the free producer (" + withoutKey + ")");
}

} // namespace

std::vector<Case> review_qg5c3_cases() {
    return {{"QGR5C3A", "review of QG5c3, R1: a spawned objective that ends the encounter in its activation (no read after the end)",
             spawned_terminal},
            {"QGR5C3B", "review of QG5c3, R2: a store failure of one finder of a proximity object keeps the object until its write",
             partial_finders},
            {"QGR5C3C", "review of QG5c3, R3: a child spawn due after the time of its child stage gives no count", late_child_spawn},
            {"QGR5C3D", "review of QG5c3, R4: a history takes an other producer of the event whose own gates pass", alternative_history}};
}

} // namespace soulwarden::backend::tests

// The corrections of the code review of stage QG5c2 (evidence/quests/qg5c2/code-review/FINDINGS.md), stage QG5c3. Each case fails on the
// tree of stage QG5c2 (trees/after-qg5c2) and passes with the correction. Made-up recipes, rows, quests, and accounts (the encounter
// fixture of tests/support/encounter_fixture.h).
//
//   QGR5C2A  QG5C2R-R1: the success gives no event to a participant whose grace ended before the last death (the scan had not run yet)
//   QGR5C2B  QG5C2R-R4: a quest plan stores the pins of the quests only: a pinned root of a linked encounter has no row of the store, also
//            after a quest grant, and none after the end of the encounter
//   QGR5C2C  QG5C2R-R3: the runner refuses a destroyed stage that needs more deaths than its members can give (no time limit ends it)
//   QGR5C2D  QG5C2R-R5: the manifest keeps the item gate of the gathering row of an encounter start object (the only producer of its event)
//   QGR5C2E  QG5C2R-R6: two producers of one event keep their own gates: a keyed rank 2 row and a free rank 5 row give "rank 5"
//   QGR5C2F  QG5C2R-R2: the encounter tool writes no file when a source of its reads was not there (check_encounters_tool.py, case complete)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using namespace encfix;
using questfix::json;

/** One kill stage of the recipe of the fixture: the two members at once (no setup, no start object, no cleanup). */
json single_fight_recipe() {
    json recipe = recipe_json(kKey, kRootAt, kStartAt, kMemberAt);
    recipe["stageOrder"] = {2};
    recipe["stages"][2]["spawns"][1]["delay"] = 0;
    return recipe;
}

/** QGR5C2A (QG5C2R-R1): the grace of B ended before the last death; the success snapshot has A only. */
void grace_at_success(const Options&) {
    EncounterShop shop({single_fight_recipe()});
    begin(shop);
    check(shop.instance() != nullptr && shop.instance()->participants.size() == 2, "control: A and B are participants before the fight");
    shop.move(kCharacterB, {100.0F, 255.0F, 30.0F});
    const world::EncounterInstance* run = shop.instance();
    if (run == nullptr || run->participants.count(kCharacterB) == 0 || !run->participants.at(kCharacterB).graceUntil) {
        check(false, "control: B is out of the sphere with a grace");
        return;
    }
    const auto deadline = *run->participants.at(kCharacterB).graceUntil;
    shop.wait(9.0F);
    const auto target = deadline + std::chrono::milliseconds(1);
    if (shop.host.clock.now() < target) {
        shop.host.clock.advance(target - shop.host.clock.now());
    }
    check(shop.host.clock.now() > deadline && run->participants.count(kCharacterB) == 1,
          "control: the grace of B ended, and no scan ran since (B is still in the map)");
    for (world::Entity* member : shop.members(false)) {
        check(shop.host.world.creatures().hurt(member->id, bags::kWolfHealth * 4.0F, shop.entity(kCharacterA)->id).error
                  == world::CreatureError::none,
              "control: A kills a member");
    }
    shop.encounters().tick();
    check(shop.progress(kCharacterA, "Test_Defend") == 1, "the participant A gets the success event");
    check(shop.progress(kCharacterB, "Test_Defend") == 0, "B, whose grace ended before the success, gets no success event");
}

/** QGR5C2B (QG5C2R-R4): the pins of the store are quest instances only. */
void pins_in_store(const Options&) {
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    begin(shop);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::EntityId root = shop.instance()->root;
    std::vector<std::uint64_t> pins = a.quests.tracked;
    pins.push_back(root);
    check(shop.host.world.quests().client_tracked(a, pins), "control: the client pins the root of the linked encounter");
    std::vector<std::uint64_t> stored = shop.host.store.list_quest_pins(kCharacterA);
    check(std::find(stored.begin(), stored.end(), root) == stored.end(), "control: the pin handler of the client stores no root");
    check(shop.host.world.quests().grant(a, "Test_Member_Hunt", "command", 0).ok(), "control: a quest grant changes the pinned list");
    stored = shop.host.store.list_quest_pins(kCharacterA);
    check(std::find(stored.begin(), stored.end(), root) == stored.end(), "the plan of the quest grant stores no pin of the encounter root");
    check(std::find(a.quests.tracked.begin(), a.quests.tracked.end(), root) != a.quests.tracked.end(),
          "the root stays in the pinned list of the entity (the client has it)");
    check(shop.encounters().reset(kKey), "control: the encounter ends");
    stored = shop.host.store.list_quest_pins(kCharacterA);
    check(std::find(stored.begin(), stored.end(), root) == stored.end() && a.encounterLinks.empty(),
          "after the end, no row of the store has the root");
}

/** QGR5C2C (QG5C2R-R3): a stage that needs three deaths and makes two members is refused before an instance. */
void impossible_count(const Options&) {
    json recipe = single_fight_recipe();
    recipe["stages"][2]["objective"]["requiredCount"] = 3;
    EncounterShop shop({recipe});
    begin(shop);
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    check(placements.size() == 1 && !placements[0].refused.empty()
              && placements[0].refused[0].find("needs 3 destroyed spawns, and the members that the runner makes can give only 2")
                     != std::string::npos,
          "the runner refuses the recipe with the cause of the count (" + (placements.empty() || placements[0].refused.empty()
                                                                              ? std::string("no cause")
                                                                              : placements[0].refused[0])
              + ")");
    check(shop.instance() == nullptr && shop.encounters().counts().made == 0, "no instance is made for the refused recipe");
    // A time limit with succeedOnTimeout ends the same stage: no cause.
    json timedEnd = single_fight_recipe();
    timedEnd["stages"][2]["objective"]["requiredCount"] = 3;
    timedEnd["stages"][2]["objective"]["timeLimit"] = 60;
    timedEnd["stages"][2]["objective"]["succeedOnTimeout"] = true;
    EncounterShop timedShop({timedEnd});
    begin(timedShop);
    check(!timedShop.encounters().placements().empty() && timedShop.encounters().placements()[0].refused.empty()
              && timedShop.instance() != nullptr,
          "control: with a time limit that ends the stage, the runner takes the recipe");
}

/** QGR5C2D (QG5C2R-R5): the item gate of the row of a start object stays in the manifest. */
void start_gate_manifest(const Options&) {
    EncounterShop shop({recipe_json(kKey, kRootAt, kStartAt, kMemberAt)});
    auto catalog = gathering_items();
    catalog.push_back(chests::chest_item("Review_Absent_Key", "Resource", "Resource", 1, 1));
    shop.host.catalog.set(catalog);
    json gathering = gathering_document();
    json row = row_json("Test_EncStart", "Gatherables", "None", "ItemRequired", "", 0, 1.0, 200.0, 300.0, "Empty", kStartEvent, true);
    row["quest"] = true;
    row["item"] = "Review_Absent_Key";
    row["itemCount"] = 1;
    gathering["rows"].push_back(row);
    json slice = template_json(kOreSlice, "encstart", "Gatherable_Quest");
    slice["slice"] = kStartSlice;
    gathering["templates"].push_back(slice);
    gathering["encounterObjects"] = {{kStartSlice, {gathering["templates"].size() - 1, gathering["rows"].size() - 1, "IO_Test_EncStart", false}}};
    check(shop.host.gathering.set(gathering.dump(), {{kRegion, region_records()}}), "control: the start row needs an item with no source");
    begin(shop);
    world::QuestSupport support(shop.host.world);
    json fact;
    const json manifest = support.manifest({});
    for (const json& entry : manifest["objectives"]) {
        if (entry["id"] == "Test_Examine") {
            fact = entry["tasks"][0];
        }
    }
    check(!fact.value("supported", true) && fact.value("reason", std::string{}).find("needs the item review_absent_key") != std::string::npos,
          "the event of the start object is not supported: its only producer needs the item (" + fact.value("reason", std::string{}) + ")");
    check(manifest["objectives"].dump().find("Test_Defend") != std::string::npos,
          "control: the manifest has the success task of the recipe");
}

/** QGR5C2E (QG5C2R-R6): the gates of two producers of one event do not join. */
void ranked_alternatives(const Options&) {
    GatherShop shop;
    json gathering = gathering_document();
    for (const auto index : {kOreRow, kStoneRow}) {
        json& row = gathering["rows"][index];
        row["quest"] = true;
        row["type"] = "None";
        row["tradeskill"] = "AzothStaff";
        row["level"] = index == kOreRow ? 2 : 5;
        row["time"] = 1.0;
        row["loot"] = "Empty";
        row["event"] = "Review_Ranked_Event";
        row["restriction"] = index == kOreRow ? "ItemRequired" : "NoToolNecessary";
        row["item"] = index == kOreRow ? "Review_Absent_Key" : "";
        row["itemCount"] = index == kOreRow ? 1 : 0;
    }
    check(shop.host.gathering.set(gathering.dump(), {{kRegion, region_records()}}), "control: a keyed rank 2 row and a free rank 5 row load");
    json quests = questfix::quest_document();
    quests["tasks"]["review_ranked_task"] = questfix::task("Review_Ranked_Task", "taskgameevent", 14, {{"KillEnemyType", "Review_Ranked_Event"}});
    quests["objectives"].push_back({{"id", "Review_Ranked"},
                                    {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "review_ranked_task", json::array()})})}});
    check(shop.host.quests.load_text(quests.dump()), "control: the task of the event loads");
    world::QuestSupport support(shop.host.world);
    json fact;
    const json manifest = support.manifest({});
    for (const json& entry : manifest["objectives"]) {
        if (entry["id"] == "Review_Ranked") {
            fact = entry["tasks"][0];
        }
    }
    check(fact.value("supplier", std::string{}).find("rank 5") != std::string::npos
              && fact.value("supplier", std::string{}).find("rank 2") == std::string::npos,
          "the task takes the free producer with its own rank 5, not the rank 2 of the keyed row (" + fact.value("supplier", std::string{})
              + ")");
}

/** QGR5C2F (QG5C2R-R2): the case complete of the check of the encounter tool. */
void tool_complete(const Options& options) {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const std::filesystem::path log = options.workDirectory / "check_encounters_tool.complete.log";
    const std::string command = "python3 -B " + (root / "tools/gamedata/check_encounters_tool.py").string() + " "
                                + options.workDirectory.string() + " complete > " + log.string() + " 2>&1";
    check(std::system(command.c_str()) == 0,
          "the tool stops with no file when a source of its reads was not there (case complete; log: work folder " + log.filename().string()
              + ")");
}

} // namespace

std::vector<Case> review_qg5c2_cases() {
    return {{"QGR5C2A", "review of QG5c2, R1: no success event for a participant whose grace ended before the success", grace_at_success},
            {"QGR5C2B", "review of QG5c2, R4: the store has no pin of an encounter root (a quest plan, the end)", pins_in_store},
            {"QGR5C2C", "review of QG5c2, R3: a destroyed count above the members that the runner makes is refused", impossible_count},
            {"QGR5C2D", "review of QG5c2, R5: the manifest keeps the item gate of an encounter start object", start_gate_manifest},
            {"QGR5C2E", "review of QG5c2, R6: the gates of two producers of one event stay apart", ranked_alternatives},
            {"QGR5C2F", "review of QG5c2, R2: the encounter tool writes no file for a source that was not there", tool_complete}};
}

} // namespace soulwarden::backend::tests

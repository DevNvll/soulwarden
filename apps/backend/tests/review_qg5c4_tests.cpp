// The corrections of the code review of stage QG5c4 (evidence/quests/qg5c4/code-review/FINDINGS.md), stage QG5c5. Each case fails on the
// tree of stage QG5c4 (trees/after-qg5c4; QGR5C4A and QGR5C4F end the test program there with a call through a null pointer) and passes
// with the correction. The inputs are the ones of the review cases (QG5C4RV1 to QG5C4RV5). Made-up recipes, rows, quests, and accounts (the
// encounter fixtures of tests/support/encounter_fixture.h and encounter_pattern_fixture.h).
//
//   QGR5C4A  QG5C4R-R1: no gathering data (the file is not there): the resolution of the recipes calls no method of a null pointer, and the
//            ambush (its proximity object has no gathering row) stays runnable
//   QGR5C4B  QG5C4R-R2: a stage with an entity-state objective and a destroyed objective (one kill): the trigger meets only its own
//            objective, and the stage waits for the kill
//   QGR5C4C  QG5C4R-R3: A and B find one proximity object; the store refuses the write of A; an other event completes the task of A, and
//            A gets a later quest of the same event: the retry of the find credits only the task that the first attempt matched
//   QGR5C4D  QG5C4R-R4: a destroyed parent (one death) whose only spawn is in a timed grandchild (5 s, delay 2 s) under a timed child of 1 s:
//            the ancestor ends the grandchild first, and the recipe is refused
//   QGR5C4E  QG5C4R-R5: a stage with a spawned objective (2 spawns, the second at 3 s) and a kill objective: both observers get the satisfied
//            spawned leaf while the kill waits
//   QGR5C4F  the world starts and runs with each encounter data file missing (gathering, interaction, creatures, encounters): no fault, and
//            the recipes that need the file are refused with their cause

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "support/encounter_pattern_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using questfix::json;

json trigger_objective() {
    return {{"type", "EntityActiveStateChangedObjective"}, {"timeLimit", 0}, {"succeedOnTimeout", false},
            {"completeOn", "deactivated"}, {"description", "@Test_Trigger"},
            {"triggers", json::array({{{"entity", "Test_Trigger"}, {"position", {100.0, 220.0, 30.0}},
                                       {"filter", "PlayerOnly"}, {"deadTargets", true},
                                       {"shapes", json::array({{{"shape", "sphere"}, {"radius", 2.0}}})}}})}};
}

void missing_gathering(const Options& options) {
    PatternShop shop({ambush_recipe()});
    check(!shop.host.gathering.load((options.workDirectory / "qgr5c4a-missing-gathering").string()),
          "control: the gathering folder is not there, and the gathering data is not loaded");
    std::printf("QGR5C4A the world resolves the recipes with no gathering data\n");
    std::fflush(stdout);
    shop.enter();
    shop.wait(0.5F);
    check(shop.encounters().counts().taken == 1,
          "with no gathering data the ambush (a proximity object needs no gathering row) is taken, and the load has no fault");
}

void trigger_join(const Options&) {
    json objective = trigger_objective();
    objective["more"] = json::array({destroyed(1, kMemberSlice)});
    json recipe = pattern_recipe(kKey, kRootAt,
                                 json::array({stage_json("Wait and kill", 0x100, kSetupSlot, true, objective,
                                                         json::array({spawn_json(kMemberSlice, 0, 1, kMemberAt)})),
                                              stage_json("Cleanup", 0x200, kCleanupSlot, false, timed(100), json::array())}),
                                 {0, 1});
    PatternShop shop({recipe});
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty(), "control: the runner takes the entity-state and destroyed stage");
    check(shop.instance() != nullptr && !shop.instance()->stages[0].ended && shop.members(false).size() == 1,
          "control: the trigger waits, and its creature lives");
    shop.move(kCharacterA, {100.0F, 220.0F, 30.0F});
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && shop.encounters().counts().triggersFound == 1, "control: the character in the trigger found it");
    if (run == nullptr) {
        return;
    }
    check(run->stages[0].met[0] && !run->stages[0].met[1] && !run->stages[0].ended,
          "the trigger meets only its own objective, and the stage waits for the required kill");
    const auto fighters = shop.members(false);
    if (!fighters.empty()) {
        shop.kill(*fighters.front(), kCharacterB);
    }
    shop.wait(0.5F);
    run = shop.instance();
    check(run != nullptr && run->stages[0].ended && run->stages[1].started, "the kill meets the second objective, and the stage ends");
}

void retry_targets(const Options& options) {
    const auto path = options.workDirectory / "qgr5c4c-retry.sqlite3";
    std::filesystem::remove(path);
    PatternShop shop({ambush_recipe()}, pattern_members(), path.string());
    json quests = pattern_quests();
    quests["tasks"]["review_later_task"] = questfix::task("Review_Later_Task", "taskgameevent", gamedata::task_type::kGameEvent,
                                                          {{"KillEnemyType", kAmbushEvent}, {"InteractTag", kAmbushTag}, {"TargetQty", "1"}});
    quests["objectives"].push_back({{"id", "Review_Later"},
                                    {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                    {"tree", json::array({json::array({0, "review_later_task", json::array()})})}});
    check(shop.host.quests.load_text(quests.dump()), "control: both quest definitions load before the characters enter");
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
    shop.move(kCharacterA, {100.0F, 212.0F, 30.0F});
    shop.move(kCharacterB, {101.0F, 212.0F, 30.0F});
    shop.wait(3.0F);
    check(sql(std::string("CREATE TRIGGER review_refuse_a BEFORE INSERT ON character_quest_tasks WHEN NEW.character_id='") + kCharacterA
              + "' BEGIN SELECT RAISE(FAIL, 'test fault'); END"),
          "control: only the quest writes of A fail");
    shop.move(kCharacterA, {100.0F, 201.0F, 30.0F});
    shop.move(kCharacterB, {101.0F, 201.0F, 30.0F});
    check(shop.progress(kCharacterA, "Test_Ambush") == 0 && shop.progress(kCharacterB, "Test_Ambush") == 1,
          "control: A waits in the ledger, and B has the find");
    check(sql("DROP TRIGGER review_refuse_a"), "control: the fault of the store ends");
    world::QuestEvent other;
    other.kind = world::QuestEventKind::gameEvent;
    other.keys = {kAmbushEvent};
    (void)shop.host.world.quests().event(*shop.entity(kCharacterA), other);
    check(shop.progress(kCharacterA, "Test_Ambush") == 1, "control: an other event completes the task of A that the find matched");
    shop.grant(kCharacterA, "Review_Later");
    check(shop.progress(kCharacterA, "Review_Later") == 0, "control: the later quest is active only after the find");
    shop.move(kCharacterA, {100.0F, 260.0F, 30.0F});
    shop.wait(1.0F);
    std::printf("QGR5C4C original=%u later=%u found=%llu\n", shop.progress(kCharacterA, "Test_Ambush"),
                shop.progress(kCharacterA, "Review_Later"), static_cast<unsigned long long>(shop.encounters().counts().objectsFound));
    check(shop.progress(kCharacterA, "Review_Later") == 0, "the retry of the old find does not credit a quest that was not active at the find");
    check(shop.encounters().counts().objectsFound == 1, "the object goes one time (the retry of A had its write, with no task)");
}

void ancestor_deadline(const Options&) {
    json recipe = pattern_recipe(kKey, kRootAt,
                                 json::array({stage_json("Required death", 0x100, kKillSlot, true, destroyed(1, kMemberSlice), json::array()),
                                              stage_json("Short parent", 0x200, kSetupSlot, false, timed(1), json::array()),
                                              stage_json("Long grandchild", 0x300, kChildSlotA, false, timed(5),
                                                         json::array({spawn_json(kMemberSlice, 2, 1, kMemberAt)})),
                                              stage_json("Cleanup", 0x400, kCleanupSlot, false, timed(100), json::array())}),
                                 {0, 3});
    recipe["stages"][0]["children"] = {1};
    recipe["stages"][1]["children"] = {2};
    PatternShop shop({recipe});
    begin(shop);
    const auto& refused = shop.encounters().placements()[0].refused;
    std::printf("QGR5C4D refused=%zu\n", refused.size());
    check(!refused.empty() && refused.front().find("can give only 0") != std::string::npos,
          "a count that only a spawn after the end of an ancestor stage gives is refused (" + (refused.empty() ? std::string("none")
                                                                                                              : refused.front()) + ")");
    // Control: the same spawn at 0.5 s (before the end of the child of 1 s) gives the count.
    recipe["stages"][2]["spawns"][0]["delay"] = 0.5;
    PatternShop early({recipe});
    begin(early);
    check(early.encounters().placements()[0].refused.empty(), "control: a spawn before the end of each stage above it gives the count");
}

void spawned_hud(const Options&) {
    json objective = destroyed(2, kMemberSlice);
    objective["type"] = "SliceSpawnedObjective";
    objective["more"] = json::array({destroyed(1, kBossSlice)});
    json recipe = pattern_recipe(kKey, kRootAt,
                                 json::array({stage_json("Spawn two and kill the boss", 0x100, kKillSlot, true, objective,
                                                         json::array({spawn_json(kMemberSlice, 0, 1, kMemberAt),
                                                                      spawn_json(kMemberSlice, 3, 1, kMemberAt),
                                                                      spawn_json(kBossSlice, 0, 1, kMemberAt)})),
                                              stage_json("Cleanup", 0x200, kCleanupSlot, false, timed(100), json::array())}),
                                 {0, 1});
    PatternShop shop({recipe});
    begin(shop);
    shop.wait(3.0F);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->stages[0].counts[0] == 2 && run->stages[0].met[0] && !run->stages[0].met[1] && !run->stages[0].ended,
          "control: both members came, and the stage waits for the boss kill");
    for (const FakeClient* client : {&shop.clientA, &shop.clientB}) {
        const auto task = root_task(root_seen(*client), 1);
        check(task && task->first == 2 && task->second == world::quest_state::kComplete,
              "each observer gets the satisfied spawned leaf (2 of 2) while the kill objective waits");
    }
}

void missing_files(const Options& options) {
    // The summoned opponent (a start object: a template and a gathering row; a direct boss) and the ambush (a proximity object, a creature
    // member) at two places.
    const state::WorldPosition otherRoot{160.0F, 205.0F, 30.0F};
    const state::WorldPosition otherStart{160.0F, 201.0F, 30.0F};
    const char* otherKey = "e4c00000-0000-4000-8000-000000000002/17";
    struct Expect {
        const char* file;
        std::uint64_t taken;
    };
    for (const Expect& expect : {Expect{"gathering", 1}, Expect{"interaction", 0}, Expect{"creatures", 0}, Expect{"encounters", 0}}) {
        PatternShop shop({summon_recipe(), ambush_recipe(otherKey, otherRoot, otherStart)});
        const std::string missing = (options.workDirectory / (std::string("qgr5c4f-missing-") + expect.file)).string();
        const std::string file = expect.file;
        bool loaded = true;
        if (file == "gathering") {
            loaded = shop.host.gathering.load(missing);
        } else if (file == "interaction") {
            loaded = shop.host.interactions.load(missing + ".json");
        } else if (file == "creatures") {
            loaded = shop.host.creatures.load(missing + ".json");
        } else {
            loaded = shop.host.encounters.load(missing + ".json");
        }
        check(!loaded, std::string("control: the ") + expect.file + " data is not there");
        std::printf("QGR5C4F the world starts with no %s data\n", expect.file);
        std::fflush(stdout);
        begin(shop);
        shop.move(kCharacterB, {160.0F, 212.0F, 30.0F});
        shop.wait(3.0F);
        const world::EncounterCounts& counts = shop.encounters().counts();
        check(counts.taken == expect.taken,
              std::string("with no ") + expect.file + " data the world runs, and the runner takes " + std::to_string(expect.taken)
                  + " of the recipes (taken " + std::to_string(counts.taken) + ")");
        if (file != "encounters") {
            const auto& placements = shop.encounters().placements();
            check(placements.size() == 2 && !placements[0].refused.empty(),
                  std::string("the summoned opponent is refused with its cause (") + (placements.empty() || placements[0].refused.empty()
                                                                                            ? std::string("none")
                                                                                            : placements[0].refused.front()) + ")");
        }
    }
}

} // namespace

std::vector<Case> review_qg5c4_cases() {
    return {{"QGR5C4A", "review of QG5c4, R1: no gathering data is no fault of the resolution of the recipes", missing_gathering},
            {"QGR5C4B", "review of QG5c4, R2: a trigger meets only its own objective (the stage waits for its kill)", trigger_join},
            {"QGR5C4C", "review of QG5c4, R3: the retry of a find credits only the tasks of its first attempt", retry_targets},
            {"QGR5C4D", "review of QG5c4, R4: the count check takes the time limits of the stages above a spawn", ancestor_deadline},
            {"QGR5C4E", "review of QG5c4, R5: both observers get a satisfied spawned objective while a kill waits", spawned_hud},
            {"QGR5C4F", "review of QG5c4: the world runs with each encounter data file missing (gathering, interaction, creatures, "
                        "encounters)", missing_files}};
}

} // namespace soulwarden::backend::tests

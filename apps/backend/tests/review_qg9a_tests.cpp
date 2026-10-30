// The cases of the code review of stage QG9a (corrected in stage QG9b; the findings
// ~/Games/soulwarden/work/evidence/quests/qg9a/code-review/FINDINGS.md; the proof cases of the review, renamed). Each finding case fails on the
// tree of stage QG9a (trees/after-qg9a). The game mode Test_Story of tests/support/story_fixture.h; all accounts, characters, ids, and places
// are made up.
//
//   QG9AR1   R1: a member row of an earlier start that the recovery cannot remove keeps its character out of the world (no later save that
//            the retry of the recovery would undo); a character with no row enters
//   QG9AR2   R2: a restored character in the main world has its StayActive TaskGameMode false again (the entry refresh of the membership)
//   QG9AR3   R3: a predicate task with a GameModeID (a possession, a level) does not count outside its mode
//   QG9AR4   R4: a boss of the background setup killed during the intro: the fight counts it at its start, and the instance succeeds
//   QG9AR5   R5: a move to an other space ends the delayed work of a cast also after a return to its space
//   QG9AR5B  R5: a move to an other space ends the motion of a cast (no movement allowance, no delayed Charge swing in the new space)
//   QG9AR6   R6: an entry whose rows the store refuses sends no 3094 Success; a later entry sends Success and enters
//   QG9ARC   the controls of the review: the refusals of the entrance, the owner view of the membership, a replaced session, a ready root

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "support/motion_fixture.h"
#include "support/shop_fixture.h"
#include "support/story_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using namespace storyfix;
using std::chrono::milliseconds;

bool review_sql(const std::filesystem::path& path, const char* statement) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }
    const bool ok = sqlite3_exec(db, statement, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

void startup_refusal(const Options& options) {
    const auto path = options.workDirectory / "qg9ar-startup.sqlite3";
    std::filesystem::remove(path);
    const state::WorldPosition home{100, 200, 50}, newer{300, 400, 50};
    {
        state::Store store;
        check(store.open(path.string()), "setup: private store");
        add_player(store, kSteamIdA, "", shop::kCharacterA, "ReviewA", home);
        add_player(store, kSteamIdB, "", shop::kCharacterB, "ReviewB", home);
        check(store.write_instance_member({shop::kCharacterA, 7, "Review_Map", home, 1}), "setup: row of an earlier process");
    }
    check(review_sql(path, "CREATE TRIGGER refuse_recovery BEFORE DELETE ON character_instances BEGIN SELECT RAISE(ABORT, 'review'); END;"),
          "setup: refuse recovery cleanup");
    ServerHost host(service::RepServerOptions{}, path.string());
    const auto a = interaction::player_of(host.store, kSteamIdA, "", shop::kCharacterA);
    const auto b = interaction::player_of(host.store, kSteamIdB, "", shop::kCharacterB);
    FakeClient ca(27000), cb(27001);
    host.enter(cb, b.registration());
    check(host.world.find_character(b.characterId) != nullptr, "control: B has no stale row and can play");
    host.enter(ca, a.registration());
    const bool admitted = host.world.find_character(a.characterId) != nullptr;
    check(host.store.list_instance_members().size() == 1, "setup: A's recovery still fails");
    if (admitted) {
        check(host.world.travel(a.characterId, newer).error == world::TravelError::none, "setup: newer main-world position commits");
    }
    check(review_sql(path, "DROP TRIGGER refuse_recovery"), "setup: storage recovers");
    host.tick();
    const auto row = host.store.find_character(a.characterId);
    note("QG9AR1 admittedWithStaleRow=" + std::to_string(admitted) + " storedXAfterRecovery=" + std::to_string(row->position->x));
    check(!admitted, "a character with an unrecovered row cannot enter main-world play");
    check(!admitted || row->position->x == newer.x, "retry cannot overwrite a later committed main-world save");
}

void restored_membership_task(const Options&) {
    for (bool logout : {false, true}) {
        StoryShop fixture;
        auto doc = story_quests();
        doc["tasks"]["test_story_enter_mode"]["cells"]["StayActive"] = "1";
        doc["tasks"]["review_wait"] = questfix::task("Review_Wait", "taskgameevent", gamedata::task_type::kGameEvent,
                                                     {{"KillEnemyType", "Review_Wait_Event"}, {"TargetQty", "1"}});
        doc["tasks"]["review_sequence"] =
            questfix::task("Review_Sequence", "consecutivetaskcontainer", gamedata::task_type::kConsecutiveContainer);
        for (auto& objective : doc["objectives"]) {
            if (objective["id"] == kStoryObjective) {
                objective["tree"] =
                    json::array({json::array({0, "test_story_enter_mode", json::array()}), json::array({1, "review_wait", json::array()}),
                                 json::array({2, "review_sequence", json::array({0, 1})})});
            }
        }

        check(fixture.host.quests.load_text(doc.dump()), "setup: membership task has StayActive");
        fixture.enter();
        auto& a = *fixture.entity(kCharacterA);
        check(fixture.host.world.quests().grant(a, kStoryObjective, "command", 0).ok(), "setup: membership quest");
        fixture.enter_story(kCharacterA);
        check(task_progress(a, kStoryObjective) == 1, "setup: current membership satisfies the task");
        const auto* waiting = questfix::quest_of(a, kStoryObjective);
        check(waiting && waiting->tasks.size() == 3 && waiting->tasks.back().state == world::quest_state::kActive,
              "setup: the root is not ready and the next task still waits");
        const auto owner = a.owner;
        if (logout) {
            check(fixture.host.world.leave(owner), "setup: remove A's session and instance membership");
            FakeClient again(27050);
            fixture.host.enter(again, fixture.a.registration());
            fixture.host.steps(10, {&again, &fixture.clientB});
            auto* restored = fixture.entity(kCharacterA);
            check(restored && restored->space == world::kMainSpace && !restored->gameModeMembership, "setup: A restores in the main world");
            if (!restored) {
                return;
            }
            note("QG9AR2 logout=1 progress=" + std::to_string(task_progress(*restored, kStoryObjective)));
            check(task_progress(*restored, kStoryObjective) == 0, "restored StayActive membership is false outside its mode");
        } else {
            check(fixture.instances().leave(kCharacterA).error == world::InstanceError::none, "setup: normal leave");
            fixture.follow(kCharacterA);
            note("QG9AR2 logout=0 progress=" + std::to_string(task_progress(a, kStoryObjective)));
            check(task_progress(a, kStoryObjective) == 0, "control: normal leave resets StayActive membership");
        }
        check(fixture.entity(kCharacterB)->space == world::kMainSpace, "B remains in the main world");
    }
}

void mode_predicate(const Options&) {
    for (bool possession : {false, true}) {
        StoryShop fixture;
        auto doc = story_quests();
        auto& t = doc["tasks"]["test_story_enter_mode"];
        t["type"] = possession ? "taskhaveandreturnitems" : "taskhavelevel";
        t["code"] = possession ? 26U : gamedata::task_type::kHaveLevel;
        t["cells"] = {{"GameModeID", kMode}, {"TargetQty", "1"}, {"StayActive", "1"}};
        if (possession) {
            t["cells"]["ItemName"] = "TestOreT1";
        }
        check(fixture.host.quests.load_text(doc.dump()), "setup: a predicate task with GameModeID");
        fixture.enter();
        auto& a = *fixture.entity(kCharacterA);
        auto& b = *fixture.entity(kCharacterB);
        fixture.enter_story(kCharacterA);
        if (possession) {
            for (auto* character : {&a, &b}) {
                check(fixture.host.world.inventory().give(character->characterId, "TestOreT1", 1, {}).ok(),
                      "setup: each character has the item");
            }
        }
        check(fixture.host.world.quests().grant(a, kStoryObjective, "command", 0).ok(), "setup: grant in the mode");
        check(fixture.host.world.quests().grant(b, kStoryObjective, "command", 0).ok(), "setup: grant outside the mode");
        note("QG9AR3 possession=" + std::to_string(possession) + " inModeProgress=" + std::to_string(task_progress(a, kStoryObjective))
             + " outsideProgress=" + std::to_string(task_progress(b, kStoryObjective)));
        check(task_progress(a, kStoryObjective) == 1, "control: the predicate is true inside its mode");
        check(task_progress(b, kStoryObjective) == 0, "a predicate task with GameModeID cannot count outside its mode");
    }
}

void early_boss_kill(const Options&) {
    for (bool early : {false, true}) {
        StoryShop fixture;
        fixture.enter();
        fixture.enter_story(kCharacterA);
        auto& a = *fixture.entity(kCharacterA);
        const auto space = a.space;
        const auto id = fixture.instances().instance_of(kCharacterA)->id;
        const auto* intro = fixture.story_run(space);
        check(intro && intro->stages[1].started && !intro->stages[1].ended && !intro->stages[2].started, "setup: intro runs before fight");
        if (!early) {
            fixture.cutscene(kCharacterA, server::rep::kTypeRequestStartCutscene, gamedata::quest_crc(kIntro));
            fixture.cutscene_end(kCharacterA);
            fixture.wait(0.3F);
        }
        auto* boss = fixture.boss(space);
        check(boss && !boss->creature.immune(), "setup: the boss can take damage");
        if (!boss) {
            return;
        }
        fixture.kill(*boss, kCharacterA);
        check(boss->creature.dead, "setup: the accepted kill kills the boss");
        fixture.wait(23.0F);
        const auto* instance = fixture.instances().find(id);
        const auto* run = fixture.story_run(space);
        note("QG9AR4 early=" + std::to_string(early) + " success=" + std::to_string(instance && instance->succeeded)
             + " fightCount=" + std::to_string(run ? run->stages[2].destroyed : 9999));
        check(instance && instance->succeeded, "the accepted boss kill reaches encounter success after the intro time limit");
        check(fixture.entity(kCharacterB)->space == world::kMainSpace, "B stays in the main world");
    }
}

void returning_cast(const Options&) {
    for (bool roundTrip : {false, true}) {
        shop::Shop fixture(false, false, true);
        fixture.enter_b();
        gamedata::GameMap map;
        map.id = "Review_Map";
        map.mode = map.id;
        map.level = world::kMainLevel;
        map.terrain = world::kMainTerrain;
        map.starts = {{shop::kPlace, 0}};
        map.teamCapacity = 1;
        check(fixture.host.gameModes.set({map}), "setup: map");
        check(fixture.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: hammer");
        auto& a = *fixture.character();
        const auto node = gamedata::progression_id("Ability_WarHammer_Shockwave");
        a.skills.pools[shop::kWarHammer][node].level = 1;
        a.skills.bars[shop::kWarHammer] = {0, node, 0};
        a.combat.selectedWeapon = world::paperdoll::kMainHand1;
        auto* wolf = fixture.wolf(1.0F);
        check(wolf != nullptr, "setup: main-world target");
        if (!wolf) {
            return;
        }
        world::AbilityPress press;
        press.place = 1;
        press.input = 1000;
        check(fixture.world().abilities().press_character(shop::kCharacterA, press) == world::CastVerdict::accepted, "setup: timed cast");
        const auto health = wolf->vitals.health;
        if (roundTrip) {
            check(fixture.world().instances().enter(shop::kCharacterA, map.id).error == world::InstanceError::none,
                  "setup: leave cast space");
            check(fixture.world().instances().leave(shop::kCharacterA).error == world::InstanceError::none,
                  "setup: return before the cast phase");
        }
        fixture.host.clock.advance(milliseconds(1500));
        fixture.host.tick();
        note("QG9AR5 roundTrip=" + std::to_string(roundTrip) + " health=" + std::to_string(health) + " -> "
             + std::to_string(wolf->vitals.health));
        check(roundTrip ? wolf->vitals.health == health : wolf->vitals.health < health,
              roundTrip ? "a space move ends pending cast work even after return" : "control: the cast hits without a move");
    }
}

void admission_store_failure(const Options& options) {
    const auto path = options.workDirectory / "qg9ar-entry.sqlite3";
    std::filesystem::remove(path);
    StoryShop fixture(path.string());
    fixture.enter();
    check(review_sql(path, "CREATE TRIGGER refuse_entry BEFORE INSERT ON character_instances BEGIN SELECT RAISE(ABORT, 'review'); END;"),
          "setup: storage refuses an admission");
    const auto from = fixture.clientA.got.size();
    fixture.enter_story(kCharacterA);
    const auto results = dungeon_results(fixture.clientA, from);
    const bool success = std::any_of(results.begin(), results.end(), [](const auto& result) { return result.result == 0; });
    note("QG9AR6 successReply=" + std::to_string(success) + " space=" + std::to_string(fixture.entity(kCharacterA)->space)
         + " members=" + std::to_string(fixture.host.store.list_instance_members().size()));
    check(fixture.entity(kCharacterA)->space == world::kMainSpace && fixture.host.store.list_instance_members().empty(),
          "a failed write changes no membership");
    check(!success, "a failed admission cannot send 3094 Success");
    check(review_sql(path, "DROP TRIGGER refuse_entry"), "setup: remove refusal");
    const auto control = fixture.clientA.got.size();
    fixture.enter_story(kCharacterA);
    const auto good = dungeon_results(fixture.clientA, control);
    check(std::any_of(good.begin(), good.end(), [](const auto& result) { return result.result == 0; })
              && fixture.entity(kCharacterA)->space != world::kMainSpace,
          "control: an accepted admission replies Success and enters");
}

void pending_motion(const Options&) {
    motion::MotionShop fixture;
    fixture.hold("TestAxe");
    gamedata::GameMap map;
    map.id = "Review_Map";
    map.mode = map.id;
    map.level = world::kMainLevel;
    map.terrain = world::kMainTerrain;
    map.starts = {{shop::kPlace, 0}};
    map.teamCapacity = 1;
    check(fixture.host.gameModes.set({map}), "setup: story space");
    check(fixture.world().instances().enter(shop::kCharacterA, map.id).error == world::InstanceError::none, "setup: A enters");
    world::AbilityPress press;
    press.place = 0;
    press.input = 1000;
    check(fixture.world().abilities().press_character(shop::kCharacterA, press) == world::CastVerdict::accepted,
          "setup: Charge starts in the instance");
    check(fixture.world().instances().leave(shop::kCharacterA).error == world::InstanceError::none, "setup: A leaves before the swing");
    const auto before = fixture.character()->action.sequence;
    const auto allowance = fixture.world().abilities().motion_allowance(*fixture.character(), fixture.host.clock.now());
    fixture.host.clock.advance(milliseconds(1100));
    fixture.host.tick();
    note("QG9AR5B afterExitSequence=" + std::to_string(before) + " laterSequence=" + std::to_string(fixture.character()->action.sequence)
         + " movementAllowance=" + std::to_string(allowance.distance));
    check(allowance.distance == 0, "the new space gets no movement allowance of the old cast");
    check(fixture.character()->action.sequence != world::ability_rules::kChargeSwingSequence,
          "the old space's delayed Charge swing does not start in the main world");
}

void entrance_controls(const Options&) {
    for (int variant = 0; variant < 4; ++variant) {
        StoryShop fixture;
        if (variant == 2) {
            auto objects = fixture.host.interactions.objects();
            objects[1].bounds[0].requiredAchievement = 0;
            check(fixture.host.interactions.set(fixture.host.interactions.options(), {{"Review_Trial_Unlocked", 70}}, objects,
                                                fixture.host.interactions.templates()),
                  "setup: the entrance requires a quest bit");
        }
        if (variant == 3) {
            auto objects = fixture.host.interactions.objects();
            objects[1].entrance->mode = "Review_Unknown_Mode";
            check(fixture.host.interactions.set(fixture.host.interactions.options(), {}, objects, fixture.host.interactions.templates()),
                  "setup: wrong mode");
        }
        fixture.enter();
        if (variant == 1) {
            fixture.move(kCharacterA, {110, 202, 30});
        }
        const auto from = fixture.clientA.got.size();
        if (variant == 0) {
            auto forged = fixture.entrance_block(kOpenMenuIndex);
            forged.actor = fixture.entity(kCharacterB)->actor.bytes;
            fixture.request(fixture.clientA, forged);
            forged = fixture.entrance_block(kEnterIndex);
            forged.actor = fixture.entity(kCharacterB)->actor.bytes;
            fixture.request(fixture.clientA, forged);
            fixture.end_block(fixture.clientA);
        } else {
            fixture.enter_story(kCharacterA);
        }
        const auto results = dungeon_results(fixture.clientA, from);
        const bool success = std::any_of(results.begin(), results.end(), [](const auto& result) { return result.result == 0; });
        check(!success && fixture.entity(kCharacterA)->space == world::kMainSpace && fixture.entity(kCharacterB)->space == world::kMainSpace
                  && fixture.host.store.list_instance_members().empty(),
              "foreign actor, distance, quest gate, or wrong mode changes no membership");
    }
    StoryShop control;
    auto doc = story_quests();
    doc["tasks"]["test_story_enter_mode"]["cells"]["StayActive"] = "1";
    check(control.host.quests.load_text(doc.dump()), "setup: StayActive task for session replacement");
    control.enter();
    auto& a = *control.entity(kCharacterA);
    check(control.host.world.quests().grant(a, kStoryObjective, "command", 0).ok(), "setup: quest");
    control.enter_story(kCharacterA);
    check(a.gameModeMembership && !server::rep::encode_game_mode_state(a, server::rep::View::owner, control.host.clock).empty()
              && server::rep::encode_game_mode_state(a, server::rep::View::observer, control.host.clock).empty(),
          "a populated membership encodes for its owner and gives an observer no bytes");
    FakeClient newer(27050);
    control.host.enter(newer, control.a.registration());
    control.host.steps(5, {&newer, &control.clientB});
    check(control.entity(kCharacterA) == &a && control.instances().in_mode(a, kMode) && task_progress(a, kStoryObjective) == 1,
          "a replacement session keeps current membership and its task");
    check(control.instances().leave(kCharacterA).error == world::InstanceError::none, "setup: ready objective leaves its mode");
    control.host.steps(5, {&newer, &control.clientB});
    check(task_progress(a, kStoryObjective) == 1, "a ready root keeps its completed task under the existing quest rule");
}

} // namespace

std::vector<Case> review_qg9a_cases() {
    return {{"QG9AR1", "review QG9a (R1): a member row of an earlier start that the recovery cannot remove keeps its character out",
             startup_refusal},
            {"QG9AR2", "review QG9a (R2): a restored StayActive membership task is false outside its mode", restored_membership_task},
            {"QG9AR3", "review QG9a (R3): a predicate task with a GameModeID does not count outside its mode", mode_predicate},
            {"QG9AR4", "review QG9a (R4): a boss kill during the intro reaches the success of the instance", early_boss_kill},
            {"QG9AR5", "review QG9a (R5): a move to an other space ends the delayed work of a cast, also after a return", returning_cast},
            {"QG9AR5B", "review QG9a (R5): a move to an other space ends the motion of a cast", pending_motion},
            {"QG9AR6", "review QG9a (R6): an entry whose rows the store refuses sends no 3094 Success", admission_store_failure},
            {"QG9ARC", "review QG9a: the controls of the entrance refusals, the owner view, a replaced session, a ready root",
             entrance_controls}};
}

} // namespace soulwarden::backend::tests

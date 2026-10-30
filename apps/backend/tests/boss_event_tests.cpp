// Tests of stage BT3, part C: the events of a boss tree through the one event path of the encounter runner (world/encounters.h,
// Encounters::creature_event; docs/systems/encounters.md, section "The events of a boss tree"), and the Thorpe event with the tree of Thorpe.
//
//   BT3H  two instances of one story mode (MP-10): a stage that waits for an event that the tree of a member sends waits; the event of the
//         creature of A meets only the stage of A; an event of the tree that came before its stage meets that stage at its start; a stage
//         whose event no tree sends is met at its start (the rule of stage QG9a)
//   BT3T  the Thorpe event (the form of the main story event "Kill Thorpe"): the member of the kind of Thorpe has the tree boss/thorpe, its
//         phase 2 at 60 percent plays its transition in the encounter, the kill ends the stage, and the success gives the quest task of A and B
//
// The accounts, the characters, the creature kinds, the slices, the trees, the recipes, and the places are made up.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/range_fixture.h"
#include "support/story_fixture.h"
#include "support/support_fixture.h"
#include "world/behavior.h"
#include "world/behavior_tree.h"
#include "world/encounters.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace storyfix;
namespace tree = world::tree;

std::filesystem::path folder_with(const Options& options, const char* name, const std::vector<std::pair<std::string, std::string>>& files) {
    const std::filesystem::path folder = options.workDirectory / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    for (const auto& [file, text] : files) {
        std::ofstream(folder / file, std::ios::binary) << text;
    }
    return folder;
}

json event_objective(const char* name) {
    json event = objective_of("EncounterEventObjective", 0, false);
    event["eventId"] = name;
    return event;
}

json member_at(const state::WorldPosition& place) {
    return {{"member", encfix::kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({encfix::place_json(place)})}};
}

// ---- BT3H: the events of a boss tree --------------------------------------------------------------------------------------------------

/**
 * The tree of the member: at 50 percent health or below, one time in the fight, the events Later_Event (before the stage that waits for it)
 * and Boss_Half; then it turns to its target.
 */
const char* const kSenderTree = R"(// the sender
{ "format": 1, "tree": "test/sender", "root": { "node": "selector", "children": [
  { "node": "sequence", "children": [ { "node": "in_fight" }, { "node": "keep_target", "lose_distance": 60 },
    { "node": "selector", "children": [
      { "node": "once", "child": { "node": "sequence", "children": [ { "node": "health_below", "percent": 50 },
          { "node": "event", "event": "Later_Event" }, { "node": "event", "event": "Boss_Half" } ] } },
      { "node": "turn_to_target" } ] } ] },
  { "node": "subtree", "tree": "default/rest" } ] } })";

void tree_events(const Options& options) {
    // The stages of the manager: 0 an event that no tree sends (met at its start), 1 the event Boss_Half of the tree of the member (it spawns
    // the member), 2 the event Later_Event (the tree sends it with Boss_Half: met at the start of the stage), 3 the kill, 4 a hold.
    json stages = json::array({encfix::stage_json("Wave 0 - No sender", 0xD00, 11, true, event_objective("Not_Sent"), json::array()),
                               encfix::stage_json("Wave 1 - Boss", 0xD10, 12, true, event_objective("Boss_Half"), json::array({member_at(kBossAt)})),
                               encfix::stage_json("Wave 2 - Later", 0xD20, 13, true, event_objective("Later_Event"), json::array()),
                               encfix::stage_json("Wave 3 - Kill", 0xD30, 14, true, encfix::destroyed(1, encfix::kMemberSlice), json::array()),
                               encfix::stage_json("Wave 4 - Hold", 0xD40, 15, false, encfix::timed(120), json::array())});
    json document = story_document();
    document["format"] = 3;
    document["recipes"][0]["stages"] = stages;
    document["recipes"][0]["stageOrder"] = {0, 1, 2, 3, 4};
    StoryShop shop(":memory:", document);
    // The member kind fights (the slice of the archer of the range world as its body) and has the tree test/sender by its kind.
    gamedata::CreatureSlice body = range::archer_slice(true);
    body.path = "slices/test/bag-wolf.dynamicslice";
    body.ranged.clear();
    gamedata::CreatureKind member = range::archer_kind();
    member.id = "worldevent_test_member";
    member.vitals = "Test_Member";
    member.category = "Test_Member_Category";
    member.vitalsId = 0x7E570001;
    member.categoryId = 0x7E570002;
    member.member = encfix::kMemberSlice;
    member.points = 0;
    std::vector<gamedata::CreatureLevel> levels = range::archer_levels();
    levels.push_back({20, bags::kWolfHealth, 10.0F, 50.0F});
    check(shop.host.creatures.set_full({body}, {range::archer_kind(), member}, {}, levels), "setup: the member kind fights");
    shop.host.creatures.set_members({{encfix::kMemberSlice, {1, ""}}});
    shop.host.world.set_creature_behavior(true);
    const std::filesystem::path folder = folder_with(
        options, "bt3h-trees",
        {{"sender.jsonc", kSenderTree},
         {"bindings.jsonc", R"({ "format": 1, "bindings": [ { "kind": "worldevent_test_member", "tree": "test/sender" }, { "tree": "default" } ] })"}});
    tree::LoadReport report;
    check(shop.host.world.creatures().behavior().reload_trees(folder.string(), report) && report.refused.empty(), "setup: the tree of the member");
    log_lines().clear();
    shop.enter();
    shop.enter_story(encfix::kCharacterA);
    shop.enter_story(encfix::kCharacterB);
    shop.wait(0.5F);
    const world::SpaceId spaceA = shop.entity(encfix::kCharacterA)->space;
    const world::SpaceId spaceB = shop.entity(encfix::kCharacterB)->space;
    const world::EncounterInstance* runA = shop.story_run(spaceA);
    const world::EncounterInstance* runB = shop.story_run(spaceB);
    check(spaceA != spaceB && runA != nullptr && runB != nullptr, "setup: A and B in two instances of the mode, each with its run");
    if (runA == nullptr || runB == nullptr) {
        return;
    }
    check(runA->stages[0].ended && runB->stages[0].ended && runA->stages[1].started && !runA->stages[1].ended && runB->stages[1].started
              && !runB->stages[1].ended && shop.encounters().counts().eventStagesWaited == 2
              && log_count("the event Not_Sent of the stage Wave 0 - No sender is met at its start (no sender of the data)") == 2
              && log_count("the stage Wave 1 - Boss waits for the event Boss_Half of the tree of its member") == 2,
          "an event that no tree sends is met at the start of its stage (the rule of stage QG9a); the stage of the event Boss_Half waits in "
          "each instance (the tree of the member sends it)");
    world::Entity* bossA = shop.boss(spaceA);
    world::Entity* bossB = shop.boss(spaceB);
    check(bossA != nullptr && bossB != nullptr && bossA->creature.mind.brain.tree->name == "test/sender", "setup: the member of each instance with its tree");
    if (bossA == nullptr || bossB == nullptr) {
        return;
    }
    // The member of A fights A and goes to 40 percent: its tree sends the two events after its step; only the stages of A get them.
    shop.host.world.creatures().behavior().hurt_by(*bossA, shop.entity(encfix::kCharacterA)->id);
    shop.host.world.creatures().behavior().hurt_by(*bossB, shop.entity(encfix::kCharacterB)->id);
    shop.wait(0.5F);
    bossA->vitals.health = 0.4F * bossA->vitals.healthMax;
    shop.wait(0.6F);
    runA = shop.story_run(spaceA);
    runB = shop.story_run(spaceB);
    check(runA != nullptr && runA->stages[1].ended && runA->stages[2].ended && runA->stages[3].started && !runA->stages[3].ended
              && runA->events.count("boss_half") == 1 && runA->events.count("later_event") == 1,
          "A: the event Boss_Half of the tree meets the wave of the boss; Later_Event came before its stage, so that stage is met at its start; "
          "the kill stage waits");
    check(log_count("the event Boss_Half of the tree of creature") == 1 && log_count("the event Later_Event of the stage Wave 2 - Later is met at its start "
                                                                                     "(the tree of a member sent it before)") == 1
              && shop.encounters().counts().treeEvents == 2 && shop.encounters().counts().treeEventsMet == 1,
          "the log and the counts: two events of the tree, one met a stage that waited, one was kept for its stage");
    check(runB != nullptr && !runB->stages[1].ended && runB->events.empty(), "B: its instance waits for the event of its own member");
    // The member of B: the same in its instance.
    bossB->vitals.health = 0.3F * bossB->vitals.healthMax;
    shop.wait(0.6F);
    runB = shop.story_run(spaceB);
    check(runB != nullptr && runB->stages[1].ended && runB->stages[3].started, "B: the event of its member meets its stage");
    // The kill of the member of A ends the kill stage of A only.
    (void)shop.host.world.creatures().hurt(bossA->id, bossA->vitals.healthMax * 2.0F, shop.entity(encfix::kCharacterA)->id);
    shop.wait(0.3F);
    runA = shop.story_run(spaceA);
    runB = shop.story_run(spaceB);
    check(runA != nullptr && runA->stages[3].ended && runA->stages[4].started && runB != nullptr && !runB->stages[3].ended,
          "the kill of the member of A ends the kill stage of A; B keeps its kill stage");
}

// ---- BT3I: the runner takes a recipe whose event stages the tree of its member ends -------------------------------------------------------

void taken_by_tree(const Options& options) {
    const auto run = [&](bool full) {
        json stages = json::array({encfix::stage_json("Wave 1 - Boss", 0xE00, 11, true, event_objective("Ev_A"), json::array({member_at(kBossAt)})),
                                   encfix::stage_json("Wave 2 - Late", 0xE10, 12, true, event_objective("Ev_B"), json::array()),
                                   encfix::stage_json("Wave 3 - Clean Up", 0xE20, 13, false, encfix::timed(1), json::array())});
        json document = story_document();
        document["format"] = 3;
        document["recipes"][0]["stages"] = stages;
        document["recipes"][0]["stageOrder"] = {0, 1, 2};
        document["recipes"][0]["refused"] = {"no stage of the encounter ends by the act of a player (each one ends at an event of the boss tree, "
                                             "a time, a cutscene, a spawn, or a wait): it would succeed with no fight"};
        StoryShop shop(":memory:", document);
        gamedata::CreatureSlice body = range::archer_slice(true);
        body.path = "slices/test/bag-wolf.dynamicslice";
        body.ranged.clear();
        gamedata::CreatureKind member = range::archer_kind();
        member.id = "worldevent_test_member";
        member.vitals = "Test_Member";
        member.category = "Test_Member_Category";
        member.vitalsId = 0x7E570001;
        member.categoryId = 0x7E570002;
        member.member = encfix::kMemberSlice;
        member.points = 0;
        std::vector<gamedata::CreatureLevel> levels = range::archer_levels();
        levels.push_back({20, bags::kWolfHealth, 10.0F, 50.0F});
        check(shop.host.creatures.set_full({body}, {range::archer_kind(), member}, {}, levels), "setup: the member kind");
        shop.host.creatures.set_members({{encfix::kMemberSlice, {1, ""}}});
        shop.host.world.set_creature_behavior(true);
        const std::string events = full ? R"({ "node": "event", "event": "Ev_A" }, { "node": "event", "event": "Ev_B" })" : R"({ "node": "event", "event": "Ev_A" })";
        const std::string tree = R"({ "format": 1, "tree": "test/ender", "root": { "node": "selector", "children": [
          { "node": "sequence", "children": [ { "node": "in_fight" }, { "node": "keep_target", "lose_distance": 60 },
            { "node": "selector", "children": [ { "node": "once", "child": { "node": "sequence", "children": [ { "node": "health_below", "percent": 50 }, )"
                                 + events + R"( ] } }, { "node": "turn_to_target" } ] } ] },
          { "node": "subtree", "tree": "default/rest" } ] } })";
        const std::filesystem::path folder = folder_with(
            options, full ? "bt3i-full" : "bt3i-part",
            {{"ender.jsonc", tree},
             {"bindings.jsonc", R"({ "format": 1, "bindings": [ { "kind": "worldevent_test_member", "tree": "test/ender" }, { "tree": "default" } ] })"}});
        tree::LoadReport report;
        (void)shop.host.world.creatures().behavior().reload_trees(folder.string(), report);
        log_lines().clear();
        shop.enter();
        const auto index = shop.encounters().placement_of(kStoryKey);
        const bool taken = index && shop.encounters().placements()[*index].refused.empty();
        if (!full) {
            check(!taken && shop.encounters().placements()[*index].refused.size() == 1,
                  "a tree that sends only Ev_A: the recipe stays refused (no act of a player ends the stage of Ev_B)");
            return;
        }
        check(taken && log_count("the tree of its member sends the event of each event stage (stage BT3)") == 1,
              "a tree that sends Ev_A and Ev_B: the runner takes the recipe that the tool refused (the fight ends its stages)");
        shop.enter_story(encfix::kCharacterA);
        shop.enter_story(encfix::kCharacterB);
        shop.wait(0.5F);
        const world::SpaceId spaceA = shop.entity(encfix::kCharacterA)->space;
        const world::SpaceId spaceB = shop.entity(encfix::kCharacterB)->space;
        world::Entity* bossA = shop.boss(spaceA);
        check(bossA != nullptr && shop.boss(spaceB) != nullptr, "setup: the member in each instance (MP-10)");
        if (bossA == nullptr) {
            return;
        }
        shop.host.world.creatures().behavior().hurt_by(*bossA, shop.entity(encfix::kCharacterA)->id);
        shop.wait(0.5F);
        bossA->vitals.health = 0.45F * bossA->vitals.healthMax;
        shop.wait(2.0F);
        const world::EncounterInstance* runA = shop.story_run(spaceA);
        const world::EncounterInstance* runB = shop.story_run(spaceB);
        const world::Instance* instanceA = shop.instances().instance_of(encfix::kCharacterA);
        check((runA == nullptr || runA->phase == world::EncounterPhase::succeeded) && instanceA != nullptr && instanceA->succeeded && runB != nullptr
                  && !runB->stages[0].ended,
              "A: the two events of the tree end the two stages, the cleanup ends, and the content of A succeeds; B waits for its member");
    };
    run(true);
    run(false);
}

// ---- BT3T: the Thorpe event -----------------------------------------------------------------------------------------------------------

/** The names of the actions of the tree boss/thorpe (the slice damned_commander_thorpe of the game data has each one). */
const char* const kThorpeAttacks[] = {"Attack_SwipeL_01", "Attack_SwipeR_01", "Attack_Stab_01", "Attack_Stab_02", "Attack_SwipeL_02",
                                      "Attack_SwipeR_02", "Attack_Shoulder_Charge_Loop", "Attack_Shoulder_Charge_Outro",
                                      "Attack_Overhead_Slash", "Attack_Double_Spin", "Attack_Flurry", "Attack_Core_Explosion"};
constexpr const char* kTransitionStatus = "Test_Thorpe_Transition";
constexpr const char* kPhaseStatus = "Test_Thorpe_Phase_2";

void thorpe_event(const Options&) {
    // The recipe of the form of the Thorpe event: the kill wave of Thorpe (one creature member) and the cleanup; its success event.
    json recipe = encfix::recipe_json(encfix::kKey, encfix::kRootAt, encfix::kStartAt, encfix::kMemberAt);
    recipe["stageOrder"] = {0, 1};
    recipe["stages"] = json::array(
        {encfix::stage_json("Wave_05 - Objective: Kill Thorpe", 0x300, encfix::kKillSlot, true, encfix::destroyed(1, encfix::kMemberSlice),
                            json::array({member_at(encfix::kMemberAt)})),
         encfix::stage_json("Wave_06 - Cleanup", 0x400, encfix::kCleanupSlot, false, encfix::timed(3), json::array())});
    encfix::EncounterShop shop({recipe});
    // The member kind of Thorpe: the slice of the archer with the 12 attacks of the tree and the tree action of the transition.
    gamedata::CreatureSlice body = range::archer_slice(true);
    body.path = "slices/test/bag-wolf.dynamicslice";
    body.ranged.clear();
    body.attacks.clear();
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = 2.4F;
    contact.factor = 0.3F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Thorpe";
    for (const char* name : kThorpeAttacks) {
        const auto action = static_cast<std::uint32_t>(body.actions.size());
        body.actions.push_back({name, name, static_cast<std::int32_t>(50 + action), false});
        body.attacks.push_back(gamedata::CreatureAttack{action, 1.2F, {contact}});
    }
    const auto phase = static_cast<std::uint32_t>(body.actions.size());
    body.actions.push_back({"Phase_Transition_1_to_2", "Phase_Transition_1_to_2", 90, false});
    body.treeActions = {gamedata::CreatureTreeAction{phase, 11.5333F, {kTransitionStatus, kPhaseStatus}, {kTransitionStatus}, std::nullopt}};
    gamedata::CreatureKind thorpe = range::archer_kind();
    thorpe.id = "worldevent_ai_thorpe_msq";
    thorpe.vitals = "Test_Member";
    thorpe.category = "Test_Member_Category";
    thorpe.vitalsId = 0x7E570001;
    thorpe.categoryId = 0x7E570002;
    thorpe.member = encfix::kMemberSlice;
    thorpe.points = 0;
    std::vector<gamedata::CreatureLevel> levels = range::archer_levels();
    levels.push_back({20, bags::kWolfHealth, 10.0F, 50.0F});
    check(shop.host.creatures.set_full({body}, {range::archer_kind(), thorpe}, {}, levels), "setup: the member kind of Thorpe");
    shop.host.creatures.set_members({{encfix::kMemberSlice, {1, ""}}});
    gamedata::AbilityTables tables = support::support_abilities();
    std::vector<gamedata::StatusRow> statusRows = tables.statusEffects;
    statusRows.push_back(shop::status(kTransitionStatus, -1.0F, shop::cells({{"TickRate", 0.1F}, {"HealthModifierPercent", 0.02F}, {"ABSSlash", 0.6F}})));
    statusRows.push_back(shop::status(kPhaseStatus, -1.0F, shop::cells({{"ABSSlash", 0.1F}}), true));
    tables.statusEffects = shop::numbered(std::move(statusRows));
    const bool rows = shop.host.abilities.set(std::move(tables));
    check(rows, "setup: the status rows of the transition (" + shop.host.abilities.error() + ")");
    shop.host.world.set_creature_behavior(true);
    tree::LoadReport report;
    (void)shop.host.world.creatures().behavior().reload_trees("", report);
    log_lines().clear();
    encfix::begin(shop);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.size() == 2, "the Thorpe event runs with A and B as participants");
    shop.wait(1.0F);
    std::vector<world::Entity*> members = shop.members(false);
    check(members.size() == 1 && members.front()->creature.mind.brain.tree->name == "boss/thorpe"
              && members.front()->creature.mind.brain.binding == "kind worldevent_ai_thorpe_msq (binding 1)",
          "the member of the wave is Thorpe with the tree boss/thorpe of the program (the binding of his kind)");
    if (members.size() != 1) {
        return;
    }
    world::Entity& member = *members.front();
    world::Entity& a = *shop.entity(encfix::kCharacterA);
    a.vitals.healthMax = 1.0e7F;
    a.vitals.health = 1.0e7F;
    shop.host.world.creatures().behavior().hurt_by(member, a.id);
    shop.wait(2.0F);
    member.vitals.health = 0.55F * member.vitals.healthMax;
    shop.wait(4.0F);
    a.vitals.health = a.vitals.healthMax;
    check(member.creature.mind.treeAction.has_value() && member.creature.reactionsOffUntil
              && log_count("starts the tree action Phase_Transition_1_to_2 (11.53 s; statuses: Test_Thorpe_Transition, Test_Thorpe_Phase_2)") == 1
              && run->stages[0].started && !run->stages[0].ended,
          "at 55 percent Thorpe plays his transition in the encounter (with its statuses); the kill wave waits");
    const auto row = [](const RootSeen& seen, std::uint32_t progress, std::uint8_t state) {
        return !seen.objectives.empty() && std::any_of(seen.objectives.back().tasks.begin(), seen.objectives.back().tasks.end(), [&](const auto& task) {
                   return task.progress == progress && task.state == state;
               });
    };
    check(row(root_seen(shop.clientA), 0, world::quest_state::kActive) && row(root_seen(shop.clientB), 0, world::quest_state::kActive),
          "the HUD row of the kill wave in the Objectives state of the root for A and B: the kill leaf, Active, the progress 0 (the transition is "
          "no kill)");
    (void)shop.host.world.creatures().hurt(member.id, member.vitals.healthMax * 2.0F, a.id);
    shop.wait(0.3F);
    run = shop.instance();
    check(run != nullptr && run->stages[0].ended && run->stages[1].started, "the kill of Thorpe ends the kill wave; the cleanup starts");
    check(row(root_seen(shop.clientA), 1, world::quest_state::kComplete) && row(root_seen(shop.clientB), 1, world::quest_state::kComplete),
          "the HUD row for A and B: the kill leaf Complete with the progress 1");
    shop.wait(3.3F);
    check(shop.progress(encfix::kCharacterA, "Test_Defend") == 1 && shop.progress(encfix::kCharacterB, "Test_Defend") == 1,
          "the success after the cleanup: the quest task of A and of B (1 / 1)");
}

} // namespace

std::vector<Case> boss_event_cases() {
    return {{"BT3H", "behavior trees (BT3, MP-10): the events of a boss tree meet the event stages of the instance of the member only", tree_events},
            {"BT3I", "behavior trees (BT3, MP-10): the runner takes a recipe whose event stages the tree of its member sends (else refused)",
             taken_by_tree},
            {"BT3T", "behavior trees (BT3, MP-10): the Thorpe event with the tree of Thorpe (the transition, the kill, the success, the quest task)",
             thorpe_event}};
}

} // namespace soulwarden::backend::tests

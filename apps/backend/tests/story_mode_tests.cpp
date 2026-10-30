// The cases of stage QG9b: the story instances of the main world level after QuestMedea (docs/systems/instances.md, docs/systems/encounters.md).
// The game mode Test_Story of tests/support/story_fixture.h with made-up recipes of the forms of the data (the werewolf lord of QuestWolf: a
// wait that spawns the boss, a fight with a boss phase; the seraphs of QuestSeraphim: four recipes at one place, a child stage that tracks the
// trigger of its parent). All accounts, characters, ids, quests, and places are made up.
//
//   SY1  the data: format 3 (the end rules, the boss phases, the detector of the mode areas) loads; a phase that is not a wave or a health
//        stops the load; the runner refuses a boss phase stage with no phase of its boss and a wait with no trigger in the main world
//   SY2  the content of a mode with the rules of stage QG9b: the detector of the mode areas (the run starts only when the character is in an
//        area), the wait that ends after its last spawn (the boss), the boss of an earlier stage counted by the fight, the boss phase at its
//        health percent and the next child stage, the stage with no objective, the success; two characters in two instances (B's run and
//        its boss do not change with A's); the character of the main world at the place of the area starts nothing
//   SY3  the gate of a mode with more than one recipe: a recipe starts only for a character with an active task of the mode that names it
//        (a kill category or the success event); a child stage counts the area trigger of its parent in the space of the instance; the
//        content succeeds with one of the recipes (MP-10: A and B in two instances of the mode, each with its own recipe)
//   SY6  stage QG9c: a creature member of an instance recipe more than 2 m above the floor stands on the floor (the boss of A and the boss
//        of B in two instances); 1 m above the floor it stands at its spawn location (control)

#include <algorithm>
#include <cmath>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "support/story_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using namespace storyfix;

constexpr const char* kWaitKey = "e4c00000-0000-4000-8000-0000000000c1/17";
constexpr const char* kSeraphX = "e4c00000-0000-4000-8000-0000000000d1/17";
constexpr const char* kSeraphY = "e4c00000-0000-4000-8000-0000000000d2/17";
constexpr const char* kAreaSlice = "slices/test/story_area_trigger.dynamicslice";
constexpr const char* kSeraphEvent = "Test_Seraph_Y_Success";
constexpr const char* kSeraphXObjective = "Test_Seraph_X";
constexpr const char* kSeraphYObjective = "Test_Seraph_Y";
constexpr state::WorldPosition kAreaAt{300.0F, 326.0F, 30.0F};
constexpr state::WorldPosition kTriggerAt{300.0F, 312.0F, 30.0F};

/** The members of the cases: the boss (the member slice of the fixture with one health phase of the fight) and the area trigger. */
json mode_members() {
    json members = members_json();
    members[kMemberSlice]["phases"] = json::array({{{"kind", "health"}, {"health", 50.0}, {"key", "Boss_Phase"}}});
    members[kAreaSlice] = {{"kind", "object"}, {"variant", ""},        {"table", ""},       {"row", "PressurePlate_Test"}, {"event", ""},
                           {"tag", ""},        {"onlyOnQuest", false}, {"proximity", true}, {"interact", false},           {"radius", 5.0}};
    return members;
}

json state_wait(const char* end) {
    return {{"type", "EntityActiveStateChangedObjective"},
            {"timeLimit", 0},
            {"succeedOnTimeout", false},
            {"failureCondition", false},
            {"end", end},
            {"triggers", json::array()}};
}

json boss_phase(std::uint32_t tracked, std::int32_t index) {
    return {{"type", "BossPhaseObjective"}, {"timeLimit", 0},          {"succeedOnTimeout", false}, {"failureCondition", false},
            {"trackedPhase", tracked},      {"phaseOf", kMemberSlice}, {"phaseIndex", index}};
}

/** A recipe of the mode from the QuestMedea form of the fixture, with its stages, its order, and its detector. */
json mode_recipe(const char* key, json stages, json order) {
    json recipe = story_recipe();
    recipe["key"] = key;
    recipe["stages"] = std::move(stages);
    recipe["stageOrder"] = std::move(order);
    return recipe;
}

/**
 * The form of the werewolf lord of QuestWolf: a wait that spawns the boss at 2 s (the rule "after"), the fight with its optional children (a
 * boss phase of the health 50, then a timed stage), and a cleanup with no objective. The detector is one area of the mode root, 20 m from
 * the root (no ParticipantTracker).
 */
json wait_recipe() {
    json wait = stage_json(
        "_Wave 0 - Wait", 0xA00, 11, false, state_wait("after"),
        json::array({{{"member", kMemberSlice}, {"delay", 2}, {"count", 1}, {"locations", json::array({place_json(kBossAt)})}}}));
    json fight = stage_json("_Wave 2 - Fight", 0xA10, 12, false, destroyed(1, kMemberSlice), json::array());
    fight["children"] = {3, 4};
    fight["childrenOptional"] = true;
    json cleanup = stage_json("_Wave 3 - Clean Up", 0xA20, 13, false, {{"type", "none"}, {"end", "start"}}, json::array());
    json phase = stage_json("Phase 2b - Fight", 0xA30, 14, true, boss_phase(1, 0), json::array());
    json after = stage_json("Phase 3a - Timed", 0xA40, 15, true, timed(1), json::array());
    json recipe = mode_recipe(kWaitKey, json::array({wait, fight, cleanup, phase, after}), {0, 1, 2});
    recipe.erase("tracker");
    recipe["detector"] = {
        {"radius", 0.0},
        {"filter", "OnlyPlayerTarget"},
        {"sphere", false},
        {"shapes", json::array({{{"shape", "aabb"}, {"extents", {4.0, 4.0, 4.0}}, {"center", {0.0, 20.0, 0.0}}, {"area", "AreaBoss"}}})},
        {"fromMode", {"AreaBoss"}}};
    return recipe;
}

/**
 * The form of a seraph of QuestSeraphim (X): the parent spawns the boss and a 20 m trigger, its child stages wait for the trigger and for the
 * boss. Y: a timed recipe with a success event (an other seraph of the same mode and place).
 */
json seraph_recipe(bool x) {
    if (!x) {
        json recipe = mode_recipe(kSeraphY,
                                  json::array({stage_json("Wave 0 - Setup", 0xC00, 21, false, timed(1), json::array()),
                                               stage_json("Wave 1 - Hold", 0xC10, 22, true, timed(1), json::array())}),
                                  {0, 1});
        recipe["successEvent"] = kSeraphEvent;
        return recipe;
    }
    json parent = stage_json(
        "Boss_Encounter", 0xB00, 16, false, destroyed(1, kMemberSlice),
        json::array({{{"member", kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(kBossAt)})}},
                     {{"member", kAreaSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(kTriggerAt)})}}}));
    parent["children"] = {2, 3};
    parent["childrenOptional"] = true;
    json recipe =
        mode_recipe(kSeraphX,
                    json::array({parent, stage_json("Wave 3 - Clean Up", 0xB10, 17, false, timed(1), json::array()),
                                 stage_json("Wave 0 - Wait", 0xB20, 18, true, destroyed(1, kAreaSlice), json::array()),
                                 stage_json("Wave 2 - Defeat Seraph", 0xB30, 19, false, destroyed(1, kMemberSlice), json::array())}),
                    {0, 1});
    return recipe;
}

json mode_document(const std::vector<json>& recipes) {
    json document = story_document();
    document["format"] = 3;
    document["recipes"] = recipes;
    document["members"] = mode_members();
    return document;
}

/** The quests of the mode and the two quests of the seraphs: a kill of the boss category and the success event of Y, in the mode. */
json mode_quests() {
    json document = story_quests();
    const auto add = [&](const char* objective, const char* task, const char* type, std::uint32_t code, json cells) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, type, code, cells);
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add(kSeraphXObjective, "Test_Seraph_X_Kill", "taskkillcontribution", gamedata::task_type::kKillContribution,
        {{"KillEnemyType", "Test_Member_Category"}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    add(kSeraphYObjective, "Test_Seraph_Y_Event", "taskgameevent", gamedata::task_type::kGameEvent,
        {{"KillEnemyType", kSeraphEvent}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    return document;
}

/** @return The run of a recipe in a space, or null. */
const world::EncounterInstance* run_of(StoryShop& shop, const char* key, world::SpaceId space) {
    const auto index = shop.encounters().placement_of(key);
    for (const auto& [root, run] : shop.encounters().instances()) {
        if (index && run.placement == *index && run.space == space) {
            return &run;
        }
    }
    return nullptr;
}

/** @return The live creature member of a run (the boss), or null. */
world::Entity* boss_of(StoryShop& shop, const world::EncounterInstance* run) {
    for (const auto& [id, member] : run != nullptr ? run->members : std::map<world::EntityId, world::EncounterMemberRun>{}) {
        world::Entity* found = shop.host.world.find(id);
        if (found != nullptr && found->kind == world::EntityKind::creature && !member.done) {
            return found;
        }
    }
    return nullptr;
}

/** SY1 */
void mode_data(const Options&) {
    gamedata::EncounterData data;
    check(data.load_text(mode_document({wait_recipe()}).dump()), "a file of format 3 loads (" + data.error() + ")");
    if (!data.loaded()) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = data.recipes()[0];
    const gamedata::EncounterMember* boss = data.member(kMemberSlice);
    check(boss != nullptr && boss->phases.size() == 1 && boss->phases[0].kind == gamedata::BossPhase::Kind::health
              && boss->phases[0].health == 50.0F && boss->phases[0].key == "Boss_Phase",
          "the boss phases of a member load (one health phase of 50)");
    check(recipe.stages[0].objective.kind == gamedata::EncounterObjectiveKind::entityState
              && recipe.stages[0].objective.end == gamedata::StageEnd::after
              && recipe.stages[2].objective.kind == gamedata::EncounterObjectiveKind::none
              && recipe.stages[2].objective.end == gamedata::StageEnd::start
              && recipe.stages[3].objective.kind == gamedata::EncounterObjectiveKind::bossPhase
              && recipe.stages[3].objective.trackedPhase == 1 && recipe.stages[3].objective.phaseIndex == 0
              && recipe.stages[3].objective.phaseOf == kMemberSlice,
          "the end rules and the boss phase objective load");
    check(recipe.detectorFromMode && recipe.detectorShapes.size() == 1
              && recipe.detectorShapes[0].kind == gamedata::EncounterShape::Kind::aabb && recipe.stages[3].userIndex == 0
              && recipe.stages[4].userIndex == 1,
          "the detector of the mode areas and the user waves of the child stages (the waves of the boss phases)");
    json bad = mode_document({wait_recipe()});
    bad["members"][kMemberSlice]["phases"][0]["health"] = 150.0;
    gamedata::EncounterData refused;
    check(!refused.load_text(bad.dump()) && refused.error().find("boss phase") != std::string::npos,
          "a health phase above 100 stops the load (" + refused.error() + ")");
    // The runner: a boss phase stage with no such phase of its boss, and a wait with no trigger in a recipe of the main world.
    json noPhase = wait_recipe();
    noPhase["key"] = "e4c00000-0000-4000-8000-0000000000c2/17";
    noPhase["stages"][3]["objective"]["phaseIndex"] = 5;
    json mainWorld =
        recipe_json("e4c00000-0000-4000-8000-0000000000c3/17", {900.0F, 900.0F, 30.0F}, {900.0F, 901.0F, 30.0F}, {902.0F, 901.0F, 30.0F});
    mainWorld["stages"][0]["objective"] = state_wait("after");
    StoryShop shop(":memory:", mode_document({wait_recipe(), noPhase, mainWorld}), mode_quests());
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().loaded_placements();
    const auto first = [&](std::size_t index) {
        return index < placements.size() && !placements[index].refused.empty() ? placements[index].refused[0] : std::string("taken");
    };
    check(first(0) == "taken", "the recipe of the mode with the rules of stage QG9b is taken (" + first(0) + ")");
    check(first(1).find("names no phase of a boss member") != std::string::npos,
          "a boss phase with no phase of its boss is refused (" + first(1) + ")");
    check(first(2).find("waits for a change of an entity that no trigger of the data sends") != std::string::npos,
          "a wait with no trigger in the main world is refused (" + first(2) + ")");
}

/** SY2 */
void mode_content(const Options&) {
    StoryShop shop(":memory:", mode_document({wait_recipe()}), mode_quests());
    shop.enter();
    // B in the main world at the place of the area: an instance recipe never runs in the main world.
    shop.move(kCharacterB, kAreaAt);
    shop.wait(1.0F);
    check(shop.encounters().instances().empty(), "the character of the main world at the place of the area starts nothing");
    shop.enter_story(kCharacterA);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::SpaceId spaceA = a.space;
    shop.wait(1.0F);
    check(spaceA != world::kMainSpace && run_of(shop, kWaitKey, spaceA) == nullptr,
          "the encounter waits while the character is not in an area of the mode root (no tracker)");
    shop.move(kCharacterA, kAreaAt);
    const world::EncounterInstance* run = run_of(shop, kWaitKey, spaceA);
    check(run != nullptr && run->stages[0].started && !run->stages[0].ended && boss_of(shop, run) == nullptr,
          "in the area the run starts with the wait, and the boss comes later");
    shop.wait(2.5F);
    run = run_of(shop, kWaitKey, spaceA);
    world::Entity* bossA = boss_of(shop, run);
    check(run != nullptr && bossA != nullptr && run->stages[0].ended && run->stages[1].started && run->stages[3].started
              && !run->stages[3].ended,
          "the wait ends after its last spawn (the boss at 2 s); the fight and its boss phase stage run");
    check(run != nullptr && run->participants.count(kCharacterA) != 0 && a.encounterLinks.size() == 1,
          "the character in the area is a participant (its link)");
    if (bossA == nullptr) {
        return;
    }
    const float full = bossA->vitals.healthMax;
    (void)shop.host.world.creatures().hurt(bossA->id, full * 0.4F, a.id);
    shop.wait(0.6F);
    run = run_of(shop, kWaitKey, spaceA);
    check(run != nullptr && !run->stages[3].ended, "at 60 % of its health the boss phase of 50 % does not end");
    (void)shop.host.world.creatures().hurt(bossA->id, full * 0.2F, a.id);
    shop.wait(0.6F);
    run = run_of(shop, kWaitKey, spaceA);
    check(run != nullptr && run->stages[3].ended && run->stages[4].started,
          "at 40 % the phase ends: its stage is met, and the next child starts");
    // B enters its own instance of the mode (from the entrance): its run and its boss are its own.
    shop.move(kCharacterB, state::WorldPosition{101.0F, 200.0F, 30.0F});
    shop.enter_story(kCharacterB);
    world::Entity& b = *shop.entity(kCharacterB);
    const world::SpaceId spaceB = b.space;
    shop.move(kCharacterB, kAreaAt);
    shop.wait(2.5F);
    const world::EncounterInstance* runB = run_of(shop, kWaitKey, spaceB);
    world::Entity* bossB = boss_of(shop, runB);
    check(spaceB != spaceA && runB != nullptr && bossB != nullptr && bossB != bossA && bossB->space == spaceB
              && bossB->vitals.health == bossB->vitals.healthMax && !runB->stages[3].ended,
          "B has its own run and its own boss at full health in its space (MP-10)");
    shop.kill(*bossA, kCharacterA);
    shop.wait(1.0F);
    const world::Instance* instanceA = shop.instances().instance_of(kCharacterA);
    check(run_of(shop, kWaitKey, spaceA) == nullptr && instanceA != nullptr && instanceA->succeeded,
          "the kill ends the fight; the cleanup with no objective ends at its start; the content of A succeeded");
    runB = run_of(shop, kWaitKey, spaceB);
    const world::Instance* instanceB = shop.instances().instance_of(kCharacterB);
    check(runB != nullptr && runB->stages[1].started && !runB->stages[1].ended && instanceB != nullptr && !instanceB->succeeded,
          "the fight of B runs on");
}

/** SY3 */
void mode_gate(const Options&) {
    StoryShop shop(":memory:", mode_document({seraph_recipe(true), seraph_recipe(false)}), mode_quests());
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().loaded_placements();
    check(placements.size() == 2 && placements[0].gated && placements[1].gated
              && std::find(placements[0].taskKeys.begin(), placements[0].taskKeys.end(), "k:test_member_category")
                     != placements[0].taskKeys.end()
              && placements[1].taskKeys == std::vector<std::string>{"e:test_seraph_y_success"},
          "the two recipes of the mode are gated: X by the kill category of its boss, Y by its success event");
    shop.enter_story(kCharacterA);
    world::Entity& a = *shop.entity(kCharacterA);
    shop.wait(1.0F);
    check(shop.encounters().instances().empty(), "with no task of the mode no recipe starts");
    check(shop.host.world.quests().grant(a, kSeraphXObjective, "command", 0).ok(), "setup: A has the kill task of the boss of X");
    shop.wait(1.0F);
    check(run_of(shop, kSeraphX, a.space) != nullptr && run_of(shop, kSeraphY, a.space) == nullptr, "A starts X only");
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(b, kSeraphYObjective, "command", 0).ok(), "setup: B has the event task of Y");
    shop.enter_story(kCharacterB);
    shop.wait(0.6F);
    check(b.space != a.space && run_of(shop, kSeraphY, b.space) != nullptr && run_of(shop, kSeraphX, b.space) == nullptr,
          "B starts Y only, in its own space (MP-10)");
    // X: the child stage counts the area trigger of its parent (the find in the space of the instance).
    const world::EncounterInstance* runX = run_of(shop, kSeraphX, a.space);
    check(runX != nullptr && runX->stages[2].started && !runX->stages[2].ended, "the wait of X for the trigger of its parent runs");
    shop.move(kCharacterA, kTriggerAt);
    runX = run_of(shop, kSeraphX, a.space);
    check(runX != nullptr && runX->stages[2].ended && runX->stages[3].started,
          "A in the trigger of the parent ends the wait; the defeat stage starts");
    world::Entity* boss = boss_of(shop, runX);
    check(boss != nullptr, "setup: the boss of X");
    if (boss != nullptr) {
        shop.kill(*boss, kCharacterA);
    }
    shop.wait(2.0F);
    const world::Instance* instanceA = shop.instances().instance_of(kCharacterA);
    check(run_of(shop, kSeraphX, a.space) == nullptr && instanceA != nullptr && instanceA->succeeded
              && shop.progress(kCharacterA, kSeraphXObjective) == 1,
          "X succeeds, the kill task of A is done, and the content succeeded with one of the recipes");
    const world::Instance* instanceB = shop.instances().instance_of(kCharacterB);
    check(instanceB != nullptr && instanceB->succeeded && shop.progress(kCharacterB, kSeraphYObjective) == 1,
          "Y succeeds for B with its success event");
}

/** SY4: an achievement conditional with a line end (the entrance of QuestApophis: "ID || (...)\r\n") has its value. */
void mode_line_end(const Options&) {
    StoryShop shop;
    shop.enter();
    const world::Quests& quests = shop.host.world.quests();
    for (const char* character : {kCharacterA, kCharacterB}) {
        const world::Entity& who = *shop.entity(character);
        check(quests.conditional(who, "!Test_Never\r\n") && quests.conditional(who, "Test_Never ||\r\n(!Test_Never && !Test_Other)\r\n")
                  && !quests.conditional(who, "Test_Never\r\n") && !quests.conditional(who, "!Test_Never\r\nTest_Other"),
              std::string("a line end is a blank for ") + character + ": the value of the expression; text after the end is still false");
    }
}

/** @return A copy of a document of the story fixture whose texts of the mode Test_Story name `mode`. */
json with_mode(json value, const std::string& mode) {
    if (value.is_string() && value.get<std::string>() == kMode) {
        return mode;
    }
    if (value.is_array() || value.is_object()) {
        for (json& item : value) {
            item = with_mode(item, mode);
        }
    }
    return value;
}

/**
 * SY5: the setting of the mode QuestMedusa (encounter_rules::kQuestMedusaRuns). Stage QG9c: on in the build (the cause of the stop of the
 * client is corrected, docs/systems/creatures.md). Off (a test sets the world option): the runner refuses its recipe with the reason of the
 * setting, the entrance refuses the entry of A and of B (10), and nothing changes. On: the entry and the instance of stage QG9b.
 */
void mode_setting(const Options&) {
    const std::string mode(world::encounter_rules::kQuestMedusaMode);
    const std::string reason(world::encounter_rules::kQuestMedusaOff);
    const json document = with_mode(story_document(), mode);
    const json quests = with_mode(story_quests(), mode);
    check(world::encounter_rules::kQuestMedusaRuns && world::WorldOptions{}.questMedusaRuns,
          "the setting of the build: QuestMedusa is on, and the world option has the value of the setting");
    {
        StoryShop shop(":memory:", document, quests, mode.c_str());
        shop.host.world.set_quest_medusa(false);
        shop.enter();
        const std::vector<world::EncounterPlacement>& placements = shop.encounters().loaded_placements();
        check(placements.size() == 1 && placements[0].refused == std::vector<std::string>{reason} && !shop.encounters().runs_mode(mode),
              "off: the runner refuses the recipe of QuestMedusa with the one reason of the setting; the backend does not run the mode");
        check(shop.host.world.quests().grant(*shop.entity(kCharacterA), kStoryObjective, "command", 0).ok(),
              "setup: A has the task of the mode");
        for (const char* character : {kCharacterA, kCharacterB}) {
            FakeClient& client = std::string(character) == kCharacterA ? shop.clientA : shop.clientB;
            const world::Entity& who = *shop.entity(character);
            const state::WorldPosition before = who.position;
            const std::size_t from = client.got.size();
            shop.enter_story(character);
            const auto results = dungeon_results(client, from);
            check(results.size() == 1 && results[0].result == world::dungeon_result::kEntranceActivationFailure
                      && who.space == world::kMainSpace && shop.instances().instance_of(character) == nullptr
                      && world::horizontal_distance(who.position, before) < 0.5F,
                  std::string("off: the entry of ") + character + " is refused (3094 with 10); it stays at its place in the main world");
        }
        check(shop.instances().counts().made == 0 && shop.instances().counts().entries == 0
                  && shop.instances().counts().entranceRefused == 2 && shop.encounters().instances().empty()
                  && task_progress(*shop.entity(kCharacterA), kStoryObjective) == 0,
              "off: no instance, no entry row, no encounter, and the task of the mode is not done");
    }
    {
        StoryShop shop(":memory:", document, quests, mode.c_str());
        shop.enter();
        check(shop.encounters().loaded_placements().size() == 1 && shop.encounters().loaded_placements()[0].refused.empty()
                  && shop.encounters().runs_mode(mode),
              "on (the build): the runner takes the recipe of QuestMedusa");
        const std::size_t from = shop.clientA.got.size();
        shop.enter_story(kCharacterA);
        const auto results = dungeon_results(shop.clientA, from);
        const world::Entity& a = *shop.entity(kCharacterA);
        const world::Instance* instance = shop.instances().instance_of(kCharacterA);
        check(results.size() == 1 && results[0].result == world::dungeon_result::kSuccess && instance != nullptr && instance->mode == mode
                  && a.space == instance->space && a.space != world::kMainSpace,
              "on: the entry is accepted (3094 with 0), and A is in an instance of QuestMedusa");
        check(shop.instances().entrance_result(*shop.entity(kCharacterB), *shop.host.interactions.objects()[1].entrance)
                  == world::dungeon_result::kSuccess,
              "on: the checks accept B too");
    }
}

/**
 * SY6 (stage QG9c, docs/systems/encounters.md, "The instance recipes (stage QG9b)"): the floor of a creature member of an instance recipe
 * (encounter_rules::kAboveFloor). The terrain of the case is the floor. 8 m below the spawn location of the boss: the boss of A and the boss
 * of B (two instances) stand on the floor. 1 m below: the boss stands at its spawn location (control).
 */
void member_floor(const Options&) {
    for (const float below : {8.0F, 1.0F}) {
        const float floor = kBossAt.z - below;
        const bool lowered = below > world::encounter_rules::kAboveFloor;
        StoryShop shop;
        shop.host.terrain.source = [floor](float, float) { return std::optional<float>(floor); };
        shop.host.world.set_creature_behavior(false);
        shop.enter();
        const std::vector<const char*> characters = lowered ? std::vector<const char*>{kCharacterA, kCharacterB}
                                                            : std::vector<const char*>{kCharacterA};
        std::vector<world::Entity*> bosses;
        for (const char* character : characters) {
            shop.enter_story(character);
            shop.wait(1.0F);
            const world::Entity* who = shop.entity(character);
            world::Entity* boss = boss_of(shop, run_of(shop, kStoryKey, who->space));
            check(who->space != world::kMainSpace && boss != nullptr && boss->space == who->space,
                  std::string("setup: the boss of the instance of ") + character + " is in its space");
            if (boss == nullptr) {
                return;
            }
            bosses.push_back(boss);
            const float expected = lowered ? floor : kBossAt.z;
            note("SY6 " + std::string(character) + " floor=" + std::to_string(floor) + " boss z=" + std::to_string(boss->position.z));
            check(std::fabs(boss->position.z - expected) < 0.01F && world::horizontal_distance(boss->position, kBossAt) < 0.01F,
                  std::string(lowered ? "8 m above the floor: the boss of " : "1 m above the floor (control): the boss of ") + character
                      + (lowered ? " stands on the floor below its spawn location" : " stands at its spawn location"));
        }
        check(shop.encounters().counts().membersLowered == (lowered ? 2U : 0U)
                  && (bosses.size() < 2 || bosses[0] != bosses[1]),
              lowered ? "two bosses in two instances, each put on the floor (the count membersLowered)" : "control: no member is lowered");
    }
}

} // namespace

std::vector<Case> story_mode_cases() {
    return {{"SY1", "story instances QG9b: format 3, the end rules, the boss phases, and the refusals of the runner", mode_data},
            {"SY2", "story instances QG9b: the mode areas, the wait, the earlier boss, the boss phase, two instances, the main world",
             mode_content},
            {"SY3", "story instances QG9b: the gate of a mode with two recipes, the trigger of a parent in the instance, two instances",
             mode_gate},
            {"SY4", "story instances QG9b: an achievement conditional with a line end (the entrance of QuestApophis)", mode_line_end},
            {"SY5", "story instances QG9b: the setting of the mode QuestMedusa, on (the build) and off (a test), two characters",
             mode_setting},
            {"SY6", "story instances QG9c: a creature member of an instance recipe above the floor stands on the floor, two instances",
             member_floor}};
}

} // namespace soulwarden::backend::tests

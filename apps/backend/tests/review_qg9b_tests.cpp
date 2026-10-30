// The cases of the code review of stage QG9b (corrected in stage QG9c; the findings
// ~/Games/soulwarden/work/evidence/quests/qg9b/code-review/FINDINGS.md; the proof cases of the review, renamed). Each finding case fails on the
// tree of stage QG9b (trees/after-qg9b). The game mode Test_Story of tests/support/story_fixture.h; all accounts, characters, ids, quests, and
// places are made up.
//
//   QG9BR1  R1: a later wave does not count the deaths of an earlier sibling wave with the same slice (A and B in two instances)
//   QG9BR2  R2: an EncounterEventObjective stage waits for its required timed child stages; then the boss phase after both intro waves
//   QG9BR3C the rejected finding R3 (a control, the form of 09A_M12_FightMedusa): an achievement action with a GameModeID acts at its
//           activation in its mode (A) and in the main world (B)
//   QG9BR4  R4: a quest task of the event of a tracked start object of the next recipe of a mode starts that recipe (A and B in two
//           instances complete the sequence)
//   QG9BRC  the controls of the review: the trigger spaces, and a mode with a refused normal recipe

#include <algorithm>
#include <string>
#include <vector>

#include "support/story_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using namespace storyfix;

json spawn_member(unsigned count = 1, double delay = 0) {
    return {{"member", kMemberSlice}, {"delay", delay}, {"count", count}, {"locations", json::array({place_json(kBossAt)})}};
}

json document_of(json stages, json order) {
    json document = story_document();
    document["format"] = 3;
    document["recipes"][0]["stages"] = std::move(stages);
    document["recipes"][0]["stageOrder"] = std::move(order);
    return document;
}

std::vector<world::EntityId> living_members(StoryShop& shop, world::SpaceId space, unsigned stage) {
    std::vector<world::EntityId> ids;
    if (const auto* run = shop.story_run(space)) {
        for (const auto& [id, member] : run->members) {
            const auto* entity = shop.host.world.find(id);
            if (member.stage == stage && entity != nullptr && entity->kind == world::EntityKind::creature && !entity->creature.dead) {
                ids.push_back(id);
            }
        }
    }
    return ids;
}

void sibling_waves(const Options&) {
    constexpr const char* shaman = "slices/test/review_shaman.dynamicslice";
    json parent = stage_json("Boss fight", 0xB00, 11, false, timed(120), json::array());
    parent["children"] = {1, 2};
    parent["childrenOptional"] = true;
    json first = stage_json("First burrow", 0xB10, 12, true, destroyed(2, kMemberSlice), json::array({spawn_member(2)}));
    json shamanSpawn = spawn_member();
    shamanSpawn["member"] = shaman;
    json secondObjective = destroyed(1, kMemberSlice);
    secondObjective["more"] = json::array({destroyed(1, shaman)});
    json second = stage_json("Second burrow", 0xB20, 13, true, secondObjective, json::array({spawn_member(), shamanSpawn}));
    json document = document_of({parent, first, second}, {0});
    document["members"][shaman] = document["members"][kMemberSlice];
    StoryShop shop(":memory:", document);
    shop.host.creatures.set_members({{kMemberSlice, {1, ""}}, {shaman, {1, ""}}});
    shop.enter();
    shop.enter_story(kCharacterA);
    shop.enter_story(kCharacterB);
    const auto aSpace = shop.entity(kCharacterA)->space;
    const auto bSpace = shop.entity(kCharacterB)->space;
    const auto firstIds = living_members(shop, aSpace, 1);
    check(firstIds.size() == 2 && living_members(shop, bSpace, 1).size() == 2, "setup: both spaces have the first burrow");
    for (const auto id : firstIds) {
        shop.kill(*shop.host.world.find(id), kCharacterA);
    }
    shop.wait(0.3F);
    const auto* run = shop.story_run(aSpace);
    check(run != nullptr && run->stages[1].ended && run->stages[2].started, "setup: A reached the second burrow");
    if (run == nullptr) {
        return;
    }
    const auto alive = living_members(shop, aSpace, 2);
    note("QG9BR1 secondCount=" + std::to_string(run->stages[2].destroyed) + " secondEnded=" + std::to_string(run->stages[2].ended)
         + " secondLiving=" + std::to_string(alive.size()));
    check(run->stages[2].destroyed == 0 && !run->stages[2].ended, "a later wave does not count the deaths of the earlier wave");
    check(living_members(shop, bSpace, 1).size() == 2 && !shop.story_run(bSpace)->stages[2].started, "B's first wave stays unchanged");
    world::EntityId shamanId = 0;
    for (const auto& [id, member] : run->members) {
        if (member.stage == 2 && member.spawn == 1) {
            shamanId = id;
        }
    }
    check(shamanId != 0 && alive.size() == 2, "setup: the second wave has its own living soldier and shaman");
    if (shamanId != 0) {
        shop.kill(*shop.host.world.find(shamanId), kCharacterA);
    }
    shop.wait(0.2F);
    note("QG9BR1 afterShamanKill secondEnded=" + std::to_string(shop.story_run(aSpace)->stages[2].ended)
         + " secondLiving=" + std::to_string(living_members(shop, aSpace, 2).size()));
    check(!shop.story_run(aSpace)->stages[2].ended, "the kill of the shaman alone does not end the second wave while its soldier lives");
    for (const auto id : living_members(shop, aSpace, 2)) {
        shop.kill(*shop.host.world.find(id), kCharacterA);
    }
    shop.wait(0.2F);
    check(shop.story_run(aSpace) == nullptr || shop.story_run(aSpace)->stages[2].ended, "the second wave ends with the deaths of its own spawns");
}

void event_children(const Options&) {
    json event = objective_of("EncounterEventObjective", 0, false);
    event["eventId"] = "Review_Intro_Complete";
    json parent = stage_json("Intro", 0xC00, 11, false, event, json::array());
    parent["children"] = {2, 3};
    parent["childrenOptional"] = false;
    json fight = stage_json("Fight", 0xC10, 12, false, destroyed(1, kMemberSlice), json::array({spawn_member()}));
    fight["children"] = {4};
    fight["childrenOptional"] = true;
    json wait = objective_of("EntityActiveStateChangedObjective", 1, true);
    wait["end"] = "time";
    wait["triggers"] = json::array();
    json child = stage_json("Intro timed wait", 0xC20, 13, true, wait, json::array());
    json next = stage_json("Intro next child", 0xC30, 14, true, event, json::array());
    json phase = stage_json("Health phase", 0xC40, 15, true,
                            {{"type", "BossPhaseObjective"}, {"timeLimit", 0}, {"succeedOnTimeout", false}, {"failureCondition", false},
                             {"trackedPhase", 1}, {"phaseOf", kMemberSlice}, {"phaseIndex", 2}},
                            json::array());
    json document = document_of({parent, fight, child, next, phase}, {0, 1});
    document["members"][kMemberSlice]["phases"] = json::array({{{"kind", "wave"}, {"wave", 0}, {"key", "Intro_Phase"}},
                                                               {{"kind", "wave"}, {"wave", 1}, {"key", "Intro_Phase"}},
                                                               {{"kind", "health"}, {"health", 50.0}, {"key", "Boss_Phase"}}});
    StoryShop shop(":memory:", document);
    shop.enter();
    shop.enter_story(kCharacterA);
    auto* run = shop.story_run(shop.entity(kCharacterA)->space);
    check(run != nullptr, "setup: A has a running encounter");
    if (run == nullptr) {
        return;
    }
    note("QG9BR2 introEnded=" + std::to_string(run->stages[0].ended) + " fightStarted=" + std::to_string(run->stages[1].started)
         + " nextChildStarted=" + std::to_string(run->stages[3].started));
    check(!run->stages[0].ended && !run->stages[1].started, "the event objective waits for its required timed child stages");
    shop.wait(2.2F);
    run = shop.story_run(shop.entity(kCharacterA)->space);
    check(run != nullptr && run->stages[1].started && run->stages[3].started && run->stages[3].ended, "both required children run before the fight");
    auto* boss = shop.boss(shop.entity(kCharacterA)->space);
    check(boss != nullptr, "setup: a live boss for the health phase");
    if (boss != nullptr) {
        (void)shop.host.world.creatures().hurt(boss->id, boss->vitals.healthMax * 0.6F, shop.entity(kCharacterA)->id);
        shop.wait(0.7F);
        run = shop.story_run(shop.entity(kCharacterA)->space);
        note("QG9BR2 at40Percent phaseEnded=" + std::to_string(run != nullptr && run->stages[4].ended));
        check(run != nullptr && run->stages[4].ended, "the boss health phase passes after both intro waves end");
    }
    check(shop.entity(kCharacterB)->space == world::kMainSpace, "B stays in the main world");
}

void mode_actions(const Options&) {
    // The rejected finding R3 (the form of 09A_M12_FightMedusa: its first task, a lock achievement action with the GameModeID QuestMedusa,
    // runs in the main world before the trial unlocks): an achievement action with a GameModeID acts at its activation outside its mode.
    json quests = story_quests();
    quests["achievements"].push_back(json::array({"Review_Mode_Achievement", 777}));
    quests["tasks"]["test_story_enter_mode"] =
        questfix::task("Test_Story_Enter_Mode", "taskunlockachievement", gamedata::task_type::kUnlockAchievement,
                       {{"GameModeID", kMode}, {"TargetQty", "1"}, {"ItemName", "Review_Mode_Achievement"}});
    StoryShop shop(":memory:", story_document(), quests);
    shop.enter();
    shop.enter_story(kCharacterA);
    auto& a = *shop.entity(kCharacterA);
    auto& b = *shop.entity(kCharacterB);
    check(a.space != world::kMainSpace && b.space == world::kMainSpace, "setup: A in its instance, B in the main world");
    for (auto* character : {&a, &b}) {
        check(shop.host.world.quests().grant(*character, kStoryObjective, "command", 0).ok(), "setup: the action of the mode");
    }
    shop.wait(0.5F);
    note("QG9BR3C inMode=" + std::to_string(task_progress(a, kStoryObjective)) + " mainWorld=" + std::to_string(task_progress(b, kStoryObjective)));
    for (auto* character : {&a, &b}) {
        check(task_progress(*character, kStoryObjective) == 1, "the achievement action completes in and outside its mode");
        check(std::count(character->achievements.begin(), character->achievements.end(), 777) == 1, "its achievement is set one time");
    }
}

void trigger_gate(const Options&) {
    constexpr const char* area = "slices/test/review_gate_area.dynamicslice";
    constexpr const char* hallwayEvent = "Review_Hallway_Complete";
    constexpr const char* pressureEvent = "Review_PressurePlate";
    constexpr const char* introEvent = "Review_Intro_Complete";
    constexpr const char* secondKey = "e4c00000-0000-4000-8000-0000000000f2/17";
    json document = document_of({stage_json("Hallway", 0xD00, 11, true, timed(1), json::array())}, {0});
    document["recipes"][0]["successEvent"] = hallwayEvent;
    json intro = document["recipes"][0];
    intro["key"] = secondKey;
    intro["successEvent"] = introEvent;
    intro["stages"] = {stage_json("Intro pressure plate", 0xD10, 12, true, destroyed(1, area),
                                  json::array({{{"member", area}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(kStoryStart)})}}}))};
    document["recipes"].push_back(intro);
    document["members"][area] = {{"kind", "object"}, {"variant", ""}, {"table", ""}, {"row", "Review_Plate"}, {"event", pressureEvent},
                                 {"tag", ""}, {"onlyOnQuest", false}, {"proximity", true}, {"interact", false}, {"radius", 10.0}};
    json quests = story_quests();
    json tree = json::array();
    unsigned index = 0;
    for (const auto* event : {hallwayEvent, pressureEvent, introEvent}) {
        const std::string name = "review_gate_" + std::to_string(index);
        quests["tasks"][name] = questfix::task(name.c_str(), "taskgameevent", gamedata::task_type::kGameEvent,
                                               {{"GameModeID", kMode}, {"KillEnemyType", event}, {"TargetQty", "1"}});
        tree.push_back(json::array({index++, name, json::array()}));
    }
    quests["tasks"]["review_gate_root"] = questfix::task("Review_Gate_Root", "consecutivetaskcontainer", gamedata::task_type::kConsecutiveContainer);
    tree.push_back(json::array({3, "review_gate_root", json::array({0, 1, 2})}));
    quests["objectives"].push_back({{"id", "Review_Gate"}, {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}}, {"tree", tree}});
    StoryShop shop(":memory:", document, quests);
    shop.enter();
    auto& a = *shop.entity(kCharacterA);
    auto& b = *shop.entity(kCharacterB);
    for (auto* character : {&a, &b}) {
        check(shop.host.world.quests().grant(*character, "Review_Gate", "command", 0).ok(), "setup: the sequential mode quest");
    }
    shop.enter_story(kCharacterA);
    shop.enter_story(kCharacterB);
    shop.wait(3.0F);
    for (auto* character : {&a, &b}) {
        const auto* quest = questfix::quest_of(*character, "Review_Gate");
        check(quest != nullptr && quest->tasks[0].progress == 1, "setup: the first recipe sent its success event");
        if (quest == nullptr) {
            return;
        }
        note("QG9BR4 " + character->characterId.substr(0, 8) + " hallway=" + std::to_string(quest->tasks[0].progress) + " pressure="
             + std::to_string(quest->tasks[1].progress) + " intro=" + std::to_string(quest->tasks[2].progress));
        check(quest->tasks[1].progress == 1 && quest->tasks[2].progress == 1,
              "the next recipe starts for its pressure plate task and completes the sequence");
    }
    check(a.space != b.space, "A and B are in two instances");
}

void area_controls(const Options&) {
    json quests = story_quests();
    const auto add = [&](const char* name, const char* mode) {
        quests["tasks"][gamedata::lower_text(name)] = questfix::task(
            name, "tasktriggerarea", 3, {{"GameModeID", mode}, {"TargetQty", "1"}, {"TP_TargetTag", "Review_Area"}, {"TriggerAreaType", "Enter"}});
        quests["objectives"].push_back({{"id", name},
                                        {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                        {"tree", json::array({json::array({0, gamedata::lower_text(name), json::array()})})}});
    };
    add("Review_Mode_Area", kMode);
    add("Review_Main_Area", "");
    add("Review_Other_Area", "Other_Mode");
    StoryShop shop(":memory:", story_document(), quests);
    gamedata::TriggerVolume volume;
    volume.tag = "review_area";
    volume.shape = gamedata::TriggerShape::sphere;
    volume.center = {kStoryStart.x, kStoryStart.y, kStoryStart.z};
    volume.radius = 5.0F;
    shop.host.triggers.set({volume});
    shop.enter();
    shop.enter_story(kCharacterA);
    shop.move(kCharacterB, kStoryStart);
    for (const auto* character : {kCharacterA, kCharacterB}) {
        auto& entity = *shop.entity(character);
        for (const auto* name : {"Review_Mode_Area", "Review_Main_Area", "Review_Other_Area"}) {
            check(shop.host.world.quests().grant(entity, name, "command", 0).ok(), "setup: a trigger task");
        }
    }
    shop.wait(0.5F);
    auto& a = *shop.entity(kCharacterA);
    auto& b = *shop.entity(kCharacterB);
    check(task_progress(a, "Review_Mode_Area") == 1 && task_progress(b, "Review_Mode_Area") == 0, "the mode trigger counts only A in its instance");
    check(task_progress(a, "Review_Main_Area") == 0 && task_progress(b, "Review_Main_Area") == 1, "the main-world task counts only B at the same place");
    check(task_progress(a, "Review_Other_Area") == 0 && task_progress(b, "Review_Other_Area") == 0, "a wrong mode counts neither character");
    json bad = story_recipe();
    bad["key"] = "e4c00000-0000-4000-8000-0000000000f3/17";
    bad["refused"] = {"Review: an unsupported normal recipe"};
    json document = story_document();
    document["recipes"].push_back(bad);
    StoryShop refused(":memory:", document);
    refused.enter();
    check(!refused.encounters().runs_mode(kMode), "one refused normal recipe refuses the whole mode");
    for (const auto* character : {kCharacterA, kCharacterB}) {
        refused.enter_story(character);
    }
    check(refused.instances().counts().entries == 0 && refused.encounters().instances().empty(),
          "both entrances refuse with no entry row and no part of an encounter");
}

} // namespace

std::vector<Case> review_qg9b_cases() {
    return {{"QG9BR1", "review QG9b (R1): a later wave does not count the deaths of an earlier sibling wave", sibling_waves},
            {"QG9BR2", "review QG9b (R2): an event stage waits for its required timed children", event_children},
            {"QG9BR3C", "review QG9b (R3 rejected): an achievement action with a GameModeID acts in and outside its mode", mode_actions},
            {"QG9BR4", "review QG9b (R4): the task of a start object event of the next recipe starts that recipe", trigger_gate},
            {"QG9BRC", "review QG9b: the controls of the trigger spaces and of a mode with a refused recipe", area_controls}};
}

} // namespace soulwarden::backend::tests

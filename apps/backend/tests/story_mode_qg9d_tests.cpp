// The cases of stage QG9d (docs/systems/instances.md, "The story instances of stage QG9d"): the quest objects of a mode root (the heartgems
// of QuestCaerDun) and the exit portal of a mode root that the client makes from the level data (the portal of QuestSeraphim). The
// accounts, characters, kinds, and places are made up.
//
//   QG9D2  the quest object of the mode root: A (an active task of the mode that names the event of the object) and B (no such task) enter
//          the mode, each in its own instance; the instance of A makes the object, the one of B does not; the encounter of each instance
//          waits; the gather of A gives its event to A (the task), the object goes, and the encounter of A starts; B keeps its wait
//   QG9D3  the exit portal of the mode root that the client makes (a level object with the mode, interaction-data.json): no server child
//          portal for a spawner with m_spawnOnInitialize; ExitDungeon of A in its instance of the mode: the leave to the return place; B
//          in the main world at the same portal: refused, no change; a level portal of an other mode in the instance of A: refused

#include <string>
#include <vector>

#include "support/story_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;

constexpr const char* kObjectsObjective = "Test_Story_Objects";
constexpr state::WorldPosition kQuestObjectAt{301.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kLevelPortalAt{302.0F, 302.0F, 30.0F};
constexpr std::uint8_t kLevelPortalTag = 0x0C;
constexpr std::uint8_t kOtherPortalTag = 0x0D;

/** The encounter data of the mode with a quest object of the mode root (the start object template, no spawn at the init). */
json objects_document() {
    json document = story_document();
    document["instanceObjects"].push_back({{"mode", kMode},
                                           {"map", kMode},
                                           {"key", "e4c00000-0000-4000-8000-0000000000c1/19"},
                                           {"slice", kStartSlice},
                                           {"spawner", "QuestObject"},
                                           {"position", {kQuestObjectAt.x, kQuestObjectAt.y, kQuestObjectAt.z}},
                                           {"yaw", 0.0},
                                           {"onInit", false}});
    return document;
}

/** The quests: the quest of the objects (the event of the object, then the kill of the boss), both with the GameModeID of the mode. */
json objects_quests() {
    json document = story_quests();
    document["tasks"]["test_story_place"] = questfix::task("Test_Story_Place", "taskgameevent", gamedata::task_type::kGameEvent,
                                                           {{"KillEnemyType", encfix::kStartEvent}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    document["tasks"]["test_story_objects_kill"] =
        questfix::task("Test_Story_Objects_Kill", "taskkillcontribution", gamedata::task_type::kKillContribution,
                       {{"KillEnemyType", "Test_Member_Category"}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    document["tasks"]["test_story_objects_root"] =
        questfix::task("Test_Story_Objects_Root", "consecutivetaskcontainer", gamedata::task_type::kConsecutiveContainer);
    document["objectives"].push_back({{"id", kObjectsObjective},
                                      {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                      {"tree", json::array({json::array({0, "test_story_place", json::array()}),
                                                            json::array({1, "test_story_objects_kill", json::array()}),
                                                            json::array({2, "test_story_objects_root", json::array({0, 1})})})}});
    return document;
}

void quest_objects(const Options&) {
    StoryShop shop(":memory:", objects_document(), objects_quests(), kMode, {}, {{kPortalSlice, 1}, {kStartSlice, 0}});
    shop.enter();
    world::Entity& a = *shop.entity(encfix::kCharacterA);
    world::Entity& b = *shop.entity(encfix::kCharacterB);
    check(shop.host.world.quests().grant(a, kObjectsObjective, "command", 0).ok(), "setup: A has the quest of the objects");
    shop.enter_story(encfix::kCharacterA);
    shop.enter_story(encfix::kCharacterB);
    shop.wait(1.0F);
    const world::Instance* instanceA = shop.instances().instance_of(encfix::kCharacterA);
    const world::Instance* instanceB = shop.instances().instance_of(encfix::kCharacterB);
    check(instanceA != nullptr && instanceB != nullptr && instanceA != instanceB, "setup: A and B each in an instance of the mode");
    if (instanceA == nullptr || instanceB == nullptr) {
        return;
    }
    check(instanceA->questObjects.size() == 1 && instanceA->questObjects[0].entity != 0,
          "A: the instance makes the quest object (a member has the task of its event)");
    check(instanceB->questObjects.size() == 1 && instanceB->questObjects[0].entity == 0, "B: no quest object (no member has its task)");
    check(instanceA->held && instanceB->held && shop.story_run(a.space) == nullptr && shop.story_run(b.space) == nullptr,
          "the encounter of each instance waits for the quest objects (A and B are in its detector)");
    world::Entity* object = instanceA->questObjects.empty() ? nullptr : shop.host.world.find(instanceA->questObjects[0].entity);
    check(object != nullptr && object->space == a.space, "A: the object is in the space of A");
    if (object == nullptr) {
        return;
    }
    const world::EntityId objectId = object->id;
    shop.gather(shop.clientA, EncounterShop::start_block(*object), 1500);
    shop.wait(1.0F);
    const world::QuestInstance* quest = questfix::quest_of(a, kObjectsObjective);
    note("QG9D2 placeProgress=" + std::to_string(quest != nullptr ? quest->tasks[0].progress : 99) + " made="
         + std::to_string(shop.instances().counts().questObjectsMade) + " gathered=" + std::to_string(shop.instances().counts().questObjectsGathered));
    check(quest != nullptr && quest->tasks[0].progress == 1 && quest->tasks[1].state == world::quest_state::kActive,
          "A: the gather gives the event to the task of A, and the kill task is active");
    check(shop.host.world.find(objectId) == nullptr && shop.instances().counts().questObjectsGathered == 1, "A: the object goes");
    check(shop.story_run(a.space) != nullptr && !shop.instances().instance_of(encfix::kCharacterA)->held,
          "A: the encounter of the instance of A starts (each quest object is gathered)");
    check(shop.story_run(b.space) == nullptr && shop.instances().instance_of(encfix::kCharacterB)->held,
          "B: the encounter of the instance of B still waits");
    check(questfix::quest_of(b, kObjectsObjective) == nullptr, "B: no quest and no event");
}

/** The block of ExitDungeon of a level object (the rule of entrance_block: the option entity G ^ 0x40). */
InteractionBlock level_exit_block(const gamedata::InteractionObject& object) {
    InteractionBlock block;
    block.actor = object.actor;
    block.group = object.group;
    block.target = object.bounds[0].target;
    block.action = 74;
    block.optionEntity = object.group ^ 0x40ULL;
    block.optionId = gamedata::interaction_option_id("ExitDungeon");
    block.text88 = "@Test_Story_Level_Portal";
    return block;
}

void level_exit_portal(const Options&) {
    json document = story_document();
    document["instanceObjects"][0]["onInit"] = true; // The client makes the portal (m_spawnOnInitialize of its spawner).
    gamedata::InteractionObject portal = object_of(kLevelPortalTag, InteractionKind::dungeonExit, kLevelPortalAt, {kExitIndex});
    portal.exitMode = kMode;
    gamedata::InteractionObject other = object_of(kOtherPortalTag, InteractionKind::dungeonExit, kLevelPortalAt, {kExitIndex});
    other.exitMode = "Test_Other_Mode";
    StoryShop shop(":memory:", document, story_quests(), kMode, {portal, other});
    shop.enter();
    world::Entity& a = *shop.entity(encfix::kCharacterA);
    world::Entity& b = *shop.entity(encfix::kCharacterB);
    const state::WorldPosition home = a.position;
    shop.enter_story(encfix::kCharacterA);
    const world::Instance* instance = shop.instances().instance_of(encfix::kCharacterA);
    check(instance != nullptr && a.space != world::kMainSpace, "setup: A is in an instance of the mode");
    if (instance == nullptr) {
        return;
    }
    check(instance->objects.empty(), "no server child portal for a portal that the client makes from the level data");
    // B in the main world at the same coordinates as the portal.
    shop.move(encfix::kCharacterB, {kLevelPortalAt.x - 1.0F, kLevelPortalAt.y, kLevelPortalAt.z});
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientB, level_exit_block(portal));
    shop.end_block(shop.clientB);
    const auto repliesB = replies(shop.clientB, fromB);
    check(!repliesB.empty() && repliesB.front().second == 2 && b.space == world::kMainSpace && shop.instances().counts().portalLeaves == 0,
          "B in the main world: the portal of the mode is refused (no instance of B), no change");
    // A at the portal in its instance: the portal of an other mode first, then the portal of its mode.
    shop.placeA = state::WorldPosition{kLevelPortalAt.x - 1.0F, kLevelPortalAt.y, kLevelPortalAt.z};
    shop.wait(0.5F);
    const std::size_t fromA = shop.clientA.got.size();
    shop.request(shop.clientA, level_exit_block(other));
    shop.end_block(shop.clientA);
    const auto repliesA = replies(shop.clientA, fromA);
    check(!repliesA.empty() && repliesA.front().second == 2 && a.space != world::kMainSpace,
          "A: the level portal of an other mode is refused in its instance");
    shop.request(shop.clientA, level_exit_block(portal));
    shop.end_block(shop.clientA);
    shop.wait(0.5F);
    note("QG9D3 portalLeaves=" + std::to_string(shop.instances().counts().portalLeaves) + " spaceA=" + std::to_string(a.space));
    check(a.space == world::kMainSpace && shop.instances().counts().portalLeaves == 1 && world::horizontal_distance(a.position, home) < 0.5F,
          "A: ExitDungeon at the portal of its mode: the leave to the return place");
    check(b.space == world::kMainSpace && shop.instances().instance_of(encfix::kCharacterB) == nullptr, "B: no change");
}

} // namespace

std::vector<Case> story_mode_qg9d_cases() {
    return {{"QG9D2", "stage QG9d: the quest objects of a mode root and the wait of its encounter (A and B in two instances)", quest_objects},
            {"QG9D3", "stage QG9d: the exit portal of a mode root that the client makes (A in the instance, B in the main world)",
             level_exit_portal}};
}

} // namespace soulwarden::backend::tests

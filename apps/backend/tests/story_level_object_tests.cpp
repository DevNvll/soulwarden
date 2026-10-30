// The case of stage DG2, part 5 (docs/systems/instances.md, "The level objects of the main world level in a story instance"): a level
// object of the main world data in an instance of a mode of the main world level, when an active task of the character with the
// GameModeID of the mode names its InteractTag (the statue of Taurus of 09A_M12_SaveTaurus in QuestMedusa). The accounts, characters,
// tags, and places are made up.
//
//   LV6  A (an active task of the mode with the tag of the object) and B (no such task) each in an instance of the mode at the object: the
//        request of A is not refused as an unknown object (the gather of the object begins), the request of B is refused (an unknown
//        object in an instance, no change); the same object in the main world keeps the rule of the main world (B there: no refusal as an
//        unknown object; InteractOnlyOnQuest refuses it with no task)

#include <string>
#include <vector>

#include "support/story_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;

constexpr const char* kStatueObjective = "Test_Story_Statue";
constexpr const char* kStatueTag = "IO_Test_Statue";
constexpr std::uint8_t kStatueObject = 0x0E;
constexpr state::WorldPosition kStatueAt{303.0F, 300.0F, 30.0F};

json statue_quests() {
    json document = story_quests();
    document["tasks"]["test_story_statue"] =
        questfix::task("Test_Story_Statue", "taskgameevent", gamedata::task_type::kGameEvent,
                       {{"KillEnemyType", "Test_Statue_Interact"}, {"TargetQty", "1"}, {"GameModeID", kMode}, {"InteractTag", kStatueTag}});
    document["objectives"].push_back({{"id", kStatueObjective},
                                      {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                      {"tree", json::array({json::array({0, "test_story_statue", json::array()})})}});
    return document;
}

/** The block of GatherOption of the statue (a level object: the rule of block_for, the option entity G ^ 0x40). */
InteractionBlock statue_block(const gamedata::InteractionObject& object) {
    InteractionBlock block;
    block.actor = object.actor;
    block.group = object.group;
    block.target = object.bounds[0].target;
    block.action = 14;
    block.baseAction = 14;
    block.optionEntity = object.group ^ 0x40ULL;
    block.committed = true;
    block.optionId = gamedata::interaction_option_id("GatherOption");
    block.text88 = "@Test_Statue";
    return block;
}

std::uint64_t unknown_objects(StoryShop& shop) {
    return shop.host.world.interactions().counts().verdicts[static_cast<std::size_t>(world::InteractionVerdict::unknownObject)];
}

void mode_level_object(const Options&) {
    gamedata::InteractionObject statue = object_of(kStatueObject, InteractionKind::gathering, kStatueAt, {kGatherOptionIndex});
    statue.bounds[0].tag = kStatueTag;
    statue.bounds[0].onlyOnQuest = true;
    StoryShop shop(":memory:", story_document(), statue_quests(), kMode, {statue});
    shop.enter();
    world::Entity& a = *shop.entity(encfix::kCharacterA);
    world::Entity& b = *shop.entity(encfix::kCharacterB);
    check(shop.host.world.quests().grant(a, kStatueObjective, "command", 0).ok(), "setup: A has the task of the statue (the mode, the tag)");
    // B in the main world at the statue first: the rule of the main world (InteractOnlyOnQuest with no task), not an unknown object.
    const state::WorldPosition startB = b.position;
    shop.move(encfix::kCharacterB, {kStatueAt.x - 1.0F, kStatueAt.y, kStatueAt.z});
    shop.wait(0.3F);
    std::uint64_t unknown = unknown_objects(shop);
    shop.request(shop.clientB, statue_block(statue));
    shop.end_block(shop.clientB);
    check(unknown_objects(shop) == unknown && b.space == world::kMainSpace, "B in the main world: the statue is a level object there");
    shop.move(encfix::kCharacterB, startB);
    shop.wait(0.3F);
    shop.enter_story(encfix::kCharacterA);
    shop.enter_story(encfix::kCharacterB);
    check(a.space != world::kMainSpace && b.space != world::kMainSpace && a.space != b.space, "setup: A and B each in an instance of the mode");
    shop.placeA = state::WorldPosition{kStatueAt.x - 1.0F, kStatueAt.y, kStatueAt.z};
    shop.placeB = state::WorldPosition{kStatueAt.x - 1.0F, kStatueAt.y, kStatueAt.z};
    shop.move(encfix::kCharacterA, shop.placeA);
    shop.move(encfix::kCharacterB, shop.placeB);
    shop.wait(0.5F);
    unknown = unknown_objects(shop);
    shop.request(shop.clientB, statue_block(statue));
    shop.end_block(shop.clientB);
    check(unknown_objects(shop) == unknown + 1 && shop.host.world.interactions().counts().modeLevelObjects == 0,
          "B in its instance with no task of the mode: the level object is unknown there (no change)");
    unknown = unknown_objects(shop);
    shop.request(shop.clientA, statue_block(statue));
    shop.end_block(shop.clientA);
    note("LV6 mode level objects " + std::to_string(shop.host.world.interactions().counts().modeLevelObjects) + ", unknown objects "
         + std::to_string(unknown_objects(shop)));
    check(unknown_objects(shop) == unknown && shop.host.world.interactions().counts().modeLevelObjects == 1,
          "A in its instance with the task of the mode that names the tag: the level object of the main world is there");
}

} // namespace

std::vector<Case> story_level_object_cases() {
    return {{"LV6", "level data DG2 (part 5): a level object of the main world that a task of the mode names, in an instance of the mode",
             mode_level_object}};
}

} // namespace soulwarden::backend::tests

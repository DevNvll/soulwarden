// Stage QG9e (docs/systems/instances.md, "The solo story modes of an own level (stage QG9e)"; docs/systems/encounters.md): the instance
// recipes of the mode root of a solo mode with a level of its own. The recipes, the member kinds, and the templates of the objects come from
// the data set of the level (gamedata::LevelData: encounter-data.json, creatures.json, interaction-data.json); the runner takes them at the
// first read of the set (Encounters::add_level) and starts them only in a space of that level. The ids, the levels, and the places are made
// up.
//
//   QG9E1  two instances of one own level with its encounter (A and B) and a character of the main world (B before its entry; MP-10): the
//          entry at the entrance (the set read one time), the recipe of the set in each space with its boss of the member kind of the set
//          (the creature data of the backend start has no kind of that slice), no run and no record for the character of the main world
//          at the same coordinates, the boss of A not in the space of B, the kill of the boss of A: the success of the content of A only,
//          the run of B goes on, the exit portal of the set in each space
//   QG9E2  the rules of the modes that run (Instances::mode_runs): a solo mode of an own level with no set, and with a recipe that the runner
//          refuses, is refused (10, no move, no row); a solo mode of an own level is not an expedition (IsDungeon 1 too: no entry with the
//          set alone); an expedition with a set still runs with the set alone (stage DG3)
//   QG9E3  a WaitForParentObjective child stage of an instance recipe before an other child (the form of `01 - Gameplay` of QuestAdiana):
//          the recipe is taken, the child runs in the background, the parent starts its next child at once, the kill ends the fight
//   QG9E4  the dungeon leave of the escape menu (A and B in the main world, then each one in its own instance; MP-10): no PlayerArena
//          fragment before the first membership; the owner gets isInArena true in a dungeon mode, an observer nothing; 926 of a character in
//          no mode, with leaveDungeon 0, or with the entity of an other character changes nothing; 926 with leaveDungeon 1 leaves the instance of A only (B
//          stays in its own instance), and then isInArena false; a mode with no IsDungeon: isInArena false and 926 changes nothing

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gamedata/level_data.h"
#include "server/rep/instance_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/story_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace storyfix;

constexpr const char* kLevelMode = "Test_Level_Story";
constexpr const char* kStoryLevel = "nw_test_story_level";
constexpr const char* kStoryTerrain = "coatlicue/nw_test_story_level";
constexpr const char* kLevelMemberSlice = "slices/test/level_member.dynamicslice";
constexpr const char* kLevelSuccess = "Test_Level_Story_Success";

/** The member kind of the set (a slice that the creature data of the backend start does not have). */
gamedata::CreatureKind level_kind() {
    gamedata::CreatureKind kind = bags::bag_wolf_kind();
    kind.id = "level_test_member";
    kind.vitals = "Test_Level_Member";
    kind.category = "Test_Member_Category";
    kind.vitalsId = 0x7E570011;
    kind.categoryId = 0x7E570002;
    kind.member = kLevelMemberSlice;
    kind.points = 0;
    return kind;
}

/**
 * The recipe of the set: the form of the story recipe (setup, intro, fight, cleanup) with the member of the set and a success event.
 * `waitChild`: the fight has a WaitForParentObjective child before its event child (the form of `01 - Gameplay` of QuestAdiana).
 */
json level_recipe(bool refused, bool waitChild = false) {
    json recipe = story_recipe();
    if (waitChild) {
        recipe["stages"][2]["children"] = {5, 4};
        recipe["stages"].push_back(stage_json("00 - Intro_Wait", 0xA00, 11, true, objective_of("WaitForParentObjective", 0, false), json::array()));
    }
    recipe["key"] = "e4c00000-0000-4000-8000-0000000000c1/21";
    recipe["mode"] = kLevelMode;
    recipe["map"] = kLevelMode;
    recipe["successEvent"] = kLevelSuccess;
    recipe["stages"][0]["spawns"][1]["member"] = kLevelMemberSlice;
    recipe["stages"][2]["objective"]["trackedSlice"] = kLevelMemberSlice;
    if (refused) {
        recipe["refused"] = json::array({"a made-up cause of the tool"});
    }
    return recipe;
}

json level_document(bool refused, bool waitChild = false) {
    json document = encounter_document({level_recipe(refused, waitChild)});
    document["format"] = 3;
    document["members"][kLevelMemberSlice] = {{"kind", "creature"},
                                              {"vitals", "Test_Level_Member"},
                                              {"category", "Test_Member_Category"},
                                              {"level", 20},
                                              {"body", "slices/test/bag-wolf.dynamicslice"},
                                              {"bodySlices", {"slices/test/bag-wolf.dynamicslice"}}};
    document["instanceObjects"] = json::array({{{"mode", kLevelMode},
                                                {"map", kLevelMode},
                                                {"key", "e4c00000-0000-4000-8000-0000000000d1/22"},
                                                {"slice", kPortalSlice},
                                                {"spawner", "Portal_End"},
                                                {"position", {kPortalAt.x, kPortalAt.y, kPortalAt.z}},
                                                {"yaw", 180.0},
                                                {"onInit", false}}});
    document["level"] = kStoryLevel;
    return document;
}

/** The set of the level in the memory: the member kind, the exit portal template, and the recipe (`withRecipe` false: no encounter part). */
std::unique_ptr<gamedata::LevelData> story_level_set(bool withRecipe, bool refused, bool waitChild = false) {
    auto data = std::make_unique<gamedata::LevelData>(kStoryLevel);
    data->accept_for_test(kStoryTerrain);
    if (!data->creatures().set_full({bags::bag_wolf_slice()}, {level_kind()}, {}, {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
        throw std::runtime_error("story level test: the creature data of the level was not set: " + data->creatures().error());
    }
    data->creatures().set_members({{kLevelMemberSlice, {0, ""}}});
    gamedata::InteractionTemplate portal;
    portal.slice = kPortalSlice;
    portal.kind = InteractionKind::dungeonExit;
    portal.asset = kPortalAsset;
    portal.sub = 2;
    portal.root = kPortalRoot;
    portal.positionSlot = 2;
    gamedata::InteractionBound bound;
    bound.slot = 4;
    bound.component = kPortalComponent;
    bound.rangeSq = 9.0F;
    bound.arc = 180.0F;
    bound.entity = kPortalBound;
    bound.options = {{0, kPortalOption}};
    portal.bounds.push_back(bound);
    if (!data->interactions().set({option_row("ExitDungeon", 74, false)}, {}, {}, {portal})) {
        throw std::runtime_error("story level test: the interaction data of the level was not set: " + data->interactions().error());
    }
    data->interactions().set_instance_objects({{kPortalSlice, 0}});
    if (withRecipe && !data->encounters().load_text(level_document(refused, waitChild).dump())) {
        throw std::runtime_error("story level test: the encounter data of the level did not load: " + data->encounters().error());
    }
    data->mark_parts_for_test();
    return data;
}

gamedata::GameMap level_map(bool solo = true) {
    gamedata::GameMap map = story_map(0, 600, kLevelMode);
    map.level = kStoryLevel;
    map.terrain = kStoryTerrain;
    map.dungeon = true;  // The solo story modes of an own level have IsDungeon 1 too [data: QuestLiang, QuestAdiana].
    map.soloTrial = solo;
    return map;
}

/** The story fixture with a solo mode of an own level: its map, its entrance near A and B, its set in the memory. */
struct LevelStoryShop : StoryShop {
    explicit LevelStoryShop(bool withSet = true, bool withRecipe = true, bool refused = false, bool solo = true, bool waitChild = false)
        : StoryShop(":memory:", story_document(), story_quests(), kLevelMode) {
        if (withSet) {
            host.levels.made[kStoryLevel] = [withRecipe, refused, waitChild] { return story_level_set(withRecipe, refused, waitChild); };
        }
        check(host.gameModes.set({level_map(solo)}), "setup: the map of the solo mode of an own level");
    }

    [[nodiscard]] std::uint32_t last_result(std::size_t from) const {
        const std::vector<DungeonResult> results = dungeon_results(clientA, from);
        return results.empty() ? 9999U : results.back().result;
    }

    [[nodiscard]] std::size_t portals(world::SpaceId space) {
        std::size_t count = 0;
        host.world.for_each_near(space, kPortalAt, 5.0F, [&](world::Entity& entity) {
            count += entity.kind == world::EntityKind::object ? 1 : 0;
        });
        return count;
    }
};

/** QG9E1 */
void two_level_instances(const Options&) {
    LevelStoryShop shop;
    shop.enter();
    // B in the main world at the coordinates of the boss of the level: the recipe of the set never runs there.
    shop.move(kCharacterB, kStoryStart);
    shop.wait(1.0F);
    check(shop.encounters().instances().empty() && root_seen(shop.clientB).interest == 0,
          "a character of the main world at the coordinates of the recipe of the level: no run, no record");
    shop.move(kCharacterB, state::WorldPosition{101.0F, 200.0F, 30.0F});
    const std::size_t fromA = shop.clientA.got.size();
    shop.enter_story(kCharacterA);
    shop.wait(1.0F);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::Instance* instanceA = shop.instances().instance_of(kCharacterA);
    check(shop.last_result(fromA) == 0 && instanceA != nullptr && a.space != world::kMainSpace && instanceA->level.level == kStoryLevel,
          "the entry at the entrance of a solo mode of an own level: 3094 with 0, the space of the level");
    const unsigned readsAfterA = shop.host.levels.reads;
    const world::EncounterInstance* runA = shop.story_run(a.space);
    world::Entity* bossA = shop.boss(a.space);
    const gamedata::CreatureKind* mainKind = shop.host.creatures.loaded()
                                                 ? (shop.host.creatures.member_kind(kLevelMemberSlice)
                                                        ? &shop.host.creatures.kinds()[*shop.host.creatures.member_kind(kLevelMemberSlice)]
                                                        : nullptr)
                                                 : nullptr;
    check(runA != nullptr && bossA != nullptr && bossA->space == a.space && bossA->creature.kind != nullptr
              && bossA->creature.kind->id == "level_test_member" && mainKind == nullptr,
          "the recipe of the set runs in the space of A, with its boss of the member kind of the set (the main data has no such kind)");
    check(shop.portals(a.space) == 1, "the exit portal of the mode root of the set (its template in the catalog of the set) in the space of A");
    // B enters too: a second instance of the level, with its own run and boss; the set is not read again.
    shop.enter_story(kCharacterB);
    shop.wait(1.0F);
    world::Entity& b = *shop.entity(kCharacterB);
    const world::Instance* instanceB = shop.instances().instance_of(kCharacterB);
    world::Entity* bossB = shop.boss(b.space);
    check(instanceB != nullptr && instanceA != nullptr && instanceB->id != instanceA->id && b.space != a.space && bossB != nullptr
              && bossB != bossA && bossB->space == b.space && shop.encounters().instances().size() == 2 && shop.host.levels.reads == readsAfterA,
          "two instances of the level: two spaces, two runs, two bosses, one read of the set (MP-10)");
    std::size_t seenByB = 0;
    shop.host.world.for_each_near(b.space, kBossAt, 30.0F, [&](world::Entity& entity) { seenByB += entity.id == bossA->id ? 1 : 0; });
    check(seenByB == 0 && shop.portals(b.space) == 1, "the boss of A is not in the space of B; B has the portal of its own space");
    // The intro of A ends at its time; the kill of the boss of A ends the run of A only.
    shop.wait(21.0F);
    if (bossA == nullptr || shop.boss(a.space) == nullptr) {
        check(false, "setup: the boss of A");
        return;
    }
    shop.kill(*shop.boss(a.space), kCharacterA);
    shop.wait(2.0F);
    const world::Instance* doneA = shop.instances().instance_of(kCharacterA);
    const world::Instance* stillB = shop.instances().instance_of(kCharacterB);
    note("QG9E1 runs=" + std::to_string(shop.encounters().instances().size()) + " succeededA=" + std::to_string(doneA != nullptr && doneA->succeeded)
         + " succeededB=" + std::to_string(stillB != nullptr && stillB->succeeded));
    check(shop.story_run(a.space) == nullptr && doneA != nullptr && doneA->succeeded && shop.story_run(b.space) != nullptr && stillB != nullptr
              && !stillB->succeeded && shop.boss(b.space) != nullptr,
          "the kill of the boss of A: the success of the content of A; the run and the boss of B go on");
}

/** QG9E2 */
void level_mode_rules(const Options&) {
    struct Case {
        bool withSet;
        bool withRecipe;
        bool refused;
        const char* what;
    };
    for (const Case& item : {Case{false, false, false, "a solo mode of an own level with no data set"},
                             Case{true, false, false, "a solo mode of an own level whose set has no instance recipe (the set alone)"},
                             Case{true, true, true, "a solo mode of an own level whose recipe the runner refuses"}}) {
        LevelStoryShop shop(item.withSet, item.withRecipe, item.refused);
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::size_t from = shop.clientA.got.size();
        shop.enter_story(kCharacterA);
        check(shop.last_result(from) == 10 && a.space == world::kMainSpace && shop.instances().instance_of(kCharacterA) == nullptr
                  && shop.host.store.list_instance_members().empty(),
              std::string(item.what) + ": refused with 10, no move, no row");
    }
    // An expedition with a set and no recipe (IsSoloTrial 0) still enters with the set alone (stage DG3; its encounters are stage DG5).
    LevelStoryShop expedition(true, false, false, false);
    expedition.enter();
    const std::size_t from = expedition.clientA.got.size();
    expedition.enter_story(kCharacterA);
    check(expedition.last_result(from) == 0 && expedition.entity(kCharacterA)->space != world::kMainSpace,
          "an expedition of an own level with an accepted set enters with the set alone (stage DG3)");
}

/** QG9E3 */
void waiting_child(const Options&) {
    LevelStoryShop shop(true, true, false, true, true);
    shop.enter();
    const std::size_t from = shop.clientA.got.size();
    shop.enter_story(kCharacterA);
    shop.wait(1.0F);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::EncounterInstance* run = shop.story_run(a.space);
    check(shop.last_result(from) == 0 && run != nullptr && run->stages.size() == 6,
          "a recipe whose fight has a WaitForParentObjective child before an other child is taken (the entry, the run)");
    if (run == nullptr) {
        return;
    }
    shop.wait(21.0F);
    run = shop.story_run(a.space);
    check(run != nullptr && run->stages[2].started && !run->stages[2].ended && run->stages[5].started && !run->stages[5].ended
              && run->stages[4].started && run->stages[4].ended,
          "the waiting child runs in the background, and the fight starts its next child (the event, met at its start) at once");
    world::Entity* boss = shop.boss(a.space);
    if (boss == nullptr) {
        check(false, "setup: the boss");
        return;
    }
    shop.kill(*boss, kCharacterA);
    shop.wait(2.0F);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(shop.story_run(a.space) == nullptr && instance != nullptr && instance->succeeded,
          "the kill of the boss ends the fight (and its waiting child), the cleanup, the success");
}

/** The PlayerArena component of the player slice (instance_messages.h): the facet target of a message is the entity id plus this id. */
constexpr std::uint64_t kArenaComponent = 0x0ADF8519A10C374DULL;

/** @return The isInArena values of the PlayerArena fragments (slot 4, type 2406) of a client from `from`: 1, 0, or -1 (a fragment of an other form). */
std::vector<int> arena_values(const FakeClient& client, std::size_t from = 0) {
    std::vector<int> values;
    for (const Seen& seen : fragments_of(client, server::rep::kPlayerArenaSlot, server::rep::kTypePlayerArenaState, from)) {
        const auto field = seen.fragment.fields.find({0, 0});
        const bool one = field != seen.fragment.fields.end() && field->second.size() == 1 && seen.fragment.fields.size() == 1;
        values.push_back(one ? field->second[0] : -1);
    }
    return values;
}

/** Sends 926 (ForfeitArena) of a character with the flag leaveDungeon, to the PlayerArena component of the entity `entity` (0: its own). */
void forfeit(StoryShop& shop, const char* character, std::uint8_t leaveDungeon, std::uint64_t component = kArenaComponent,
             world::EntityId entity = 0) {
    const world::Entity* own = shop.entity(character);
    FakeClient& client = std::string(character) == kCharacterA ? shop.clientA : shop.clientB;
    const world::EntityId named = entity != 0 ? entity : own->id;
    server::hub::Writer body;
    body.u64(named + component);
    body.u64(named);
    body.u8(leaveDungeon);
    shop.host.datagram(client.peer, client.component_message(server::rep::kTypeForfeitArena, own->actor.bytes, body.data()));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
}

/** QG9E4 */
void dungeon_leave(const Options&) {
    StoryShop shop;
    shop.enter();
    shop.wait(0.5F);
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    const state::WorldPosition home = a.position;
    check(arena_values(shop.clientA).empty() && arena_values(shop.clientB).empty(), "no PlayerArena fragment before the first membership");
    // B in no mode: 926 changes nothing.
    forfeit(shop, kCharacterB, 1);
    check(b.space == world::kMainSpace && shop.instances().counts().requestLeaves == 0, "926 of a character in no mode: no change");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.enter_story(kCharacterA);
    check(arena_values(shop.clientA, fromA) == std::vector<int>{1} && a.gameModeMembership && a.gameModeMembership->dungeon,
          "the owner in a dungeon mode gets isInArena true (`01 01 01`)");
    check(arena_values(shop.clientB, fromB).empty(), "the observer B gets no PlayerArena fragment of A");
    shop.enter_story(kCharacterB);
    const world::SpaceId spaceB = b.space;
    check(spaceB != world::kMainSpace && spaceB != a.space, "setup: B is in its own instance of the mode");
    // 926 with leaveDungeon 0, and 926 that names the entity of B: no change. (The test host has no player slice: the facet check of
    // the component is the check of the entity.)
    forfeit(shop, kCharacterA, 0);
    check(a.space != world::kMainSpace, "926 with leaveDungeon 0: no leave");
    forfeit(shop, kCharacterA, 1, kArenaComponent, b.id);
    check(a.space != world::kMainSpace && b.space == spaceB, "926 that names the entity of an other character: no leave");
    const std::size_t beforeLeave = shop.clientA.got.size();
    forfeit(shop, kCharacterA, 1);
    shop.follow(kCharacterA);
    check(a.space == world::kMainSpace && world::horizontal_distance(a.position, home) < 0.5F && shop.instances().counts().requestLeaves == 1,
          "926 with leaveDungeon 1: the leave of A to its return place");
    check(b.space == spaceB && shop.instances().instance_of(kCharacterB) != nullptr, "B stays in its own instance (MP-10)");
    const std::vector<int> after = arena_values(shop.clientA, beforeLeave);
    check(!after.empty() && after.back() == 0, "after the leave the owner gets isInArena false (`01 01 00`)");
    // A mode with no IsDungeon: isInArena false, and 926 changes nothing.
    gamedata::GameMap plain = story_map();
    plain.dungeon = false;
    check(shop.host.gameModes.set({plain}), "setup: the mode has no IsDungeon");
    const std::size_t plainFrom = shop.clientA.got.size();
    shop.enter_story(kCharacterA);
    const std::vector<int> plainValues = arena_values(shop.clientA, plainFrom);
    check(a.space != world::kMainSpace && !plainValues.empty() && plainValues.back() == 0 && a.gameModeMembership && !a.gameModeMembership->dungeon,
          "a mode with no IsDungeon: the membership, and isInArena false");
    forfeit(shop, kCharacterA, 1);
    check(a.space != world::kMainSpace && shop.instances().counts().requestLeaves == 1, "926 in a mode with no IsDungeon: no leave");
}

} // namespace

/**
 * QG9ER1 (code review of stage QG9e, finding QG9ER-R1; stage BT3): no main encounter file. The level set of the mode has a valid recipe: its
 * encounter runs in the instance of A (the tick runs from the recipes that the runner took), and the support manifest counts its success
 * event (the event walk of QuestSupport::build does not need the main file). B stays in the main world.
 */
void missing_main_encounters(const Options&) {
    json quests = story_quests();
    quests["tasks"]["test_level_story_done"] = questfix::task("Test_Level_Story_Done", "taskgameevent", gamedata::task_type::kGameEvent,
                                                              {{"KillEnemyType", kLevelSuccess}, {"TargetQty", "1"}});
    quests["objectives"].push_back({{"id", "Test_Level_Done"},
                                    {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                    {"tree", json::array({json::array({0, "test_level_story_done", json::array()})})}});
    LevelStoryShop shop;
    check(shop.host.quests.load_text(quests.dump()), "setup: a quest task of the success event of the level recipe");
    shop.host.encounters = gamedata::EncounterData{};
    shop.enter();
    check(shop.encounters().data() == nullptr, "setup: no main encounter file");
    shop.enter_story(kCharacterA);
    shop.wait(2.0F);
    const world::Entity* a = shop.entity(kCharacterA);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(a != nullptr && instance != nullptr && a->space != world::kMainSpace && instance->content && shop.encounters().counts().taken == 1,
          "setup: the entry is accepted, the content of the level set is registered, and the runner takes its recipe");
    note("QG9ER1 scans=" + std::to_string(shop.encounters().counts().scans) + " runs=" + std::to_string(shop.encounters().instances().size())
         + " boss=" + std::to_string(a != nullptr && shop.boss(a->space) != nullptr));
    check(a != nullptr && shop.story_run(a->space) != nullptr && shop.boss(a->space) != nullptr,
          "the recipe of the level set runs with no main encounter file (the scans, the run, its boss)");
    check(shop.entity(kCharacterB)->space == world::kMainSpace, "B stays in the main world");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    bool supported = false;
    std::string reason;
    for (const json& entry : manifest["objectives"]) {
        if (entry["id"] == "Test_Level_Done") {
            supported = entry["tasks"][0].value("supported", false);
            reason = entry["tasks"][0].value("reason", std::string());
        }
    }
    check(supported, "the support manifest counts the success event of the level recipe with no main encounter file (" + reason + ")");
}

std::vector<Case> story_level_cases() {
    return {{"QG9E1", "stage QG9e: two instances of a solo mode of an own level with the recipe of its set, and the main world (MP-10)",
             two_level_instances},
            {"QG9E2", "stage QG9e: the rules of the modes that run for a solo mode of an own level", level_mode_rules},
            {"QG9E3", "stage QG9e: a WaitForParentObjective child stage of an instance recipe runs in the background", waiting_child},
            {"QG9E4", "stage QG9e: the dungeon leave of the escape menu (PlayerArena, 926) with two characters (MP-10)", dungeon_leave},
            {"QG9ER1", "review of QG9e (QG9ER-R1): a level recipe runs and counts in the manifest with no main encounter file", missing_main_encounters}};
}

} // namespace soulwarden::backend::tests

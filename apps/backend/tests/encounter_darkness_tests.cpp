// The Darkness events and the boat reinforcements of the encounter runner (stage QG5c5, docs/systems/encounters.md): the child stages that
// wait for their parent (WaitForParentObjective), the DestroySliceAction, the group members (one creature for each part, at its place, with
// its path; the parts that the client does not show), the spawns of an alias (the pylons, the cores), the slices of a tracked alias, the
// members of an earlier stage that a later stage tracks (the spyglass), and the season rule. Made-up slices, kinds, places, recipes, quests,
// and accounts (the encounter fixtures of tests/support/encounter_fixture.h and encounter_pattern_fixture.h).
//
//   QG5C5A  the file: the parts of a group, the alias of a spawn, the slices of a tracked alias, the DestroySliceAction, the
//           WaitForParentObjective, the season; the runner takes the Darkness form (the season rule), and refuses a WaitForParentObjective
//           stage of the manager and one before an other child stage
//   QG5C5B  (I1 to I4, I8) the Darkness form with two participants: the pylons of an alias (one gather each), the sustain child ends with
//           its parent (both observers), the group of the attack (2 parts made at their places, 1 hidden part counted at the start, the path
//           of a part), the boss, the cleanup (KillSpawnsAction, DestroySliceAction), the success event for both
//   QG5C5C  (I8) a nearby character that does not take part (35 m: no credit, no link) and a late observer (the current stage states)
//   QG5C5D  (I5, I8) the boat form: the start object of the timed setup that the next stage tracks (the spyglass), the timed boat stage,
//           the landing group (2 parts on their paths, turned by the yaw of the spawn location), the success for both participants
//   QG5C5E  (I9) the load: 4,001 Darkness placements with group members; a scan looks only near the characters (time limit)
//   QG5C5F  (I7) the support manifest: the success event of the Darkness form has a producer, and the kinds of the parts are kill sources

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "support/encounter_pattern_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using questfix::json;

constexpr const char* kGroupSlice = "slices/test/darkness_ai_test_wave.dynamicslice";
constexpr const char* kPylonAlias = "sharedassets/test/alias_test_pylon.aliasasset";
constexpr const char* kGroupAlias = "sharedassets/test/alias_test_wave.aliasasset";
constexpr const char* kHazardAlias = "sharedassets/test/alias_test_hazard.aliasasset";
constexpr const char* kWolfBody = "slices/test/bag-wolf.dynamicslice";
constexpr const char* kDarknessSlice = "slices/darkness/events/pois/enc_darkness_test_monolith.dynamicslice";

/** A part of the group member: its place in the member slice, its yaw, and (for the first part) a path of two waypoints. */
json part_json(const char* name, float x, float y, bool route) {
    json part{{"name", name},   {"vitals", "Test_Member"},      {"category", "Test_Member_Category"}, {"body", kWolfBody},
              {"bodySlices", {kWolfBody}}, {"offset", {x, y, 0.0}}, {"yaw", 90.0}};
    if (route) {
        part["route"] = {{"policy", 1}, {"points", json::array({json::array({x + 4.0, y, 0.0, 0.0, 1.0, 2.0, "", 0}),
                                                               json::array({x + 8.0, y, 0.0, 0.0, -1.0, -1.0, "", 0})})}};
    }
    return part;
}

json darkness_members() {
    json members = pattern_members();
    members[kGroupSlice] = {{"kind", "creature"},
                            {"vitals", "Test_Member"},
                            {"category", "Test_Member_Category"},
                            {"level", 20},
                            {"body", kWolfBody},
                            {"bodySlices", {kWolfBody}},
                            {"parts", 3},
                            {"group", json::array({part_json("Corrupted_Entity", 1.0, 0.0, true), part_json("Corrupted_Impaler", 0.0, 2.0, false),
                                                   part_json("Corrupted_Swarmer", -1.0, 0.0, false)})}};
    return members;
}

json alias_spawn(const char* member, const char* alias, const state::WorldPosition& at) {
    json spawn = spawn_json(member, 0, 1, at);
    spawn["alias"] = alias;
    return spawn;
}

json tracked_alias(std::uint32_t count, const char* alias, json slices) {
    json objective = destroyed(count);
    objective["trackedAlias"] = alias;
    objective["trackedSlices"] = std::move(slices);
    return objective;
}

json wait_parent() {
    return {{"type", "WaitForParentObjective"}, {"timeLimit", 0}, {"succeedOnTimeout", false}, {"failureCondition", false}};
}

/**
 * The form of a Darkness monolith: a timed setup (a visual of an alias), the pylons (two start objects of an alias, tracked by the alias;
 * a sustain child that waits for its parent), the carapace (the boss; children: an attack of a group of three parts, one of them hidden,
 * tracked by an alias, and a sustain child), and a cleanup (a KillSpawnsAction and a DestroySliceAction of the alias of the visual).
 */
json darkness_recipe(const char* key = kKey, const state::WorldPosition& root = kRootAt) {
    const state::WorldPosition pylonA{root.x, root.y - 4.0F, root.z};
    const state::WorldPosition pylonB{root.x + 4.0F, root.y, root.z};
    const state::WorldPosition fight{root.x + 3.0F, root.y + 1.0F, root.z};
    json setup = stage_json("Wave_00 - Spawn: Setup Master", 0x100, kSetupSlot, false, timed(3),
                            json::array({alias_spawn(kVisualSlice, kHazardAlias, root)}));
    json pylons = stage_json("Wave_01 - Objective: Destroy the Pylons", 0x200, kInteractSlot, true, tracked_alias(2, kPylonAlias, json::array()),
                             json::array({alias_spawn(kStartSlice, kPylonAlias, pylonA), alias_spawn(kStartSlice, kPylonAlias, pylonB)}));
    pylons["children"] = {4};
    pylons["childrenOptional"] = true;
    json carapace = stage_json("Wave_02 - Objective: Damage Carapace", 0x300, kKillSlot, true, destroyed(1, kBossSlice),
                               json::array({spawn_json(kBossSlice, 0, 1, fight)}));
    carapace["children"] = {5, 6};
    carapace["childrenOptional"] = true;
    json cleanup = stage_json("Wave_04 - Cleanup: Monolith", 0x400, kCleanupSlot, false, timed(3), json::array());
    cleanup["startActions"] = json::array({{{"type", "KillSpawnsAction"}, {"localOnly", false}},
                                           {{"type", "DestroySliceAction"}, {"slice", nullptr}, {"alias", kHazardAlias},
                                            {"aliasSlices", {kVisualSlice}}, {"localOnly", false}}});
    json sustainA = stage_json("01_Sustain", 0x500, kChildSlotA, false, wait_parent(), json::array({spawn_json(kMemberSlice, 0, 1, fight)}));
    json attack = stage_json("01_Attack", 0x600, kChildSlotB, false, tracked_alias(3, kGroupAlias, json::array({kWolfBody})),
                             json::array({spawn_json(kGroupSlice, 0, 1, fight)}));
    json sustainB = stage_json("02_Sustain", 0x700, 11, false, wait_parent(), json::array({spawn_json(kMemberSlice, 0, 1, fight)}));
    json recipe = pattern_recipe(key, root, json::array({setup, pylons, carapace, cleanup, sustainA, attack, sustainB}), {0, 1, 2, 3});
    recipe["slice"] = kDarknessSlice;
    recipe["pattern"] = "a seasonal event (the Darkness)";
    recipe["season"] = "darkness";
    recipe["successEvent"] = kSuccessEvent;
    recipe["questEvents"] = {gamedata::lower_text(kSuccessEvent)};
    return recipe;
}

/** The world with the Darkness form: the kinds of the parts of the group (the third part: the client does not show it). */
struct DarknessShop : PatternShop {
    explicit DarknessShop(const std::vector<json>& recipes, const std::string& database = ":memory:")
        : PatternShop(recipes, darkness_members(), database) {
        host.creatures.set_members({{kMemberSlice, {1, ""}},
                                    {kBossSlice, {1, ""}},
                                    {kHiddenSlice, {-1, "the client does not show its slice"}},
                                    {kGroupSlice, {1, ""}},
                                    {std::string(kGroupSlice) + "#0", {1, ""}},
                                    {std::string(kGroupSlice) + "#1", {1, ""}},
                                    {std::string(kGroupSlice) + "#2", {-1, "the client does not show its slice corruption_swarmer_dt6"}}});
    }
};

void darkness_file(const Options&) {
    DarknessShop shop({darkness_recipe()});
    const gamedata::EncounterData& data = shop.host.encounters;
    check(data.loaded() && data.recipes().size() == 1, "the file with the Darkness form loads");
    if (!data.loaded() || data.recipes().empty()) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = data.recipes()[0];
    const gamedata::EncounterMember* group = data.member(kGroupSlice);
    check(group != nullptr && group->parts.size() == 3 && group->parts[0].name == "Corrupted_Entity" && group->parts[0].offset[0] == 1.0F
              && group->parts[0].yaw == 90.0F && group->parts[0].route.points.size() == 2
              && group->parts[0].route.policy == gamedata::PatrolPolicy::bounce && group->parts[0].route.points[0].minWait == 1.0F
              && group->parts[1].route.points.empty(),
          "a group member has its parts with their places, their yaw, and the path of a part");
    check(recipe.season == "darkness" && recipe.stages[1].spawns[0].alias == gamedata::lower_text(kPylonAlias)
              && recipe.stages[1].objective.tracked == gamedata::lower_text(kPylonAlias)
              && recipe.stages[5].objective.trackedSlices == std::vector<std::string>{kWolfBody},
          "the season, the alias of a spawn, and the slices of a tracked alias");
    check(recipe.stages[4].objective.kind == gamedata::EncounterObjectiveKind::waitParent && recipe.stages[4].parent == 1
              && recipe.stages[3].destroyAtStart.size() == 1 && recipe.stages[3].destroyAtStart[0].alias == gamedata::lower_text(kHazardAlias)
              && recipe.stages[3].killSpawnsAtStart,
          "the WaitForParentObjective of a child stage and the DestroySliceAction of the cleanup");
    shop.enter();
    shop.wait(0.5F);
    const world::EncounterPlacement& placement = shop.encounters().placements()[0];
    check(world::encounter_rules::kDarknessAtAllTimes && placement.refused.empty(),
          "the season rule: the runner takes the Darkness form (" + (placement.refused.empty() ? std::string("taken") : placement.refused.front())
              + ")");
    check(placement.partKinds.size() == data.members().size() && placement.fills[5].size() == 1 && placement.fills[5][0] == 1,
          "the attack needs 3 parts: the 2 parts that the runner makes and 1 hidden part (its fill)");
    // A WaitForParentObjective stage of the manager, and one before an other child stage, are refused.
    json top = darkness_recipe("e4c00000-0000-4000-8000-000000000003/17", {300.0F, 300.0F, 30.0F});
    top["stages"][3]["objective"] = wait_parent();
    json before = darkness_recipe("e4c00000-0000-4000-8000-000000000004/17", {500.0F, 500.0F, 30.0F});
    before["stages"][2]["children"] = {6, 5};
    DarknessShop refused({top, before});
    refused.enter();
    refused.wait(0.5F);
    const auto has = [](const std::vector<std::string>& causes, const std::string& text) {
        return std::any_of(causes.begin(), causes.end(), [&](const std::string& cause) { return cause.find(text) != std::string::npos; });
    };
    check(has(refused.encounters().placements()[0].refused, "waits for its parent, and it is a stage of the manager")
              && has(refused.encounters().placements()[1].refused, "waits for its parent, and a child stage after it would never start"),
          "a WaitForParentObjective stage of the manager and one before an other child stage are refused");
}

/** @return The stage states of a slot that a client got (the statuses in their order). */
std::vector<std::uint32_t> seen(const FakeClient& client, std::uint32_t slot, std::size_t from = 0) {
    return statuses(root_seen(client, from), slot);
}

/** @return The members of the instance that are parts of a group, with their part index. */
std::vector<std::pair<world::Entity*, std::int32_t>> parts_of(DarknessShop& shop) {
    std::vector<std::pair<world::Entity*, std::int32_t>> out;
    if (const world::EncounterInstance* run = shop.instance()) {
        for (const auto& [id, member] : run->members) {
            world::Entity* entity = shop.host.world.find(id);
            if (entity != nullptr && member.part >= 0 && !member.done && !entity->creature.dead) {
                out.emplace_back(entity, member.part);
            }
        }
    }
    return out;
}

bool near(const state::WorldPosition& a, float x, float y) {
    return std::fabs(a.x - x) < 0.01F && std::fabs(a.y - y) < 0.01F;
}

void darkness_run(const Options&) {
    DarknessShop shop({darkness_recipe()});
    begin(shop);
    shop.wait(3.5F);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->stages[1].started && run->stages[4].started && shop.members(true).size() == 2
              && shop.members(false).size() == 1 && shop.encounters().counts().spawnsSkipped >= 1,
          "the pylon stage makes its two start objects (an alias), its sustain child its member; the visual of the setup is not made");
    // The pylons: A and B each gather one (the alias of the spawn is the tracked alias of the objective).
    const auto objects = shop.members(true);
    if (objects.size() < 2) {
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*objects[0]), 1500);
    shop.move(kCharacterB, {objects[1]->position.x - 1.5F, objects[1]->position.y, objects[1]->position.z});
    shop.gather(shop.clientB, EncounterShop::start_block(*objects[1]), 1500);
    shop.wait(0.5F);
    run = shop.instance();
    check(run != nullptr && run->stages[1].ended && run->stages[4].ended && run->stages[2].started,
          "the two pylons end their stage, and its sustain child (WaitForParentObjective) ends with it");
    for (const FakeClient* client : {&shop.clientA, &shop.clientB}) {
        check(seen(*client, kChildSlotA) == std::vector<std::uint32_t>{2, 3} && seen(*client, kInteractSlot) == std::vector<std::uint32_t>{2, 3},
              "each observer gets the end of the pylon stage and of its sustain child (status 3)");
    }
    check(shop.members(false).size() >= 1, "the member of the sustain child stays (no KillSpawnsAction yet)");
    // The carapace: the boss and the attack group (2 parts made, 1 hidden part counted at the start).
    run = shop.instance();
    const world::EncounterCounts& counts = shop.encounters().counts();
    check(run != nullptr && run->stages[5].started && run->stages[5].counts[0] == 1 && counts.partsMade == 2 && counts.partsHidden == 1
              && counts.partsFilled == 1,
          "the attack makes 2 parts, the hidden part is not made, and its objective counts it at its start (1 of 3)");
    const auto parts = parts_of(shop);
    const world::Entity* first = nullptr;
    const world::Entity* second = nullptr;
    for (const auto& [entity, part] : parts) {
        (part == 0 ? first : second) = entity;
    }
    check(parts.size() == 2 && first != nullptr && second != nullptr && near(first->position, 104.0F, 206.0F)
              && near(second->position, 103.0F, 208.0F),
          "each part is at its offset from the spawn location");
    check(first != nullptr && first->creature.memberRoute != nullptr && first->creature.memberRoute->points.size() == 2
              && near(first->creature.memberRoute->points[0].position, 108.0F, 206.0F)
              && near(first->creature.memberRoute->points[1].position, 112.0F, 206.0F)
              && first->creature.memberRoute->policy == gamedata::PatrolPolicy::bounce && second != nullptr
              && second->creature.memberRoute == nullptr,
          "the part with a path of the data has its route in world coordinates (the other part has none)");
    for (const auto& [entity, part] : parts) {
        shop.kill(*entity, part == 0 ? kCharacterA : kCharacterB);
    }
    shop.wait(0.5F);
    run = shop.instance();
    check(run != nullptr && run->stages[5].ended && run->stages[6].started && !run->stages[2].ended,
          "the two kills end the attack (3 of 3 with the hidden part), and the next child starts (02_Sustain)");
    // The boss ends the carapace; its child that waits for it ends with it.
    world::Entity* boss = nullptr;
    for (world::Entity* entity : shop.members(false)) {
        if (entity->creature.kind != nullptr && run != nullptr) {
            const auto member = run->members.find(entity->id);
            if (member != run->members.end() && member->second.stage == 2) {
                boss = entity;
            }
        }
    }
    check(boss != nullptr, "control: the boss of the carapace lives");
    if (boss == nullptr) {
        return;
    }
    shop.kill(*boss, kCharacterA);
    shop.wait(0.5F);
    run = shop.instance();
    check(run != nullptr && run->stages[2].ended && run->stages[6].ended && run->stages[3].started && shop.members(false).empty(),
          "the boss ends the carapace and its waiting child; the cleanup removes the other members (KillSpawnsAction)");
    for (const FakeClient* client : {&shop.clientA, &shop.clientB}) {
        check(seen(*client, 11) == std::vector<std::uint32_t>{2, 3}, "each observer gets the end of the second waiting child");
    }
    shop.wait(3.5F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 1 && shop.encounters().counts().slicesDestroyed == 0,
          "the success event goes to both participants (the DestroySliceAction found no member of the visual)");
}

void darkness_observers(const Options&) {
    DarknessShop shop({darkness_recipe()});
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 240.0F, 30.0F}), "control: B is 35 m from the root");
    shop.placeB = {100.0F, 240.0F, 30.0F};
    begin(shop);
    shop.wait(3.5F);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.size() == 1 && run->participants.count(kCharacterA) == 1
              && shop.entity(kCharacterB)->encounterLinks.empty() && !root_seen(shop.clientB).managers.empty(),
          "B near the event (35 m) observes the root, and it is no participant (no link)");
    // A late observer: B goes out of the interest and comes back in the pylon stage.
    shop.move(kCharacterB, {100.0F, 500.0F, 30.0F});
    shop.wait(1.0F);
    const std::size_t from = shop.clientB.got.size();
    shop.move(kCharacterB, {100.0F, 240.0F, 30.0F});
    shop.wait(1.0F);
    check(seen(shop.clientB, kSetupSlot, from) == std::vector<std::uint32_t>{2, 3} && seen(shop.clientB, kInteractSlot, from).back() == 2
              && seen(shop.clientB, kChildSlotA, from).back() == 2,
          "the late observer gets the history of each stage state (the setup ended, the pylons and the sustain child run)");
    // A ends the event alone; B gets no credit.
    for (world::Entity* object : shop.members(true)) {
        shop.move(kCharacterA, {object->position.x - 1.5F, object->position.y, object->position.z});
        shop.gather(shop.clientA, EncounterShop::start_block(*object), 1500);
    }
    shop.wait(0.5F);
    for (world::Entity* creature : shop.members(false)) {
        shop.kill(*creature, kCharacterA);
    }
    shop.wait(0.5F);
    for (world::Entity* creature : shop.members(false)) {
        shop.kill(*creature, kCharacterA);
    }
    shop.wait(4.0F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 0,
          "the participant A gets the success event, and the nearby character B gets none");
}

/** The boat form: the timed setup spawns the spyglass, the next stage tracks it, a timed boat stage, the landing group, a cleanup. */
json boat_recipe(const char* key = kKey, const state::WorldPosition& root = kRootAt) {
    json landing = place_json({root.x + 3.0F, root.y + 1.0F, root.z}, 90.0F);
    json group = spawn_json(kGroupSlice, 0, 1, kMemberAt);
    group["locations"] = json::array({landing});
    json recipe = pattern_recipe(
        key, root,
        json::array({stage_json("Wave_00 - Setup - Spyglass", 0x100, kSetupSlot, true, timed(1), json::array({spawn_json(kStartSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_01 - Look through Spyglass", 0x200, kInteractSlot, true, destroyed(1, kStartSlice), json::array()),
                     stage_json("Wave_01 - Spawn Boat, Start Timeline", 0x300, kChildSlotA, true, timed(2), json::array()),
                     stage_json("Wave_02 - Spawn Knights", 0x400, kKillSlot, true, destroyed(2), json::array({group})),
                     stage_json("Wave_05 - Cleanup", 0x500, kCleanupSlot, false, timed(3), json::array())}),
        {0, 1, 2, 3, 4});
    recipe["slice"] = "slices/worldevents/monarchsbluffs/scallywagshore/enc_test_boat.dynamicslice";
    recipe["pattern"] = "a world event of the open world";
    return recipe;
}

void boat_run(const Options&) {
    json members = darkness_members();
    members[kGroupSlice]["group"] = json::array({part_json("PointSpawner_3wp_Path - Undead_Officer", 1.0, 0.0, true),
                                                 part_json("PointSpawner_3wp_Path - Undead_Navigator", 0.0, 2.0, false)});
    members[kGroupSlice]["parts"] = 2;
    DarknessShop shop({boat_recipe()});
    // The two parts of the landing (a fresh member file of two parts).
    const json document{{"format", 1}, {"recipes", {boat_recipe()}}, {"members", members}};
    check(shop.host.encounters.load_text(document.dump()), "control: the boat form loads");
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty(), "the runner takes the boat form (a start object that the next stage tracks)");
    shop.wait(1.5F);
    const world::EncounterInstance* run = shop.instance();
    const auto objects = shop.members(true);
    check(run != nullptr && run->stages[0].ended && run->stages[1].started && objects.size() == 1,
          "the spyglass of the setup stays when its timed stage ends; the next stage waits for it");
    if (objects.empty()) {
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(0.3F);
    run = shop.instance();
    check(run != nullptr && run->stages[1].ended && run->stages[2].started && shop.progress(kCharacterA, "Test_Examine") == 1,
          "the look through the spyglass (its gather) ends the stage that tracks it (the event of its row goes to A)");
    shop.wait(2.2F);
    const auto parts = parts_of(shop);
    const world::Entity* officer = nullptr;
    for (const auto& [entity, part] : parts) {
        if (part == 0) {
            officer = entity;
        }
    }
    // The location yaw 90 turns the offsets: the officer (1, 0) at (103, 207), its path (5, 0) and (9, 0) at (103, 211) and (103, 215).
    check(parts.size() == 2 && officer != nullptr && near(officer->position, 103.0F, 207.0F) && officer->creature.memberRoute != nullptr
              && near(officer->creature.memberRoute->points[0].position, 103.0F, 211.0F)
              && near(officer->creature.memberRoute->points[1].position, 103.0F, 215.0F),
          "the landing makes the officer and the navigator; the yaw of the spawn location turns their places and the path");
    for (const auto& [entity, part] : parts) {
        shop.kill(*entity, part == 0 ? kCharacterA : kCharacterB);
    }
    shop.wait(3.5F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 1,
          "the two kills end the landing; after the cleanup both participants get the success event");
}

void darkness_load(const Options&) {
    std::vector<json> recipes;
    for (int index = 0; index < 4001; ++index) {
        const float x = index == 0 ? kRootAt.x : 2000.0F + static_cast<float>(index % 80) * 150.0F;
        const float y = index == 0 ? kRootAt.y : 2000.0F + static_cast<float>(index / 80) * 150.0F;
        char key[64];
        std::snprintf(key, sizeof key, "e4c05000-0000-4000-8000-%012d/17", index);
        recipes.push_back(darkness_recipe(key, {x, y, 30.0F}));
    }
    DarknessShop shop(recipes);
    begin(shop);
    const std::uint64_t scans = shop.encounters().counts().scans;
    const std::uint64_t looked = shop.encounters().counts().placementsLooked;
    shop.wait(10.0F);
    const std::uint64_t scanCount = shop.encounters().counts().scans - scans;
    const std::uint64_t perScan = scanCount > 0 ? (shop.encounters().counts().placementsLooked - looked) / scanCount : 0;
    check(shop.encounters().counts().taken == 4001 && scanCount >= 15 && perScan <= 4 && shop.encounters().instances().size() == 1,
          "4,001 Darkness placements: a scan looks at " + std::to_string(perScan) + " placements (the instance near the characters)");
    const double longest = std::chrono::duration<double, std::milli>(shop.encounters().scan_load().largest).count();
    check(longest < 20.0, "the longest scan of the runner is below 20 ms (" + std::to_string(longest) + " ms)");
}

void darkness_manifest(const Options&) {
    DarknessShop shop({darkness_recipe()});
    begin(shop);
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const auto fact = [&](const char* objective) {
        for (const json& entry : manifest["objectives"]) {
            if (entry["id"] == objective) {
                return entry["tasks"][0];
            }
        }
        return json::object();
    };
    check(fact("Test_Defend").value("supported", false), "the success event of the Darkness form has a producer (the season rule)");
    const world::EncounterPlacement& placement = shop.encounters().placements()[0];
    const gamedata::EncounterMember* group = shop.host.encounters.member(kGroupSlice);
    const std::size_t index = group != nullptr ? static_cast<std::size_t>(group - shop.host.encounters.members().data()) : 0;
    check(group != nullptr && placement.partKinds[index].size() == 3 && placement.partKinds[index][0] != nullptr
              && placement.partKinds[index][2] == nullptr,
          "the kinds of the parts of the group are the kill sources of the manifest (the hidden part has none)");
}

} // namespace

std::vector<Case> encounter_darkness_cases() {
    return {{"QG5C5A", "encounters QG5c5 (I1, I2, I3): the file of the Darkness form (groups, aliases, wait for the parent, destroy a slice, "
                       "the season) and its refusals",
             darkness_file},
            {"QG5C5B", "encounters QG5c5 (I1 to I4, I8): the Darkness form with two participants (pylons, waiting children, a group, the "
                       "cleanup, the success)",
             darkness_run},
            {"QG5C5C", "encounters QG5c5 (I8): a nearby character that does not take part and a late observer", darkness_observers},
            {"QG5C5D", "encounters QG5c5 (I5, I8): the boat form (the spyglass of the setup, the landing group on its paths) with two sessions",
             boat_run},
            {"QG5C5E", "encounters QG5c5 (I9): 4,001 Darkness placements; a scan looks only near the characters (time limit)", darkness_load},
            {"QG5C5F", "encounters QG5c5 (I7): the support manifest takes the success of the Darkness form and the kinds of the parts",
             darkness_manifest}};
}

} // namespace soulwarden::backend::tests

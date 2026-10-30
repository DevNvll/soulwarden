// The cases of stage QG5c4 (docs/systems/encounters.md, world/encounters.h): the interaction sequences (an entity-state stage with its
// trigger, a recipe with no creature member), the defenses (the timed fight and its fail), and the stages with more than one objective.
// Made-up slices, kinds, places, recipes, quests, and accounts (the fixtures of tests/support/encounter_fixture.h and
// encounter_pattern_fixture.h).
//
//   QG5C4A  (I1, I4) the file: an entity-state objective with its trigger (its place, its shapes, its reach), the other objectives of a
//           stage; the loader refuses a trigger with no place; the runner refuses an entity-state stage with no trigger, a second objective
//           with a time limit, an objective of an other class, and a stage whose only objective is of a creature that the client does not
//           show; it takes the same objective beside an other one (met at its start)
//   QG5C4B  (I1, I8) the tarot tower form: the setup waits for a character in its trigger 40 m above the root (a character on the ground in
//           the box is a participant and does not end it), the boss at the top, the trigger shape is a shape of the detector (A at the top
//           is a participant), a late observer (B: the current stage states, no link), a late participant (B at the top), the success
//           event for A and B
//   QG5C4C  (I1, I8) the Disk of Atum form (no creature member): the proximity start, the gather of a start object, a timed hold of three
//           objects that succeeds at its time with one gathered, the cleanup removes the rest; a nearby character that does not take part
//           gets no event
//   QG5C4D  (I2, I8) the hacker form (a defense in the data: no defended target with a health, no failure condition): the start object,
//           the timed fight (10 s) whose members take a participant as their target, the success at its time with living members, the
//           success event for A and B; the same recipe with no succeedOnTimeout fails at the end of its time (the manager status 4 for
//           both, no event)
//   QG5C4E  (I8) a stage with three objectives (two members, the boss, two creatures that the client does not show): the two kills do not
//           end it, the kill of the boss ends it; the hidden creatures are not made, and their objective is met at the start
//   QG5C4F  (I9) the load: 4,001 tarot tower placements; each scan looks at the placements and at the triggers near the two characters only;
//           the longest scan has a time limit
//   QG5C4G  (I6) the support manifest: the success event of an entity-state recipe and of a recipe with no creature member have producers;
//           a refused recipe gives its cause
//   QG5C4H  (I5, I8) the HUD row (static read D of stage QG5c4): the leaves of the objectives of the user-wave stages (two for a stage with
//           two objectives), a satisfied leaf (its target, state 2) and wrapper (1, state 2), the outer container counts them; field 0 of the
//           root has the start of the timed leaf when its stage starts; A (a participant) and B (an observer) see the same root

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "support/encounter_pattern_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using namespace encfix;
using questfix::json;

constexpr const char* kOpenSlice = "slices/test/io_test_diskstart.dynamicslice";
constexpr const char* kOpenEvent = "Test_DiskStart_Interact";
/** The top of the made-up tower: 40 m above the root (the trigger and the boss are there). */
constexpr state::WorldPosition kTopAt{100.0F, 205.0F, 70.0F};

json sequence_members() {
    json members = pattern_members();
    members[kOpenSlice] = {{"kind", "object"},       {"variant", "Test_DiskStart"}, {"table", "Gatherable_Quest_Proximity"},
                           {"row", "Test_DiskStart"}, {"event", kOpenEvent},          {"tag", ""},
                           {"onlyOnQuest", false},   {"proximity", true},            {"interact", true},
                           {"radius", 4.5}};
    return members;
}

json state_objective(const state::WorldPosition& at, double radius = 20.0) {
    return {{"type", "EntityActiveStateChangedObjective"}, {"timeLimit", 0}, {"succeedOnTimeout", false}, {"failureCondition", false},
            {"target", "000076D766AB0000"}, {"targetName", "Trigger_Start"}, {"completeOn", "deactivated"},
            {"triggers", json::array({{{"entity", "Trigger_Start"}, {"position", {at.x, at.y, at.z}}, {"filter", "PlayerOnly"},
                                       {"deadTargets", true},
                                       {"shapes", json::array({{{"shape", "sphere"}, {"radius", radius}, {"center", {0.0, 0.0, 0.0}}}})}}})}};
}

/**
 * The tarot tower form: the setup (an entity-state objective whose trigger is a sphere of 20 m at the top, 40 m above the root), the boss
 * stage (one direct boss at the top, its slice tracked), a cleanup (3 s); the detector is a box at the root and the sphere of the trigger
 * (the rule of encounter_recipes.py for a shape entity that the slice does not have).
 */
json tower_recipe(const char* key = kKey, const state::WorldPosition& root = kRootAt) {
    const state::WorldPosition top{root.x, root.y, root.z + 40.0F};
    json recipe = pattern_recipe(
        key, root,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, state_objective(top), json::array()),
                     stage_json("Wave_01 - Objective: Kill Boss", 0x200, kKillSlot, false, destroyed(1, kBossSlice),
                                json::array({spawn_json(kBossSlice, 0, 1, {top.x + 3.0F, top.y + 1.0F, top.z})})),
                     stage_json("Wave_02 - Cleanup", 0x300, kCleanupSlot, false, timed(3), json::array())}),
        {0, 1, 2});
    recipe["stages"][1]["childrenOptional"] = true;
    recipe["detector"] = {{"radius", 20.0},
                          {"filter", "OnlyPlayerTarget"},
                          {"sphere", false},
                          {"shapes", json::array({{{"shape", "aabb"}, {"extents", {22.5, 22.5, 12.5}}, {"center", {0.0, 0.0, 0.0}},
                                                   {"shapeEntity", "missing"}},
                                                  {{"shape", "sphere"}, {"radius", 20.0}, {"center", {0.0, 0.0, 40.0}},
                                                   {"fromTrigger", "Trigger_Start"}}})}};
    recipe["pattern"] = "an interaction sequence";
    return recipe;
}

/** The Disk of Atum form: no creature member. */
json disk_recipe(const char* key = kKey) {
    json recipe = pattern_recipe(
        key, kRootAt,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                     stage_json("Wave_01: Interact with IO", 0x200, kInteractSlot, false, destroyed(1, kOpenSlice),
                                json::array({spawn_json(kOpenSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_02 - Objective: Kill AI", 0x300, kKillSlot, false, destroyed(1, kStartSlice),
                                json::array({spawn_json(kStartSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_03 - Hold", 0x400, kChildSlotA, false, destroyed(3, kStartSlice),
                                json::array({spawn_json(kStartSlice, 0, 3, kStartAt)})),
                     stage_json("Wave_04 - Cleanup", 0x500, kCleanupSlot, false, timed(3), json::array())}),
        {0, 1, 2, 3, 4});
    recipe["stages"][3]["objective"]["timeLimit"] = 30;
    recipe["stages"][3]["objective"]["succeedOnTimeout"] = true;
    recipe["stages"][3]["startActions"] = json::array({{{"type", "KillSpawnsAction"}, {"localOnly", false}}});
    recipe["stages"][4]["startActions"] = json::array({{{"type", "KillSpawnsAction"}, {"localOnly", false}}});
    recipe["pattern"] = "an interaction sequence";
    return recipe;
}

/** The hacker form: a setup, the start object (each spawn of its stage counts), the timed fight (10 s, a countdown). */
json hacker_recipe(const char* key = kKey, bool succeed = true) {
    json fight = timed(10);
    fight["succeedOnTimeout"] = succeed;
    fight["countDown"] = true;
    json recipe = pattern_recipe(
        key, kRootAt,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                     stage_json("Wave_01 - Objective: Initiate Hack", 0x200, kInteractSlot, true, destroyed(1),
                                json::array({spawn_json(kStartSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_02 - Objective: Kill First Wave", 0x300, kKillSlot, true, fight,
                                json::array({spawn_json(kMemberSlice, 0, 2, kMemberAt), spawn_json(kMemberSlice, 5, 1, kMemberAt)}))}),
        {0, 1, 2});
    recipe["manager"]["overrideAggroDelay"] = true; // m_overrideAggroDelay 1 and m_aggroDelayOverride 0 of the hacker data.
    recipe["manager"]["aggroDelay"] = 0.0;
    recipe["pattern"] = "a defense";
    return recipe;
}

struct SequenceShop : PatternShop {
    explicit SequenceShop(const std::vector<json>& recipes) : PatternShop(recipes, sequence_members()) {
        if (!host.interactions.set_encounter_objects({{kStartSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}},
                                                      {kAmbushSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}},
                                                      {kOpenSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}}})) {
            throw std::runtime_error("sequence test: the start object templates were not set: " + host.interactions.error());
        }
    }

    /** The member kind gets a body that fights (a sense, a speed, a bite): the target rule of the members (Behavior::provoke). */
    void members_fight() {
        gamedata::CreatureSlice slice = bags::bag_wolf_slice();
        slice.actions.push_back({"Attack_Bite", "Attack_Bite", 7, false});
        gamedata::CreatureBehavior values;
        values.sightAngle = 180.0F;
        values.sightDistance = 12.0F;
        values.combatAngle = 180.0F;
        values.combatDistance = 30.0F;
        values.restLeash = 20.0F;
        values.combatLeash = 70.0F;
        values.walkSpeed = 2.0F;
        values.runSpeed = 5.0F;
        slice.behavior = values;
        gamedata::CreatureContact contact;
        contact.time = 0.5F;
        contact.reach = 2.2F;
        contact.factor = 1.0F;
        contact.type = "Slash";
        contact.wire = 4;
        contact.category = gamedata::DamageCategory::physical;
        contact.row = "Test_Bite";
        contact.hitCharge = 3.0F;
        slice.attacks = {gamedata::CreatureAttack{static_cast<std::uint32_t>(slice.actions.size() - 1), 1.4F, {contact}}};
        gamedata::CreatureKind member = bags::bag_wolf_kind();
        member.id = "worldevent_test_member";
        member.vitals = "Test_Member";
        member.category = "Test_Member_Category";
        member.vitalsId = 0x7E570001;
        member.categoryId = 0x7E570002;
        member.member = kMemberSlice;
        member.points = 0;
        if (!host.creatures.set_full({slice}, {bags::bag_wolf_kind(), member}, {}, {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("sequence test: the creature data was not set: " + host.creatures.error());
        }
        host.creatures.set_members({{kMemberSlice, {1, ""}}, {kBossSlice, {1, ""}}, {kHiddenSlice, {-1, "the client does not show its slice"}}});
    }

    /** Gathers the first start object of the instance (a GatherOption of 1 s) by a character. */
    bool gather_first(FakeClient& client) {
        const std::vector<world::Entity*> objects = members(true);
        for (world::Entity* object : objects) {
            if (encounters().start_object(object->id) != nullptr) {
                gather(client, EncounterShop::start_block(*object), 1500);
                wait(0.3F);
                return true;
            }
        }
        return false;
    }
};

/** QG5C4A: the file and the refusals. */
void sequence_file(const Options&) {
    json tower = tower_recipe();
    tower["stages"][1]["objective"]["more"] = json::array({destroyed(1, kMemberSlice)});
    tower["stages"][1]["spawns"].push_back(spawn_json(kMemberSlice, 0, 1, kTopAt));
    const json good{{"format", 1}, {"recipes", json::array({tower})}, {"members", sequence_members()}};
    gamedata::EncounterData data;
    check(data.load_text(good.dump()), "the file with an entity-state objective and a second objective loads (" + data.error() + ")");
    if (data.loaded()) {
        const gamedata::EncounterStage& setup = data.recipes()[0].stages[0];
        const gamedata::EncounterStage& kill = data.recipes()[0].stages[1];
        check(setup.objective.kind == gamedata::EncounterObjectiveKind::entityState && setup.objective.triggers.size() == 1
                  && setup.objective.triggers[0].entity == "Trigger_Start" && setup.objective.triggers[0].deadTargets
                  && std::fabs(setup.objective.triggers[0].position.z - 70.0F) < 0.01F && setup.objective.triggers[0].reach > 19.9F
                  && setup.objective.triggers[0].reach < 20.1F,
              "the entity-state objective has its trigger: its entity, its place 40 m up, its sphere, its reach");
        check(kill.more.size() == 1 && kill.more[0].kind == gamedata::EncounterObjectiveKind::destroyed && kill.more[0].tracked == kMemberSlice
                  && kill.more[0].requiredCount == 1,
              "the second objective of the kill stage has its kind, its tracked slice, and its count");
        check(data.recipes()[0].detectorShapes.size() == 2 && data.recipes()[0].detectorShapes[1].center[2] == 40.0F,
              "the detector has the box at the root and the sphere of the trigger 40 m up");
    }
    json noPlace = tower_recipe();
    noPlace["stages"][0]["objective"]["triggers"][0].erase("position");
    gamedata::EncounterData broken;
    check(!broken.load_text(json{{"format", 1}, {"recipes", json::array({noPlace})}, {"members", sequence_members()}}.dump()),
          "the loader refuses a trigger with no place (" + broken.error() + ")");
    // The refusals of the runner.
    json noTrigger = tower_recipe("e4c00000-0000-4000-8000-000000000051/17", {400.0F, 205.0F, 30.0F});
    noTrigger["stages"][0]["objective"]["triggers"] = json::array();
    json timedMore = tower_recipe("e4c00000-0000-4000-8000-000000000052/17", {700.0F, 205.0F, 30.0F});
    timedMore["stages"][1]["objective"]["more"] = json::array({timed(5)});
    json other = tower_recipe("e4c00000-0000-4000-8000-000000000053/17", {1000.0F, 205.0F, 30.0F});
    other["stages"][1]["objective"]["type"] = "TestUnknownObjective"; // Stage QG9b: BossPhaseObjective is a class of the runner now.
    json hiddenOnly = tower_recipe("e4c00000-0000-4000-8000-000000000054/17", {1300.0F, 205.0F, 30.0F});
    hiddenOnly["stages"][1]["objective"] = destroyed(1, kHiddenSlice);
    hiddenOnly["stages"][1]["spawns"] = json::array({spawn_json(kHiddenSlice, 0, 1, {1300.0F, 206.0F, 70.0F})});
    json hiddenBeside = tower_recipe("e4c00000-0000-4000-8000-000000000055/17", {1600.0F, 205.0F, 30.0F});
    hiddenBeside["stages"][1]["objective"]["more"] = json::array({destroyed(2, kHiddenSlice)});
    hiddenBeside["stages"][1]["spawns"].push_back(spawn_json(kHiddenSlice, 0, 2, {1600.0F, 206.0F, 70.0F}));
    SequenceShop shop({noTrigger, timedMore, other, hiddenOnly, hiddenBeside});
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    const auto first = [&](std::size_t index) {
        return index < placements.size() && !placements[index].refused.empty() ? placements[index].refused[0] : std::string("taken");
    };
    check(first(0).find("waits for a change of an entity that no trigger of the data sends") != std::string::npos,
          "an entity-state stage with no trigger is refused (" + first(0) + ")");
    check(first(1).find("the objective 2 of the stage Wave_01 - Objective: Kill Boss is not a destroyed or a spawned objective") != std::string::npos,
          "a second objective with a time limit is refused (" + first(1) + ")");
    check(first(2).find("the objective TestUnknownObjective of the stage Wave_01 - Objective: Kill Boss: the runner does not run it")
              != std::string::npos,
          "an objective of an other class is refused by the runner too (" + first(2) + ")");
    check(first(3) == "the creature member worldevent_ai_hidden.dynamicslice: the client does not show its slice",
          "a stage whose only objective is of a creature that the client does not show is refused (" + first(3) + ")");
    check(first(4) == "taken" && placements.size() == 5 && placements[4].voided.size() == 3 && placements[4].voided[1].size() == 2
              && !placements[4].voided[1][0] && placements[4].voided[1][1],
          "the same objective beside an other one is met at its start, and the recipe is taken (" + first(4) + ")");
    // The loader of the gathering data: a Handcrafting row with NoToolNecessary (the chests of the shipwreck) is a row that the backend
    // gathers (the rule of the type None); the same type with ToolRequired is not.
    json handcraft = gathering_document();
    json chest = row_json("Test_Chest", "QuestGatherables", "Handcrafting", "NoToolNecessary", "", 0, 3.0, 200.0, 300.0, "Empty", "", true);
    json tool = row_json("Test_Fishing", "QuestGatherables", "Fishing", "ToolRequired", "", 0, 3.0, 200.0, 300.0, "Empty", "", true);
    handcraft["rows"].push_back(chest);
    handcraft["rows"].push_back(tool);
    gamedata::GatheringData rows;
    check(rows.set(handcraft.dump(), {{kRegion, region_records()}}), "control: the rows load (" + rows.error() + ")");
    const auto row_causes = [&](const char* id) {
        for (const gamedata::GatherableRow& entry : rows.rows()) {
            if (entry.id == id) {
                return entry.unsupported.empty() ? std::string("none") : entry.unsupported.front();
            }
        }
        return std::string("no row");
    };
    check(row_causes("Test_Chest") == "none" && row_causes("Test_Fishing") == "the gathering type Fishing",
          "a Handcrafting row with NoToolNecessary is gathered; a Fishing row with ToolRequired is not (" + row_causes("Test_Chest") + ", "
              + row_causes("Test_Fishing") + ")");
    // A start object whose gatherable row has a rule that the backend cannot check (the gathering system refuses its gather) refuses the
    // recipe (the two chests of the shipwreck before the rule of NoToolNecessary of stage QG5c4).
    SequenceShop rowShop({hacker_recipe()});
    json gathering = gathering_document();
    json row = row_json("Test_EncStart", "Gatherables", "None", "NoToolNecessary", "", 0, 1.0, 200.0, 300.0, "Empty", kStartEvent, true);
    row["quest"] = true;
    row["unsupported"] = {"the gathering type Handcrafting"};
    gathering["rows"].push_back(row);
    json slice = template_json(kOreSlice, "encstart", "Gatherable_Quest");
    slice["slice"] = kStartSlice;
    gathering["templates"].push_back(slice);
    gathering["encounterObjects"] = {{kStartSlice, {gathering["templates"].size() - 1, gathering["rows"].size() - 1, "IO_Test_EncStart", false}}};
    check(rowShop.host.gathering.set(gathering.dump(), {{kRegion, region_records()}}), "control: the gathering data with the row loads");
    rowShop.enter();
    const std::vector<world::EncounterPlacement>& rowPlacements = rowShop.encounters().placements();
    check(rowPlacements.size() == 1 && !rowPlacements[0].refused.empty()
              && rowPlacements[0].refused[0]
                     == "the start object encstart.dynamicslice: its gatherable row Test_EncStart has a rule that the backend cannot check (the "
                        "gathering type Handcrafting)",
          "a start object whose gatherable row the backend cannot gather refuses the recipe (" +
              (rowPlacements.empty() || rowPlacements[0].refused.empty() ? std::string("taken") : rowPlacements[0].refused[0]) + ")");
}

/** QG5C4B: the tarot tower form with two sessions. */
void sequence_tower(const Options&) {
    SequenceShop shop({tower_recipe()});
    // A on the ground 10 m from the root (in the box of the detector), B far away (no interest).
    check(shop.host.store.set_character_position(kCharacterA, {100.0F, 215.0F, 30.0F}), "control: A is on the ground near the root");
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 700.0F, 30.0F}), "control: B is 500 m away");
    shop.placeA = {100.0F, 215.0F, 30.0F};
    shop.placeB = {100.0F, 700.0F, 30.0F};
    begin(shop);
    shop.wait(1.0F);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.count(kCharacterA) == 1 && !run->stages[0].ended && shop.members(false).empty(),
          "A on the ground is a participant (the box), and the setup waits: A is not in the trigger at the top");
    check(statuses(root_seen(shop.clientA), kSetupSlot) == std::vector<std::uint32_t>{2}, "the setup has only its start status");
    shop.move(kCharacterA, {kTopAt.x - 5.0F, kTopAt.y, kTopAt.z});
    shop.wait(0.6F);
    const std::vector<world::Entity*> bosses = shop.members(false);
    check(shop.encounters().counts().triggersFound == 1 && shop.instance() != nullptr && shop.instance()->stages[0].ended
              && bosses.size() == 1 && bosses.front()->position.z > 69.0F,
          "A in the trigger at the top ends the setup; the boss comes at the top");
    check(shop.instance() != nullptr && shop.instance()->participants.count(kCharacterA) == 1
              && shop.entity(kCharacterA)->encounterLinks.size() == 1,
          "A at the top is a participant (the sphere of the trigger is a shape of the detector) with its link");
    // B comes near (35 m from the root, out of the detector): a late observer with the current stage states.
    const std::size_t before = shop.clientB.got.size();
    shop.move(kCharacterB, {100.0F, 240.0F, 30.0F});
    shop.wait(0.6F);
    const RootSeen late = root_seen(shop.clientB, before);
    check(late.interest != 0 && statuses(late, kSetupSlot) == std::vector<std::uint32_t>{2, 3}
              && statuses(late, kKillSlot) == std::vector<std::uint32_t>{2} && links_seen(shop.clientB, before).empty(),
          "B, a late observer, gets the current states of the stages (the setup ended, the boss stage runs) and no link");
    check(shop.instance() != nullptr && shop.instance()->participants.count(kCharacterB) == 0, "B at 35 m on the ground is no participant");
    shop.move(kCharacterB, {kTopAt.x + 2.0F, kTopAt.y, kTopAt.z});
    shop.wait(0.6F);
    check(shop.instance() != nullptr && shop.instance()->participants.count(kCharacterB) == 1, "B at the top is a late participant");
    if (bosses.size() != 1) {
        return;
    }
    shop.kill(*bosses.front(), kCharacterA);
    shop.wait(3.5F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 1 && shop.entity(kCharacterA)->encounterLinks.empty(),
          "the kill of the boss and the cleanup: the success event for A and B one time each; the links end");
}

/** QG5C4C: the Disk of Atum form (no creature member) with two sessions. */
void sequence_disk(const Options&) {
    SequenceShop shop({disk_recipe()});
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 240.0F, 30.0F}), "control: B is 35 m from the root");
    shop.placeB = {100.0F, 240.0F, 30.0F};
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.instance() != nullptr,
          "the recipe with no creature member is taken, and its instance runs");
    shop.wait(3.2F);
    shop.move(kCharacterA, {100.0F, 203.0F, 30.0F});
    shop.wait(0.6F);
    check(shop.encounters().counts().objectsFound == 1 && shop.instance() != nullptr && shop.instance()->stages[1].ended,
          "A in the sphere of the proximity start object finds it; its stage ends");
    check(shop.gather_first(shop.clientA) && shop.instance() != nullptr && shop.instance()->stages[2].ended,
          "the gather of the start object of the next stage ends it");
    check(shop.progress(kCharacterA, "Test_Examine") == 1 && shop.progress(kCharacterB, "Test_Examine") == 0,
          "the event of the start object goes to its gatherer A only");
    check(shop.members(true).size() == 3, "the hold stage makes its three objects");
    check(shop.gather_first(shop.clientA) && shop.instance() != nullptr && !shop.instance()->stages[3].ended
              && shop.instance()->stages[3].destroyed == 1,
          "one gather of the three: the hold stage counts 1 of 3 and runs");
    shop.wait(30.0F);
    check(shop.instance() != nullptr && shop.instance()->stages[3].ended && shop.members(true).empty(),
          "the hold stage succeeds at the end of its time with 1 of 3; the cleanup removes the other objects");
    shop.wait(3.2F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 0,
          "the success event goes to the participant A; B (35 m, no participant) gets no event");
}

/** QG5C4D: the hacker form (the defenses of the data) with two sessions, and its fail. */
void sequence_defense(const Options&) {
    {
        SequenceShop shop({hacker_recipe()});
        shop.members_fight();
        shop.host.world.set_creature_behavior(true);
        begin(shop);
        shop.wait(3.2F);
        check(shop.gather_first(shop.clientA) && shop.instance() != nullptr && shop.instance()->stages[1].ended,
              "the start object of the hack (each spawn of its stage counts) ends its stage");
        const std::vector<world::Entity*> first = shop.members(false);
        const world::Entity* a = shop.entity(kCharacterA);
        const world::Entity* b = shop.entity(kCharacterB);
        check(first.size() == 2 && std::all_of(first.begin(), first.end(), [&](const world::Entity* member) {
                  return member->creature.mind.target == a->id || member->creature.mind.target == b->id;
              }),
              "the members of the fight take a participant as their target (no target that is not a player)");
        shop.host.world.set_creature_behavior(false);
        shop.wait(5.5F);
        check(shop.members(false).size() == 3, "the third member comes at its delay of 5 s");
        shop.wait(4.6F);
        check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
                  && shop.progress(kCharacterB, "Test_Defend") == 1 && shop.members(false).empty(),
              "the fight stage succeeds at the end of its 10 s with living members: the success event for A and B; the members go");
    }
    {
        SequenceShop shop({hacker_recipe(kKey, false)});
        begin(shop);
        shop.wait(3.2F);
        (void)shop.gather_first(shop.clientA);
        shop.wait(10.2F);
        const RootSeen viewA = root_seen(shop.clientA);
        const RootSeen viewB = root_seen(shop.clientB);
        check(shop.encounters().counts().failed == 1 && shop.encounters().counts().succeeded == 0
                  && shop.progress(kCharacterA, "Test_Defend") == 0 && shop.progress(kCharacterB, "Test_Defend") == 0,
              "with no succeedOnTimeout the end of the time fails the encounter: no success event");
        check(viewA.stops >= 1 && viewB.stops >= 1 && shop.entity(kCharacterA)->encounterLinks.empty()
                  && shop.entity(kCharacterB)->encounterLinks.empty(),
              "the root of the failed instance stops for A and for B (its objective goes off the HUD), and the links end");
    }
}

/** QG5C4E: a stage with three objectives. */
void sequence_objectives(const Options&) {
    json recipe = summon_recipe();
    recipe["stages"][2]["objective"] = destroyed(2, kMemberSlice);
    recipe["stages"][2]["objective"]["more"] = json::array({destroyed(1, kBossSlice), destroyed(2, kHiddenSlice)});
    recipe["stages"][2]["spawns"] = json::array({spawn_json(kMemberSlice, 0, 2, kMemberAt), spawn_json(kBossSlice, 0, 1, kMemberAt),
                                                 spawn_json(kHiddenSlice, 0, 2, kMemberAt)});
    SequenceShop shop({recipe});
    begin(shop);
    shop.wait(3.2F);
    (void)shop.gather_first(shop.clientA);
    shop.wait(0.2F);
    const std::vector<world::Entity*> fighters = shop.members(false);
    check(fighters.size() == 3 && shop.encounters().counts().spawnsSkipped == 2 && shop.encounters().counts().objectivesVoided == 1,
          "two members and the boss come; the two hidden creatures are not made, and their objective is met at the start");
    // The spawn entry of each fighter: 0 the two members, 1 the boss.
    std::vector<world::Entity*> members;
    world::Entity* boss = nullptr;
    for (world::Entity* fighter : fighters) {
        const auto run = shop.instance()->members.find(fighter->id);
        if (run != shop.instance()->members.end() && run->second.spawn == 1) {
            boss = fighter;
        } else {
            members.push_back(fighter);
        }
    }
    if (members.size() != 2 || boss == nullptr) {
        check(false, "control: the two members and the boss");
        return;
    }
    shop.kill(*members[0], kCharacterA);
    shop.kill(*members[1], kCharacterB);
    shop.wait(0.3F);
    check(shop.instance() != nullptr && !shop.instance()->stages[2].ended && shop.instance()->stages[2].counts[0] == 2
              && !shop.instance()->stages[2].met[1],
          "the two kills meet the first objective; the stage waits for the objective of the boss");
    shop.kill(*boss, kCharacterA);
    shop.wait(3.5F);
    check(shop.encounters().counts().succeeded == 1 && shop.progress(kCharacterA, "Test_Defend") == 1
              && shop.progress(kCharacterB, "Test_Defend") == 1,
          "the kill of the boss meets each objective: the stage ends, and A and B get the success event");
}

/** QG5C4F: the load of 4,001 tower placements. */
void sequence_load(const Options&) {
    std::vector<json> recipes;
    for (int index = 0; index < 4001; ++index) {
        const float x = index == 0 ? kRootAt.x : 2000.0F + static_cast<float>(index % 80) * 150.0F;
        const float y = index == 0 ? kRootAt.y : 2000.0F + static_cast<float>(index / 80) * 150.0F;
        char key[64];
        std::snprintf(key, sizeof key, "e4c04000-0000-4000-8000-%012d/17", index);
        recipes.push_back(tower_recipe(key, {x, y, 30.0F}));
    }
    SequenceShop shop(recipes);
    begin(shop);
    const std::uint64_t scans = shop.encounters().counts().scans;
    const std::uint64_t looked = shop.encounters().counts().placementsLooked;
    const std::uint64_t triggers = shop.encounters().counts().triggerLooks;
    shop.wait(10.0F);
    const std::uint64_t scanCount = shop.encounters().counts().scans - scans;
    const std::uint64_t perScan = scanCount > 0 ? (shop.encounters().counts().placementsLooked - looked) / scanCount : 0;
    const std::uint64_t triggersPerScan = scanCount > 0 ? (shop.encounters().counts().triggerLooks - triggers) / scanCount : 0;
    check(shop.encounters().counts().taken == 4001 && scanCount >= 15 && perScan <= 4 && triggersPerScan <= 1
              && shop.encounters().instances().size() == 1,
          "4,001 towers: a scan looks at " + std::to_string(perScan) + " placements and " + std::to_string(triggersPerScan)
              + " trigger (the instance near the characters)");
    const double longest = std::chrono::duration<double, std::milli>(shop.encounters().scan_load().largest).count();
    check(longest < 20.0, "the longest scan of the runner is below 20 ms (" + std::to_string(longest) + " ms)");
}

/** QG5C4G: the support manifest. */
void sequence_manifest(const Options&) {
    json disk = disk_recipe("e4c00000-0000-4000-8000-000000000061/17");
    disk["position"] = {400.0, 205.0, 30.0};
    disk["successEvent"] = kAmbushSuccess;
    disk["questEvents"] = {gamedata::lower_text(kAmbushSuccess)};
    json refused = tower_recipe("e4c00000-0000-4000-8000-000000000062/17", {700.0F, 205.0F, 30.0F});
    refused["refused"] = {"the stage Wave_00 - Wait for Players waits for a change of the entity Start_Trigger_Event that the gather of "
                          "Gatherable_Complete of the encounter slice sends (an object of the root record): a later stage"};
    refused["successEvent"] = "Test_Refused_Success";
    SequenceShop shop({tower_recipe(), disk, refused});
    begin(shop);
    shop.grant(kCharacterA, "Test_Ambush_Win");
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
    check(fact("Test_Defend").value("supported", false) && fact("Test_Ambush_Win").value("supported", false),
          "the success events of the tower (an entity-state stage) and of the disk (no creature member) have producers");
    check(!fact("Test_Refused_Defend").value("supported", true)
              && fact("Test_Refused_Defend").value("reason", std::string{}).find("the gather of Gatherable_Complete") != std::string::npos,
          "the success event of the refused recipe names its cause (" + fact("Test_Refused_Defend").value("reason", std::string{}) + ")");
}

/** QG5C4H: the HUD row of the timed user-wave stages (the task tree of the client, the starts of the timers) with two sessions. */
void sequence_hud(const Options&) {
    json loyalists = destroyed(1, kMemberSlice);
    loyalists["more"] = json::array({destroyed(1, kBossSlice)});
    json recipe = pattern_recipe(
        kKey, kRootAt,
        json::array({stage_json("Wave_01 - Objective: Kill Supporters", 0x100, kKillSlot, true, destroyed(2, kMemberSlice),
                                json::array({spawn_json(kMemberSlice, 0, 2, kMemberAt)})),
                     stage_json("Wave_01b - Intermission", 0x200, kSetupSlot, true, timed(10), json::array()),
                     stage_json("Wave_02 - Objective: Kill Loyalists", 0x300, kInteractSlot, true, loyalists,
                                json::array({spawn_json(kMemberSlice, 0, 1, kMemberAt), spawn_json(kBossSlice, 0, 1, kMemberAt)})),
                     stage_json("Wave_06 - Cleanup", 0x400, kCleanupSlot, false, timed(3), json::array())}),
        {0, 1, 2, 3});
    SequenceShop shop({recipe});
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 240.0F, 30.0F}), "control: B is 35 m from the root");
    shop.placeB = {100.0F, 240.0F, 30.0F};
    begin(shop);
    const gamedata::EncounterRecipe& data = *shop.encounters().placements()[0].recipe;
    check(data.taskLeaves == 4 && data.stages[0].objective.leaf == 1 && data.stages[0].objective.target == 2 && data.stages[1].objective.leaf == 3
              && data.stages[1].objective.target == 1 && data.stages[2].objective.leaf == 5 && data.stages[2].more[0].leaf == 7
              && data.stages[3].objective.leaf == -1,
          "the task tree: one leaf for each objective of a user-wave stage (1, 3, 5, 7: the stage with two objectives has two), no leaf for "
          "the cleanup; the outer container 9");
    const auto task = [&](const FakeClient& client, std::uint8_t index) {
        const std::optional<std::pair<std::uint32_t, std::uint8_t>> value = root_task(root_seen(client), index);
        return value ? std::to_string(value->first) + "/" + std::to_string(value->second) : std::string("none");
    };
    const auto row = [&](const FakeClient& client, std::initializer_list<std::uint8_t> indices) {
        std::string out;
        for (const std::uint8_t index : indices) {
            out += (out.empty() ? "" : " ") + std::to_string(index) + ":" + task(client, index);
        }
        return out;
    };
    check(row(shop.clientA, {1, 2, 3, 9}) == "1:0/1 2:0/1 3:none 9:0/1", "the first stage runs: its leaf and its wrapper are active, the "
                                                                         "outer container counts 0 (" + row(shop.clientA, {1, 2, 3, 9}) + ")");
    for (world::Entity* member : shop.members(false)) {
        shop.kill(*member, kCharacterA);
    }
    shop.wait(0.3F);
    const RootSeen afterKills = root_seen(shop.clientB);
    const std::uint64_t root = shop.instance() != nullptr ? shop.instance()->root : 0;
    const bool started = !afterKills.objectives.empty() && afterKills.objectives.back().timers.size() == 1
                         && std::get<0>(afterKills.objectives.back().timers[0]) == root && std::get<1>(afterKills.objectives.back().timers[0]) == 3
                         && std::get<2>(afterKills.objectives.back().timers[0]) != 0;
    check(started, "the intermission starts: field 0 of the root has the start of the timed leaf 3 (the synced wall clock), also for B");
    check(row(shop.clientB, {1, 2, 3, 4, 9}) == "1:2/2 2:1/2 3:0/1 4:0/1 9:1/1",
          "the first leaf is satisfied (2 of 2, state 2) with its wrapper (1 of 1); the timed leaf is active; the outer counts 1 ("
              + row(shop.clientB, {1, 2, 3, 4, 9}) + ")");
    shop.wait(10.2F);
    check(row(shop.clientA, {3, 4, 5, 6, 7, 8, 9}) == "3:1/2 4:1/2 5:0/1 6:0/1 7:0/1 8:0/1 9:2/1",
          "the end of the intermission satisfies its leaf (1 of 1); the two objectives of the next stage are two active pairs ("
              + row(shop.clientA, {3, 4, 5, 6, 7, 8, 9}) + ")");
    world::Entity* boss = nullptr;
    world::Entity* loyalist = nullptr;
    for (world::Entity* fighter : shop.members(false)) {
        const auto run = shop.instance()->members.find(fighter->id);
        (run != shop.instance()->members.end() && run->second.spawn == 1 ? boss : loyalist) = fighter;
    }
    if (boss == nullptr || loyalist == nullptr) {
        check(false, "control: the loyalist and the boss");
        return;
    }
    shop.kill(*loyalist, kCharacterA);
    shop.wait(0.3F);
    check(row(shop.clientA, {5, 6, 7, 8, 9}) == "5:1/2 6:1/2 7:0/1 8:0/1 9:3/1",
          "the kill of the loyalist satisfies the first objective of the stage only (" + row(shop.clientA, {5, 6, 7, 8, 9}) + ")");
    shop.kill(*boss, kCharacterB);
    shop.wait(0.3F);
    check(row(shop.clientB, {7, 8, 9}) == "7:1/2 8:1/2 9:4/2",
          "the kill of the boss satisfies the second objective; the outer container has its 4 of 4 (" + row(shop.clientB, {7, 8, 9}) + ")");
}

} // namespace

std::vector<Case> encounter_sequence_cases() {
    return {{"QG5C4A", "encounters QG5c4 (I1, I4): the file (an entity-state objective and its trigger, the other objectives of a stage) and the "
                       "refusals",
             sequence_file},
            {"QG5C4B", "encounters QG5c4 (I1, I8): the tarot tower form with two sessions (the trigger at the top, a late observer, a late "
                       "participant)",
             sequence_tower},
            {"QG5C4C", "encounters QG5c4 (I1, I8): the Disk of Atum form (no creature member) with two sessions (a timed hold)", sequence_disk},
            {"QG5C4D", "encounters QG5c4 (I2, I8): the hacker form (a defense of the data) with two sessions: the timed fight, its success, its "
                       "fail",
             sequence_defense},
            {"QG5C4E", "encounters QG5c4 (I8): a stage with three objectives (one of creatures that the client does not show)",
             sequence_objectives},
            {"QG5C4F", "encounters QG5c4 (I9): 4,001 towers; a scan looks only near the characters (time limit)", sequence_load},
            {"QG5C4G", "encounters QG5c4 (I6): the support manifest takes the producers of the new patterns", sequence_manifest},
            {"QG5C4H", "encounters QG5c4 (I5, I8): the HUD row of the timed user-wave stages (the task tree, the satisfied tasks, the start of a "
                       "timer) with two sessions",
             sequence_hud}};
}

} // namespace soulwarden::backend::tests

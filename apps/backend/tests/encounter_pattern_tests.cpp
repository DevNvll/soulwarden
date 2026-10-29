// The cases of stage QG5c3 (docs/systems/encounters.md, world/encounters.h): the summoned opponents, the ambushes, and the kill waves with a
// cause of the data. Made-up slices, kinds, places, recipes, quests, and accounts (the encounter fixture of tests/support/encounter_fixture.h).
//
//   QG5C3A  (I3, I9) the recipe file: the child stages and their parent, the spawned objective, the detector shapes and their reach, the
//           members (a proximity radius, an area trigger with no Interact, a lifetime, a direct creature); the loader refuses a stage in two
//           lists, a child stage that the manager names, a proximity object with no radius, a cylinder with no height; the runner refuses a
//           creature member with no kind in a stage that needs it and takes one in a timed stage; a stage that counts the deaths of the
//           members with no time limit is refused
//   QG5C3B  (I1, I6) a summoned opponent: the gather of the start object by A (its event to A only), the boss (a direct creature member)
//           at its delay, a nearby character out of the sphere (B: the record, no link, no event), B a late participant, the success
//           event for A and B
//   QG5C3C  (I2, I6) an ambush: the proximity start object (its record), B in its sphere with no task of its tag (InteractOnlyOnQuest: no
//           find), A in its sphere with the task (the event and the tag to A only), the participant event of each participant, the child
//           stages one after the other, the parent counts the deaths of their members and waits for its last child stage, a late observer
//           (the current states of the parent and of the children), the success event for A and B
//   QG5C3D  (I3, I6) a kill wave with causes: an area trigger with no record (each character in its 20 m sphere gets its event), a
//           spawned objective (the spawn of the boss), a member that ends itself (it counts at its lifetime in a stage that tracks nothing),
//           a visual and a creature member that the client does not show (not made), a stage that counts each death of a member (the boss
//           of an earlier stage), a cylinder detector (a character above it is no participant)
//   QG5C3E  (I6) the ends: a reset while a member that ends itself is due (its end of the old generation changes nothing), a reset before
//           the find of a proximity object (the object goes; the find counts for the new generation only), a dormant end of an ambush
//   QG5C3F  (I7) the load: 4,001 placements of ambushes; each scan looks at the placements near the two characters and at the proximity
//           objects of the instances that run; the longest scan has a time limit
//   QG5C3G  (I5) the support manifest: the participant event, the event of a proximity object and of an area trigger, the kill source of a
//           direct creature member, the cause of a refused recipe

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

/** QG5C3A: the file and the refusals. */
void pattern_file(const Options&) {
    json ambush = ambush_recipe();
    ambush["detector"] = {{"radius", 30.0},
                          {"filter", "OnlyPlayerTarget"},
                          {"sphere", false},
                          {"shapes", json::array({{{"shape", "cylinder"}, {"radius", 30.0}, {"height", 10.0}, {"center", {0.0, 0.0, 5.0}}},
                                                  {{"shape", "aabb"}, {"extents", {20.0, 30.0, 4.0}}, {"center", {0.0, 0.0, 0.0}}}})}};
    ambush["stages"][1]["objective"]["type"] = "SliceSpawnedObjective";
    const json good{{"format", 1}, {"recipes", json::array({ambush})}, {"members", pattern_members()}};
    gamedata::EncounterData data;
    check(data.load_text(good.dump()), "the file with the child stages, the shapes, and the members of stage QG5c3 loads (" + data.error() + ")");
    if (data.loaded()) {
        const gamedata::EncounterRecipe& recipe = data.recipes()[0];
        const gamedata::EncounterMember* proximity = data.member(kAmbushSlice);
        const gamedata::EncounterMember* trigger = data.member(kTriggerSlice);
        const gamedata::EncounterMember* smoke = data.member(kSmokeSlice);
        const gamedata::EncounterMember* boss = data.member(kBossSlice);
        check(recipe.stages[2].children == std::vector<std::uint32_t>{4, 5} && recipe.stages[4].parent == 2 && recipe.stages[5].parent == 2
                  && recipe.stages[2].parent == -1 && recipe.stages[1].objective.kind == gamedata::EncounterObjectiveKind::spawned,
              "the child stages of the kill stage have their parent; the SliceSpawnedObjective has its kind");
        check(recipe.detectorShapes.size() == 2 && recipe.detectorShapes[0].kind == gamedata::EncounterShape::Kind::cylinder
                  && recipe.detectorShapes[1].kind == gamedata::EncounterShape::Kind::aabb && recipe.detectorReach > 36.0F
                  && recipe.detectorReach < 37.0F,
              "the detector has its cylinder and its axis box, and the reach that holds both (" + std::to_string(recipe.detectorReach) + " m)");
        check(proximity != nullptr && proximity->proximity && proximity->interact && proximity->radius == 5.0F && trigger != nullptr
                  && !trigger->interact && smoke != nullptr && smoke->lifetime > 4.19F && boss != nullptr && boss->direct,
              "the members: the proximity radius, the area trigger with no Interact, the lifetime, the direct creature member");
    }
    json twoLists = ambush_recipe();
    twoLists["stages"][3]["children"] = {4};
    json managerChild = ambush_recipe();
    managerChild["stages"][2]["children"] = {3, 4};
    json noRadius = pattern_members();
    noRadius[kAmbushSlice].erase("radius");
    json flat = ambush_recipe();
    flat["detector"]["shapes"] = json::array({{{"shape", "cylinder"}, {"radius", 30.0}, {"center", {0.0, 0.0, 0.0}}}});
    for (const auto& [name, document] :
         {std::pair{"a stage in two lists", json{{"format", 1}, {"recipes", json::array({twoLists})}, {"members", pattern_members()}}},
          std::pair{"a child stage that the manager names", json{{"format", 1}, {"recipes", json::array({managerChild})}, {"members", pattern_members()}}},
          std::pair{"a proximity object with no radius", json{{"format", 1}, {"recipes", json::array({ambush_recipe()})}, {"members", noRadius}}},
          std::pair{"a cylinder with no height", json{{"format", 1}, {"recipes", json::array({flat})}, {"members", pattern_members()}}}}) {
        gamedata::EncounterData broken;
        check(!broken.load_text(document.dump()) && broken.recipes().empty(), std::string("the loader refuses ") + name + " (" + broken.error() + ")");
    }
    // The runner: a creature member with no kind in a stage that counts it is a cause; in a timed stage it is not made.
    json needed = summon_recipe();
    needed["stages"][2]["spawns"].push_back(spawn_json(kHiddenSlice, 0, 1, kMemberAt));
    needed["stages"][2]["objective"]["trackedSlice"] = "";
    json notNeeded = summon_recipe("e4c00000-0000-4000-8000-000000000031/17");
    notNeeded["position"] = {400.0, 205.0, 30.0};
    notNeeded["stages"][3]["spawns"] = json::array({spawn_json(kHiddenSlice, 0, 1, {400.0F, 206.0F, 30.0F})});
    json anyMember = summon_recipe("e4c00000-0000-4000-8000-000000000032/17");
    anyMember["position"] = {700.0, 205.0, 30.0};
    anyMember["stages"][2]["spawns"] = json::array();
    anyMember["stages"][2]["objective"]["trackedSlice"] = "";
    PatternShop shop({needed, notNeeded, anyMember});
    shop.enter();
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    check(placements.size() == 3 && !placements[0].refused.empty()
              && placements[0].refused[0] == "the creature member worldevent_ai_hidden.dynamicslice: the client does not show its slice"
              && placements[1].refused.empty(),
          "the runner refuses the member with no kind in the stage that counts it, and takes the same member in a timed stage");
    check(placements.size() == 3 && !placements[2].refused.empty()
              && placements[2].refused[0].find("counts the deaths of the members of its encounter and has no time limit") != std::string::npos,
          "a stage with no spawn and no tracked slice needs a time limit that ends it (" + (placements.size() == 3 && !placements[2].refused.empty()
                                                                                             ? placements[2].refused[0] : std::string()) + ")");
}

/** QG5C3B: a summoned opponent with two sessions. */
void pattern_summon(const Options&) {
    PatternShop shop({summon_recipe()});
    // B enters 35 m from the root: out of the sphere of 30 m, in the interest of the root.
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 240.0F, 30.0F}), "control: B is 35 m from the root");
    shop.placeB = {100.0F, 240.0F, 30.0F};
    begin(shop);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.size() == 1 && run->participants.count(kCharacterA) == 1,
          "A (5 m) is a participant; B (35 m) is not");
    check(root_seen(shop.clientB).interest != 0 && links_seen(shop.clientB).empty(), "B observes the root and has no link");
    shop.wait(3.2F);
    const std::vector<world::Entity*> objects = shop.members(true);
    if (objects.size() != 1) {
        check(false, "control: the start object after the setup");
        return;
    }
    shop.gather(shop.clientA, EncounterShop::start_block(*objects.front()), 1500);
    shop.wait(0.3F);
    check(shop.progress(kCharacterA, "Test_Examine") == 1 && shop.progress(kCharacterB, "Test_Examine") == 0,
          "the event of the start object goes to its gatherer A only");
    check(shop.members(false).empty(), "the boss is not there before its delay of 1 s");
    shop.wait(1.0F);
    const std::vector<world::Entity*> bosses = shop.members(false);
    check(bosses.size() == 1 && bosses.front()->creature.encounter && world::horizontal_distance(bosses.front()->position, kMemberAt) < 0.01F,
          "the summoned boss (a direct creature member) comes at its spawn location after its delay, with the mark of its encounter");
    // B comes into the sphere before the kill: a late participant.
    shop.move(kCharacterB, {101.0F, 210.0F, 30.0F});
    shop.wait(0.6F);
    check(shop.instance()->participants.count(kCharacterB) == 1 && shop.entity(kCharacterB)->encounterLinks.size() == 1,
          "B in the sphere is a participant with its link");
    if (bosses.size() != 1) {
        return;
    }
    shop.kill(*bosses.front(), kCharacterA);
    shop.wait(0.2F);
    const RootSeen afterKill = root_seen(shop.clientA);
    check(statuses(afterKill, kKillSlot) == std::vector<std::uint32_t>{2, 3} && statuses(afterKill, kCleanupSlot) == std::vector<std::uint32_t>{2},
          "the death of the boss ends its stage; the cleanup starts");
    shop.wait(3.3F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.progress(kCharacterB, "Test_Defend") == 1
              && shop.encounters().counts().succeeded == 1 && a.encounterLinks.empty(),
          "the success: one event for each participant (A and B); the links end");
}

/** QG5C3C: an ambush with two sessions. */
void pattern_ambush(const Options&) {
    PatternShop shop({ambush_recipe()});
    // The characters enter far from the placement (no instance) and get their quests; then A comes 11 m from the proximity object (in the
    // detector of 30 m), and B into the sphere of the object (5 m) with no task of its tag.
    for (const char* character : {kCharacterA, kCharacterB}) {
        check(shop.host.store.set_character_position(character, {100.0F, 600.0F, 30.0F}), "control: the character is 400 m away");
    }
    shop.placeA = {100.0F, 600.0F, 30.0F};
    shop.placeB = {101.0F, 600.0F, 30.0F};
    begin(shop);
    check(shop.instance() == nullptr, "control: no instance with no character near");
    shop.grant(kCharacterA, "Test_Ambush");
    shop.grant(kCharacterA, "Test_Ambush_Join");
    shop.grant(kCharacterB, "Test_Ambush_Join");
    shop.grant(kCharacterA, "Test_Ambush_Win");
    shop.grant(kCharacterB, "Test_Ambush_Win");
    shop.move(kCharacterA, {100.0F, 212.0F, 30.0F});
    shop.move(kCharacterB, {101.0F, 201.0F, 30.0F});
    shop.wait(3.0F);
    const std::vector<world::Entity*> objects = shop.members(true);
    check(objects.size() == 1 && shop.instance() != nullptr && shop.instance()->members.begin()->second.proximity,
          "after the setup the proximity object is a server child of its template (its record)");
    for (const FakeClient* client : {&shop.clientA, &shop.clientB}) {
        check(variation_seen(*client).has_value(), std::string("the record of the proximity object for ") + (client == &shop.clientA ? "A" : "B"));
    }
    shop.wait(1.0F);
    check(shop.encounters().counts().objectsFound == 0 && shop.progress(kCharacterB, "Test_Ambush") == 9999 && shop.members(true).size() == 1,
          "B in the sphere with no task of the tag finds nothing (InteractOnlyOnQuest); A out of the sphere finds nothing");
    // The participants joined at the start: the participant event for each one (m_participantAddedGameEventId).
    check(shop.progress(kCharacterA, "Test_Ambush_Join") == 1 && shop.progress(kCharacterB, "Test_Ambush_Join") == 1,
          "the participant event goes to each participant (A and B)");
    shop.move(kCharacterA, {100.0F, 203.0F, 30.0F});
    shop.wait(0.6F);
    check(shop.encounters().counts().objectsFound == 1 && shop.members(true).empty() && shop.progress(kCharacterA, "Test_Ambush") == 1,
          "A in the sphere with the task finds the object: its event to A; the object goes");
    const RootSeen first = root_seen(shop.clientA);
    check(statuses(first, kInteractSlot) == std::vector<std::uint32_t>{2, 3} && statuses(first, kKillSlot) == std::vector<std::uint32_t>{2}
              && statuses(first, kChildSlotA) == std::vector<std::uint32_t>{2} && statuses(first, kChildSlotB).empty()
              && shop.members(false).size() == 2,
          "the kill stage starts and its first child stage (2 members); the second child stage waits");
    for (world::Entity* member : shop.members(false)) {
        shop.kill(*member, kCharacterA);
    }
    shop.wait(0.3F);
    check(shop.instance()->stages[2].destroyed == 2 && shop.instance()->stages[4].destroyed == 0,
          "the parent counts the deaths of the members of its child stage (2 of 3); the timed child counts nothing");
    // A late observer: B goes out of the interest and comes back.
    shop.move(kCharacterB, {100.0F, 400.0F, 30.0F});
    shop.wait(1.0F);
    const std::size_t from = shop.clientB.got.size();
    shop.move(kCharacterB, {101.0F, 208.0F, 30.0F});
    shop.wait(1.0F);
    const RootSeen late = root_seen(shop.clientB, from);
    check(late.interest != 0 && statuses(late, kKillSlot) == std::vector<std::uint32_t>{2} && statuses(late, kChildSlotA) == std::vector<std::uint32_t>{2}
              && statuses(late, kChildSlotB).empty(),
          "a late observer gets the current states of the parent and of the children (no replay)");
    // The first child stage ends at its 10 s: the second one starts; its member comes after 2 s; the visual is not made.
    shop.wait(6.5F);
    const RootSeen second = root_seen(shop.clientA);
    check(statuses(second, kChildSlotA) == std::vector<std::uint32_t>{2, 3} && statuses(second, kChildSlotB) == std::vector<std::uint32_t>{2},
          "the second child stage starts at the end of the first one (one after the other)");
    shop.wait(2.2F);
    const std::vector<world::Entity*> third = shop.members(false);
    check(third.size() == 1 && shop.encounters().counts().spawnsSkipped >= 1, "the member of the second child stage comes at 2 s; the visual is not made");
    if (third.size() != 1) {
        return;
    }
    shop.kill(*third.front(), kCharacterA);
    shop.wait(0.3F);
    const RootSeen waiting = root_seen(shop.clientA);
    check(shop.instance()->stages[2].destroyed == 3 && shop.instance()->stages[2].objectiveMet && statuses(waiting, kKillSlot) == std::vector<std::uint32_t>{2},
          "the parent has its count (3 of 3) and waits for its child stage that runs (m_childStagesOptional 0)");
    shop.wait(8.0F);
    const RootSeen ended = root_seen(shop.clientA);
    check(statuses(ended, kChildSlotB) == std::vector<std::uint32_t>{2, 3} && statuses(ended, kKillSlot) == std::vector<std::uint32_t>{2, 3},
          "the end of the last child stage ends the parent");
    shop.wait(3.3F);
    check(shop.progress(kCharacterA, "Test_Ambush_Win") == 1 && shop.progress(kCharacterB, "Test_Ambush_Win") == 1
              && shop.progress(kCharacterB, "Test_Ambush") == 9999,
          "the success event for each participant (A and B); B never got the event of the object");
}

/** QG5C3D: a kill wave with the causes of the data. */
void pattern_causes(const Options&) {
    json recipe = pattern_recipe(
        kKey, kRootAt,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                     stage_json("Wave_01 - Trigger", 0x200, kInteractSlot, false, destroyed(1, kTriggerSlice),
                                json::array({spawn_json(kTriggerSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_02 - Setup - Spawn Boss", 0x300, kKillSlot, false, destroyed(1, kBossSlice),
                                json::array({spawn_json(kBossSlice, 0, 1, kMemberAt)})),
                     stage_json("Wave_03 - Objective: Kill Lost", 0x400, kChildSlotA, false, destroyed(2),
                                json::array({spawn_json(kMemberSlice, 0, 1, kMemberAt), spawn_json(kSmokeSlice, 0, 1, kMemberAt)})),
                     stage_json("Wave_04 - Intermission", 0x500, kChildSlotB, false, timed(2),
                                json::array({spawn_json(kVisualSlice, 0, 1, kMemberAt), spawn_json(kHiddenSlice, 0, 1, kMemberAt)})),
                     stage_json("Wave_05 - Objective: Kill the Boss", 0x600, 11, false, destroyed(1), json::array()),
                     stage_json("Wave_06 - Cleanup", 0x700, kCleanupSlot, false, timed(1), json::array())}),
        {0, 1, 2, 3, 4, 5, 6});
    recipe["stages"][2]["objective"]["type"] = "SliceSpawnedObjective";
    recipe["stages"][5]["objective"]["timeLimit"] = 60;
    recipe["stages"][5]["objective"]["succeedOnTimeout"] = true;
    recipe["detector"] = {{"radius", 30.0},
                          {"filter", "OnlyPlayerTarget"},
                          {"sphere", false},
                          {"shapes", json::array({{{"shape", "cylinder"}, {"radius", 30.0}, {"height", 10.0}, {"center", {0.0, 0.0, 5.0}}}})}};
    PatternShop shop({recipe});
    // B is 15 m above the root: in the sphere of the trigger (20 m), above the cylinder of the detector (0 to 10 m above the root).
    check(shop.host.store.set_character_position(kCharacterB, {100.0F, 205.0F, 45.0F}), "control: B is 15 m above the root");
    shop.placeB = {100.0F, 205.0F, 45.0F};
    begin(shop);
    shop.grant(kCharacterA, "Test_Trigger");
    shop.grant(kCharacterB, "Test_Trigger");
    const std::vector<world::EncounterPlacement>& placements = shop.encounters().placements();
    check(placements.size() == 1 && placements[0].refused.empty(),
          "control: the runner takes the recipe (" + (placements.empty() || placements[0].refused.empty() ? std::string() : placements[0].refused[0])
              + ")");
    const world::EncounterInstance* run = shop.instance();
    check(run != nullptr && run->participants.count(kCharacterA) == 1 && run->participants.count(kCharacterB) == 0,
          "the cylinder: A (5 m away, 0 m up) is a participant; B above the cylinder is not");
    if (run == nullptr) {
        return;
    }
    shop.wait(2.6F);
    check(shop.progress(kCharacterA, "Test_Trigger") == 1 && shop.progress(kCharacterB, "Test_Trigger") == 1 && shop.members(true).empty()
              && shop.encounters().counts().objectsFound == 1,
          "the area trigger has no record; each character in its 20 m sphere gets its event; it counts for its stage (A "
              + std::to_string(shop.progress(kCharacterA, "Test_Trigger")) + ", B " + std::to_string(shop.progress(kCharacterB, "Test_Trigger"))
              + ", records " + std::to_string(shop.members(true).size()) + ", finds " + std::to_string(shop.encounters().counts().objectsFound)
              + ", B estimated " + std::to_string(shop.entity(kCharacterB)->positionEstimated) + ")");
    const RootSeen spawned = root_seen(shop.clientA);
    check(statuses(spawned, kInteractSlot) == std::vector<std::uint32_t>{2, 3} && statuses(spawned, kKillSlot) == std::vector<std::uint32_t>{2, 3}
              && statuses(spawned, kChildSlotA) == std::vector<std::uint32_t>{2} && shop.members(false).size() == 2,
          "the spawned objective ends at the spawn of the boss; the kill stage makes its member and its member that ends itself");
    for (world::Entity* creature : shop.members(false)) {
        if (shop.instance()->members.at(creature->id).stage == 3) {
            shop.kill(*creature, kCharacterA);
        }
    }
    shop.wait(0.3F);
    check(shop.instance()->stages[3].destroyed == 1 && shop.no_entity().first == 1, "one death of 2; the member that ends itself is still there");
    shop.wait(4.2F);
    const RootSeen afterSmoke = root_seen(shop.clientA);
    check(shop.encounters().counts().timedEnds == 1 && statuses(afterSmoke, kChildSlotA) == std::vector<std::uint32_t>{2, 3}
              && statuses(afterSmoke, kChildSlotB) == std::vector<std::uint32_t>{2},
          "the member that ends itself counts at its lifetime (4.2 s): the stage that tracks nothing ends (ends "
              + std::to_string(shop.encounters().counts().timedEnds) + ", destroyed " + std::to_string(shop.instance()->stages[3].destroyed) + ")");
    check(shop.encounters().counts().spawnsSkipped == 2, "the visual and the creature member that the client does not show are not made");
    shop.wait(2.2F);
    const RootSeen bossStage = root_seen(shop.clientA);
    check(statuses(bossStage, 11) == std::vector<std::uint32_t>{2}, "the stage of the boss runs (no spawn of its own)");
    for (world::Entity* creature : shop.members(false)) {
        shop.kill(*creature, kCharacterA);
    }
    shop.wait(0.3F);
    const RootSeen bossDead = root_seen(shop.clientA);
    check(statuses(bossDead, 11) == std::vector<std::uint32_t>{2, 3}, "the death of the boss of an earlier stage ends the stage that counts each death");
    shop.wait(1.3F);
    check(shop.progress(kCharacterA, "Test_Defend") == 1 && shop.progress(kCharacterB, "Test_Defend") == 0,
          "the success event for the participant A only (B above the cylinder)");
}

/** QG5C3E: the ends of the new members. */
void pattern_ends(const Options&) {
    {
        json recipe = pattern_recipe(kKey, kRootAt,
                                     json::array({stage_json("Wave_00 - Kill", 0x100, kKillSlot, false, destroyed(2),
                                                             json::array({spawn_json(kMemberSlice, 0, 1, kMemberAt),
                                                                          spawn_json(kSmokeSlice, 0, 1, kMemberAt)}))}),
                                     {0});
        PatternShop shop({recipe});
        begin(shop);
        check(shop.instance() != nullptr && shop.instance()->generation == 1 && shop.no_entity().first == 1, "control: generation 1 with its smoke");
        const std::uint64_t old = shop.encounters().counts().oldGeneration;
        check(shop.encounters().reset(kKey), "a reset while the smoke is due");
        shop.wait(0.6F);
        check(shop.instance() != nullptr && shop.instance()->generation == 2, "a new instance (generation 2)");
        shop.wait(3.0F);
        check(shop.instance()->stages[0].destroyed == 0 && shop.encounters().counts().oldGeneration > old
                  && shop.encounters().counts().timedEnds == 0,
              "at 4.2 s the end of the old smoke changes nothing (an old generation)");
        shop.wait(1.6F);
        check(shop.instance()->stages[0].destroyed == 1 && shop.encounters().counts().timedEnds == 1,
              "the smoke of generation 2 counts at its own end (4.2 s after its spawn)");
    }
    {
        PatternShop shop({ambush_recipe()});
        check(shop.host.store.set_character_position(kCharacterA, {100.0F, 212.0F, 30.0F}), "control: A out of the sphere of the object");
        shop.placeA = {100.0F, 212.0F, 30.0F};
        begin(shop);
        shop.grant(kCharacterA, "Test_Ambush");
        shop.wait(3.5F);
        const std::vector<world::Entity*> objects = shop.members(true);
        const world::EntityId oldObject = objects.empty() ? 0 : objects.front()->id;
        check(objects.size() == 1 && shop.encounters().reset(kKey), "a reset before the find");
        check(shop.host.world.find(oldObject) == nullptr, "the proximity object of the old generation goes with its instance");
        shop.move(kCharacterA, {100.0F, 203.0F, 30.0F});
        shop.wait(1.0F);
        check(shop.encounters().counts().objectsFound == 0, "no find during the setup of the new instance (no object yet)");
        shop.wait(3.0F);
        check(shop.encounters().counts().objectsFound == 1 && shop.instance()->generation == 2 && shop.instance()->stages[1].ended,
              "the find of A counts for the new generation");
        // A dormant end: both characters go far; the members go.
        shop.move(kCharacterA, {2000.0F, 2000.0F, 30.0F});
        shop.move(kCharacterB, {2000.0F, 2010.0F, 30.0F});
        shop.wait(1.0F);
        check(shop.instance() == nullptr && shop.encounters().counts().dormant == 1, "a dormant end with no character near");
    }
}

/** QG5C3F: the load of the ambushes. */
void pattern_load(const Options&) {
    std::vector<json> recipes;
    for (int index = 0; index < 4001; ++index) {
        const float x = index == 0 ? kRootAt.x : 2000.0F + static_cast<float>(index % 80) * 150.0F;
        const float y = index == 0 ? kRootAt.y : 2000.0F + static_cast<float>(index / 80) * 150.0F;
        char key[64];
        std::snprintf(key, sizeof key, "e4c03000-0000-4000-8000-%012d/17", index);
        json recipe = ambush_recipe(key, {x, y, 30.0F}, {x, y - 4.0F, 30.0F});
        recipe["stages"][0]["objective"]["timeLimit"] = 0.5;
        recipes.push_back(std::move(recipe));
    }
    PatternShop shop(recipes);
    begin(shop);
    const std::uint64_t scans = shop.encounters().counts().scans;
    const std::uint64_t looked = shop.encounters().counts().placementsLooked;
    const std::uint64_t proximity = shop.encounters().counts().proximityLooks;
    shop.wait(10.0F);
    const std::uint64_t scanCount = shop.encounters().counts().scans - scans;
    const std::uint64_t perScan = scanCount > 0 ? (shop.encounters().counts().placementsLooked - looked) / scanCount : 0;
    const std::uint64_t proximityPerScan = scanCount > 0 ? (shop.encounters().counts().proximityLooks - proximity) / scanCount : 0;
    check(shop.encounters().counts().taken == 4001 && scanCount >= 15 && perScan <= 4 && proximityPerScan <= 1
              && shop.encounters().instances().size() == 1,
          "4,001 ambushes: a scan looks at " + std::to_string(perScan) + " placements and " + std::to_string(proximityPerScan)
              + " proximity object (the instance near the characters)");
    const double longest = std::chrono::duration<double, std::milli>(shop.encounters().scan_load().largest).count();
    check(longest < 20.0, "the longest scan of the runner is below 20 ms (" + std::to_string(longest) + " ms)");
}

/** QG5C3G: the support manifest. */
void pattern_manifest(const Options&) {
    json refused = ambush_recipe("e4c00000-0000-4000-8000-000000000041/17", {700.0F, 205.0F, 30.0F}, {700.0F, 201.0F, 30.0F});
    refused["refused"] = {"an interaction sequence: stage QG5c4"};
    refused["successEvent"] = "Test_Refused_Success";
    json trigger = pattern_recipe(
        "e4c00000-0000-4000-8000-000000000042/17", {400.0F, 205.0F, 30.0F},
        json::array({stage_json("Wave_00 - Trigger", 0x100, kInteractSlot, false, destroyed(1, kTriggerSlice),
                                json::array({spawn_json(kTriggerSlice, 0, 1, {400.0F, 201.0F, 30.0F})})),
                     stage_json("Wave_01 - Kill", 0x200, kKillSlot, false, destroyed(1), json::array({spawn_json(kBossSlice, 0, 1, {403.0F, 206.0F, 30.0F})}))}),
        {0, 1});
    PatternShop shop({ambush_recipe(), trigger, refused});
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
    check(fact("Test_Ambush_Join").value("supported", false) && fact("Test_Ambush").value("supported", false)
              && fact("Test_Ambush_Win").value("supported", false) && fact("Test_Trigger").value("supported", false),
          "the participant event, the event of the proximity object, the success event, and the event of the area trigger have producers");
    check(fact("Test_Ambush").value("supplier", std::string{}).find("the proximity object of the encounter") != std::string::npos,
          "the producer of the event of the proximity object names it (" + fact("Test_Ambush").value("supplier", std::string{}) + ")");
    check(!fact("Test_Refused_Defend").value("supported", true)
              && fact("Test_Refused_Defend").value("reason", std::string{}).find("an interaction sequence: stage QG5c4") != std::string::npos,
          "the success event of the refused recipe names its cause");
}

} // namespace

std::vector<Case> encounter_pattern_cases() {
    return {{"QG5C3A", "encounters QG5c3 (I3): the file (child stages, the spawned objective, the shapes, the new members) and the refusals",
             pattern_file},
            {"QG5C3B", "encounters QG5c3 (I1, I6): a summoned opponent with two sessions (a nearby character, a late participant)", pattern_summon},
            {"QG5C3C",
             "encounters QG5c3 (I2, I6): an ambush with two sessions: the proximity object and its quest gate, the participant event, the child "
             "stages, a late observer",
             pattern_ambush},
            {"QG5C3D",
             "encounters QG5c3 (I3, I6): a kill wave with the causes of the data (an area trigger, a spawned objective, a member that ends itself, "
             "a stage that counts each death, a cylinder detector)",
             pattern_causes},
            {"QG5C3E", "encounters QG5c3 (I6): a reset while a member is due, a reset before a find, a dormant end", pattern_ends},
            {"QG5C3F", "encounters QG5c3 (I7): 4,001 ambushes; a scan looks only near the characters (time limit)", pattern_load},
            {"QG5C3G", "encounters QG5c3 (I5): the support manifest takes the producers of the new patterns", pattern_manifest}};
}

} // namespace soulwarden::backend::tests

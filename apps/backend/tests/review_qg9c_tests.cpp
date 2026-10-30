// The cases of the code review of stage QG9c (corrected in stage QG9d; the findings:
// ~/Games/soulwarden/work/evidence/quests/qg9c/code-review/FINDINGS.md). Each case of a finding fails on the tree of stage QG9c
// (~/Games/soulwarden/work/trees/after-qg9c) and passes now. The base of the cases is the proof cases QG9CRV1, QG9CRV2, QG9CRV3, and QG9CRV5 of
// the review. The accounts, characters, kinds, and places are made up.
//
//   QG9CR1  finding QG9CR-R1: a creature member of an instance recipe on a roof (navigation floor 30 m, terrain 22 m) whose navigation tile
//           is not in the world yet waits for the tile: the boss, its anchor, and its home stay on the roof; the control with the tile in
//           the world gives the same place
//   QG9CR2  finding QG9CR-R2: A hits a brute (no ClearDamaged action) between the enter and the leave distance of A and B, and it dies: A and
//           B each get the renewed body after the gap (the leave distance holds for an observer that had it), for a slice of 60 m and one
//           of 100 m (beyond the radius of the scan); control: the body inside the enter distance
//   QG9CR3  finding QG9CR-R3: two sibling waves of one slice, each with its creature in a child setup: the death that the first wave counted
//           does not count for the second wave (A); B in its own instance keeps its first wave
//   QG9CR4  finding QG9CR-R4: a stage with an EncounterEventObjective and a destroyed objective in `more`: the met event does not meet the
//           destroyed objective, and the stage does not end while its creature lives (A and B, each in its own instance)
//   QG9CRC  control (passes on both trees): the renewed body of A and B has its health fields and its Healthy skinning state, one renewal,
//           and no record after a removal in the gap

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

#include "support/range_fixture.h"
#include "support/story_fixture.h"
#include "support/test_navigation.h"

namespace soulwarden::backend::tests {
namespace {

gamedata::CreatureSlice no_clear_slice() {
    auto slice = range::wolf_slice();
    slice.actions.erase(std::remove_if(slice.actions.begin(), slice.actions.end(), [](const auto& action) { return action.name == "Reaction"; }),
                        slice.actions.end());
    return slice;
}

void roof_spawn(const Options& options) {
    const auto folder = options.workDirectory / "qg9cr1-roof";
    TestNavRegion region;
    region.ground = 22;
    region.tiles[{4, 4}] = [](int, int, std::vector<TestFloor>& floors) {
        floors[0].sightTo = 7;
        floors.push_back({30, 0});
    };
    region.write(folder);
    for (const bool warm : {false, true}) {
        storyfix::StoryShop shop;
        shop.host.world.set_creature_behavior(false);
        shop.host.terrain.source = [](float, float) { return std::optional<float>(22); };
        check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation has the terrain at 22 m and a roof at 30 m");
        shop.enter();
        shop.host.clock.advance(std::chrono::milliseconds(500));
        shop.host.world.navigation().tick();
        if (warm) {
            (void)shop.host.world.navigation().find_path(storyfix::kBossAt, storyfix::kBossAt);
        }
        // A direct entry: the character is in the space of the instance before the next look of the navigation at the wanted tiles.
        check(shop.instances().enter_mode(encfix::kCharacterA, storyfix::kMode).error == world::InstanceError::none, "setup: A enters the mode");
        world::Entity& entrant = *shop.entity(encfix::kCharacterA);
        world::PositionTrack track;
        check(shop.host.world.report_position(entrant.owner, world::PositionReport{1, entrant.position, wire_height(entrant.position.z)}, track)
                  == world::PositionVerdict::accepted,
              "setup: the client confirms the place of the entry");
        world::Entity* boss = nullptr;
        for (int step = 0; step < 40 && boss == nullptr; ++step) {
            shop.host.clock.advance(std::chrono::milliseconds(10));
            shop.host.world.tick();
            boss = shop.boss(shop.entity(encfix::kCharacterA)->space);
        }
        check(boss != nullptr, "setup: the boss is made");
        if (boss == nullptr) {
            continue;
        }
        const std::optional<float> floor = shop.host.world.navigation().ground_height(storyfix::kBossAt);
        note("QG9CR1 warm=" + std::to_string(warm) + " boss=" + std::to_string(boss->position.z) + " anchor="
             + std::to_string(boss->creature.mind.anchor.z) + " home=" + std::to_string(boss->creature.mind.home.z) + " floor="
             + std::to_string(floor.value_or(-1.0F)));
        check(floor && std::fabs(*floor - 30.0F) < 0.01F, "control: the floor of the navigation at the spawn location is the roof");
        check(std::fabs(boss->position.z - 30.0F) < 0.01F && std::fabs(boss->creature.mind.anchor.z - 30.0F) < 0.01F
                  && std::fabs(boss->creature.mind.home.z - 30.0F) < 0.01F,
              std::string(warm ? "the tile in the world: " : "a tile that is not in the world yet: ")
                  + "the boss, its anchor, and its home stay on the roof");
    }
    std::filesystem::remove_all(folder);
}

void renewal_margin(const Options&) {
    for (const float radius : {60.0F, 100.0F}) {
        range::Range rig(true, true);
        gamedata::CreatureSlice slice = no_clear_slice();
        slice.interestRadius = radius;
        check(rig.host.creatures.set_full({slice}, {range::wolf_kind()}, {}, {{1, range::kWolfHealth, 100.0F, 50.0F}}),
              "setup: a slice with a death action and no ClearDamaged action");
        rig.host.world.set_creature_behavior(false);
        rig.enter_both();
        world::Entity* creature = rig.host.world.creatures().spawn("Test_Wolf", range::at(range::kPlace.x + 2.0F, range::kPlace.y), 90).entity;
        check(creature != nullptr && rig.until([&] {
            return range::interest_in(rig.host, 1, creature->id) != 0 && range::interest_in(rig.host, 2, creature->id) != 0;
        }),
              "setup: A and B observe the creature");
        if (creature == nullptr) {
            return;
        }
        check(rig.strike(*creature, "Test_Light").verdict == world::HitVerdict::accepted, "setup: A hits the creature");
        const world::EntityId id = creature->id;
        const std::uint32_t oldA = range::interest_in(rig.host, 1, id);
        const std::uint32_t oldB = range::interest_in(rig.host, 2, id);
        const float enter = rig.host.world.interest_enter(*creature);
        const float leave = rig.host.world.interest_leave(*creature);
        rig.host.world.place_creature(*creature, range::at(range::kPlace.x + (enter + leave) / 2.0F, range::kPlace.y));
        rig.step(20);
        check(range::interest_in(rig.host, 1, id) == oldA && range::interest_in(rig.host, 2, id) == oldB,
              "control: A and B keep the live creature between the enter and the leave distance");
        check(rig.host.world.creatures().set_health(id, 0).error == world::CreatureError::none, "setup: the creature dies");
        rig.step(40);
        note("QG9CR2 radius=" + std::to_string(radius) + " enter=" + std::to_string(enter) + " leave=" + std::to_string(leave)
             + " A=" + std::to_string(range::interest_in(rig.host, 1, id)) + " B=" + std::to_string(range::interest_in(rig.host, 2, id)));
        check(range::interest_in(rig.host, 1, id) != 0 && range::interest_in(rig.host, 2, id) != 0,
              "A and B get the renewed body after the gap (the leave distance of an observer that had it)");
        rig.host.world.place_creature(*creature, range::at(range::kPlace.x + 2.0F, range::kPlace.y));
        check(rig.until([&] { return range::interest_in(rig.host, 1, id) != 0 && range::interest_in(rig.host, 2, id) != 0; }),
              "control: the body inside the enter distance is in both tables");
    }
}

using namespace storyfix;

json member_spawn() {
    return {{"member", encfix::kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({encfix::place_json(kBossAt)})}};
}

json stages_document(json stages, json order = json::array({0})) {
    json document = story_document();
    document["format"] = 3;
    document["recipes"][0]["stages"] = std::move(stages);
    document["recipes"][0]["stageOrder"] = std::move(order);
    return document;
}

std::vector<world::EntityId> living_members(StoryShop& shop, world::SpaceId space, unsigned stage) {
    std::vector<world::EntityId> found;
    if (const world::EncounterInstance* run = shop.story_run(space)) {
        for (const auto& [id, member] : run->members) {
            const world::Entity* entity = shop.host.world.find(id);
            if (member.stage == stage && entity != nullptr && entity->kind == world::EntityKind::creature && !entity->creature.dead) {
                found.push_back(id);
            }
        }
    }
    return found;
}

void child_setup_waves(const Options&) {
    json parent = encfix::stage_json("Fight", 0xB00, 11, false, encfix::timed(120), json::array());
    parent["children"] = {1, 2};
    parent["childrenOptional"] = true;
    json first = encfix::stage_json("First wave", 0xB10, 12, true, encfix::destroyed(1, encfix::kMemberSlice), json::array());
    first["children"] = {3};
    json second = encfix::stage_json("Second wave", 0xB20, 13, true, encfix::destroyed(1, encfix::kMemberSlice), json::array());
    second["children"] = {4};
    const json none = {{"type", "none"}, {"end", "start"}};
    json setup1 = encfix::stage_json("First wave setup", 0xB30, 14, false, none, json::array({member_spawn()}));
    json setup2 = encfix::stage_json("Second wave setup", 0xB40, 15, false, none, json::array({member_spawn()}));
    StoryShop shop(":memory:", stages_document({parent, first, second, setup1, setup2}));
    shop.enter();
    shop.enter_story(encfix::kCharacterA);
    shop.enter_story(encfix::kCharacterB);
    const world::SpaceId spaceA = shop.entity(encfix::kCharacterA)->space;
    const world::SpaceId spaceB = shop.entity(encfix::kCharacterB)->space;
    const std::vector<world::EntityId> firstA = living_members(shop, spaceA, 3);
    check(firstA.size() == 1 && living_members(shop, spaceB, 3).size() == 1, "setup: each instance has the creature of its first wave setup");
    if (firstA.empty()) {
        return;
    }
    shop.kill(*shop.host.world.find(firstA.front()), encfix::kCharacterA);
    shop.wait(0.3F);
    const world::EncounterInstance* run = shop.story_run(spaceA);
    check(run != nullptr && run->stages[1].ended && run->stages[2].started, "setup: A reaches the second wave");
    if (run == nullptr) {
        return;
    }
    note("QG9CR3 secondCount=" + std::to_string(run->stages[2].destroyed) + " secondEnded=" + std::to_string(run->stages[2].ended)
         + " secondLiving=" + std::to_string(living_members(shop, spaceA, 4).size()));
    check(run->stages[2].destroyed == 0 && !run->stages[2].ended,
          "A: the second wave does not count the death that the first wave counted through its child setup");
    check(living_members(shop, spaceA, 4).size() == 1, "A: the creature of the second wave setup lives");
    const world::EncounterInstance* runB = shop.story_run(spaceB);
    check(runB != nullptr && !runB->stages[2].started && living_members(shop, spaceB, 3).size() == 1, "B: the first wave lives in its instance");
}

void event_and_kill(const Options&) {
    json event = storyfix::objective_of("EncounterEventObjective", 0, false);
    event["eventId"] = "Review_Intro";
    event["more"] = json::array({encfix::destroyed(1, encfix::kMemberSlice)});
    json stage = encfix::stage_json("Event and kill", 0xC00, 11, true, event, json::array({member_spawn()}));
    json hold = encfix::stage_json("Hold", 0xC10, 12, false, encfix::timed(120), json::array());
    StoryShop shop(":memory:", stages_document({stage, hold}, {0, 1}));
    shop.enter();
    shop.enter_story(encfix::kCharacterA);
    shop.enter_story(encfix::kCharacterB);
    for (const char* who : {encfix::kCharacterA, encfix::kCharacterB}) {
        const world::SpaceId space = shop.entity(who)->space;
        const world::EncounterInstance* run = shop.story_run(space);
        check(run != nullptr && !living_members(shop, space, 0).empty(), std::string("setup: a living tracked creature in the instance of ") + who);
        if (run == nullptr) {
            continue;
        }
        note("QG9CR4 " + std::string(who) + " eventMet=" + std::to_string(run->stages[0].met[0]) + " killMet="
             + std::to_string(run->stages[0].met[1]) + " killCount=" + std::to_string(run->stages[0].counts[1]) + " ended="
             + std::to_string(run->stages[0].ended));
        check(run->stages[0].met[0] && !run->stages[0].met[1] && !run->stages[0].ended,
              std::string(who) + ": the met event does not meet the destroyed objective, and the stage waits for the kill");
    }
    // The kill of A ends the stage of A (the destroyed objective is met then); B keeps its stage.
    const world::SpaceId spaceA = shop.entity(encfix::kCharacterA)->space;
    const std::vector<world::EntityId> living = living_members(shop, spaceA, 0);
    if (!living.empty()) {
        shop.kill(*shop.host.world.find(living.front()), encfix::kCharacterA);
        shop.wait(0.3F);
        const world::EncounterInstance* run = shop.story_run(spaceA);
        check(run != nullptr && run->stages[0].ended && run->stages[1].started, "A: the kill ends the stage, and the next stage starts");
    }
    const world::EncounterInstance* runB = shop.story_run(shop.entity(encfix::kCharacterB)->space);
    check(runB != nullptr && !runB->stages[0].ended, "B: its stage waits for its own kill");
}

void corpse_control(const Options&) {
    gather::GatherShop shop;
    gamedata::CreatureSlice slice = bags::bag_wolf_slice();
    slice.actions.erase(std::remove_if(slice.actions.begin(), slice.actions.end(), [](const auto& action) { return action.name == "Reaction"; }),
                        slice.actions.end());
    check(shop.host.creatures.set_full({slice}, {bags::bag_wolf_kind()}, {}, {{20, bags::kWolfHealth, 0, 0}}),
          "setup: a slice that can be skinned and has no ClearDamaged action");
    shop.enter();
    world::Entity* creature = shop.host.world.creatures().spawn("Test_BagWolf", bags::kKillPlace, 0).entity;
    check(creature != nullptr, "setup: the creature is in the world");
    if (creature == nullptr) {
        return;
    }
    const world::EntityId id = creature->id;
    shop.host.steps(100, {&shop.clientA, &shop.clientB});
    check(range::interest_in(shop.host, 1, id) != 0 && range::interest_in(shop.host, 2, id) != 0, "setup: A and B observe the creature");
    (void)shop.host.world.creatures().hurt(id, bags::kWolfHealth * 2, shop.entity(encfix::kCharacterA)->id);
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    check(range::interest_in(shop.host, 1, id) == 0 && range::interest_in(shop.host, 2, id) == 0, "the gap of the renewal: no record in A and B");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.host.steps(50, {&shop.clientA, &shop.clientB});
    for (const auto& [session, client, from] : {std::tuple{1U, &shop.clientA, fromA}, std::tuple{2U, &shop.clientB, fromB}}) {
        const std::uint32_t interest = range::interest_in(shop.host, session, id);
        const auto states = gather::node_states(*client, from);
        check(interest != 0 && std::find(states.begin(), states.end(), std::pair{interest, gather::kHealthy}) != states.end(),
              "the new record has the Healthy skinning state of the body");
        const auto fragments = range::fragments_for(*client, interest, from);
        const auto vitals = std::find_if(fragments.begin(), fragments.end(), [](const Fragment& fragment) { return fragment.type == 15; });
        check(vitals != fragments.end() && vitals->fields.count({0, 0}) != 0 && vitals->fields.count({0, 2}) != 0 && vitals->fields.count({0, 4}) != 0,
              "the new record has the health, the maximum health, and the vitals data");
    }
    check(creature->creature.renewal == 1 && shop.host.world.creatures().set_health(id, 0).error == world::CreatureError::dead,
          "a second death request is refused, with no second renewal");
    world::Entity* removed = shop.host.world.creatures().spawn("Test_BagWolf", bags::kKillPlace, 0).entity;
    check(removed != nullptr, "setup: a second creature for a removal in the gap");
    if (removed == nullptr) {
        return;
    }
    const world::EntityId removedId = removed->id;
    shop.host.steps(100, {&shop.clientA, &shop.clientB});
    (void)shop.host.world.creatures().hurt(removedId, bags::kWolfHealth * 2, shop.entity(encfix::kCharacterA)->id);
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    check(shop.host.world.creatures().remove(removedId).error == world::CreatureError::none, "the removal of the body in the gap");
    shop.host.steps(100, {&shop.clientA, &shop.clientB});
    check(shop.host.world.find(removedId) == nullptr && range::interest_in(shop.host, 1, removedId) == 0
              && range::interest_in(shop.host, 2, removedId) == 0,
          "no session makes a removed body again after the gap");
}

} // namespace

std::vector<Case> review_qg9c_cases() {
    return {{"QG9CR1", "code review QG9c R1: a roof spawn of an instance recipe waits for its navigation tile", roof_spawn},
            {"QG9CR2", "code review QG9c R2: the renewal keeps an observer in the leave margin (A and B, two radii)", renewal_margin},
            {"QG9CR3", "code review QG9c R3: a later wave does not count a death of a child setup of an earlier wave", child_setup_waves},
            {"QG9CR4", "code review QG9c R4: a met event does not meet a destroyed objective of the same stage", event_and_kill},
            {"QG9CRC", "code review QG9c: control of the renewed body (passes on both trees)", corpse_control}};
}

} // namespace soulwarden::backend::tests

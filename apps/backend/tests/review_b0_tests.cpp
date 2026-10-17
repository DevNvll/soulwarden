// The corrections of the code review of stage B0 (~/Games/soulwarden/work/evidence/bow-build/b0/code-review/FINDINGS.md, findings B0-R1 to
// B0-R6; corrected in stage B1). Each case fails on the tree of stage B0 (4cfa9cf) and passes with the correction. All ids, names, and
// values are made up: no file of the game and no value of a capture is here.
//
//   RQ1  B0-R1  a free push of 0.9 m next to a wall: the kept path stays only when the real segment from the new place to its next cell
//               is free (the floor checks of the slide); the next steps do not cross the wall
//   RQ2  B0-R2  a build preset that replaces the rapier in the hand in a Riposte window: the window ends, and the next hit of a creature
//               does damage
//
// The findings B0-R3 to B0-R5 (the artifact objectives of the item catalog) are the cases BP10 to BP12 (build_preset_tests.cpp). The
// finding B0-R6 (the form of the tool window) has no test program: the hook has no test form (live screenshots of stage B1).

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/idle_fixture.h"
#include "support/motion_fixture.h"
#include "support/test_navigation.h"
#include "world/build_presets.h"
#include "world/navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace std::chrono_literals;

state::WorldPosition ground(float x, float y) {
    return state::WorldPosition{x, y, 50.0F};
}

/** RQ1 (B0-R1): the kept path after a free push of 0.9 m next to a wall. */
void path_after_long_push(const Options& options) {
    ServerHost host;
    host.world.set_creature_behavior(false);
    check(host.creatures.set_full({idle::idle_wolf_slice()}, {idle::kind_of("Test_Wolf", "Solo", 0x21112222)}, {}, {{1, shop::kWolfHealth, 100.0F, 50.0F}}),
          "setup: the creature data");
    const std::filesystem::path folder = options.workDirectory / "rq1-wall";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    // A wall cell block below the straight path from (1000.125, 1000.125) to (1004.125, 1000.125): x 1001 to 1001.5, y 999.5 to 1000.
    region.tiles[{15, 15}] = [](int cx, int cy, std::vector<TestFloor>& floors) {
        const float x = 960.0F + (static_cast<float>(cx) + 0.5F) * 0.25F;
        const float y = 960.0F + (static_cast<float>(cy) + 0.5F) * 0.25F;
        if (x >= 1001.0F && x < 1001.5F && y >= 999.5F && y < 1000.0F) {
            floors = {{53.0F, gamedata::NavFloor::kBlocked}};
        }
    };
    region.write(folder);
    check(host.navigation.set_directory(folder.string()), "setup: the navigation data of the wall loads");
    world::Navigation& navigation = host.world.navigation();
    const state::WorldPosition goal = ground(1004.125F, 1000.125F);
    // One run: a wolf on its free path above the wall, a push to the south at 1 m/s for `push`, then the steps to the goal.
    struct Run {
        bool setup = false;
        float pushedY = 0.0F;
        bool ended = false;
        bool crossed = false;
        bool reached = false;
    };
    const auto run = [&](std::chrono::milliseconds push) {
        Run result;
        world::Entity* wolf = host.world.creatures().spawn("Test_Wolf", ground(1000.125F, 1000.125F), 0.0F).entity;
        if (wolf == nullptr) {
            return result;
        }
        world::NavStep first;
        for (int index = 0; index < 30; ++index) {
            first = navigation.next_step(wolf->id, wolf->position, goal, 0.05F, false);
            if (first.kind == world::StepKind::moved) {
                break;
            }
            host.clock.advance(10ms);
            host.world.tick();
        }
        if (first.kind != world::StepKind::moved || navigation.slide(wolf->position, first.position).blocked) {
            return result;
        }
        host.world.creatures().move(*wolf, first.position);
        const state::WorldPosition source = ground(wolf->position.x, wolf->position.y + 1.0F);
        if (!host.world.creatures().impact(*wolf, motion::impact_of(1.0F, 0.0F), source, source, "Test_SouthPush", push)) {
            return result;
        }
        for (int index = 0; index < 30 && wolf->creature.forced; ++index) {
            host.clock.advance(50ms);
            host.world.tick();
        }
        result.setup = !wolf->creature.forced;
        result.pushedY = wolf->position.y;
        const std::uint64_t ended = navigation.counts().pushedOffPath;
        for (int index = 0; index < 400 && world::horizontal_distance(wolf->position, goal) > 0.2F; ++index) {
            const state::WorldPosition from = wolf->position;
            const world::NavStep step = navigation.next_step(wolf->id, from, goal, 0.2F, false);
            if (step.kind == world::StepKind::moved) {
                if (navigation.slide(from, step.position).blocked) {
                    note("a push of " + std::to_string(push.count()) + " ms: a step from " + std::to_string(from.x) + " " + std::to_string(from.y)
                         + " to " + std::to_string(step.position.x) + " " + std::to_string(step.position.y) + " crosses the wall");
                    result.crossed = true;
                    break;
                }
                host.world.creatures().move(*wolf, step.position);
            }
            if (index == 0) {
                result.ended = navigation.counts().pushedOffPath == ended + 1;
            }
            host.clock.advance(10ms);
            host.world.tick();
        }
        result.reached = world::horizontal_distance(wolf->position, goal) <= 0.2F;
        return result;
    };
    const Run main = run(900ms);
    check(main.setup && shop::near(main.pushedY, 999.225F, 0.001F), "setup: a free path above the wall; the push ends 0.9 m from it");
    check(main.ended, "the push ended the path: the segment from the new place to its next cell is not free");
    check(!main.crossed, "after the push of 0.9 m no step crosses the wall (the direct path from the new place needs its real segment too)");
    check(main.reached, "the wolf reaches the goal around the wall");
    // The pushes of 0.2 s to 1.0 s: no step crosses the wall, and the wolf reaches the goal.
    bool sweep = true;
    for (int tenth = 2; tenth <= 10; ++tenth) {
        const Run other = run(std::chrono::milliseconds(tenth * 100));
        sweep = sweep && other.setup && !other.crossed && other.reached;
    }
    check(sweep, "the pushes of 0.2 s to 1.0 s: no step crosses the wall, and each wolf reaches the goal");
}

/** RQ2 (B0-R2): a build preset that replaces the rapier in a Riposte window. */
void preset_in_riposte(const Options&) {
    motion::MotionShop shop;
    check(shop.host.perks.set(shop::shop_perks()), "setup: the perk data");
    shop.hold("TestRapier");
    world::BuildPreset preset;
    preset.name = "test-hammer";
    preset.title = "A test weapon";
    preset.level = 1;
    preset.attributes.fill(5);
    world::PresetItem hammer;
    hammer.itemId = "TestHammer";
    hammer.gearScore = 100;
    hammer.slot = world::paperdoll::kMainHand1;
    preset.items = {hammer};
    preset.variants = {{"default", "Default", {}}};
    shop.world().presets().set({preset});
    check(shop.world().presets().presets().front().enabled, "setup: the preset passes the check at the start");
    world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
    world::AbilityPress press;
    press.place = 0;
    check(shop.world().abilities().press(shop.character()->owner, press) == world::CastVerdict::accepted, "setup: Riposte is accepted");
    check(shop.world().statuses().has(*shop.character(), "Rapier_Riposte_Timer"), "setup: the window of Riposte is open");
    const world::PresetResult result = shop.world().presets().apply(shop::kCharacterA, "test-hammer", "");
    const world::ItemInstance* weapon = world::Combat::weapon_in_hand(*shop.character());
    check(result.ok() && weapon != nullptr && weapon->itemId == "TestHammer", "setup: the preset puts the hammer into the slot of the rapier");
    check(!shop.world().statuses().has(*shop.character(), "Rapier_Riposte_Timer"), "the preset ends the window of Riposte (a change of the weapon)");
    gamedata::CreatureContact bite;
    bite.row = "Test_Bite";
    bite.type = "Slash";
    bite.wire = 4;
    bite.reach = 2.0F;
    bite.factor = 1.0F;
    const float before = shop.character()->vitals.health;
    shop.world().health().creature_hit(*wolf, bite, *shop.character());
    check(shop.character()->vitals.health < before, "the next bite of the wolf does damage: no absorption of the old window");
}

} // namespace

std::vector<Case> review_b0_cases() {
    return {{"RQ1", "B0-R1: a path after a free push of 0.9 m stays only with a free segment to its next cell (no step through a wall)",
             path_after_long_push},
            {"RQ2", "B0-R2: a build preset that replaces the rapier in the hand ends the window of Riposte", preset_in_riposte}};
}

} // namespace soulwarden::backend::tests

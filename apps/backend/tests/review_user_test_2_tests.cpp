// The corrections of the code review of user test 2 (~/Games/soulwarden/work/evidence/user-test-2/code-review/FINDINGS.md, findings U2-R1
// to U2-R6; corrected in stage B0). Each case fails on the tree of user test 2 (8514129) and passes with the correction. All ids, names,
// and values are made up: no file of the game and no value of a capture is here. The proof on the tree of user test 2 builds this file with
// SOULWARDEN_B0_BASELINE (the counter pushedOffPath of the stage counts as absent).
//
//   RT1  U2-R1  a short sideways push next to a wall: the path stays only when the line from the new place to its next cell is free;
//               the next steps do not cross the wall (the slide rule of the navigation refuses none of them); a path from the new place
//               reaches the goal
//   RT2  U2-R2  a Magic row of power 0 plays no reaction and gives no hit stun (the branch below power 1 of the action list names only
//               Ability); an Ability row of power 0 and a Magic row of power 1 play one
//   RT3  U2-R3  the reaction limit ends 2 s after the real exit of its reaction (a step ended it early), not 2 s after its natural end
//   RT4  U2-R4  a combat file with the member `reactions` must have a reaction for each damage row: a partial or an empty list does not
//               load; a file with no member loads (the old rule)
//   RT5  U2-R5  a passive creature on a wander (rest mode) waits for its hit stun: no step, and the reaction plays
//   RT6  U2-R6  three exports of the gathering tool while a backend holds the first manifest: its region files stay, and that backend
//               reads its node; a new backend reads the current export; --prune removes the old sets only with no backend

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/combat_tables.h"
#include "gamedata/gathering_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/idle_fixture.h"
#include "support/test_navigation.h"
#include "world/behavior.h"
#include "world/navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace idle;
using nlohmann::json;
using namespace std::chrono_literals;

state::WorldPosition ground(float x, float y) {
    return state::WorldPosition{x, y, 50.0F};
}

std::vector<std::string> run_tool_check(const Options& options, const std::vector<std::string>& arguments, const char* log) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_gathering_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / log;
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + path.lexically_normal().string() + "\"";
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    command += " > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream file(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        note(line.substr(0, 220));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

bool line_ok(const std::vector<std::string>& lines, const std::string& name) {
    bool ok = false;
    for (const std::string& line : lines) {
        if (line.find(name + ":") == std::string::npos) {
            continue;
        }
        if (line.rfind("FAIL", 0) == 0) {
            return false;
        }
        ok = ok || line.rfind("ok", 0) == 0;
    }
    return ok;
}

/** RT1 (U2-R1): a path after a short sideways push next to a wall. */
void path_beside_wall(const Options& options) {
    ServerHost host;
    host.world.set_creature_behavior(false);
    check(host.creatures.set_full({idle_wolf_slice()}, {kind_of("Test_Wolf", "Solo", 0x21112222)}, {}, {{1, shop::kWolfHealth, 100.0F, 50.0F}}),
          "setup: the creature data");
    const std::filesystem::path folder = options.workDirectory / "rt1-wall";
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
    world::Entity* wolf = host.world.creatures().spawn("Test_Wolf", ground(1000.125F, 1000.125F), 0.0F).entity;
    check(wolf != nullptr, "setup: the wolf");
    if (wolf == nullptr) {
        return;
    }
    const state::WorldPosition goal = ground(1004.125F, 1000.125F);
    world::NavStep first;
    for (int index = 0; index < 20; ++index) {
        first = navigation.next_step(wolf->id, wolf->position, goal, 0.1F, false);
        if (first.kind == world::StepKind::moved) {
            break;
        }
        host.clock.advance(10ms);
        host.world.tick();
    }
    check(first.kind == world::StepKind::moved, "setup: a direct path above the wall");
    host.world.creatures().move(*wolf, first.position);
    const state::WorldPosition before = wolf->position;
    check(host.world.creatures().impact(*wolf, impact_of(1.0F, 0.0F), ground(before.x, before.y + 1.0F), ground(before.x, before.y + 1.0F),
                                        "Test_SidePush", 500ms),
          "setup: a sideways push");
    for (int index = 0; index < 12 && wolf->creature.forced; ++index) {
        host.clock.advance(50ms);
        host.world.tick();
    }
    const state::WorldPosition after = wolf->position;
    check(!wolf->creature.forced && near(after.y, before.y - 0.5F, 0.02F), "setup: the push ends 0.5 m from the path");
#ifndef SOULWARDEN_B0_BASELINE
    const std::uint64_t ended = navigation.counts().pushedOffPath;
#endif
    bool crossed = false;
    for (int index = 0; index < 8; ++index) {
        const state::WorldPosition from = wolf->position;
        const world::NavStep step = navigation.next_step(wolf->id, from, goal, 0.4F, false);
        if (step.kind != world::StepKind::moved) {
            break;
        }
        if (navigation.slide(from, step.position).blocked) {
            note("a step from " + std::to_string(from.x) + " " + std::to_string(from.y) + " to " + std::to_string(step.position.x) + " "
                 + std::to_string(step.position.y) + " crosses the wall");
            crossed = true;
            break;
        }
        host.world.creatures().move(*wolf, step.position);
    }
    check(!crossed, "after the short push no step of the kept path crosses the wall");
#ifndef SOULWARDEN_B0_BASELINE
    check(navigation.counts().pushedOffPath == ended + 1, "the push ended the path: the line from the new place to its next cell is not free");
#endif
    // From the new place: a path that goes around the wall and reaches the goal.
    bool controlCrossed = false;
    for (int index = 0; index < 200 && world::horizontal_distance(wolf->position, goal) > 0.2F; ++index) {
        const state::WorldPosition from = wolf->position;
        const world::NavStep step = navigation.next_step(wolf->id, from, goal, 0.4F, false);
        if (step.kind == world::StepKind::moved) {
            controlCrossed = controlCrossed || navigation.slide(from, step.position).blocked;
            host.world.creatures().move(*wolf, step.position);
        }
        host.clock.advance(10ms);
        host.world.tick();
    }
    check(!controlCrossed && world::horizontal_distance(wolf->position, goal) <= 0.2F, "the wolf reaches the goal around the wall");
}

/** RT2 (U2-R2): a Magic hit of power 0. */
void magic_power_zero(const Options&) {
    IdleShop shop;
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    world::Creatures& creatures = shop.world().creatures();
    const std::uint64_t count = creatures.counts().reactions;
    const world::CreatureResult result =
        creatures.hurt(wolf->id, 1.0F, shop.character()->id, 0, world::hit_reaction(reaction_row("Test_Magic", "Magic", std::nullopt, false, 0, 15)));
    check(result.error == world::CreatureError::none && creatures.counts().reactions == count && !wolf->creature.hitStunUntil,
          "a Magic row of power 0: no reaction and no hit stun (the branch below power 1 names only Ability)");
    const world::WorldClock::time_point now = shop.host.clock.now();
    check(creatures.reaction_allowed(*wolf, world::hit_reaction(reaction_row("Test_Ability", "Ability", std::nullopt, false, 0, 0)), now),
          "an Ability row of power 0 on a Solo creature: a reaction (the branch below power 1)");
    check(creatures.reaction_allowed(*wolf, world::hit_reaction(reaction_row("Test_StrongMagic", "Magic", std::nullopt, false, 1, 0)), now),
          "a Magic row of power 1: a reaction (the branch of power 1 or more)");
    check(!creatures.reaction_allowed(*wolf, world::hit_reaction(reaction_row("Test_Spell", "MagicSpell", std::nullopt, false, 0, 15)), now),
          "a MagicSpell row of power 0: no reaction");
}

/** RT3 (U2-R3): the end of the reaction limit after an early exit of its reaction. */
void limit_after_exit(const Options&) {
    IdleShop shop;
    world::Entity* wolf = shop.spawn("Test_Wolf", 4.0F);
    world::Creatures& creatures = shop.world().creatures();
    const world::HitReaction reaction = world::hit_reaction(reaction_row("Test_Chain", "Light", std::nullopt, false, 2, 0));
    for (int index = 0; index < 3; ++index) {
        (void)creatures.hurt(wolf->id, 1.0F, shop.character()->id, 0, reaction);
        if (index < 2) {
            shop.host.clock.advance(100ms);
        }
    }
    check(wolf->creature.reactionLimitUntil.has_value(), "setup: the third chained reaction starts the limit");
    shop.host.clock.advance(100ms);
    state::WorldPosition moved = wolf->position;
    moved.x -= 0.1F;
    creatures.move(*wolf, moved); // A step of the behavior ends the reaction.
    check(!wolf->creature.action && wolf->creature.moving, "setup: a step ended the reaction before its natural end (1.2 s)");
    const world::WorldClock::time_point exited = shop.host.clock.now();
    check(wolf->creature.reactionLimitUntil && *wolf->creature.reactionLimitUntil == exited + 2s,
          "the limit ends 2 s after the real exit of the reaction");
    shop.host.clock.advance(1900ms);
    const std::uint64_t held = creatures.counts().reactions;
    (void)creatures.hurt(wolf->id, 1.0F, shop.character()->id, 0, reaction);
    check(creatures.counts().reactions == held, "1.9 s after the exit a light hit plays no reaction (the limit holds)");
    shop.host.clock.advance(101ms);
    state::WorldPosition step = wolf->position;
    step.x -= 0.1F;
    creatures.move(*wolf, step);
    const std::uint64_t count = creatures.counts().reactions;
    (void)creatures.hurt(wolf->id, 1.0F, shop.character()->id, 0, reaction);
    check(creatures.counts().reactions == count + 1, "2.001 s after the exit a light hit plays its reaction again");
}

json combat_document() {
    return json{{"format", 1},
                {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreSmallerRate", 0.6667}, {"gearScoreInterval", 5},
                               {"gearScoreMinimum", 100}, {"gearScoreLimit", 500}}},
                {"damageTypes", {{"Slash", 1, "Physical"}}},
                {"attributes", {{"minimum", 0}, {"strength", {0.0}}, {"dexterity", {0.0}}, {"intelligence", {0.0}}, {"focus", {0.0}}}},
                {"damageRows", {{"Test_Light", "Sword", "Light", "Slash", 1.0, 0.0}, {"Test_Heavy", "Sword", "Heavy", "Slash", 1.2, 0.0}}},
                {"weapons", {{"TestSword", "Combat Melee", "Sword", 64.0, 0.01, 0.0, 0.0, 0.0, "Test_Light", "Test_Heavy", 0.0}}},
                {"items", {{"testsword", 0}}},
                {"defenses", json::array()},
                {"vitals", json::object()}};
}

/** RT4 (U2-R4): a partial reaction list. */
void partial_reactions(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "rt4-reactions.json";
    json document = combat_document();
    const auto load = [&](const json& value) {
        std::ofstream(path) << value.dump();
        gamedata::CombatTables tables;
        const bool loaded = tables.load(path.string());
        if (!loaded) {
            note("the loader: " + tables.error());
        }
        return loaded;
    };
    check(load(document), "a file with no member reactions loads (the old rule for each row)");
    document["reactions"] = {{"Test_Light", false, 2, 18, -1, -1}, {"Test_Heavy", true, 3, 22, -1, -1}};
    check(load(document), "a file with a reaction for each damage row loads");
    document["reactions"].erase(1);
    check(!load(document), "a file whose reactions do not have each damage row does not load");
    document["reactions"] = json::array();
    check(!load(document), "a file with an empty reaction list does not load");
}

/** RT5 (U2-R5): the hit stun of a passive creature on a wander. */
void passive_hit_stun(const Options&) {
    IdleShop shop;
    gamedata::CreatureSlice slice = idle_wolf_slice();
    slice.attacks.clear();
    slice.behavior->wander = true;
    slice.behavior->wanderNearest = 2.0F;
    slice.behavior->wanderFarthest = 10.0F;
    slice.behavior->ambientSpeed = 2.0F;
    check(shop.host.creatures.set_full({slice}, {kind_of("Test_Passive", "Solo", 0x21112222)}, {}, {{1, shop::kWolfHealth, 100.0F, 50.0F}}),
          "setup: a passive creature that wanders");
    shop.world().set_creature_behavior(true);
    world::Entity* animal = shop.spawn("Test_Passive", 4.0F);
    shop.run(1.0F);
    animal->creature.mind.roamWaitUntil = shop.host.clock.now();
    state::WorldPosition goal = animal->position;
    goal.y += 10.0F;
    animal->creature.mind.roamGoal = goal;
    (void)shop.world().creatures().hurt(animal->id, 1.0F, shop.character()->id, 0,
                                        world::hit_reaction(reaction_row("Test_Stun", "Light", std::nullopt, false, 2, 60)));
    check(animal->creature.hitStunUntil.has_value() && animal->creature.mind.mode == world::CreatureMode::rest,
          "setup: a hit stun of 2 s on a passive creature with no target (rest mode)");
    const state::WorldPosition before = animal->position;
    shop.run(0.4F);
    const float moved = world::horizontal_distance(before, animal->position);
    note("the creature moved " + std::to_string(moved) + " m in 0.4 s of its hit stun");
    check(moved < 0.001F && animal->creature.action.has_value(), "the wander waits for the hit stun, and the reaction plays");
    shop.run(2.0F);
    check(world::horizontal_distance(before, animal->position) > 0.1F, "after the hit stun the wander goes on");
    shop.world().set_creature_behavior(false);
}

/** RT6 (U2-R6): the region files of a manifest that a backend holds. */
void held_manifest(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "rt6-gathering";
    std::filesystem::remove_all(folder);
    const auto publish = [&](int number) {
        const auto lines = run_tool_check(options, {folder.string(), "--publish", std::to_string(number)}, "rt6-publish.log");
        return line_ok(lines, "publish " + std::to_string(number));
    };
    check(publish(1), "setup: the first made-up export");
    gamedata::GatheringData held;
    check(held.load(folder.string()), "setup: a backend loads the first manifest (no region file read yet)");
    check(publish(3) && publish(5), "setup: two more exports while that backend holds the first manifest");
    gamedata::GatheringData current;
    check(current.load(folder.string()) && current.natural_at(0, 0).has_value(), "a new backend reads the current export");
    check(held.natural_at(0, 0).has_value(), "the backend that holds the first manifest still reads its node (its region file stays)");
    const auto lines = run_tool_check(options, {(options.workDirectory / "rt6-check").string(), "held-manifest", "prune", "export-publish"},
                                      "rt6-check.log");
    check(line_ok(lines, "held-manifest") && line_ok(lines, "prune") && line_ok(lines, "export-publish"),
          "the tool check: each set stays after a publication; --prune removes the old sets only with no backend");
}

} // namespace

std::vector<Case> review_user_test_2_cases() {
    return {{"RT1", "U2-R1: a path after a short push stays only with a free line to its next cell (no step through a wall)", path_beside_wall},
            {"RT2", "U2-R2: a Magic row of power 0 plays no reaction (the branch below power 1 is for Ability only)", magic_power_zero},
            {"RT3", "U2-R3: the reaction limit ends 2 s after the real exit of its reaction", limit_after_exit},
            {"RT4", "U2-R4: a combat file with reactions must have one for each damage row", partial_reactions},
            {"RT5", "U2-R5: a passive creature on a wander waits for its hit stun", passive_hit_stun},
            {"RT6", "U2-R6: an export keeps the region files of a manifest that a backend holds; --prune with no backend", held_manifest}};
}

} // namespace soulwarden::backend::tests

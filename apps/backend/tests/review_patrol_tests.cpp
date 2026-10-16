// Tests of the corrections of the code review of stage 3.4e (patrol and wander, and the reset;
// ~/Games/soulwarden/work/evidence/patrol/review/FINDINGS.md). Each case fails with the code of
// stage 3.4e. The cases use the fixtures of patrol_tests.cpp (the file is a part of this one).
//
//   RP1  F1  a creature whose anchor is on a different floor above it does not jump to it while
//            a session observes it, and the trail keeps a place of a different floor
//   RP2  F2  a waypoint on a floor above the creature that has no way to it is no arrival
//   RP3  F3  a movement state of a walk in the 1.5 s of a first spawn record has no scope fields;
//            the walk follows at the end of the lead
//   RP4  F4  the 12 s of the wait with no way start at the place nearest to the target, not at the
//            first step of the way to it
//   RP5  F5, F6  the creature tool stops for a patrol source that is not complete and for a
//            number that is not finite, and keeps the file that was there (runs
//            tools/gamedata/check_creature_tool.py)
//   RP7  F7  the health command and the kill command do not lower the health of an immune creature
//   RP8  F8  a creature that stayed at a waypoint with a task for ever plays the task again after
//            a fight
//   RP9  F9  the documents say that a creature with no attack can patrol and wander
//
// The accounts, the characters, the creature kinds, and the places are made up.

#define patrol_cases fixture_patrol_cases
#include "patrol_tests.cpp"
#undef patrol_cases

namespace soulwarden::backend::tests {
namespace {

/** Cells with two floors that have no way between them: a floor at 50 m with a ceiling at 53 m, and a floor at 55 m. */
void two_floors(TestNavRegion& region) {
    region.tiles[{15, 15}] = cells_of(15, 15, [](float, float, std::vector<TestFloor>& floors) {
        floors = {{50.0F, 0, 0, 3.0F}, {55.0F, 0}};
    });
}

/** RP1 (F1): the anchor of a fight on a different floor at the same place in the plane. */
void anchor_on_another_floor(const Options& options) {
    NavPatrolHost p(options, "review-3.4e-anchor-floor");
    TestNavRegion region;
    two_floors(region);
    region.write(p.folder);
    check(p.load(), "setup: two floors with no way between them");
    world::Entity* player = p.character(at(1003, 1000));
    p.behavior().set_ignored(player->characterId, true);
    p.pass(0.6F);
    world::Entity* wolf = p.creatures().spawn("Test_Patroller", at(1000, 1000), 0).entity;
    p.behavior().set_ignored(player->characterId, false);
    p.creatures().hurt(wolf->id, 1, player->id);
    check(wolf->creature.mind.mode == CreatureMode::fight, "setup: the creature has its target");
    check(p.behavior().move_anchor(*wolf, at(1000, 1000, 55)), "setup: the anchor of the fight is on the upper floor");
    p.behavior().set_ignored(player->characterId, true);
    p.pass(2.0F);
    note("the creature is at the height " + std::to_string(wolf->position.z) + ", immune " + std::to_string(wolf->creature.immune())
         + ", jumps " + std::to_string(p.behavior().counts().jumps));
    check(near(wolf->position.z, 50.0F) && wolf->creature.immune() && p.behavior().counts().jumps == 0,
          "a creature whose anchor is on a floor with no way to it does not jump through the ceiling while a session observes "
          "it: it stays on its floor, immune");
    // With no session near, the jump of PT7 is permitted.
    (void)p.host.world.travel(player->characterId, at(1600, 1000), world::TravelScreen::off);
    p.pass(1.0F);
    check(near(wolf->position.z, 55.0F) && !wolf->creature.immune() && p.behavior().counts().jumps == 1,
          "with no session that observes it, the creature is at its anchor in one step");
}

/** RP2 (F2): a waypoint on the upper floor above the creature. */
void waypoint_on_another_floor(const Options& options) {
    gamedata::SpawnPoint point = spawn_point("review-3.4e-upper-waypoint", 0, 1000, 1000, 0);
    gamedata::PatrolWaypoint upper = waypoint(1000, 1000, -1, -1, "POI_Test_Eat_LOOP");
    upper.position.z = 55.0F;
    NavPatrolHost p(options, "review-3.4e-upper-waypoint", {point}, {{gamedata::PatrolPolicy::loop, {upper}}});
    TestNavRegion region;
    two_floors(region);
    region.write(p.folder);
    check(p.load(), "setup: a patrol with one waypoint on the upper floor");
    world::Entity* player = p.character(at(1003, 1000));
    p.behavior().set_ignored(player->characterId, true);
    p.pass(5.0F);
    world::Entity* wolf = p.creatures().find(p.creatures().spawn_points()[0].entity);
    check(wolf != nullptr, "setup: the spawn point made its creature");
    if (wolf == nullptr) {
        return;
    }
    note("height " + std::to_string(wolf->position.z) + ", waypoints " + std::to_string(p.behavior().counts().waypoints)
         + ", settled " + std::to_string(wolf->creature.mind.settled) + ", no way " + std::to_string(p.behavior().counts().roamNoWay));
    check(p.behavior().counts().waypoints == 0 && !wolf->creature.mind.settled && p.behavior().counts().roamNoWay >= 1
              && near(wolf->position.z, 50.0F) && playing(*wolf) != static_cast<int>(kLoopTask),
          "a waypoint on a floor with no way to it is no arrival: the creature does not play its task and does not stay "
          "there, and the navigation says that it has no way");
}

/** RP3 (F3): a walk in the lead of a first spawn record. */
void walk_in_the_spawn_lead(const Options&) {
    PatrolHost p(at(1000, 1000));
    ServerHost& host = p.host;
    gamedata::CreatureSlice body = test_slice("slices/test/review-spawn.dynamicslice", 0x55, false);
    body.actions.push_back({"Spawn", "Spawn", 18, false});
    body.spawnActionName = "Spawn";
    check(host.creatures.set_full({body}, {test_kind("Review_Spawn", 0x55555555, 0)}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "setup: a slice with a spawn action loads");
    host.world.set_creature_behavior(false);
    FakeClient client(29150);
    std::uint16_t input = 0;
    enter_at(host, client, p.alpha, at(1000, 1000), input);
    world::Entity* wolf = p.creatures().spawn("Review_Spawn", at(1005, 1000), 0).entity;
    p.creatures().move(*wolf, at(1005.1F, 1000), world::Gait::walk);
    for (int i = 0; i < 70 && interest_of(host, 1, wolf->id) == 0; ++i) {
        pass(host, 0.01F, {&client});
    }
    const std::uint32_t interest = interest_of(host, 1, wolf->id);
    const std::vector<Fragment> first = fragments_of(client, 0, interest, kSlots[0]);
    check(!first.empty() && scope_of(first.front()) == 8, "setup: the first record of the session has the spawn action");
    std::size_t from = client.got.size();
    bool early = false;
    // A step of the walk each 200 ms for 1.2 s.
    for (int step = 0; step < 6; ++step) {
        p.creatures().move(*wolf, at(1005.5F + 0.4F * static_cast<float>(step), 1000), world::Gait::walk);
        pass(host, 0.2F, {&client});
        for (const Fragment& part : fragments_of(client, from, interest, kSlots[0])) {
            early = early || scope_of(part) != -2;
        }
        from = client.got.size();
    }
    check(!early, "the movement states of the walk in the 1.5 s after the first record have no scope fields: the client plays "
                  "the spawn action");
    pass(host, 0.5F, {&client});
    bool walk = false;
    for (const Fragment& part : fragments_of(client, from, interest, kSlots[0])) {
        walk = walk || scope_of(part) == static_cast<int>(kWalk);
    }
    check(walk, "at the end of the lead the session sends the walk");
}

/** RP4 (F4): a slow creature on a long way to the place nearest to its target. */
void hold_starts_at_the_place(const Options& options) {
    NavPatrolHost p(options, "review-3.4e-hold");
    gamedata::CreatureSlice body = test_slice("slices/test/review-slow.dynamicslice", 0x56, false);
    body.behavior->runSpeed = 0.5F;
    check(p.host.creatures.set_full({body}, {test_kind("Review_Slow", 0x56565656, 0)}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "setup: a slow creature loads");
    TestNavRegion region;
    // A stone 0.625 m high (a step of a creature is 0.35 m): no way to its top.
    region.tiles[{15, 15}] = cells_of(15, 15, [](float x, float y, std::vector<TestFloor>& floors) {
        if (x > 1010 && x < 1020 && y > 995 && y < 1005) {
            floors = {{50.625F, 0}};
        }
    });
    region.write(p.folder);
    check(p.load(), "setup: a stone with no way to its top");
    world::Entity* player = p.character(at(1015, 1000, 50.625F));
    p.behavior().set_ignored(player->characterId, true);
    p.pass(0.6F);
    world::Entity* wolf = p.creatures().spawn("Review_Slow", at(1000, 1000), 270).entity;
    p.behavior().set_ignored(player->characterId, false);
    p.creatures().hurt(wolf->id, 1, player->id);
    p.pass(15.0F);
    note("after 15 s: x " + std::to_string(wolf->position.x) + ", mode " + std::to_string(static_cast<int>(wolf->creature.mind.mode))
         + ", no way " + std::to_string(p.behavior().counts().targetsNoWay));
    check(wolf->creature.mind.mode == CreatureMode::fight && p.behavior().counts().targetsNoWay == 0 && wolf->position.x < 1009.0F,
          "after 15 s the creature still goes to the place nearest to its target (a way of about 20 s), with its target");
    float nearest = 0.0F;
    for (int i = 0; i < 600 && p.behavior().counts().targetsNoWay == 0; ++i) {
        nearest = std::max(nearest, wolf->position.x);
        p.pass(0.1F);
    }
    note("the nearest place " + std::to_string(nearest) + ", no way " + std::to_string(p.behavior().counts().targetsNoWay));
    check(nearest > 1009.0F && p.behavior().counts().targetsNoWay == 1,
          "the creature gets to the place nearest to its target, waits there, and then loses the target");
}

/** RP5 (F5, F6): the check of the creature tool, tools/gamedata/check_creature_tool.py. */
void creature_tool(const Options& options) {
    // The golden folder of the options is REPOSITORY/apps/backend/tests/golden.
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the creature tool is in the tree (the case needs --golden-dir)");
    if (!std::filesystem::is_regular_file(script)) {
        return;
    }
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "creature-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "creature-tool").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    // The seven patrol cases of the check (the check has the ranged cases of stage 3.4f-A too: case CRG1 reads them).
    static const char* const kPatrolCases[] = {"none", "valid", "empty", "missing-reference", "missing-waypoint", "nonfinite-position",
                                               "nonfinite-basis"};
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        const std::string name = line.size() > 6 ? line.substr(6, line.find(':') == std::string::npos ? std::string::npos : line.find(':') - 6)
                                                 : std::string();
        const bool patrol = std::find(std::begin(kPatrolCases), std::end(kPatrolCases), name) != std::end(kPatrolCases);
        correct += patrol && line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && correct == 7 && wrong == 0,
          "the creature tool writes a file for a spawner with no patrol, with a patrol, and with an empty patrol of the "
          "retail data (a count of the file); it stops with status 1 and keeps the file that was there for a patrol entity "
          "or a waypoint entity that the slice does not have (F5) and for a transform with a number that is not finite (F5, F6)");
}

/** RP7 (F7): the commands that lower the health of an immune creature. */
void immune_commands(const Options&) {
    PatrolHost p(at(1000, 1000));
    ServerHost& host = p.host;
    (void)host.world.spawn_character(kCharacterA, at(1000, 1000));
    world::Entity* player = host.world.find_character(kCharacterA);
    world::Entity* wolf = p.creatures().spawn("Test_Patroller", at(1010, 1000), 90).entity;
    p.creatures().hurt(wolf->id, 1, player->id);
    pass(host, 0.4F);
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 0.2F);
    check(wolf->creature.immune(), "setup: the creature is on its way back");
    const float before = wolf->vitals.health;
    check(p.creatures().hurt(wolf->id, 10).error == world::CreatureError::immune, "a hit with no attacker is refused (as before)");
    const world::CreatureResult lowered = p.creatures().set_health(wolf->id, before - 10);
    check(lowered.error == world::CreatureError::immune && wolf->vitals.health == before,
          "the health command does not lower the health of an immune creature");
    const world::CreatureResult killed = p.creatures().kill(wolf->id);
    check(killed.error == world::CreatureError::immune && !wolf->creature.dead, "the kill command does not kill an immune creature");
    check(p.creatures().set_health(wolf->id, wolf->vitals.healthMax).error == world::CreatureError::none,
          "a health that goes up is not a damage: the command gives it");
}

/** RP8 (F8): a creature that stays at a waypoint with a task for ever, and a fight. */
void settled_task_after_a_fight(const Options&) {
    PatrolHost p(at(1300, 1000));
    ServerHost& host = p.host;
    (void)host.world.spawn_character(kCharacterA, at(1300, 1000));
    world::Entity* player = host.world.find_character(kCharacterA);
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 8.0F);
    world::Entity* wolf = p.of_point(3);
    check(wolf != nullptr && wolf->creature.mind.settled && playing(*wolf) == static_cast<int>(kLoopTask),
          "setup: the creature of a route of one waypoint plays the task of the waypoint for ever");
    if (wolf == nullptr) {
        return;
    }
    p.behavior().set_ignored(kCharacterA, false);
    p.creatures().hurt(wolf->id, 1, player->id);
    pass(host, 0.4F);
    check(playing(*wolf) != static_cast<int>(kLoopTask), "setup: the fight replaced the task");
    p.behavior().set_ignored(kCharacterA, true);
    pass(host, 5.0F);
    note("after the fight: settled " + std::to_string(wolf->creature.mind.settled) + ", mode "
         + std::to_string(static_cast<int>(wolf->creature.mind.mode)) + ", action " + std::to_string(playing(*wolf)));
    check(wolf->creature.mind.mode == CreatureMode::rest && wolf->creature.mind.settled && playing(*wolf) == static_cast<int>(kLoopTask),
          "after the reset the creature goes to its waypoint again and plays the task of the waypoint again");
}

/** RP9 (F9): the documents about the creatures with no attack. */
void documents_of_creatures_with_no_attack(const Options& options) {
    const std::filesystem::path root = options.goldenDirectory / ".." / ".." / ".." / "..";
    struct Wrong {
        const char* file;
        const char* text;
    };
    // The sentences of stage 3.4e that say that a creature with no attack does not move.
    const Wrong wrong[] = {{"README.md", "have no attack yet and stand still"},
                           {"docs/systems/creature-behavior.md", "stands and takes damage (859 of the 3,848 kinds)"},
                           {"docs/REFERENCE.md", "have no attack that the tool can use: they stand."},
                           {"docs/PROTOCOL_REVERSE_ENGINEERING_NOTES.md", "The 859 kinds with no attack stand."}};
    bool all = true;
    for (const Wrong& entry : wrong) {
        std::ifstream file(root / entry.file);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (text.empty() || text.find(entry.text) != std::string::npos) {
            note(std::string(entry.file) + (text.empty() ? ": not found" : ": has the old sentence"));
            all = false;
        }
    }
    check(all, "no document says that a creature with no attack stands: a kind with no attack can patrol and wander");
    // The behavior that the documents describe: a creature with no attack walks its patrol.
    PatrolHost p(at(1000, 1000), {}, {});
    gamedata::CreatureSlice body = test_slice("slices/test/review-passive.dynamicslice", 0x57, false);
    body.attacks.clear();
    gamedata::SpawnPoint point = spawn_point("review-3.4e-passive", 0, 1006, 1000, 0);
    check(p.host.creatures.set_full({body}, {test_kind("Review_Passive", 0x57575757, 0)}, {point}, {{18, 1690.875977F, 616.41F, 180.0F}},
                                    {{gamedata::PatrolPolicy::loop, {waypoint(1010, 1000, 1, 1)}}}),
          "setup: a kind with no attack and a patrol");
    (void)p.host.world.spawn_character(kCharacterA, at(1000, 1000));
    pass(p.host, 8.0F);
    world::Entity* creature = p.of_point(0);
    check(creature != nullptr && !creature->creature.body->fights() && world::horizontal_distance(creature->position, at(1006, 1000)) > 3.0F,
          "a creature with no attack walks its patrol");
}

} // namespace

std::vector<Case> review_patrol_cases() {
    return {{"RP1", "review of 3.4e (F1): an anchor on a different floor, and a session that observes the creature", anchor_on_another_floor},
            {"RP2", "review of 3.4e (F2): a waypoint on a floor with no way to it", waypoint_on_another_floor},
            {"RP3", "review of 3.4e (F3): a walk in the lead of a first spawn record", walk_in_the_spawn_lead},
            {"RP4", "review of 3.4e (F4): the wait with no way starts at the place nearest to the target", hold_starts_at_the_place},
            {"RP5", "review of 3.4e (F5, F6): the creature tool with a patrol source that is not complete (runs "
                    "tools/gamedata/check_creature_tool.py)",
             creature_tool},
            {"RP7", "review of 3.4e (F7): the health command and the kill command on an immune creature", immune_commands},
            {"RP8", "review of 3.4e (F8): a task of a waypoint for ever, after a fight", settled_task_after_a_fight},
            {"RP9", "review of 3.4e (F9): a creature with no attack patrols, and the documents say so",
             documents_of_creatures_with_no_attack}};
}

} // namespace soulwarden::backend::tests

// Cases for the corrections of the code review of stage 3.5a (findings F5, F6, F7;
// ~/Games/soulwarden/work/evidence/loot/review/FINDINGS.md). Each case starts from the proof test
// of the review, and it fails with the code of stage 3.5a. The fixture of the patrol cases is in
// patrol_tests.cpp: this file takes it with its helpers. All values are made up.

#define patrol_cases review_scope_fixture_patrol_cases
#include "patrol_tests.cpp"
#undef patrol_cases

namespace soulwarden::backend::tests {
namespace {

/** RL5 (F5): a walk ends in the spawn lead: at the end of the lead the session sends the scope fields with no record. */
void idle_after_spawn_lead(const Options&) {
    PatrolHost p(at(1000, 1000));
    ServerHost& host = p.host;
    auto body = test_slice("slices/test/review-idle-spawn.dynamicslice", 0x58, false);
    body.actions.push_back({"Spawn", "Spawn", 18, false});
    body.spawnActionName = "Spawn";
    check(host.creatures.set_full({body}, {test_kind("Review_Idle_Spawn", 0x58585858, 0)}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "setup: a slice with a spawn action");
    host.world.set_creature_behavior(false);
    FakeClient client(29201);
    std::uint16_t input = 0;
    enter_at(host, client, p.alpha, at(1000, 1000), input);
    auto* wolf = p.creatures().spawn("Review_Idle_Spawn", at(1005, 1000), 0).entity;
    p.creatures().move(*wolf, at(1005.1F, 1000), world::Gait::walk);
    for (int i = 0; i < 70 && interest_of(host, 1, wolf->id) == 0; ++i) pass(host, 0.01F, {&client});
    const auto interest = interest_of(host, 1, wolf->id);
    const auto first = fragments_of(client, 0, interest, kSlots[0]);
    check(!first.empty() && scope_of(first.front()) == 8, "setup: the first record has the spawn action");
    pass(host, 0.2F, {&client});
    const auto from = client.got.size();
    p.creatures().stop(*wolf);
    pass(host, 1.8F, {&client});
    bool clear = false;
    for (const auto& part : fragments_of(client, from, interest, kSlots[0])) clear = clear || scope_of(part) == -1;
    const auto& known = host.session(1).observer().known().at(wolf->id);
    check(clear && known.scope == server::rep::creature_scope(wolf->creature, false).key,
          "the end of the lead sends the scope fields with no record (the walk ended in the lead)");
}

/** RL6 (F6): a creature that does not walk does not jump through a ceiling to its anchor while a session observes it. */
void rooted_reset(const Options& options) {
    NavPatrolHost p(options, "review-rooted-reset");
    TestNavRegion region;
    region.tiles[{15, 15}] = cells_of(15, 15, [](float, float, std::vector<TestFloor>& floors) {
        floors = {{50, gamedata::NavFloor::kBlocked, 0, 3}, {55, 0}};
    });
    region.write(p.folder);
    check(p.load(), "setup: a blocked lower floor and a free upper floor");
    auto* player = p.character(at(1003, 1000));
    p.behavior().set_ignored(player->characterId, true);
    p.pass(1);
    auto* wolf = p.creatures().spawn("Test_Patroller", at(1000, 1000), 0).entity;
    check(wolf != nullptr && wolf->creature.mind.groundKnown && !wolf->creature.mind.walks && player->owner != 0,
          "setup: a creature with no free floor, which a session observes");
    p.behavior().set_ignored(player->characterId, false);
    (void)p.creatures().hurt(wolf->id, 1, player->id);
    check(p.behavior().move_anchor(*wolf, at(1000, 1000, 55)), "setup: the anchor command puts the anchor on the upper floor");
    p.behavior().set_ignored(player->characterId, true);
    p.pass(2);
    check(near(wolf->position.z, 50) && wolf->creature.immune(),
          "the creature waits on its floor and stays immune: no jump through the ceiling in front of the session");
}

/** RL7 (F7): a new nearest place for a target with no way: the full hold starts at the new place. */
void new_nearest_place(const Options& options) {
    NavPatrolHost p(options, "review-moving-unreachable-goal");
    auto body = test_slice("slices/test/review-moving-goal.dynamicslice", 0x59, false);
    body.behavior->runSpeed = 0.5F;
    check(p.host.creatures.set_full({body}, {test_kind("Review_Moving_Goal", 0x59595959, 0)}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "setup: a slow creature");
    TestNavRegion region;
    const auto cells = [](float x, float y, std::vector<TestFloor>& floors) {
        if (((x > 1010 && x < 1020) || (x > 1040 && x < 1050)) && y > 995 && y < 1005) floors = {{50.625F, 0}};
    };
    region.tiles[{15, 15}] = cells_of(15, 15, cells);
    region.tiles[{16, 15}] = cells_of(16, 15, cells);
    region.write(p.folder);
    check(p.load(), "setup: two raised stones with no path to their tops");
    auto* player = p.character(at(1015, 1000, 50.625F));
    p.behavior().set_ignored(player->characterId, true);
    p.pass(1);
    auto* wolf = p.creatures().spawn("Review_Moving_Goal", at(1000, 1000), 270).entity;
    p.behavior().set_ignored(player->characterId, false);
    (void)p.creatures().hurt(wolf->id, 1, player->id);
    for (int i = 0; i < 600 && !wolf->creature.mind.holdUntil; ++i) p.pass(0.1F);
    check(wolf->creature.mind.holdUntil.has_value() && wolf->position.x > 1009, "setup: the first hold starts at the first stone");
    if (!wolf->creature.mind.holdUntil) return;
    (void)p.host.world.set_position(player->id, at(1045, 1000, 50.625F), false);
    std::optional<world::WorldClock::time_point> nearNewGoal;
    for (int i = 0; i < 1500 && p.behavior().counts().targetsNoWay == 0; ++i) {
        p.pass(0.1F);
        if (wolf->position.x >= 1039.5F && !nearNewGoal) nearNewGoal = p.host.clock.now();
    }
    const double waited = nearNewGoal ? std::chrono::duration<double>(p.host.clock.now() - *nearNewGoal).count() : -1;
    note("the wait at the new stone: " + std::to_string(waited) + " s");
    check(nearNewGoal.has_value() && waited >= 12 && p.behavior().counts().targetsNoWay == 1,
          "the creature waits the full hold (12 s) at the new nearest place");
}

} // namespace

std::vector<Case> review_scope_cases() {
    return {{"RL5", "review of 3.5a, F5: the end of a spawn lead after a walk sends the scope with no record", idle_after_spawn_lead},
            {"RL6", "review of 3.5a, F6: a creature that does not walk does not jump to an anchor on a different floor", rooted_reset},
            {"RL7", "review of 3.5a, F7: a new nearest place of a target with no way gets the full hold", new_nearest_place}};
}

} // namespace soulwarden::backend::tests

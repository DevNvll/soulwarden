// Cases of the corrections of the code review of stage 3.3b-S4 (made in stage 3.3b-S3). Each case fails on the tree of
// stage 3.3b-S4 (commit 2d0f037, with this file and the motion fixture of the tests) and passes after the correction. The
// cases are the proof cases of the reviewer (RM1, RM2). All ids, names, and values are made up.
//
//   RM1  F1: one Charge gives its extra distance one time: 22 m/s for 2.5 s is refused, and the travel stays in one dash
//        and the normal travel of the time
//   RM2  F2: Riposte needs the rapier in the hand: a hit after a change to a hammer starts no counter and does no damage

#include <algorithm>
#include <chrono>
#include <string>

#include "support/cases.h"
#include "support/check.h"
#include "support/motion_fixture.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using namespace motion;

/** RM1 (F1): one accepted Charge, then reports at 22 m/s for 2.5 s (below the peak of the envelope, 23.5 m/s). */
void dash_budget(const Options&) {
    MotionShop shop;
    shop.hold("TestAxe");
    world::AbilityPress press;
    press.place = 0;
    press.yaw = 1.5708F;
    check(shop.world().abilities().press(shop.character()->owner, press) == world::CastVerdict::accepted, "one accepted Charge");
    world::PositionTrack track;
    const state::WorldPosition start = shop.character()->position;
    unsigned refused = 0;
    for (unsigned index = 0; index <= 75; ++index) {
        if (index != 0) {
            shop.host.clock.advance(kTick);
        }
        const world::PositionReport report{static_cast<std::uint16_t>(5000 + index), at(start.x - 22.0F * static_cast<float>(index) / 30.0F, start.y),
                                           wire_height(kHeight)};
        if (shop.world().report_position(shop.character()->owner, report, track) != world::PositionVerdict::accepted) {
            ++refused;
        }
        shop.world().tick();
    }
    const float travel = start.x - shop.character()->position.x;
    // One dash (the envelope distance) and the normal travel of 2.5 s with its margin.
    const float budget = world::ability_rules::kChargeTravel * world::ability_rules::kChargeMultiplier
                         + (world::position_limits::kWindowSpeed + world::position_limits::kWindowMargin) * 2.5F;
    note("one Charge, 22 m/s for 2.5 s: travel " + std::to_string(travel) + " m, " + std::to_string(refused) + " refusals, budget "
         + std::to_string(budget) + " m");
    check(refused > 0 && travel <= budget + 0.01F, "one cast does not give its full dash distance again in each window");
}

/** RM2 (F2): Riposte accepted with the rapier, then the hammer is in the hand before the bite in the window. */
void counter_weapon(const Options&) {
    MotionShop shop;
    shop.hold("TestRapier");
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1 + 1).ok(), "setup: the hammer in the second weapon slot");
    world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
    const world::EntityId id = wolf->id;
    world::AbilityPress press;
    press.place = 0;
    check(shop.world().abilities().press(shop.character()->owner, press) == world::CastVerdict::accepted, "Riposte accepted with the rapier");
    shop.world().combat().select_weapon(shop.character()->owner, 2);
    const world::ItemInstance* weapon = world::Combat::weapon_in_hand(*shop.character());
    check(weapon != nullptr && weapon->itemId == "TestHammer", "the weapon select of the client puts the hammer in the hand");
    gamedata::CreatureContact bite;
    bite.row = "Review_Bite";
    bite.type = "Slash";
    bite.wire = 4;
    bite.reach = 2.0F;
    bite.factor = 1.0F;
    const float before = shop.creature(id)->vitals.health;
    shop.world().health().creature_hit(*shop.creature(id), bite, *shop.character());
    shop.run(0.5F);
    const world::AbilityCounts& counts = shop.world().abilities().counts();
    note("the rapier window, the hammer, the bite: " + std::to_string(counts.counters) + " counters, " + std::to_string(counts.counterHits)
         + " counter hits, " + std::to_string(before - shop.creature(id)->vitals.health) + " damage");
    check(counts.counters == 0 && counts.counterHits == 0 && shop.creature(id)->vitals.health == before,
          "a hit with a different weapon in the hand starts no counter");
    const bool window = std::any_of(shop.character()->statuses.begin(), shop.character()->statuses.end(),
                                    [](const world::ActiveStatus& status) { return status.status == "Rapier_Riposte_Timer"; });
    check(!window, "the window of the counter ended with the change of the weapon");
}

} // namespace

std::vector<Case> review_motion_cases() {
    return {{"RM1", "review of 3.3b-S4, F1: one Charge gives its extra distance one time", dash_budget},
            {"RM2", "review of 3.3b-S4, F2: Riposte needs the rapier in the hand at the hit", counter_weapon}};
}

} // namespace soulwarden::backend::tests

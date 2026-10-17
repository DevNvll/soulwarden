// Cases of the corrections of the code review of stage U3 (corrected in stage B2): the homing of the attack of a creature (a root, an
// additive push, two clips that overlap), the weight rule of a press that a weapon switch kept and of a basic ranged press, the kept press of
// a switch to a bow, a dodge that cancels Rapid Shot, and the switch marks of a bow fragment. The cases are the independent cases of the review
// (evidence of the review of stage U3, U3P1 to U3P8), with made-up data; each one fails on the tree of stage U3.
//
//   UR1   U3-R1: a root holds the place of a creature in the homing of its attack (the turn goes on)
//   UR2   U3-R2: the push of an additive reaction adds to the time curve of a homing (the curve does not take it back)
//   UR3   U3-R3: a press that a weapon switch kept is refused above the weight limit, as a direct press
//   UR4   U3-R4: a basic ranged press above the weight limit starts no shot
//   UR5   U3-R5: a press kept by a switch to a bow launches the first arrow of the bow
//   UR6   U3-R6: a later Homing clip starts at its time while an earlier clip runs
//   UR7   U3-R7: a dodge after the cancel mark of Rapid Shot ends its arrows that did not fly
//   UR8   U3-R8: a weapon key in Rapid Shot waits for the switch mark of the fragment (the data: buffer 1.868 s, condition 2.567 s)

#include <cmath>
#include <chrono>
#include <string>
#include <vector>

#include "support/feel_fixture.h"
#include "support/gawh_fixture.h"
#include "support/bow_fixture.h"
#include "support/cases.h"
#include "support/stamina_fixture.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {
using namespace feel;

void begin_homing(FeelShop& shop, world::Entity& wolf) {
    check(shop.world().creatures().start_action(wolf, shop::kBite, std::chrono::milliseconds(1400)), "setup: an attack starts");
    shop.world().creatures().attack_motion().start(wolf, shop.character()->id, 0);
}

void rooted_homing(const Options&) {
    FeelShop shop;
    world::Entity* wolf = shop.spawn("Test_Wolf", 0, 1.4F, 180);
    const state::WorldPosition before = wolf->position;
    begin_homing(shop, *wolf);
    shop.wait(0.1F);
    const auto* root = shop.host.abilities.status("GreatAxe_Vortex_Root");
    check(root != nullptr && shop.world().creatures().give_status(*wolf, *root, shop.character()->id), "setup: the root is applied");
    shop.wait(0.6F);
    note("UR1 rooted=" + std::to_string(wolf->creature.rooted(shop.host.clock.now())) + " moved=" + std::to_string(world::horizontal_distance(before, wolf->position)));
    check(wolf->creature.rooted(shop.host.clock.now()), "setup: the root still holds");
    check(world::horizontal_distance(before, wolf->position) < 0.01F, "a rooted creature must not lunge to its target");
}

void additive_homing(const Options&) {
    FeelShop control;
    auto* still = control.spawn("Test_Wolf", 0, 2.4F, 180);
    auto controlSource = still->position;
    controlSource.x += 2;
    const float controlX = still->position.x;
    check(control.world().creatures().impact(*still, impact_of(0.5F, 0), controlSource, controlSource, "Test_Additive", std::chrono::milliseconds(300), 1, 1, true), "control: the same additive push starts with no homing");
    control.wait(0.4F);
    const float controlSide = controlX - still->position.x;
    check(controlSide > 0.13F && controlSide < 0.17F, "control: the push alone moves the creature 0.15 m");
    // The same homing with no push, and with the push in the clip (the clip starts at 0.2 s; the push at 0.3 s): the push (0.5 m/s for 0.3 s
    // away from its source point, a direction that turns while the wolf moves to its target) adds its travel to the homing: the two end
    // places are about 0.15 m apart (a chord of the turn of the direction). Before the correction the curve took the push back.
    FeelShop alone;
    world::Entity* homed = alone.spawn("Test_Wolf", 0, 2.4F, 180);
    begin_homing(alone, *homed);
    alone.wait(0.7F);
    FeelShop shop;
    world::Entity* wolf = shop.spawn("Test_Wolf", 0, 2.4F, 180);
    begin_homing(shop, *wolf);
    shop.wait(0.3F);
    state::WorldPosition source = wolf->position;
    source.x += 2;
    check(shop.world().creatures().impact(*wolf, impact_of(0.5F, 0), source, source, "Test_Additive", std::chrono::milliseconds(300), 1, 1, true), "setup: an additive side push starts during the homing");
    shop.wait(0.4F);
    const float apart = std::hypot(wolf->position.x - homed->position.x, wolf->position.y - homed->position.y);
    note("UR2 push_without_homing=" + std::to_string(controlSide) + " push_with_homing=" + std::to_string(apart) + " (the distance to the end place of the homing alone)");
    check(apart > 0.13F && apart < 0.17F, "the 0.15 m additive push must add to the homing movement");
}

void equip_axe2(gawh::GawhShop& shop) {
    world::GiveOptions opt;
    opt.gearScore = 100;
    auto given = shop.world().inventory().give(kCharacterA, "TestAxe", 1, opt);
    check(given.ok() && !given.slots.empty() && shop.world().inventory().equip(kCharacterA, given.slots.front(), world::paperdoll::kMainHand1 + 1).ok(), "setup: the axe is in slot 2");
}

void add_mass(gawh::GawhShop& shop) {
    auto items = shop::shop_catalog();
    auto mass = shop::definition("TestMass", "Resource", "Resource", 5, 0, 0);
    mass.weight = 4000;
    items.push_back(mass);
    shop.host.catalog.set(items);
    check(shop.world().inventory().give(kCharacterA, "TestMass", 1).ok(), "setup: the bag gets 400.0 weight");
    check(shop.world().inventory().encumbrance(*shop.character()).overencumbered, "setup: the bag is above 1.5 times its limit");
}

void switched_weight(const Options&) {
    gawh::GawhShop shop;
    shop.hold("TestHammer");
    equip_axe2(shop);
    add_mass(shop);
    shop.walker.idle(10);
    const auto before = shop.visits().size();
    shop.step({kActionAttack});
    shop.step({kActionAttackRelease});
    shop.walker.idle(5);
    check(shop.visits().size() == before, "control: a direct press from rest is refused for the weight");
    shop.step({kActionWeapon2, kActionAttack});
    shop.step({kActionAttackRelease});
    shop.walker.idle(5);
    note("UR3 new_visits=" + std::to_string(shop.visits().size() - before) + " visits=" + attack_fixture::joined(shop.visits()));
    check(shop.visits().size() == before, "a press kept by a weapon switch must also be refused above the weight limit");
}

void ranged_weight(const Options&) {
    bow::BowShop shop;
    shop.learn(*shop.character());
    auto items = shop::shop_catalog();
    items.push_back(shop::definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
    items.push_back(shop::definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
    auto mass = shop::definition("TestMass", "Resource", "Resource", 5, 0, 0);
    mass.weight = 4000;
    items.push_back(mass);
    shop.host.catalog.set(items);
    check(shop.world().inventory().give(kCharacterA, "TestMass", 1).ok(), "setup: the bow character gets 400.0 weight");
    check(shop.world().inventory().encumbrance(*shop.character()).overencumbered, "setup: the bow character is overencumbered");
    const auto id = shop.wolf_at(10)->id;
    shop.walker.bits({kActionAttack});
    shop.walker.bits({kActionAttackRelease});
    shop.send_item(bow::BowShop::bow_item("Basic", "TestBowLight"));
    shop.walker.idle(10);
    note("UR4 damage=" + std::to_string(shop.lost(id)));
    check(shop.lost(id) == 0, "an overencumbered character must not start a basic ranged attack from rest");
}

void ranged_switch_press(const Options&) {
    bow::BowShop shop;
    shop.learn(*shop.character());
    world::GiveOptions opt;
    opt.gearScore = 100;
    auto given = shop.world().inventory().give(kCharacterA, "TestStaff", 1, opt);
    check(given.ok() && !given.slots.empty() && shop.world().inventory().equip(kCharacterA, given.slots.front(), world::paperdoll::kMainHand1 + 1).ok(), "setup: a staff is in slot 2");
    shop.world().combat().select_weapon(1, 2);
    const auto id = shop.wolf_at(10)->id;
    shop.walker.idle(5);
    shop.walker.bits({kActionWeapon1, kActionAttack});
    shop.walker.bits({kActionAttackRelease});
    shop.walker.idle(2);
    check(world::Combat::weapon_in_hand(*shop.character())->itemId == "TestBow", "setup: the switch changed to the bow");
    shop.send_item(bow::BowShop::bow_item("Basic", "TestBowLight"));
    shop.walker.idle(8);
    const float buffered = shop.lost(id);
    shop.walker.bits({kActionAttack});
    shop.walker.bits({kActionAttackRelease});
    shop.send_item(bow::BowShop::bow_item("Basic", "TestBowLight"));
    shop.walker.idle(8);
    note("UR5 damage_from_kept_press=" + std::to_string(buffered) + " damage_from_fresh_press=" + std::to_string(shop.lost(id) - buffered));
    check(shop.lost(id) > buffered, "control: the same arrow with a fresh bow press hits");
    check(buffered > 0, "a press kept for the new bow must launch its first arrow after the switch");
}

void overlapping_clips(const Options&) {
    FeelShop shop;
    auto slice = feel_slice();
    auto move = slice.attacks[0].homing[0];
    move.start = 0.2F;
    auto turn = move;
    turn.start = 0;
    turn.duration = 0.8F;
    turn.moveToTarget = false;
    turn.turnAfterTarget = true;
    slice.attacks[0].homing = {turn, move};
    check(shop.host.creatures.set_full({slice}, {kind_of("Test_Wolf", "Solo", 0x21112222)}, {}, {{1, shop::kWolfHealth, 100, 50}}), "setup: overlapping turn and movement clips load");
    auto* wolf = shop.spawn("Test_Wolf", 0, 2.4F, 180);
    begin_homing(shop, *wolf);
    shop.wait(0.7F);
    const float away = world::horizontal_distance(wolf->position, shop.character()->position);
    note("UR6 clips_started=" + std::to_string(shop.world().creatures().attack_motion().counts().clips) + " target_distance=" + std::to_string(away));
    check(std::fabs(away - 0.75F) < 0.05F, "the movement clip must run at 0.2 s while the earlier turn clip still runs");
}

/** The switch marks and the dodge marks of the fragment of Rapid Shot (player_bow.adb; the projectile data of format 3 of stage B2). */
void rapid_marks(bow::BowShop& shop) {
#ifndef SOULWARDEN_B2_BASELINE
    gamedata::ProjectileTables projectiles = bow::bow_projectiles();
    for (gamedata::AbilityActionData& action : projectiles.actions) {
        if (action.action == "Bow_RapidShot") {
            action.switchBuffer = 1.868143F;
            action.switchAt = 2.566667F;
            action.dodgeMarks = {{0.2F, true}, {0.566667F, false}, {0.866667F, true}, {1.1F, false}, {1.733333F, true}, {2.333333F, false}};
        }
    }
    check(shop.host.projectiles.set(projectiles), "setup: the projectile data with the marks of the fragment");
#else
    (void)shop;
#endif
}

/** A dodge in the buffer window of Rapid Shot (0.4 s) cancels at the next cancel mark (0.567 s): the first arrow (0.333 s) still flies. */
void buffered_dodge(const Options&) {
    bow::BowShop shop;
    shop.learn(*shop.character());
    rapid_marks(shop);
    check(shop.host.stamina.set(stamina::stamina_tables()), "setup: the stamina data loads");
    const auto id = shop.wolf_at(10)->id;
    shop.press(2);
    shop.until(12);
    shop.walker.bits({attack_fixture::kActionDodge});
    // The item of the first arrow comes late (its time 0.333 s is before the cancel at 0.567 s); the second item comes at its time.
    shop.item_at(14, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    const float first = shop.lost(id);
    shop.item_at(26, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(7);
    note("UR7 buffered dodge: shots=" + std::to_string(shop.volley_shots()) + " damage=" + std::to_string(shop.lost(id)) + " first=" + std::to_string(first));
    check(shop.volley_shots() == 1, "a dodge in the buffer window cancels at the next cancel mark: the first arrow flies, the second does not");
}

void dodge_ends_volley(const Options&) {
    bow::BowShop shop;
    shop.learn(*shop.character());
    rapid_marks(shop);
    check(shop.host.stamina.set(stamina::stamina_tables()), "setup: the stamina data loads");
    const auto id = shop.wolf_at(10)->id;
    shop.press(2);
    shop.item_at(9, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.until(19);
    const float before = shop.lost(id);
    const auto dodges = shop.world().stamina().counts().dodges[static_cast<std::size_t>(world::DodgeVerdict::accepted)];
    shop.walker.bits({attack_fixture::kActionDodge});
    check(shop.world().stamina().counts().dodges[static_cast<std::size_t>(world::DodgeVerdict::accepted)] == dodges + 1, "setup: a dodge cancels Rapid Shot after its 0.5667 s cancel mark");
    shop.item_at(25, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(7);
    note("UR7 shots=" + std::to_string(shop.volley_shots()) + " damage_after_dodge=" + std::to_string(shop.lost(id) - before));
    check(before > 0, "setup: the first arrow hit before the dodge");
    check(shop.volley_shots() == 1 && shop.lost(id) == before, "B1-R1: a dodge must end the unlaunched arrows of the cancelled volley");
}

void rapid_switch_time(const Options&) {
    bow::BowShop shop;
    shop.learn(*shop.character());
    rapid_marks(shop);
    world::GiveOptions opt;
    opt.gearScore = 100;
    auto given = shop.world().inventory().give(kCharacterA, "TestStaff", 1, opt);
    check(given.ok() && !given.slots.empty() && shop.world().inventory().equip(kCharacterA, given.slots.front(), world::paperdoll::kMainHand1 + 1).ok(), "setup: the second weapon is a staff");
    const auto id = shop.wolf_at(10)->id;
    shop.press(2);
    shop.item_at(9, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.item_at(25, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.until(48);
    shop.walker.bits({kActionWeapon2});
    const bool bowKept = world::Combat::weapon_in_hand(*shop.character())->itemId == "TestBow";
    shop.item_at(52, bow::BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
    shop.walker.idle(8);
    note("UR8 bow_kept_at_1.6s=" + std::to_string(bowKept) + " volley_shots=" + std::to_string(shop.volley_shots()) + " damage=" + std::to_string(shop.lost(id)));
    check(bowKept && shop.volley_shots() == 3, "the switch must wait for Rapid Shot's 2.5667 s switch mark and keep its third arrow");
}
}
std::vector<Case> review_u3_cases() {
    return {{"UR1", "review of stage U3, finding U3-R1: root during a homing attack", rooted_homing},
            {"UR2", "review of stage U3, finding U3-R2: additive push during homing", additive_homing},
            {"UR3", "review of stage U3, finding U3-R3: weight check for a kept switch press", switched_weight},
            {"UR4", "review of stage U3, finding U3-R4: weight check for a basic bow shot", ranged_weight},
            {"UR5", "review of stage U3, finding U3-R5: a kept press for a ranged weapon", ranged_switch_press},
            {"UR6", "review of stage U3, finding U3-R6: overlapping Homing clips", overlapping_clips},
            {"UR7", "review of stage U3, finding U3-R7: dodge cancels a bow volley", dodge_ends_volley},
            {"UR9", "review of stage U3, finding U3-R7: a dodge in the buffer window of Rapid Shot cancels at the next cancel mark", buffered_dodge},
            {"UR8", "review of stage U3, finding U3-R8: switch during Rapid Shot", rapid_switch_time}};
}
}

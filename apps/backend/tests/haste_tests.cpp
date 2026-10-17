// Cases of stage B3: the haste chain of the bow build of the user (world/passives.h: the weapon swap, the end of a dodge, the time factor of a
// status, the condition StatusEffectCategories; world/statuses.h: the time factor and OnStackStatusEffect; world/stamina.h: the dodge action).
// The data has the structure and the values of the game data of these rows (support/haste_fixture.h). The proof on the tree of stage B2 builds
// this file with SOULWARDEN_B3_BASELINE (support/haste_fixture.h).
//
//   HS1   Dodge and Weave: the haste at the end of a dodge with the bow and 99 percent stamina or more (the mark CanTransition), 2 s; no new
//         haste while one runs; none with the staff or at 98 stamina; a buffered next dodge continues the action; the haste ends when the bow
//         leaves the hand (the key of a weapon, and an equipment change)
//   HS2   Archer's Speed: OnWeaponSwap of the key of a weapon to the bow: the haste (5 s) and its timer (10 s, OnStackStatusEffect at the first
//         stack); no second haste while the timer runs; again after it; an equipment change is no swap; no node, no haste
//   HS3   the time factor of a status on the character: Speedy and Alacrity II on one haste (1 + 0.5 + 0.094 x potency), DurationMax, the timer
//         and an empower keep their time, a time of a command stays, the row with no status and no category gives nothing
//   HS4   Speedy: +0.1 BaseDamage on a hit while the character has a haste (the condition StatusEffectCategories of the attacker); a value
//         with a condition of a status and no event is a value of each hit while the condition holds, not a fixed value
//   HS5   Evasive Tactics and the cooldown change after a dodge: AfterAction and AfterSequence with no event at the end of a dodge; a row with an
//         event in the window after an action is not a rule of a hit
//   HS6   Closing In: OnHit at 50 percent health of the target or less, the cooldowns of the weapon -5 percent (the column Duration is a mark)
//   HS7   two sessions (MP-10): the speed table and the HUD map go to the owner; the observer gets the public status only (no name plate, no
//         speed table); the position check of the owner takes the haste; the run of the owner is accepted
//   HS8   the load: 300 swaps and dodges with their rules, with 40 or 4,000 far creatures, with a time limit
//   HS9   the list of the rules: what the evaluator applies of the haste chain, and what it does not apply with the cause
//   HS10  the ability data of format 7: the dodge status actions and the tag removal actions, the refusals, a file of format 6

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/haste_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace haste;

/** @return The view of one rule of a character in the list of the evaluator (an empty view: none). */
world::RuleView view_of(HasteShop& shop, const char* id) {
    for (const world::RuleView& view : shop.world().passives().describe(*shop.character())) {
        if (view.id == id) {
            return view;
        }
    }
    return {};
}

/** HS1: Dodge and Weave (the dodge status action of the data) and the end of its haste with the bow. */
void dodge_and_weave(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {}, {kDodgeWeave});
    const auto fill = [&](float stamina) {
        world::Entity& own = *shop.character();
        (void)shop.world().stamina().change(own, 100.0F, "a test");
        if (stamina < 100.0F) {
            (void)shop.world().stamina().change(*shop.character(), stamina - 100.0F, "a test");
        }
    };
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge with full stamina and the bow in the hand");
    shop.wait(480);
    check(shop.find(kDodgeHaste) == nullptr, "no haste before the end of the dodge action (0.48 s)");
    shop.wait(40);
    const world::ActiveStatus* haste = shop.find(kDodgeHaste);
    note("the haste after the mark CanTransition: " + std::to_string(shop.left(kDodgeHaste)) + " s left");
    check(haste != nullptr && near(shop.left(kDodgeHaste), 2.0F, 0.05F) && haste->source.id == kDodgeAction && shop.dodge_statuses() == 1,
          "the haste at the end of the dodge action (the mark CanTransition, 0.5 s): 2 s, the action of the data as its source");
    check(near(shop.character()->sprintMultiplier, 1.07F) && near(shop.character()->moveMultiplier, 1.07F), "the speed table of the owner has 1.07");
    // No new haste while one runs (Not IfHasStatusEffect of the action).
    fill(100.0F);
    const float before = shop.left(kDodgeHaste);
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a second dodge with full stamina while the haste runs");
    shop.wait(520);
    check(near(shop.left(kDodgeHaste), before - 0.52F, 0.05F) && shop.dodge_statuses() == 1, "the haste that runs gets no new time");
    // The bow leaves the hand: the haste ends (the tag removal action Bow_Remove_StatusEffects_Sheathe).
    shop.key(2);
    check(shop.find(kDodgeHaste) == nullptr && shop.tag_removals() == 1, "the key of the staff: the haste of Dodge and Weave ends");
    // No haste with the staff in the hand, and none at 98 stamina.
    shop.wait(1500);
    fill(100.0F);
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge with the staff");
    shop.wait(520);
    check(shop.find(kDodgeHaste) == nullptr, "a dodge with the staff in the hand gives no haste (the tag Bow)");
    shop.key(1);
    shop.wait(1500);
    fill(98.0F);
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge with the bow at 98 stamina");
    shop.wait(520);
    check(shop.find(kDodgeHaste) == nullptr, "a dodge at 98 stamina gives no haste (IfStaminaValue 0.99 of the maximum)");
    // A buffered next dodge (0.3 s) continues the action: the haste at the end of the second dodge (1.0 s).
    shop.wait(1500);
    fill(100.0F);
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: the first dodge of a chain");
    shop.wait(300);
    check(shop.dodge() == world::DodgeVerdict::buffered, "setup: a press in the buffered window (0.3 s)");
    shop.wait(220);
    check(shop.find(kDodgeHaste) == nullptr, "at 0.52 s the action continues with the next dodge: no haste yet");
    shop.wait(500);
    check(shop.find(kDodgeHaste) != nullptr && near(shop.left(kDodgeHaste), 2.0F, 0.06F), "the haste at the end of the second dodge (1.0 s)");
    // An equipment change that takes the bow out of the hand ends the haste too.
    check(shop.world().inventory().unequip(kCharacterA, world::paperdoll::kMainHand1).ok(), "setup: the bow goes into the bag");
    check(shop.find(kDodgeHaste) == nullptr && shop.tag_removals() == 2, "the bow out of the hand by an equipment change: the haste ends");
}

/** HS2: Archer's Speed (OnWeaponSwap) and its timer (OnStackStatusEffect). */
void archers_speed(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {}, {kArcherSpeed});
    shop.key(2);
    check(shop.find(kSwapHaste) == nullptr, "the key of the staff: no haste (the node is a rule of the bow tree)");
    shop.key(1);
    note("after the swap to the bow: the haste " + std::to_string(shop.left(kSwapHaste)) + " s, the timer " + std::to_string(shop.left(kSwapTimer)) + " s");
    check(near(shop.left(kSwapHaste), 5.0F, 0.01F) && near(shop.left(kSwapTimer), 10.0F, 0.01F) && shop.swap_rules() == 1 && shop.on_stack() == 1,
          "the swap to the bow: the haste for 5 s and its timer for 10 s (OnStackStatusEffect at the first stack)");
    shop.wait(2000);
    shop.key(2);
    shop.key(1);
    check(shop.swap_rules() == 1 && near(shop.left(kSwapHaste), 3.0F, 0.05F), "a second swap in the 10 s of the timer: no new haste");
    shop.wait(8100);
    check(shop.find(kSwapTimer) == nullptr && shop.find(kSwapHaste) == nullptr, "after 10 s: no timer and no haste");
    shop.key(2);
    shop.key(1);
    check(shop.swap_rules() == 2 && shop.find(kSwapHaste) != nullptr, "a swap after the timer: the haste again");
    // An equipment change is no swap: the staff out of slot 2 (selected) puts the bow into the hand.
    shop.wait(10100);
    shop.key(2);
    check(shop.world().inventory().unequip(kCharacterA, world::paperdoll::kMainHand1 + 1).ok()
              && world::Combat::weapon_in_hand(*shop.character())->itemId == "TestBow",
          "setup: the staff goes into the bag, and the bow is the weapon in the hand");
    check(shop.swap_rules() == 2 && shop.find(kSwapHaste) == nullptr, "an equipment change that puts the bow into the hand gives no haste");
    // With no node, no haste.
    HasteShop other;
    other.ready(kCharacterA, {}, {});
    other.key(2);
    other.key(1);
    check(other.find(kSwapHaste) == nullptr, "a character with no node Archer's Speed gets no haste at the swap");
}

/** HS3: the time factor of a status on the character (Speedy and Alacrity II, client getter 0x68308E0). */
void time_factor(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {kSpeedy, kAlacrity, "", "", ""}, {kDodgeWeave, kArcherSpeed}, 200);
    const float potency = shop.potency(kAlacrity);
    const float factor = 1.0F + 0.5F + 0.094F * potency;
    note("the potency of Alacrity II: " + std::to_string(potency) + "; the time factor of a haste: " + std::to_string(factor));
    check(near(potency, 1.274F, 0.001F), "setup: the potency of Alacrity II is 1.274 (gear score 800 in the game)");
#ifndef SOULWARDEN_B3_BASELINE
    check(near(shop.world().passives().duration_factor(*shop.character(), *shop.host.abilities.status(kDodgeHaste)), factor, 0.0001F),
          "the time factor of a haste: 1 + 0.5 + 0.094 x potency (a character with no haste: the new haste counts as present)");
#endif
    shop.key(2);
    shop.key(1);
    note("Archer's Speed: " + std::to_string(shop.left(kSwapHaste)) + " s, the timer " + std::to_string(shop.left(kSwapTimer)) + " s");
    check(near(shop.left(kSwapHaste), 5.0F * factor, 0.01F) && near(shop.left(kSwapTimer), 10.0F, 0.01F),
          "Archer's Speed with Speedy and Alacrity II: 5 x 1.62 = 8.1 s; the timer (category Timer) keeps 10 s");
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge");
    shop.wait(520);
    note("Dodge and Weave: " + std::to_string(shop.left(kDodgeHaste)) + " s");
    check(near(shop.left(kDodgeHaste), 2.0F * factor, 0.05F), "Dodge and Weave with Speedy and Alacrity II: 2 x 1.62 = 3.24 s (DurationMax 4)");
    // A status of a different category: no factor of a haste perk.
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    (void)shop.world().statuses().apply(*shop.character(), kEvasiveEmpower, how);
    check(near(shop.left(kEvasiveEmpower), 5.0F, 0.01F), "an empower keeps its time (the perks name the category Haste)");
    // DurationMax limits the time with the factor: 3 x 1.62 = 4.86, not more than 4.
    (void)shop.world().statuses().apply(*shop.character(), kShortHaste, how);
    check(near(shop.left(kShortHaste), 4.0F, 0.01F), "a haste of 3 s with DurationMax 4: 4 s");
#ifndef SOULWARDEN_B3_BASELINE
    // The time of a command stays (the development command with seconds).
    (void)shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus& entry) { return entry.status == kShortHaste; },
                                         world::StatusEnd::command);
    world::StatusApply command = how;
    command.seconds = 2.0F;
    command.fixedTime = true;
    (void)shop.world().statuses().apply(*shop.character(), kShortHaste, command);
    check(near(shop.left(kShortHaste), 2.0F, 0.01F), "the time of a command stays (no time factor)");
#endif
    // Speedy alone: 1.5.
    HasteShop speedy;
    speedy.ready(kCharacterA, {kSpeedy, "", "", "", ""}, {kDodgeWeave});
    check(speedy.dodge() == world::DodgeVerdict::accepted, "setup: a dodge with Speedy");
    speedy.wait(520);
    check(near(speedy.left(kDodgeHaste), 3.0F, 0.05F), "Dodge and Weave with Speedy alone: 2 x 1.5 = 3 s");
    // A time factor with no StatusEffect and no StatusEffectCategories (the form of Ability_Perk_Amulet_DurEmpower) matches no status.
    HasteShop empower;
    empower.ready(kCharacterA, {kEmpowerTime, "", "", "", ""}, {});
    world::StatusApply plain;
    plain.source = {world::StatusSourceKind::command, "test", 0, empower.character()->id};
    (void)empower.world().statuses().apply(*empower.character(), kEvasiveEmpower, plain);
    check(near(empower.left(kEvasiveEmpower), 5.0F, 0.01F), "the row with StatusEffectDurationCats only gives no time factor");
}

/** HS4: Speedy, +0.1 BaseDamage while the attacker has a haste (StatusEffectCategories, client self test 0x67A0130). */
void speedy_damage(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {kSpeedy, "", "", "", ""}, {});
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    world::HitContext probe;
    probe.table = gamedata::progression_id(kBowTable);
    probe.row = "TestBowLight";
    probe.attackType = "Light";
    world::Entity& target = *shop.world().creatures().find(wolf);
    const float without = shop.world().passives().before_hit(*shop.character(), target, probe).damage;
    const float rawWithout = shop.shot(target, "TestBowLight").damage.raw;
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    check(shop.world().statuses().apply(*shop.character(), kShortHaste, how) == world::StatusVerdict::applied, "setup: a haste on the character");
    const float with = shop.world().passives().before_hit(*shop.character(), *shop.world().creatures().find(wolf), probe).damage;
    const float rawWith = shop.shot(*shop.world().creatures().find(wolf), "TestBowLight").damage.raw;
    note("the damage part without a haste " + std::to_string(without) + ", with " + std::to_string(with) + "; raw " + std::to_string(rawWithout) + " and "
         + std::to_string(rawWith));
    check(near(with - without, 0.1F, 0.0001F), "a hit while hasted has +0.1 BaseDamage (Speedy)");
    check(rawWith > rawWithout * 1.05F, "the raw damage of the arrow is larger with the haste");
    // A value with a condition of a status and no event (the form of BonusDamageSelfCCPerk1): a value of each hit while the character has a
    // slow or a root; the gear values do not take it (before stage B3 they gave it with no slow).
    HasteShop slowed;
    slowed.ready(kCharacterA, {kWhileSlowed, "", "", "", ""}, {});
    const world::EntityId other = slowed.wolf_at(6.0F)->id;
    const float plain = slowed.world().passives().before_hit(*slowed.character(), *slowed.world().creatures().find(other), probe).damage;
    const float gearPart = slowed.character()->gear.baseDamage;
    world::StatusApply slow;
    slow.source = {world::StatusSourceKind::command, "test", 0, slowed.character()->id};
    check(slowed.world().statuses().apply(*slowed.character(), kSlow, slow) == world::StatusVerdict::applied, "setup: a slow on the character");
    const float whileSlowed = slowed.world().passives().before_hit(*slowed.character(), *slowed.world().creatures().find(other), probe).damage;
    note("the value of the row with no event: " + std::to_string(plain) + " with no slow, " + std::to_string(whileSlowed) + " with a slow; the gear part "
         + std::to_string(gearPart));
    check(near(plain, 0.0F, 0.0001F) && near(gearPart, 0.0F, 0.0001F), "with no slow: no part of the row (also not in the gear values)");
    check(near(whileSlowed, 0.12F, 0.0001F), "with a slow: +0.12 BaseDamage of each hit");
}

/** HS5: Evasive Tactics (AfterAction, AfterSequence, no event) and the cooldown change after a dodge; a row with an event after an action. */
void after_dodge(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {kDodgeCdr, "", "", "", ""}, {kEvasive, kWindowRule});
    // A cooldown of Rapid Shot (the bar of the bow) that runs.
    shop.press(2);
    shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(90);
    const float running = shop.remaining(kRapid);
    check(running > 5.0F, "setup: the cooldown of Rapid Shot runs");
    const auto start = shop.host.clock.now();
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge");
    shop.wait(520);
    const float elapsed = std::chrono::duration<float>(shop.host.clock.now() - start).count();
    const float reduced = running - elapsed - shop.remaining(kRapid);
    note("Evasive Tactics: " + std::to_string(shop.left(kEvasiveEmpower)) + " s; the cooldown change after the dodge: " + std::to_string(reduced) + " s");
    check(shop.find(kEvasiveEmpower) != nullptr && near(shop.left(kEvasiveEmpower), 5.0F, 0.05F) && shop.after_dodge() == 2,
          "the end of the dodge: Evasive Tactics gives its empower for 5 s");
    check(near(reduced, 0.005F * kRapidCooldown, 0.02F), "the cooldown change after a dodge (CooldownTimer -0.005, ActiveWeapon)");
    // A row with an event in the window after an action (OnHit, AfterAction, Duration) adds nothing to a hit.
    world::HitContext probe;
    probe.table = gamedata::progression_id(kBowTable);
    probe.row = "TestBowLight";
    probe.attackType = "Light";
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    check(near(shop.world().passives().before_hit(*shop.character(), *shop.world().creatures().find(wolf), probe).damage, 0.0F, 0.0001F),
          "a rule with OnHit and AfterAction gives no damage part to a hit (no window after an action in the backend)");
}

/** HS6: Closing In (OnHit, TargetHealthPercent 50, the column Duration -1 is a mark, CooldownTimer -0.05 ActiveWeapon). */
void closing_in(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {}, {kClosingIn});
    shop.press(2);
    shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(90);
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto hit_at = [&](float part) {
        check(shop.world().creatures().set_health(wolf, kWolfHealth * part).error == world::CreatureError::none, "setup: the health of the wolf");
        const float before = shop.remaining(kRapid);
        (void)shop.shot(*shop.world().creatures().find(wolf), "TestBowLight");
        return before - shop.remaining(kRapid);
    };
    const float high = hit_at(0.6F);
    const float low = hit_at(0.4F);
    note("the cooldown change of a hit at 60 percent: " + std::to_string(high) + " s, at 40 percent: " + std::to_string(low) + " s");
    check(near(high, 0.0F, 0.001F), "a hit at 60 percent health of the target: no change");
    check(near(low, 0.05F * kRapidCooldown, 0.01F), "a hit at 40 percent: the cooldowns of the weapon in the hand -5 percent of their time");
}

/** HS7: two sessions (MP-10): the owner gets the speed table and the HUD map; the observer only the public status. */
void two_sessions(const Options&) {
    HasteShop shop(true);
    shop.enter_b();
    shop.ready(kCharacterA, {}, {kDodgeWeave, kArcherSpeed});
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const auto step = [&](int steps) {
        for (int index = 0; index < steps; ++index) {
            shop.walker.idle(1);
            walkerB.idle(1);
        }
    };
    step(3);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    shop.key(2);
    shop.key(1);
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge after the swap");
    step(17);
    const auto has = [](const std::vector<std::uint16_t>& numbers, const gamedata::StatusRow* row) {
        return row != nullptr && std::find(numbers.begin(), numbers.end(), row->index) != numbers.end();
    };
    const gamedata::StatusRow* dodge = shop.host.abilities.status(kDodgeHaste);
    const gamedata::StatusRow* swap = shop.host.abilities.status(kSwapHaste);
    const gamedata::StatusRow* timer = shop.host.abilities.status(kSwapTimer);
    const std::vector<std::uint16_t> owner = full_map_numbers(shop.a, 1, fromA);
    const std::vector<std::uint16_t> seen = full_map_numbers(shop.b, 3, fromB);
    const std::vector<std::uint16_t> plates = compact_numbers(shop.b, fromB);
    check(has(owner, dodge) && has(owner, swap) && has(owner, timer), "A (the owner) gets the two hastes and the timer in its full map (the HUD icons)");
    check(has(seen, dodge) && !has(seen, swap) && !has(seen, timer), "B gets only the haste with AlwaysReplicate in the public map of A");
    check(!has(plates, dodge) && !has(plates, swap), "B gets no name plate icon of a haste (no ShowInNameplates)");
    check(!fragments_of(shop.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromA).empty()
              && fragments_of(shop.b, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromB).empty(),
          "A gets its speed table; B gets no speed table of A");
    check(near(shop.character()->sprintMultiplier, 1.14F) && near(shop.world().find_character(kCharacterB)->sprintMultiplier, 1.0F)
              && shop.world().find_character(kCharacterB)->statuses.empty(),
          "the two hastes are values of A (1.14); B has none");
    float speedA = 0.0F;
    float speedB = 0.0F;
    for (const std::uint32_t id : {1U, 2U}) {
        if (const server::rep::RepSession* session = shop.host.server->session(id)) {
            (session->character_id() == kCharacterA ? speedA : speedB) = session->position_track().client_speed(shop.host.clock.now());
        }
    }
    check(near(speedA, 1.14F) && near(speedB, 1.0F), "the position check of A takes the haste (1.14); the check of B stays 1.0");
    // A run of A at the hasted sprint (5.5 m/s x 1.14) with a dodge in it: each report is accepted (no correction), and B sees A there.
    std::uint64_t refusedBefore = 0;
    for (const std::uint32_t id : {1U, 2U}) {
        if (const server::rep::RepSession* session = shop.host.server->session(id); session != nullptr && session->character_id() == kCharacterA) {
            refusedBefore = session->position_track().refused();
        }
    }
    for (int index = 0; index < 30; ++index) {
        shop.walker.position.x += 6.27F * std::chrono::duration<float>(kTick).count() * (index >= 10 && index < 25 ? 1.4F : 1.0F);
        step(1);
    }
    std::uint64_t refusedAfter = 0;
    for (const std::uint32_t id : {1U, 2U}) {
        if (const server::rep::RepSession* session = shop.host.server->session(id); session != nullptr && session->character_id() == kCharacterA) {
            refusedAfter = session->position_track().refused();
        }
    }
    check(refusedAfter == refusedBefore && near(shop.character()->position.x, shop.walker.position.x, 0.01F),
          "each report of the hasted run of A is accepted: the position of the world follows it");
}

/** HS8: the load of the rules of the stage: 300 swaps and dodges with their rules, with 40 or 4,000 far creatures. */
void load(const Options&) {
    const auto run = [](unsigned farCount, std::uint64_t& looked, double& seconds) {
        HasteShop shop;
        shop.ready(kCharacterA, {kSpeedy, kAlacrity, kDodgeCdr, "", ""}, {kDodgeWeave, kArcherSpeed, kEvasive, kClosingIn});
        for (unsigned index = 0; index < farCount; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 600.0F + static_cast<float>(index % 100) * 3.0F,
                                                                   kPlace.y + 600.0F + static_cast<float>(index / 100) * 3.0F), 90.0F);
        }
        shop.walker.idle(3);
        const std::uint64_t start = shop.host.world.creatures().counts().effectsLooked + shop.host.world.passives().counts().rulesLooked;
        const auto clock = std::chrono::steady_clock::now();
        for (int round = 0; round < 300; ++round) {
            shop.key(2);
            shop.key(1);
            (void)shop.world().stamina().change(*shop.character(), 100.0F, "a test");
            (void)shop.dodge();
            shop.wait(600);
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
        looked = shop.host.world.creatures().counts().effectsLooked + shop.host.world.passives().counts().rulesLooked - start;
        return shop.swap_rules() + shop.dodge_statuses() + shop.after_dodge();
    };
    std::uint64_t smallLooked = 0;
    std::uint64_t largeLooked = 0;
    double smallSeconds = 0.0;
    double largeSeconds = 0.0;
    const std::uint64_t small = run(40, smallLooked, smallSeconds);
    const std::uint64_t large = run(4000, largeLooked, largeSeconds);
    note("300 swaps and dodges: 40 far creatures: " + std::to_string(smallSeconds) + " s, " + std::to_string(smallLooked) + " looks; 4,000 far: "
         + std::to_string(largeSeconds) + " s, " + std::to_string(largeLooked) + " looks; rules that acted " + std::to_string(large));
    check(large > 0 && large == small, "the swaps and the dodges give the same rules in the two worlds");
    check(largeLooked == smallLooked, "the rules and the effects that they look at do not depend on the far creatures");
    check(largeSeconds < 2.0, "the time limit: 300 swaps and dodges with their rules in less than 2 s in a world of 4,000 creatures");
}

/** HS9: the list of the rules of the haste chain. */
void rule_list(const Options&) {
    HasteShop shop;
    shop.ready(kCharacterA, {kSpeedy, kAlacrity, kEmpowerTime, kDodgeCdr, ""}, {kDodgeWeave, kArcherSpeed, kEvasive, kClosingIn, kWindowRule, kOutgoingOnly});
    const auto applied = [&](const char* id, const char* part) {
        const world::RuleView view = view_of(shop, id);
        note(std::string(id) + ": " + (view.applied ? "applied" : "not applied") + ": " + view.note);
        return view.applied && view.note.find(part) != std::string::npos;
    };
    const auto refused = [&](const char* id, const char* part) {
        const world::RuleView view = view_of(shop, id);
        note(std::string(id) + ": " + (view.applied ? "applied" : "not applied") + ": " + view.note);
        return !view.id.empty() && !view.applied && view.note.find(part) != std::string::npos;
    };
    check(applied(kDodgeWeave, "dodge status action"), "Dodge and Weave: applied (the dodge status action of the data)");
    check(applied(kArcherSpeed, "OnWeaponSwap"), "Archer's Speed: applied (OnWeaponSwap)");
    check(applied(kEvasive, "after the end of a dodge"), "Evasive Tactics: applied (after the end of a dodge)");
    check(applied(kClosingIn, "OnHit"), "Closing In: applied (OnHit; the column Duration is a mark)");
    check(applied(kSpeedyTime, "time of a status"), "Speedy: the time factor is applied");
    check(applied(kSpeedyDamage, "OnHit"), "Speedy: the damage part while hasted is applied");
    check(applied(kAlacrityRow, "outgoing part"), "Alacrity II: applied, and the list names the outgoing part that is not applied");
    check(applied(kDodgeCdrRow, "after the end of a dodge"), "the cooldown change after a dodge: applied");
    check(refused(kEmpowerTimeRow, "names no status and no category"), "the time factor with StatusEffectDurationCats only: not applied, with the cause");
    check(refused(kWindowRule, "window after an action"), "a row with an event in the window after an action: not applied, with the cause");
    check(refused(kOutgoingOnly, "TargetStatusEffectDurationCats"), "the outgoing time factor alone (Battle Precision, stage B4): not applied");
}

/** HS10: the ability data of format 7: the dodge status actions and the tag removal actions. */
void data_format(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "haste-ability-data.json";
    const bool lower = gamedata::progression_id("statusa") < gamedata::progression_id("statusb");
    nlohmann::json good{
        {"format", 7},
        {"statusRows", 2},
        {"counts", {{"cooldowns", 1}, {"statusEffects", 2}, {"spells", 0}, {"manaCosts", 0}, {"staminaCosts", 0}, {"abilityCosts", 0},
                    {"abilityRows", 0}, {"statusCategories", 0}, {"actionSpells", 0}, {"actionNodes", 0}, {"actionStatuses", 0},
                    {"actionHitStatuses", 0}, {"actionDodgeStatuses", 1}, {"actionTagRemovals", 1}}},
        {"cooldowns", {{"Test", 1, 0}}},
        {"statusEffects", {{"StatusA", lower ? 0 : 1, 2, 0.07, false, false, 0, false, 0, {{"EffectCategories", "Buff+Haste"}}},
                           {"StatusB", lower ? 1 : 0, 3, 0, false, false, 0, false, 0, nlohmann::json::object()}}},
        {"spells", nlohmann::json::array()},
        {"manaCosts", nlohmann::json::array()},
        {"staminaCosts", nlohmann::json::array()},
        {"abilityCosts", nlohmann::json::array()},
        {"abilityRows", nlohmann::json::array()},
        {"statusCategories", nlohmann::json::array()},
        {"actionSpells", nlohmann::json::array()},
        {"actionNodes", nlohmann::json::array()},
        {"actionStatuses", nlohmann::json::array()},
        {"actionHitStatuses", nlohmann::json::array()},
        {"actionDodgeStatuses",
         nlohmann::json::array({nlohmann::json::array({"playeractions_ability_bow", "Passive_Test_HasteOnDodge",
                                                       {{"dodge", "Dodge"}, {"nodes", {"Passive_Test_DodgeHaste"}}, {"tags", {"Bow"}},
                                                        {"stamina", {"GreaterThanOrEqual", 0.99}}, {"without", {"StatusA"}}, {"exitTags", {"Bow"}}},
                                                       {"StatusA"}})})},
        {"actionTagRemovals", nlohmann::json::array({nlohmann::json::array({"playeractions_ability_bow", "Test_Remove", {"Bow"}, {"StatusA"}})})}};
    const auto load = [&](const nlohmann::json& document, gamedata::AbilityData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        return data.load(path.string());
    };
    gamedata::AbilityData data;
    const bool loaded = load(good, data);
    note(loaded ? std::string("format 7: loaded") : "format 7: " + data.error());
    check(loaded, "a file of format 7 loads");
#ifndef SOULWARDEN_B3_BASELINE
    check(loaded && data.tables().actionDodgeStatuses.size() == 1 && data.tables().actionDodgeStatuses.front().dodge == "Dodge"
              && data.tables().actionDodgeStatuses.front().stamina && near(data.tables().actionDodgeStatuses.front().stamina->value, 0.99F)
              && data.tables().actionDodgeStatuses.front().exitTags == std::vector<std::string>{"Bow"}
              && data.tables().actionTagRemovals.size() == 1 && data.tables().actionTagRemovals.front().statuses == std::vector<std::string>{"StatusA"},
          "the dodge status action (its tests, its exit tag, its status) and the tag removal action");
#endif
    nlohmann::json bad = good;
    bad["actionDodgeStatuses"][0][3][0] = "NoSuchStatus";
    check(!load(bad, data), "a dodge status action that names a status that the data does not have: the file is refused");
    bad = good;
    bad["actionTagRemovals"][0][2] = nlohmann::json::array();
    check(!load(bad, data), "a tag removal action with no tag: the file is refused");
    bad = good;
    bad["actionDodgeStatuses"][0][2].erase("exitTags");
    check(!load(bad, data), "a dodge status action with no exit tags: the file is refused");
    bad = good;
    bad["format"] = 6;
    bad.erase("actionDodgeStatuses");
    bad.erase("actionTagRemovals");
    bad["counts"].erase("actionDodgeStatuses");
    bad["counts"].erase("actionTagRemovals");
    const bool older = load(bad, data);
#ifndef SOULWARDEN_B3_BASELINE
    check(older && data.tables().actionDodgeStatuses.empty() && data.tables().actionTagRemovals.empty(),
          "a file of format 6 still loads, with no dodge status action (a dodge then gives no haste of Dodge and Weave)");
#else
    check(older, "a file of format 6 loads");
#endif
    // The world with no dodge status action: no haste after a dodge.
    HasteShop shop(false, false);
    shop.ready(kCharacterA, {}, {kDodgeWeave});
    (void)shop.dodge();
    shop.wait(520);
    check(shop.find(kDodgeHaste) == nullptr, "a world with the data of an older file: no haste of Dodge and Weave");
}

} // namespace

std::vector<Case> haste_cases() {
    return {
        {"HS1", "stage B3: Dodge and Weave (the haste at the end of a dodge with the bow at full stamina, its end when the bow leaves the hand)",
         dodge_and_weave},
        {"HS2", "stage B3: Archer's Speed (OnWeaponSwap, the haste and its timer, one haste in 10 s, an equipment change is no swap)", archers_speed},
        {"HS3", "stage B3: the time factor of a status (Speedy and Alacrity II on one haste, DurationMax, the timer, a command)", time_factor},
        {"HS4", "stage B3: Speedy, +0.1 base damage while hasted (StatusEffectCategories of the attacker)", speedy_damage},
        {"HS5", "stage B3: Evasive Tactics and the cooldown change after the end of a dodge; no window of an event after an action", after_dodge},
        {"HS6", "stage B3: Closing In (OnHit at 50 percent health or less, the cooldowns -5 percent; the column Duration)", closing_in},
        {"HS7", "stage B3: two sessions (MP-10): the speed table and the HUD map of the owner, the public status of the observer, the run", two_sessions},
        {"HS8", "stage B3: the load of the swaps and the dodges with their rules (40 and 4,000 far creatures)", load},
        {"HS9", "stage B3: the list of the rules of the haste chain (applied, and the causes of what is not applied)", rule_list},
        {"HS10", "stage B3: the ability data of format 7 (the dodge status actions, the tag removal actions, the refusals, format 6)", data_format},
    };
}

} // namespace soulwarden::backend::tests

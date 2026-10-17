// Cases of stage B2: the rules of the Boltcaster of the bow build of the user and the stacks of a status on a creature (world/creatures.h,
// world/passives.h, server/rep/states/status_state.cpp). The data has the structure and the values of the game data of these rows with a
// made-up damage type and made-up rows of the test bow (support/boltcaster_fixture.h). The proof on the tree of stage U3 builds this file with
// SOULWARDEN_B2_BASELINE (support/boltcaster_fixture.h).
//
//   BC1   the stacks of a status on a creature: StackMax, the refresh of the time (RefreshDuration), the end of all stacks at the end, the
//         count in the compact map of each observer (0x81, 0x82, 0x83), DontReplaceStack, the damage part of each stack
//   BC2   the one status path of a creature: the ApplicationCooldown of a row on the target (each source), the removal window (a running
//         effect whose row removes a row or its category refuses it), a row with no duration: one damage and its icon for DurationMax
//   BC3   the Boltcaster: Electrified (+0.2 for a light or heavy hit with its lightning part), a charge for each light or heavy hit, the bolt
//         on the hit after 3 charges (0.8 of the damage of the hit, Lightning, one number), the end of the charges, no charge of the hit of
//         the bolt, one bolt in 3 s for each target, an ability hit of the list gives the bolt and no charge, a row out of the list no bolt
//   BC4   Shirking Shock II: a creature hit in the immune window of a dodge gives the tracker; the next hit has 0.236 x potency of the base
//         weapon damage as Lightning, then the tracker ends; no tracker, no damage
//   BC5   Surprise Attack: +0.2 on the first hit on a target, none in the 10 s of the tracker; the helper row renews the tracker; the tracker
//         of a different character blocks the bonus (decision D10)
//   BC6   two sessions (MP-10): the observer gets the charge icon with its count and the health of the wolf with the bytes of the owner, and
//         no number; the shot of a client gives the charge
//   BC7   two attackers on one creature (decision D10): the charges of each source count for the bolt of the third one; the cooldown of
//         the bolt is on the target (code review of stage B2, finding B2-R6: 3 new charges after the removal window, one refusal of the
//         cooldown of the target)
//   BC8   the conditions with an owner and with the stacks of the attacker (CheckStatusEffectsOnTargetOwned, StatusEffectStackSize), the end
//         of a status of the attacker (AbilitySpecific with StatusEffectsList), and the list of the rules (the helper row, the row of a status)
//   BC9   the load: 300 hits with charges on 40 near creatures, with 4,000 far creatures, look only at the target
//   BC10  a perk of the instance in a place replaces the fixed perk of the definition in that place (the charm in place 4 of the Boltcaster)
//   BC11  the projectile data of format 3: the switch marks and the dodge marks of a bow action, the refusals, a file of format 2

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/boltcaster_fixture.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {

using namespace boltcaster;

/** The amount of each damage number with one entry of a type (the bolt and a tick have one entry; a hit with a conversion has two). */
std::vector<float> single_numbers(const FakeClient& client, std::uint8_t type, std::size_t from) {
    std::vector<float> amounts;
    for (const auto& entries : damage_entries(client, from)) {
        if (entries.size() == 1 && entries.front().first == type) {
            amounts.push_back(entries.front().second);
        }
    }
    return amounts;
}

std::string joined(const std::vector<std::uint8_t>& bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        char part[8];
        std::snprintf(part, sizeof(part), "%s%02x", text.empty() ? "" : " ", static_cast<unsigned>(byte));
        text += part;
    }
    return text;
}

/** BC1: the stacks of a status on a creature. */
void creature_stacks(const Options&) {
    BoltShop shop(true);
    shop.enter_b();
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto step = [&] {
        shop.walker.idle(1);
        walkerB.idle(1);
    };
    // Both sessions observe the wolf (the interest of a session comes some steps after the spawn).
    for (int index = 0; index < 30; ++index) {
        step();
    }
    const gamedata::StatusRow* charge = shop.host.abilities.status(kCharge);
    check(charge != nullptr, "setup: the charge row");
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    std::vector<unsigned> stacks;
    for (int index = 0; index < 4; ++index) {
        (void)shop.host.world.creatures().give_status(*shop.host.world.creatures().find(wolf), *charge, shop.character()->id);
        step();
        stacks.push_back(shop.stacks(wolf, kCharge));
    }
    note("the stacks after 4 applications: " + std::to_string(stacks[0]) + " " + std::to_string(stacks[1]) + " " + std::to_string(stacks[2]) + " "
         + std::to_string(stacks[3]));
    check(stacks == std::vector<unsigned>{1, 2, 3, 3}, "one stack for each application, to StackMax 3 (a fourth keeps 3)");
    const std::vector<std::uint8_t> seenA = packed_sequence(shop.a, charge->index, fromA);
    const std::vector<std::uint8_t> seenB = packed_sequence(shop.b, charge->index, fromB);
    note("the packed bytes of the charge: A " + joined(seenA) + ", B " + joined(seenB));
    check(seenA == std::vector<std::uint8_t>{0x81, 0x82, 0x83} && seenB == seenA,
          "each observer gets the compact map with the count of the stacks (0x81, 0x82, 0x83; live check L1 of stage B2), with the same bytes");
    // RefreshDuration: an application 2 s later starts the 3 s again from now; the stacks stay.
    for (int index = 0; index < 30; ++index) {
        step();
    }
    const float before = shop.ends_in(wolf, kCharge);
    (void)shop.host.world.creatures().give_status(*shop.host.world.creatures().find(wolf), *charge, shop.character()->id);
    const float after = shop.ends_in(wolf, kCharge);
    note("the end before and after the refresh: " + std::to_string(before) + " s, " + std::to_string(after) + " s");
    check(before < 1.2F && near(after, 3.0F, 0.05F) && shop.stacks(wolf, kCharge) == 3, "RefreshDuration: the end is now plus 3 s, the stacks stay 3");
    // The end: all stacks go together; each observer gets the list with no charge.
    for (int index = 0; index < 50; ++index) {
        step();
    }
    check(shop.stacks(wolf, kCharge) == 0 && last_packed(shop.a, charge->index, fromA) == 0 && last_packed(shop.b, charge->index, fromB) == 0,
          "at the end the three stacks go together (RemoveUnappliedStacks), and each observer gets a list with no charge");
    // DontReplaceStack at StackMax 2: a third application changes nothing; the damage part counts for each stack.
    const gamedata::StatusRow* weaken = shop.host.abilities.status(kTestStacks);
    world::Entity* creature = shop.host.world.creatures().find(wolf);
    const bool first = shop.host.world.creatures().give_status(*creature, *weaken, 0);
    const bool second = shop.host.world.creatures().give_status(*creature, *weaken, 0);
    const bool third = shop.host.world.creatures().give_status(*creature, *weaken, 0);
    const float factor = creature->creature.damage_factor(shop.host.clock.now(), "Slash");
    note("the weaken: " + std::to_string(shop.stacks(wolf, kTestStacks)) + " stacks, the damage factor " + std::to_string(factor));
    check(first && second && !third && shop.stacks(wolf, kTestStacks) == 2, "DontReplaceStack at StackMax 2: the third application changes nothing");
    check(near(factor, 0.8F, 0.001F), "the damage part of the row counts for each stack: 1 - 2 x 0.1");
}

/** BC2: the ApplicationCooldown on the target, the removal window, a row with no duration. */
void one_status_path(const Options&) {
    BoltShop shop;
    shop.enter_b();
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    world::Creatures& creatures = shop.host.world.creatures();
    const gamedata::StatusRow* charge = shop.host.abilities.status(kCharge);
    const gamedata::StatusRow* removal = shop.host.abilities.status(kRemoval);
    const gamedata::StatusRow* bolt = shop.host.abilities.status(kBolt);
    // The removal window: Status_Perk_Artifact_Set1_Bow_StaticCharge_Removal (0.2 s) refuses a new charge while it runs.
    (void)creatures.give_status(*creatures.find(wolf), *charge, shop.character()->id);
    check(creatures.give_status(*creatures.find(wolf), *removal, shop.character()->id) && shop.stacks(wolf, kCharge) == 0,
          "setup: the removal takes the charge at its application");
    const bool during = creatures.give_status(*creatures.find(wolf), *charge, shop.character()->id);
    shop.pass(0.3F);
    const bool afterWindow = creatures.give_status(*creatures.find(wolf), *charge, shop.character()->id);
    check(!during && afterWindow && shop.blocked() >= 1, "a charge in the 0.2 s of the removal is refused; after it the charge comes");
    // The same for a category (the form of the CC immunity row): a made-up window of 1 s removes the category Weaken.
    (void)creatures.give_status(*creatures.find(wolf), *shop.host.abilities.status(kTestWindow), 0);
    check(!creatures.give_status(*creatures.find(wolf), *shop.host.abilities.status(kTestStacks), 0),
          "a running effect whose row removes a category refuses a row of that category");
    // A row with no duration and a damage (the bolt): one damage of the source, its icon for its DurationMax (0.1 s), and its
    // ApplicationCooldown of 3 s on the target for each source.
    shop.pass(1.2F);
    const float lostBefore = shop.lost(wolf);
    world::StatusDamage damage;
    damage.amount = 100.0F;
    damage.type = "Lightning";
    const world::Entity* beta = shop.host.world.find_character(kCharacterB);
    const bool firstBolt = creatures.give_status(*creatures.find(wolf), *bolt, shop.character()->id, std::nullopt, &damage);
    const bool icon = shop.ends_in(wolf, kBolt) > 0.0F;
    shop.walker.idle(2);
    const float boltLost = shop.lost(wolf) - lostBefore;
    const bool secondBolt = creatures.give_status(*creatures.find(wolf), *bolt, beta->id, std::nullopt, &damage);
    shop.pass(3.1F);
    const bool thirdBolt = creatures.give_status(*creatures.find(wolf), *bolt, beta->id, std::nullopt, &damage);
    shop.walker.idle(2);
    note("the bolt: " + std::to_string(boltLost) + " health (100 Lightning, elemental mitigation 0.1); icon " + std::to_string(icon));
    check(firstBolt && near(boltLost, 90.0F, 0.5F) && icon, "a row with no duration: one damage of its type now, and its icon for its DurationMax");
    check(!secondBolt && thirdBolt && shop.instant_damage() == 2,
          "ApplicationCooldown 3 s on the target: a second bolt of a different source in 3 s is refused, after 3 s it comes");
    check(shop.stacks(wolf, kCharge) == 0, "the bolt ends the charges (its RemoveStatusEffects)");
}

/** BC3: the Boltcaster. */
void boltcaster_rules(const Options&) {
    BoltShop shop;
    shop.arm(kCharacterA, BoltShop::boltcaster());
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto target = [&]() -> world::Entity& { return *shop.host.world.creatures().find(wolf); };
    const std::size_t from = shop.a.got.size();
    // Electrified: a light hit with its lightning part (the conversion of 99 percent) has +0.2 BaseDamage: 100 x 1.2.
    const world::HitResult first = shop.shot(target(), "TestBowLight");
    shop.pass(0.4F);
    note("the light hit: raw " + std::to_string(first.damage.raw) + ", converted " + std::to_string(first.damage.converted));
    check(near(first.damage.raw, 120.0F, 0.5F) && first.damage.converted > first.damage.dealt * 0.95F,
          "Electrified: a light hit with the Lightning part has +0.2 base damage (100 x 1.2), and 99 percent of it is Lightning");
    const world::HitResult heavy = shop.shot(target(), "TestBowHeavy");
    shop.pass(0.4F);
    const world::HitResult third = shop.shot(target(), "TestBowLight");
    shop.pass(0.4F);
    check(near(heavy.damage.raw, 180.0F, 0.5F) && shop.stacks(wolf, kCharge) == 3,
          "a light or a heavy hit adds one charge (Bolt, AttackType Light,Heavy): 3 charges after 3 hits; the heavy hit has +0.2 too (150 x 1.2)");
    // The fourth hit: the bolt (the conditions see the 3 charges before the hit), 0.8 of the raw damage of the hit as Lightning.
    const float lostBefore = shop.lost(wolf);
    const world::HitResult fourth = shop.shot(target(), "TestBowLight");
    shop.walker.idle(2);
    const std::vector<float> bolts = single_numbers(shop.a, kLightningWire, from);
    const float expected = 0.8F * fourth.damage.raw * (1.0F - kElemental);
    note("the bolt numbers: " + std::to_string(bolts.size()) + (bolts.empty() ? "" : ", the first " + std::to_string(bolts.front())) + ", expected "
         + std::to_string(expected));
    check(bolts.size() == 1 && near(bolts.front(), expected, 0.5F) && near(shop.lost(wolf) - lostBefore, fourth.damage.dealt + expected, 0.5F),
          "the hit after 3 charges gives the bolt: one Lightning number of 0.8 x the raw damage of the hit (with the defense)");
    check(shop.stacks(wolf, kCharge) == 0 && shop.blocked() >= 1,
          "the bolt ends the charges, and the charge of the same hit is refused (the removal of 0.2 s): the 4th hit is the bolt");
    check(third.damage.dealt > 0.0F, "setup: the third hit");
    // No bolt in 3 s on the same target: three more hits (1.2 s), then a fourth at 1.6 s: the charges end, no damage.
    for (int index = 0; index < 3; ++index) {
        shop.pass(0.4F);
        (void)shop.shot(target(), "TestBowLight");
    }
    shop.pass(0.4F);
    (void)shop.shot(target(), "TestBowLight");
    shop.walker.idle(2);
    const std::size_t afterCooldown = single_numbers(shop.a, kLightningWire, from).size();
    check(afterCooldown == 1, "at most one bolt in 3 s on each target (ApplicationCooldown 3 of the bolt status)");
    // After 3 s: the ability hit of a row of the list (Rapid Shot, AttackType Ability) at 3 charges gives the bolt and adds no charge.
    shop.pass(3.2F);
    for (int index = 0; index < 3; ++index) {
        (void)shop.shot(target(), "TestBowLight");
        shop.pass(0.4F);
    }
    const unsigned beforeAbility = shop.stacks(wolf, kCharge);
    (void)shop.shot(target(), "Bow_RapidAttack_Standard", kRapid);
    shop.walker.idle(2);
    const std::size_t abilityBolts = single_numbers(shop.a, kLightningWire, from).size();
    check(beforeAbility == 3 && abilityBolts == 2 && shop.stacks(wolf, kCharge) == 0,
          "an ability hit of a row of the list (Bow_RapidAttack_Standard) at 3 charges gives the bolt (live check B2-2)");
    shop.pass(0.4F);
    (void)shop.shot(target(), "Bow_RapidAttack_Standard", kRapid);
    shop.walker.idle(1);
    check(shop.stacks(wolf, kCharge) == 0, "an ability hit adds no charge (Bolt: AttackType Light,Heavy)");
    // A row out of the list of the bolt rule (TestBowHip, a light row): it adds a charge, and it gives no bolt at 3 charges.
    shop.pass(3.2F);
    for (int index = 0; index < 4; ++index) {
        (void)shop.shot(target(), "TestBowHip");
        shop.pass(0.4F);
    }
    check(shop.stacks(wolf, kCharge) == 3 && single_numbers(shop.a, kLightningWire, from).size() == 2,
          "a hit of a row out of the DamageTableRow list of the bolt rule gives no bolt");
}

/** BC4: Shirking Shock II. */
void shirking_shock(const Options&) {
    BoltShop shop;
    shop.arm(kCharacterA, world::PerkIds{kShirking, "", "", "", ""}, 200);
    world::Entity& own = *shop.character();
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const std::size_t from = shop.a.got.size();
    // No dodge: no tracker, no lightning damage.
    (void)shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight");
    shop.walker.idle(3);
    check(single_numbers(shop.a, kLightningWire, from).empty(), "a hit with no dodge before it has no lightning damage");
    // A creature hit in the immune window of a dodge gives the tracker (the rule of stage 3.4f-B) with the potency of the perk at 200.
    world::Stamina& stamina = shop.host.world.stamina();
    check(stamina.dodge(own, shop.host.clock.now(), false) == world::DodgeVerdict::accepted, "setup: a dodge");
    stamina.evaded(own, *shop.host.world.creatures().find(wolf), 100.0F, "a test bite");
    check(shop.has_status(kShockTracker), "a creature hit in the immune window gives the tracker of Shirking Shock II");
    shop.pass(1.0F);
    // The next hit: the row that the tracker equips gives one tick of 0.236 x potency x the base weapon damage (Common_DamageOverTime_Override
    // with the weapon) as Lightning, and the tracker ends.
    const gamedata::PerkRow* perk = shop.host.perks.find(kShirking);
    const float potency = gamedata::PerkData::potency(*perk, 200);
    const gamedata::DamageRow& forced = shop.host.combat.damage_row(*shop.host.combat.row_index("Common_DamageOverTime_Override"));
    const float base = shop.host.world.combat().damage(own, *shop.host.combat.weapon_of("TestBow"), 200, forced, nullptr).raw;
    const float expected = 0.236F * potency * base * (1.0F - kElemental);
    (void)shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight");
    shop.walker.idle(3);
    const std::vector<float> shocks = single_numbers(shop.a, kLightningWire, from);
    note("the potency " + std::to_string(potency) + ", the base " + std::to_string(base) + ", the shock numbers " + std::to_string(shocks.size())
         + (shocks.empty() ? "" : " (" + std::to_string(shocks.front()) + ")") + ", expected " + std::to_string(expected));
    check(near(potency, 1.274F, 0.001F) && shocks.size() == 1 && near(shocks.front(), expected, 0.3F),
          "the next hit has 0.236 x 1.274 = 30 percent of the base weapon damage as Lightning (one number)");
    check(!shop.has_status(kShockTracker), "the tracker ends after the hit (RemoveStatusEffects of the rule)");
    shop.pass(1.0F);
    (void)shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight");
    shop.walker.idle(3);
    check(single_numbers(shop.a, kLightningWire, from).size() == 1, "a later hit has no lightning damage");
}

/** BC5: Surprise Attack. */
void surprise_attack(const Options&) {
    BoltShop shop;
    shop.enter_b();
    shop.arm(kCharacterA, world::PerkIds{}, 100, {kSurprise});
    shop.arm(kCharacterB, world::PerkIds{}, 100, {kSurprise});
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto target = [&]() -> world::Entity& { return *shop.host.world.creatures().find(wolf); };
    const world::HitResult first = shop.shot(target(), "TestBowLight");
    shop.pass(2.0F);
    const world::HitResult second = shop.shot(target(), "TestBowLight");
    const float renewed = shop.ends_in(wolf, kSurpriseTracker);
    shop.pass(1.0F);
    const world::HitResult other = shop.shot(target(), "TestBowLight", "", kCharacterB);
    note("raw: first " + std::to_string(first.damage.raw) + ", 2 s later " + std::to_string(second.damage.raw) + ", B 1 s later "
         + std::to_string(other.damage.raw) + "; the tracker ends in " + std::to_string(renewed) + " s after the second hit");
    check(near(first.damage.raw, 120.0F, 0.5F), "the first hit on a target with no tracker has +0.2 (the data, not the 0.15 of the text)");
    check(near(second.damage.raw, 100.0F, 0.5F), "a hit 2 s later has no bonus (the tracker of 10 s is on the target)");
    check(near(renewed, 10.0F, 0.05F), "the helper row (RequiredEquippedAbilityId, StatusEffect of the self tracker) renews the 10 s of the tracker");
    check(near(other.damage.raw, 100.0F, 0.5F), "a second character gets no bonus while the tracker of the first is on the target (decision D10)");
    // 11 s with no hit: the tracker ends, and the next hit has the bonus again.
    shop.pass(11.0F);
    const world::HitResult later = shop.shot(target(), "TestBowLight");
    check(near(later.damage.raw, 120.0F, 0.5F), "after the end of the tracker the first hit has the bonus again");
}

/** BC6: two sessions (MP-10). */
void two_sessions(const Options&) {
    BoltShop shop(true);
    shop.enter_b();
    shop.arm(kCharacterA, BoltShop::boltcaster());
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    walkerB.idle(5);
    const std::uint16_t charge = shop.host.abilities.status(kCharge)->index;
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    // A shot of the client of A (a light arrow): its hit gives the first charge.
    shop.shoot(ProjectileShop::item("Basic", "TestBowLight"));
    for (int index = 0; index < 12; ++index) {
        shop.walker.idle(1);
        walkerB.idle(1);
    }
    check(shop.stacks(wolf, kCharge) == 1, "the light arrow of the client of A gives one charge");
    for (int index = 0; index < 2; ++index) {
        (void)shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight");
        shop.walker.idle(4);
        walkerB.idle(4);
    }
    const std::vector<std::uint8_t> seenA = packed_sequence(shop.a, charge, fromA);
    const std::vector<std::uint8_t> seenB = packed_sequence(shop.b, charge, fromB);
    note("the charge bytes: A " + joined(seenA) + ", B " + joined(seenB));
    check(!seenB.empty() && seenB.back() == 0x83 && seenA == seenB, "the observer gets the charge icon with its count (0x83) with the bytes of A");
    check(!fragments_of(shop.b, kWolfSlots[1], server::rep::kTypeVitalsState, fromB).empty(), "the observer gets the health of the wolf (its vitals state)");
    const std::size_t numbersA = damage_entries(shop.a, fromA).size();
    const std::size_t numbersB = damage_entries(shop.b, fromB).size();
    check(numbersA == 3 && numbersB == 0, "only the attacker gets the damage numbers of its hits");
    // The bolt: A gets its number, B gets the icon of the bolt and the end of the charges.
    const std::size_t boltA = shop.a.got.size();
    const std::size_t boltB = shop.b.got.size();
    (void)shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight");
    for (int index = 0; index < 4; ++index) {
        shop.walker.idle(1);
        walkerB.idle(1);
    }
    const std::uint16_t bolt = shop.host.abilities.status(kBolt)->index;
    check(single_numbers(shop.a, kLightningWire, boltA).size() == 1 && damage_entries(shop.b, boltB).empty(), "A gets the number of the bolt, B none");
    const std::vector<std::uint8_t> boltIcon = packed_sequence(shop.b, bolt, boltB);
    check(std::find(boltIcon.begin(), boltIcon.end(), 0x81) != boltIcon.end() && last_packed(shop.b, charge, boltB) == 0,
          "B gets the icon of the bolt (its visual effect) and a list with no charge");
}

/** BC7: two attackers on one creature (decision D10). */
void two_attackers(const Options&) {
    BoltShop shop;
    shop.enter_b();
    shop.arm(kCharacterA, BoltShop::boltcaster());
    shop.arm(kCharacterB, BoltShop::boltcaster());
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto target = [&]() -> world::Entity& { return *shop.host.world.creatures().find(wolf); };
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    const auto step = [&](int steps) {
        for (int index = 0; index < steps; ++index) {
            shop.walker.idle(1);
            walkerB.idle(1);
        }
    };
    (void)shop.shot(target(), "TestBowLight");
    step(5);
    (void)shop.shot(target(), "TestBowLight", "", kCharacterB);
    step(5);
    (void)shop.shot(target(), "TestBowLight");
    step(5);
    const unsigned charges = shop.stacks(wolf, kCharge);
    // B hits the target with the 3 charges of A and B: B gets the bolt.
    (void)shop.shot(target(), "TestBowLight", "", kCharacterB);
    step(3);
    const std::size_t boltsA = single_numbers(shop.a, kLightningWire, fromA).size();
    const std::size_t boltsB = single_numbers(shop.b, kLightningWire, fromB).size();
    note("charges before the hit of B: " + std::to_string(charges) + "; bolts: A " + std::to_string(boltsA) + ", B " + std::to_string(boltsB));
    check(charges == 3 && boltsB == 1 && boltsA == 0, "the charges of each source count: the hit of B on 3 charges (2 of A, 1 of B) gives B the bolt");
    // The cooldown of the bolt is on the target: A in the 3 s gets no bolt with 3 new charges. The first new charge comes after the removal
    // window of the bolt (0.2 s), so that the target has 3 charges before the hit of A and the bolt rule reaches the cooldown of the target
    // (code review of stage B2, finding B2-R6: with only 0.2 s the window refused the first charge, and the case did not test the cooldown).
    step(6);
#ifndef SOULWARDEN_B2_BASELINE
    const std::uint64_t cooldowns = shop.host.world.creatures().counts().applicationCooldowns;
#else
    const std::uint64_t cooldowns = 0;
#endif
    for (int index = 0; index < 3; ++index) {
        (void)shop.shot(target(), "TestBowLight", "", index % 2 == 0 ? kCharacterA : kCharacterB);
        step(5);
    }
    const unsigned recharged = shop.stacks(wolf, kCharge);
    (void)shop.shot(target(), "TestBowLight");
    step(3);
#ifndef SOULWARDEN_B2_BASELINE
    const std::uint64_t refused = shop.host.world.creatures().counts().applicationCooldowns - cooldowns;
#else
    const std::uint64_t refused = 0;
#endif
    note("charges before the second bolt of the 3 s: " + std::to_string(recharged) + "; refusals of the cooldown of the target: " + std::to_string(refused));
    check(recharged == 3, "the target has 3 new charges before the hit of A (the removal window of the bolt ended)");
    check(refused == 1, "the bolt of A in the 3 s reaches the ApplicationCooldown of the target, which refuses it");
    check(single_numbers(shop.a, kLightningWire, fromA).empty(), "a bolt of A in the 3 s after the bolt of B on the same target is refused");
}

/** BC8: the conditions with an owner and with the stacks of the attacker, the end of a status of the attacker, the list of the rules. */
void rule_conditions(const Options&) {
    BoltShop shop;
    shop.enter_b();
    shop.arm(kCharacterA, world::PerkIds{kTestPerk, kBoltPerk, "", "", ""}, 100, {kSurprise});
    shop.arm(kCharacterB, world::PerkIds{kBoltPerk, "", "", "", ""});
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    const auto target = [&]() -> world::Entity& { return *shop.host.world.creatures().find(wolf); };
    const auto give = [&](const char* status, unsigned count) {
        world::StatusApply how;
        how.source = world::StatusSource{world::StatusSourceKind::command, "test", 0, 0};
        for (unsigned index = 0; index < count; ++index) {
            (void)shop.host.world.statuses().apply(*shop.character(), status, how);
        }
    };
    // CheckStatusEffectsOnTargetOwned: 2 charges whose source is B: the rule of A does not pass; 2 more of A: it passes (+0.5). The Surprise
    // Attack node gives +0.2 on the first hit only.
    (void)shop.shot(target(), "TestBowLight", "", kCharacterB);
    shop.pass(0.3F);
    (void)shop.shot(target(), "TestBowLight", "", kCharacterB);
    shop.pass(0.3F);
    const world::HitResult others = shop.shot(target(), "TestBowLight");
    shop.pass(0.3F);
    const world::HitResult own = shop.shot(target(), "TestBowLight");
    note("raw with the charges of B: " + std::to_string(others.damage.raw) + "; with the charges of A: " + std::to_string(own.damage.raw));
    check(near(others.damage.raw, 120.0F, 0.5F), "the charges of B do not pass the rule with CheckStatusEffectsOnTargetOwned for A (only +0.2 of the first hit)");
    check(near(own.damage.raw, 150.0F, 0.5F), "the charges with the source A (the last application) pass it: +0.5");
    // StatusEffectStackSize 2 GreaterThanOrEqual: one stack of the status of A: no part; two stacks: +0.3. A bow with the test perk only
    // (no charge of A now; the charges end in 3 s; the tracker of Surprise Attack stays while A hits).
    shop.arm(kCharacterA, world::PerkIds{kTestPerk, "", "", "", ""}, 100, {kSurprise});
    shop.pass(3.5F);
    give(kSelfStacks, 1);
    const world::HitResult oneStack = shop.shot(target(), "TestBowLight");
    give(kSelfStacks, 1);
    shop.pass(0.3F);
    const world::HitResult twoStacks = shop.shot(target(), "TestBowLight");
    note("raw with 1 and 2 stacks of the status of A: " + std::to_string(oneStack.damage.raw) + ", " + std::to_string(twoStacks.damage.raw));
    check(near(oneStack.damage.raw, 100.0F, 0.5F) && near(twoStacks.damage.raw, 130.0F, 0.5F),
          "StatusEffect with StatusEffectStackSize 2 GreaterThanOrEqual: the stacks of the status of the attacker (client 0x67A0130)");
    // The form of Momentum: StatusEffect, BaseDamage 0.25, RemoveStatusEffects AbilitySpecific with StatusEffectsList: one hit has the part,
    // and the status ends.
    shop.pass(0.3F);
    give(kMomentum, 1);
    const world::HitResult momentum = shop.shot(target(), "TestBowLight");
    shop.walker.idle(1);
    const bool ended = !shop.has_status(kMomentum);
    shop.pass(0.3F);
    const world::HitResult afterMomentum = shop.shot(target(), "TestBowLight");
    check(near(momentum.damage.raw - twoStacks.damage.raw, 25.0F, 0.5F) && ended && near(afterMomentum.damage.raw, twoStacks.damage.raw, 0.5F),
          "RemoveStatusEffects AbilitySpecific: the status of StatusEffectsList ends after the hit that it gave +0.25");
    // The list of the rules names the new rows as applied: the perk rows, the Surprise Attack node, its helper row.
    shop.arm(kCharacterA, BoltShop::boltcaster(), 100, {kSurprise});
    const std::vector<world::RuleView> views = shop.host.world.passives().describe(*shop.character());
    const auto applied = [&](const char* id) {
        return std::any_of(views.begin(), views.end(), [&](const world::RuleView& view) { return view.id == id && view.applied; });
    };
    for (const world::RuleView& view : views) {
        if (view.id.find("Artifact") != std::string::npos || view.id.find("DmgOnFirst") != std::string::npos) {
            note(view.id + (view.applied ? ": applied, " : ": not applied, ") + view.note);
        }
    }
    check(applied(kElectrifiedRule) && applied(kBoltRule) && applied(kChargeRule) && applied(kSurprise) && applied(kSurpriseHelper),
          "the list names Electrified, the bolt rule, the charge rule, Surprise Attack, and its helper row as applied");
}

/** BC9: the load. */
void stack_load(const Options&) {
    const auto run = [](unsigned farCount, std::uint64_t& looked, double& seconds) {
        BoltShop shop;
        shop.arm(kCharacterA, BoltShop::boltcaster());
        std::vector<world::EntityId> near;
        for (unsigned index = 0; index < 40; ++index) {
            near.push_back(shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 3.0F + static_cast<float>(index), kPlace.y), 90.0F).entity->id);
        }
        for (unsigned index = 0; index < farCount; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 600.0F + static_cast<float>(index % 100) * 3.0F,
                                                                   kPlace.y + 600.0F + static_cast<float>(index / 100) * 3.0F), 90.0F);
        }
        shop.walker.idle(3);
        const std::uint64_t start = shop.host.world.passives().counts().rulesLooked + shop.host.world.creatures().counts().effectsLooked;
        const auto clock = std::chrono::steady_clock::now();
        for (int round = 0; round < 300; ++round) {
            (void)shop.shot(*shop.host.world.creatures().find(near[static_cast<std::size_t>(round) % near.size()]), "TestBowLight");
            if (round % 10 == 0) {
                shop.walker.idle(1);
            }
        }
        shop.walker.idle(5);
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
        looked = shop.host.world.passives().counts().rulesLooked + shop.host.world.creatures().counts().effectsLooked - start;
        return shop.stacks_added();
    };
    std::uint64_t smallLooked = 0;
    std::uint64_t largeLooked = 0;
    double smallSeconds = 0.0;
    double largeSeconds = 0.0;
    const std::uint64_t smallStacks = run(40, smallLooked, smallSeconds);
    const std::uint64_t largeStacks = run(4000, largeLooked, largeSeconds);
    note("300 hits with charges: 40 far creatures: " + std::to_string(smallLooked) + " looks, " + std::to_string(smallSeconds) + " s; 4,000 far: "
         + std::to_string(largeLooked) + " looks, " + std::to_string(largeSeconds) + " s; new stacks " + std::to_string(largeStacks));
    check(largeStacks > 0 && largeStacks == smallStacks, "the hits add the same stacks in the two worlds");
    check(largeLooked == smallLooked, "the rules and the effects that the hits look at do not depend on the far creatures");
    check(largeSeconds < 2.0, "the time limit: 300 hits with their rules and stacks in less than 2 s in a world of 4,040 creatures");
}

/** BC10: a perk of the instance in a place replaces the fixed perk of the definition in that place (the charm of the Boltcaster). */
void charm_place(const Options&) {
    const auto run = [](bool charm) {
        BoltShop shop;
        // The structure of the definition of Artifact_Set1_2hBow: the fixed perks of places 1 to 4 (place 4: a made-up slottable perk of the
        // basic attacks, BaseDamage 0.1, as Slottable_Common_DmgBasic) and an empty place 5.
        gamedata::PerkTables tables = boltcaster_perks();
        tables.perks.push_back(perk("PerkID_Test_Slottable_DmgBasic", "OffensiveCharm", "OnEquip", "Bow", "", {}, {"Test_Slottable_DmgBasic"}));
        tables.perks.push_back(perk("PerkID_Test_Slottable_Charm", "OffensiveCharm", "OnEquip", "Bow", "", {}, {}));
        tables.abilities["test_slottable_dmgbasic"] = cells({{"OnHit", 1.0F}, {"BaseDamage", 0.1F}}, {{"AttackType", "Light,Heavy"}});
        gamedata::PerkLayout layout;
        layout.fixed = {kElectrified, kBoltPerk, kShirking, "PerkID_Test_Slottable_DmgBasic", ""};
        tables.layouts.push_back(layout);
        tables.items["testbow"] = tables.layouts.size() - 1;
        if (!shop.host.perks.set(tables)) {
            throw std::runtime_error("boltcaster test: the perk data with the layout: " + shop.host.perks.error());
        }
        shop.arm(kCharacterA, charm ? world::PerkIds{kElectrified, kBoltPerk, kShirking, "PerkID_Test_Slottable_Charm", ""}
                                    : world::PerkIds{kElectrified, kBoltPerk, kShirking, "", ""});
        const world::EntityId wolf = shop.wolf_at(6.0F)->id;
        return shop.shot(*shop.host.world.creatures().find(wolf), "TestBowLight").damage.raw;
    };
    const float slotted = run(true);
    const float fixed = run(false);
    note("raw with a charm in place 4: " + std::to_string(slotted) + "; with an empty place 4: " + std::to_string(fixed));
    check(near(fixed, 130.0F, 0.5F), "control: an empty place 4 keeps the fixed perk of the definition (+0.1), with Electrified (+0.2)");
    check(near(slotted, 120.0F, 0.5F), "a charm of the instance in place 4 replaces the fixed perk of that place: only Electrified (+0.2)");
}

/** BC11: the projectile data of format 3 (code review of stage U3, findings U3-R7 and U3-R8): the switch marks and the dodge marks. */
void projectile_marks(const Options& options) {
    using nlohmann::json;
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "b2-projectile-data.json";
    // The form of the tool (format 3) with made-up profiles and rows; the marks of Rapid Shot of player_bow.adb.
    json document{{"format", 3},
                  {"counts", {{"attacks", 1}, {"slices", 1}, {"ammo", 1}, {"weapons", 1}, {"shots", 2}, {"abilityActions", 1}}},
                  {"attacks", {{"P", "TripleShot", false, 100.0, 0.275, 2.0, 0.2, 20.0, 30.0, 80.0, 1.0, 0, true, true, {0.0, 0.0, -9.8}, 0}}},
                  {"slices", {{"slices/test/arrow.dynamicslice", 100.0, 25.0, 1.0, {0.0, 0.0, -9.8}, 10.0, 8.0}}},
                  {"ammo", {{"DefaultArrow", "Arrow", 1.2, 0}}},
                  {"weapons", {{"W", "P", "Arrow"}}},
                  {"shots", {{"P", "TripleShot", "Row_A", 0.0, -1, "", "", false, 12, true, true},
                             {"P", "TripleShot", "Row_B", 0.0, -1, "", "", false, 12, false, true}}},
                  {"abilityActions", {{"Test_Rapid", "Test_Ability", "Test_Fragment", "Bow", 2.6,
                                       {{0.3333, "TripleShot", "Row_A", 0.0, true}, {1.7333, "TripleShot", "Row_B", 0.0, false}},
                                       json::array(), json::array(),
                                       {1.868143, 2.566667, {{0.2, "buffer"}, {0.566667, "cancel"}, {0.866667, "buffer"}}}}}}};
    const auto load = [&path](const json& content, gamedata::ProjectileData& out) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return out.load(path.string());
    };
    gamedata::ProjectileData file;
    const bool loaded = load(document, file);
#ifndef SOULWARDEN_B2_BASELINE
    const gamedata::AbilityActionData* rapid = loaded ? file.action("Test_Rapid") : nullptr;
    check(rapid != nullptr && near(rapid->switchBuffer, 1.868143F, 0.0001F) && near(rapid->switchAt, 2.566667F, 0.0001F) && rapid->dodgeMarks.size() == 3
              && rapid->dodgeMarks[0].buffer && !rapid->dodgeMarks[1].buffer && near(rapid->dodgeMarks[1].time, 0.566667F, 0.0001F),
          "a file of format 3 loads with the switch marks and the dodge marks of each action");
#else
    check(loaded, "a file of format 3 loads with the switch marks and the dodge marks of each action");
#endif
    json noMarks = document;
    noMarks["abilityActions"][0].erase(8);
    json badMark = document;
    badMark["abilityActions"][0][8][0] = -0.5;
    json unsorted = document;
    unsorted["abilityActions"][0][8][2] = {{0.566667, "cancel"}, {0.2, "buffer"}};
    json formatTwo = noMarks;
    formatTwo["format"] = 2;
    gamedata::ProjectileData other;
    check(!load(noMarks, other) && !load(badMark, other) && !load(unsorted, other),
          "format 3 with no marks, a mark below 0 that is not -1, and dodge marks out of the time order are refused");
    gamedata::ProjectileData older;
    check(load(formatTwo, older) && older.action("Test_Rapid") != nullptr,
          "a file of format 2 (no marks) loads: the backend of stage B2 starts with the file of the common folder");
}

} // namespace

std::vector<Case> boltcaster_cases() {
    return {
        {"BC1", "stage B2: the stacks of a status on a creature (StackMax, RefreshDuration, the end, the count on the wire, DontReplaceStack)", creature_stacks},
        {"BC2", "stage B2: the one status path of a creature (ApplicationCooldown on the target, the removal window, a row with no duration)",
         one_status_path},
        {"BC3", "stage B2: the Boltcaster (Electrified, the charges, the bolt, its cooldown on the target, an ability hit, the row list)", boltcaster_rules},
        {"BC4", "stage B2: Shirking Shock II (the tracker after a dodge, the lightning damage of the next hit, the end of the tracker)", shirking_shock},
        {"BC5", "stage B2: Surprise Attack (+0.2 on the first hit, the tracker and its helper, a second character: decision D10)", surprise_attack},
        {"BC6", "stage B2: two sessions (MP-10): the charge icon with its count and the health for the observer, the numbers for the owner", two_sessions},
        {"BC7", "stage B2: two attackers on one creature (decision D10): the charges of each source, the cooldown of the bolt on the target", two_attackers},
        {"BC8", "stage B2: the conditions with an owner and with the stacks of the attacker, AbilitySpecific, the list of the rules", rule_conditions},
        {"BC9", "stage B2: the load of the hits with charges (40 near and 4,000 far creatures)", stack_load},
        {"BC10", "stage B2: a perk of the instance in a place replaces the fixed perk of the definition in that place (a charm)", charm_place},
        {"BC11", "stage B2: the projectile data of format 3 (the switch marks and the dodge marks of the bow actions)", projectile_marks},
    };
}

} // namespace soulwarden::backend::tests

// The corrections of the code review of stage B1 (~/Games/soulwarden/work/evidence/bow-build/b1/code-review/FINDINGS.md, findings B1-R1 to
// B1-R9 but B1-R4). Each case fails on the tree of stage B1 (5633c84) and passes with the correction. All ids, names, and values are made up:
// no file of the game and no value of a capture is here (the data of support/bow_fixture.h and support/motion_fixture.h).
//
//   BRV1  B1-R1  the shots of a volley end with its action: a weapon replacement, an item 30 s late, a travel; two sessions (MP-10): the
//                volley of A ends, the volley of B goes on
//   BRV2  B1-R2  the first item of Rapid Shot is lost: the second item and the third item (KnockBack, Final Blow) fly; two hits give no
//                Rapid Accuracy
//   BRV3  B1-R3  the death ends the aim of Penetrating Shot (no confirm after a revival) and the shots of a volley that did not fly
//   BRV5  B1-R5  a preset that replaces the rapier with a different instance of the same item: the window of Riposte ends, and a counter
//                that started does not hit with the new instance
//   BRV6  B1-R6  a default arrow with no slice: the press of Rapid Shot is refused before its cooldown; Explosive Arrow (the slice of its
//                spell) still casts
//   BRV7  B1-R7  the explosion takes the radius of the offense of its slice (0.25 m in the data of the case), not the radius of the
//                spell row (2.5 m)
//   BRV9  B1-R9  a claimed origin of a bow shot behind a low wall: the arrow starts at the launch place of the server, and the wall
//                stops it
//
// The finding B1-R8 (the projectile data tool) is the tool cases missing-bow-cast-spell and missing-bow-clip-row of
// tools/gamedata/check_projectile_tool.py (case PJ11 runs it).

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "support/bow_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/motion_fixture.h"
#include "world/build_presets.h"
#include "world/navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace bow;

/** A bite of the wolf of the cases that kills a character (the factor of its damage). */
gamedata::CreatureContact bite(float factor) {
    gamedata::CreatureContact contact;
    contact.row = "Test_Bite";
    contact.type = "Slash";
    contact.wire = 4;
    contact.reach = 2.0F;
    contact.factor = factor;
    return contact;
}

/** BRV1 (B1-R1): the shots of a volley end with its action. */
void volley_ends(const Options&) {
    {
        // A weapon replacement after the first arrow: the second item of the volley does not shoot a bow arrow with a staff in the hand.
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        const float before = shop.lost(wolf);
        shop.hold("TestStaff");
        shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        note("weapon replacement: shots " + std::to_string(shop.volley_shots()) + ", damage after it " + std::to_string(shop.lost(wolf) - before));
        check(before > 0.0F && shop.volley_shots() == 1 && shop.lost(wolf) == before,
              "a weapon replacement ends the volley: the second item of Rapid Shot shoots no arrow with the staff in the hand");
    }
    {
        // An item 30 s after the cast: the action ended long before; the unused second arrow does not fly.
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.pass(30.0F);
        const float before = shop.lost(wolf);
        shop.send_item(BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        note("late item: shots " + std::to_string(shop.volley_shots()) + ", damage 30 s late " + std::to_string(shop.lost(wolf) - before));
        check(before > 0.0F && shop.volley_shots() == 1 && shop.lost(wolf) == before,
              "an item 30 s after the cast is not a shot: the action of the cast ended (its latest time)");
    }
    {
        // A travel with the loading screen: the volley ends, and the item in the lock of the session shoots nothing.
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        const float before = shop.lost(wolf);
        shop.world().travel(kCharacterA, kPlace, world::TravelScreen::on);
        shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        note("travel: shots " + std::to_string(shop.volley_shots()) + ", damage in the loading screen " + std::to_string(shop.lost(wolf) - before));
        check(before > 0.0F && shop.volley_shots() == 1 && shop.lost(wolf) == before,
              "a travel ends the volley: an item in the loading screen shoots nothing");
    }
    {
        // Two sessions (rule MP-10): A and B cast Rapid Shot. A takes a staff after its first arrow: the volley of A ends, the volley of B
        // goes on (the end is a value of the cast of A, not of the world).
        BowShop shop(true);
        shop.enter_b();
        shop.learn(*shop.character());
        shop.learn(*shop.world().find_character(kCharacterB));
        Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
        walkerB.input = 400;
        const world::EntityId wolfA = shop.wolf_at(10.0F)->id;
        const world::EntityId wolfB = shop.wolf_at(9.0F, 7.0F)->id;
        walkerB.idle(5);
        shop.press(2);
        walkerB.press(2);
        const auto pressB = static_cast<std::uint16_t>(walkerB.input - 2);
        const auto itemB = [&](const char* row) {
            InputShotItem shot = BowShop::bow_item("TripleShot", row, {1.0F, 0.6F, 0.0F});
            shot.origin = {kPlace.x - 0.5F, kPlace.y + 1.0F, kHeight + 0.6F};
            InputEntry entry = walkerB.entry();
            entry.shotItems = {shot};
            walkerB.send(entry);
        };
        const std::array<const char*, 3> rows{"Bow_RapidAttack_Standard", "Bow_RapidAttack_Standard", "Bow_RapidAttack_KnockBack"};
        const std::array<int, 3> times{9, 25, 52};
        float firstA = 0.0F;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            while (static_cast<int>(static_cast<std::uint16_t>(walkerB.input + 1 - pressB)) < times[index]) {
                walkerB.idle(1);
                shop.walker.idle(1);
            }
            itemB(rows[index]);
            if (index == 1) {
                firstA = shop.lost(wolfA);
                shop.hold("TestStaff");
            }
            shop.item_at(times[index], BowShop::bow_item("TripleShot", rows[index]));
        }
        shop.walker.idle(10);
        walkerB.idle(10);
        note("two sessions: shots " + std::to_string(shop.volley_shots()) + ", wolf of A " + std::to_string(shop.lost(wolfA)) + ", wolf of B "
             + std::to_string(shop.lost(wolfB)));
        check(firstA > 0.0F && shop.lost(wolfA) == firstA && shop.lost(wolfB) > 0.0F && shop.volley_shots() == 4,
              "two sessions: the weapon change of A ends only the volley of A (one arrow); the three arrows of B fly");
    }
}

/** BRV2 (B1-R2): a lost first item of Rapid Shot. */
void lost_first_item(const Options&) {
    BowShop shop;
    shop.learn(*shop.character(), {kFinalBlow, kRapidAccuracy});
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    shop.press(2);
    // The item of the first clip did not come. The second and the third come at their times, in new input steps.
    shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(6);
    const float second = shop.lost(wolf);
    shop.item_at(52, BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
    shop.walker.idle(6);
    const float third = shop.lost(wolf) - second;
    note("lost first item: shots " + std::to_string(shop.volley_shots()) + ", second arrow " + std::to_string(second) + ", third arrow "
         + std::to_string(third));
    check(shop.volley_shots() == 2 && second > 0.0F && third > second * 1.4F,
          "a lost first item does not block a later clip: the second arrow and the third arrow (KnockBack with Final Blow) fly");
    check(shop.host.world.passives().counts().cooldownReductions == 0 && shop.remaining(kRapid) > kRapidCooldown - 3.0F,
          "two hits of the volley give no Rapid Accuracy (three hits of one cast are necessary)");
}

/** BRV3 (B1-R3): the death ends the aim and the volley. */
void death_ends_aim(const Options&) {
    {
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        shop.press(0);
        check(shop.shot_confirms() == 0 && shop.cooldown(kPenetrating) == nullptr, "setup: only the aim of Penetrating Shot is open");
        world::Entity* attacker = shop.host.world.creatures().find(wolf);
        shop.world().health().creature_hit(*attacker, bite(100000.0F), *shop.character());
        check(shop.character()->life.dead, "setup: the bite kills the character");
        check(shop.world().health().heal(kCharacterA).revived, "setup: the heal command revives the character at its place (no travel)");
        shop.send_item(BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack"));
        shop.walker.idle(8);
        note("aim after the death: confirms " + std::to_string(shop.shot_confirms()) + ", damage " + std::to_string(shop.lost(wolf)));
        check(shop.shot_confirms() == 0 && shop.cooldown(kPenetrating) == nullptr && shop.lost(wolf) == 0.0F,
              "the death ends the aim: a later shot item confirms nothing (no cooldown, no damage)");
    }
    {
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        const float before = shop.lost(wolf);
        world::Entity* attacker = shop.host.world.creatures().find(wolf);
        shop.world().health().creature_hit(*attacker, bite(100000.0F), *shop.character());
        check(shop.character()->life.dead && shop.world().health().heal(kCharacterA).revived, "setup: the character dies and revives");
        shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(6);
        note("volley after the death: shots " + std::to_string(shop.volley_shots()) + ", damage " + std::to_string(shop.lost(wolf) - before));
        check(before > 0.0F && shop.volley_shots() == 1 && shop.lost(wolf) == before,
              "the death ends the volley: the shots that did not fly do not fly after the revival");
    }
}

/** A preset of the case: one TestRapier with a gear score in the first weapon slot (a different instance of the same item). */
void set_rapier_preset(motion::MotionShop& shop) {
    world::BuildPreset preset;
    preset.name = "test-new-rapier";
    preset.title = "A new rapier";
    preset.level = 1;
    preset.attributes.fill(5);
    world::PresetItem item;
    item.itemId = "TestRapier";
    item.gearScore = 200;
    item.slot = world::paperdoll::kMainHand1;
    preset.items = {item};
    preset.variants = {{"default", "Default", {}}};
    shop.world().presets().set({preset});
}

/** BRV5 (B1-R5): a different instance of the same rapier in the Riposte window and in the counter. */
/**
 * BRV4 (B1-R4): two casters refresh the same burn of Flame Arrow on one creature. The refresh gives the effect the source of the second
 * caster, and with it the damage snapshot of that caster (its forced row with its own weapon), not the damage of the first one. The tick
 * cadence stays. Two sessions (MP-10). Made-up data only.
 */
void refreshed_burn(const Options&) {
    BowShop shop(true);
    shop.enter_b();
    shop.learn(*shop.character(), {kFlameArrow});
    world::Entity* other = shop.world().find_character(kCharacterB);
    shop.learn(*other, {kFlameArrow});
    // A different damage of the second caster: a higher gear score of its bow, with the normal damage of the forced row.
    other->equipment.slots.at(world::paperdoll::kMainHand1).item->gearScore = 600;
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    InputShotItem shot = BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr);
    shop.press(1);
    shop.item_at(13, shot);
    shop.walker.idle(8);
    world::Entity* target = shop.world().creatures().find(wolf);
    const auto effect = [&]() -> const world::CreatureEffect* {
        for (const world::CreatureEffect& entry : target->creature.effects) {
            if (entry.status == kBurn) {
                return &entry;
            }
        }
        return nullptr;
    };
    check(effect() != nullptr, "setup: the first caster applied the burn");
    const float first = effect() != nullptr ? effect()->tickDamage : 0.0F;
    // A second cast of B on the same wolf, from the place of B.
    Walker walkerB{shop.host, shop.b, other->position};
    walkerB.input = 500;
    walkerB.press(1);
    for (int step = 0; step < 14; ++step) {
        walkerB.idle(1);
    }
    InputShotItem second = shot;
    second.origin = {other->position.x + 0.5F, other->position.y, other->position.z + 0.6F};
    const float dx = target->position.x - second.origin[0];
    const float dy = target->position.y - second.origin[1];
    const float size = std::hypot(dx, dy);
    second.direction = {dx / size, dy / size, 0.0F};
    InputEntry entry = walkerB.entry();
    entry.shotItems = {second};
    walkerB.send(entry);
    walkerB.idle(8);
    const world::ItemInstance* held = world::Combat::weapon_in_hand(*other);
    const auto forcedIndex = shop.host.combat.row_index("Common_DamageOverTime_Override");
    const gamedata::DamageRow* forced = forcedIndex ? &shop.host.combat.damage_row(*forcedIndex) : nullptr;
    const float expected = held != nullptr && forced != nullptr
                               ? 0.08F * shop.world().combat().damage(*other, *shop.host.combat.weapon_of(held->itemId), held->gearScore, *forced,
                                                                     nullptr).raw
                               : 0.0F;
    note("BRV4 first tick " + std::to_string(first) + ", refreshed tick " + std::to_string(effect() != nullptr ? effect()->tickDamage : 0.0F)
         + ", the tick of the second source " + std::to_string(expected));
    check(effect() != nullptr && effect()->source == other->id, "setup: the second caster owns the refreshed burn");
    check(effect() != nullptr && expected > first && near(effect()->tickDamage, expected, 0.01F),
          "a burn that changes its source takes the damage of the forced row of the new source");
}

void same_rapier(const Options&) {
    {
        motion::MotionShop shop;
        check(shop.host.perks.set(shop::shop_perks()), "setup: the perk data");
        shop.hold("TestRapier");
        const std::string old = world::Combat::weapon_key(*shop.character());
        set_rapier_preset(shop);
        world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
        world::AbilityPress press;
        press.place = 0;
        check(shop.world().abilities().press(shop.character()->owner, press) == world::CastVerdict::accepted, "setup: Riposte is accepted");
        const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-new-rapier", "");
        const world::ItemInstance* held = world::Combat::weapon_in_hand(*shop.character());
        check(result.ok() && held != nullptr && held->itemId == "TestRapier" && world::Combat::weapon_key(*shop.character()) != old,
              "setup: the preset puts a different instance of TestRapier into the slot");
        const bool window = shop.world().statuses().has(*shop.character(), "Rapier_Riposte_Timer");
        const float before = shop.character()->vitals.health;
        shop.world().health().creature_hit(*wolf, bite(1.0F), *shop.character());
        note("window after the replacement " + std::to_string(window) + ", bite damage " + std::to_string(before - shop.character()->vitals.health));
        check(!window && shop.character()->vitals.health < before,
              "a different instance of the same rapier ends the window of the old instance: the next bite does damage");
    }
    {
        motion::MotionShop shop;
        check(shop.host.perks.set(shop::shop_perks()), "setup: the perk data");
        shop.hold("TestRapier");
        set_rapier_preset(shop);
        world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
        const world::EntityId wolfId = wolf->id;
        world::AbilityPress press;
        press.place = 0;
        check(shop.world().abilities().press(shop.character()->owner, press) == world::CastVerdict::accepted, "setup: Riposte is accepted");
        shop.world().health().creature_hit(*wolf, bite(1.0F), *shop.character());
        check(shop.world().abilities().counts().counters == 1, "setup: the window counters the bite (the counter action starts)");
        const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-new-rapier", "");
        check(result.ok(), "setup: the preset replaces the rapier before the counter hit");
        shop.walker.idle(25);
        const world::Entity* hurt = shop.host.world.creatures().find(wolfId);
        note("counter hits " + std::to_string(shop.world().abilities().counts().counterHits));
        check(shop.world().abilities().counts().counterHits == 0 && hurt != nullptr && hurt->vitals.health == shop::kWolfHealth,
              "the counter of the old instance does not hit with the new instance of the same rapier");
    }
}

/** BRV6 (B1-R6): a default arrow with no slice. */
void no_arrow_slice(const Options&) {
    BowShop shop;
    gamedata::ProjectileTables data = bow_projectiles();
    data.ammo.front().slice = -1;
    check(shop.host.projectiles.set(data), "setup: the projectile data with a DefaultArrow of no slice loads (the start with this data stays usable)");
    shop.learn(*shop.character());
    shop.press(2);
    shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(4);
    note("Rapid Shot with no arrow slice: cooldown " + std::to_string(shop.cooldown(kRapid) != nullptr) + ", shots "
         + std::to_string(shop.volley_shots()));
    check(shop.presses(world::CastVerdict::noRule) == 1 && shop.cooldown(kRapid) == nullptr && shop.volley_shots() == 0,
          "the clips of Rapid Shot have no launchable slice: the press is refused (noRule) before the cooldown");
    shop.press(1);
    shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
    shop.walker.idle(4);
    check(shop.cooldown(kExplosive) != nullptr && shop.volley_shots() == 1,
          "Explosive Arrow (the slice of its projectile spell) casts and shoots with the same data");
}

/** BRV7 (B1-R7): the radius of the explosion is the radius of the offense of its slice. */
void explosion_radius(const Options&) {
    const auto run = [](float radius) {
        BowShop shop;
        gamedata::SpellTables slices = bow_spells();
        slices.slices.front().offenses.front().radius = radius;
        const bool loaded = shop.host.spells.set(slices);
        shop.learn(*shop.character());
        const world::EntityId direct = shop.wolf_at(10.0F)->id;
        const world::EntityId side = shop.wolf_at(8.5F, 2.0F)->id;
        shop.press(1);
        shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
        shop.walker.idle(8);
        struct Result {
            bool loaded{false};
            float direct{0.0F};
            float side{0.0F};
            std::uint64_t explosions{0};
        };
        return Result{loaded, shop.lost(direct), shop.lost(side), shop.explosions()};
    };
    const auto normal = run(2.5F);
    check(normal.loaded && normal.explosions == 1 && normal.side > 0.0F,
          "control: the offense radius of the slice and the spell row agree (2.5 m): the creature at the side takes the explosion");
    const auto small = run(0.25F);
    note("slice radius 0.25 m: side damage " + std::to_string(small.side) + ", direct damage " + std::to_string(small.direct) + ", explosions "
         + std::to_string(small.explosions));
    check(small.loaded && small.explosions == 1 && small.direct > 0.0F && small.side == 0.0F,
          "the offense radius of the slice is 0.25 m (the spell row 2.5 m): the creature at the side is outside the explosion");
}

/** BRV9 (B1-R9): a claimed origin of a bow shot behind a low wall. */
void origin_behind_wall(const Options& options) {
    BowShop shop;
    // A wall 2 m high from 2 m to 2.25 m east of A for the whole tile.
    TestNavRegion region;
    region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
        const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
        if (px >= kPlace.x + 2.0F && px < kPlace.x + 2.25F) {
            floors = {{kHeight + 2.0F, gamedata::NavFloor::kBlocked}};
        }
    };
    const std::filesystem::path folder = options.workDirectory / "brv9-low-wall";
    std::filesystem::remove_all(folder);
    region.write(folder);
    check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation data of the low wall loads");
    shop.walker.idle(10);
    shop.learn(*shop.character());
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    InputShotItem shot = BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack");
    shot.origin[0] = kPlace.x + 3.0F;
    state::WorldPosition launch = kPlace;
    launch.z += 1.5F;
    const state::WorldPosition origin{shot.origin[0], shot.origin[1], shot.origin[2]};
    check(shop.world().navigation().line_stop(launch, origin).has_value(),
          "setup: the wall is on the real segment from the launch place of the server to the claimed origin");
    shop.press(0);
    shop.send_item(shot);
    shop.walker.idle(10);
    note("origin behind the wall: shots " + std::to_string(shop.volley_shots()) + ", damage behind the wall " + std::to_string(shop.lost(wolf)));
    check(shop.volley_shots() == 1 && shop.lost(wolf) == 0.0F,
          "the arrow starts at the launch place of the server, not at the claimed origin behind the wall: the wall stops it");
}

} // namespace

std::vector<Case> review_b1_cases() {
    return {{"BRV1", "B1-R1: the shots of a volley end with a weapon change, a late item, a travel; two sessions", volley_ends},
            {"BRV2", "B1-R2: a lost first item of Rapid Shot does not block the later arrows", lost_first_item},
            {"BRV3", "B1-R3: the death ends the aim of Penetrating Shot and the shots of a volley", death_ends_aim},
            {"BRV4", "B1-R4: a refreshed burn takes the damage of its new source with its credit", refreshed_burn},
            {"BRV5", "B1-R5: a different instance of the same rapier ends the Riposte window and its counter", same_rapier},
            {"BRV6", "B1-R6: a bow clip with no launchable slice is refused before the cooldown", no_arrow_slice},
            {"BRV7", "B1-R7: the explosion takes the radius of the offense of its slice", explosion_radius},
            {"BRV9", "B1-R9: a claimed bow origin behind a low wall does not pass the wall", origin_behind_wall}};
}

} // namespace soulwarden::backend::tests

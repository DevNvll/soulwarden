// The cases of the corrections of the code review of stage B2 (findings B2-R1 to B2-R5; B2-R6 is the correction of case BC7 in
// tests/boltcaster_tests.cpp). The data is the made-up data of the bow and Boltcaster cases (support/boltcaster_fixture.h) with the
// structure of the game rows; no id of an account.
//
// Each case fails on the tree of stage B2 (598cc02) and passes after the correction.

#include <chrono>
#include <string>
#include <vector>

#include "support/attack_fixture.h"
#include "support/boltcaster_fixture.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {

using namespace boltcaster;
using namespace std::chrono_literals;

/** The cancel marks and the switch marks of Rapid Shot (player_bow.adb, the projectile data of format 3). */
void rapid_marks(BowShop& shop) {
    gamedata::ProjectileTables data = bow_projectiles();
    for (gamedata::AbilityActionData& action : data.actions) {
        if (action.action == "Bow_RapidShot") {
            action.switchBuffer = 1.868143F;
            action.switchAt = 2.566667F;
            action.dodgeMarks = {{0.2F, true}, {0.566667F, false}, {0.866667F, true}, {1.1F, false}, {1.733333F, true}, {2.333333F, false}};
        }
    }
    check(shop.host.projectiles.set(data), "setup: Rapid Shot has the cancel marks of the data");
}

/** BV1 (B2-R1): a Rapid Shot that a dodge cancelled does not refuse the next ability after the cancel. */
void cancelled_cast_frees_next(const Options&) {
    BowShop shop;
    shop.learn(*shop.character());
    rapid_marks(shop);
    stamina::set_stamina(shop.host);
    shop.press(2);
    shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.until(19);
    const auto accepted = [&]() { return shop.world().stamina().counts().dodges[static_cast<std::size_t>(world::DodgeVerdict::accepted)]; };
    const std::uint64_t dodges = accepted();
    shop.walker.bits({attack_fixture::kActionDodge});
    check(accepted() == dodges + 1, "the dodge 0.63 s after the cast is accepted (the first cancel window of Rapid Shot)");
    shop.until(49);
    check(!shop.world().abilities().playing_action(kCharacterA), "the action of Rapid Shot ended at the cancel");
    const std::uint64_t casts = shop.presses(world::CastVerdict::accepted);
    const std::uint64_t casting = shop.presses(world::CastVerdict::casting);
    shop.press(1);
    note("Explosive Arrow 1.6 s after the cast: accepted " + std::to_string(shop.presses(world::CastVerdict::accepted) - casts) + ", refused (casting) "
         + std::to_string(shop.presses(world::CastVerdict::casting) - casting));
    check(shop.presses(world::CastVerdict::accepted) == casts + 1, "a cancelled Rapid Shot does not refuse Explosive Arrow after the cancel");
}

/** BV2 (B2-R2): a status of a creature whose end came, before the sweep of the due list, starts again with one stack. */
void ended_effect_starts_again(const Options&) {
    BoltShop shop;
    shop.arm(kCharacterA, BoltShop::boltcaster());
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    (void)shop.shot(*shop.world().creatures().find(wolf), "TestBowLight");
    (void)shop.shot(*shop.world().creatures().find(wolf), "TestBowLight");
    check(shop.stacks(wolf, kCharge) == 2, "setup: two charges on the wolf");
    // A world task after the end of the charges (3 s) and before the sweep of the due list of the creatures.
    shop.world().after(3100ms, [&shop, wolf](world::World& world) { (void)shop.shot(*world.creatures().find(wolf), "TestBowLight"); });
    shop.host.clock.advance(3100ms);
    shop.world().tick();
    const unsigned stacks = shop.stacks(wolf, kCharge);
    (void)shop.shot(*shop.world().creatures().find(wolf), "TestBowLight");
    note("charges after a hit after the end: " + std::to_string(stacks) + "; bolts after one more hit: " + std::to_string(shop.instant_damage()));
    check(stacks == 1, "a hit after the end of the charges starts one new charge (no old stack)");
    check(shop.instant_damage() == 0, "two hits after the end of the old charges give no bolt");

    // The same with a shot item of the client near the muzzle (a hit at the input step, before the sweep of the world tick).
    BoltShop packet;
    packet.arm(kCharacterA, BoltShop::boltcaster());
    world::Entity* near = packet.wolf_at(1.8F);
    const world::EntityId nearId = near->id;
    const gamedata::StatusRow* charge = packet.host.abilities.status(kCharge);
    (void)packet.world().creatures().give_status(*near, *charge, packet.character()->id);
    (void)packet.world().creatures().give_status(*near, *charge, packet.character()->id);
    InputShotItem item = BowShop::bow_item("Basic", "TestBowLight");
    item.receiverActor = near->actor.bytes;
    item.receiver = nearId;
    item.facet = near->creature.body->target(nearId, near->creature.body->components[1]);
    item.point = {near->position.x - 0.6F, near->position.y, kHeight + 0.6F};
    packet.pass(2.96F);
    packet.host.clock.advance(2ms);
    packet.send_item(item, {kActionAttack, kActionAttackRelease});
    check(packet.lost(nearId) > 0.0F, "setup: the item of the client hits before the sweep");
    check(packet.stacks(nearId, kCharge) == 1, "a shot item after the end of the charges starts one new charge");
}

/** BV3 (B2-R3): Mark keeps its bonus on a creature with the rend of a perk (an effect with no row in the snapshot of the hit). */
void mark_against_perk_rend(const Options&) {
    constexpr const char* kMark = "Passive_Bow_Skirmish_DmgVsDebuff";
    constexpr const char* kRend = "Status_Perk_Hammer_Shockwave_Rend";
    BoltShop shop;
    shop.enter_b();
    gamedata::AbilityTables tables = bow_abilities();
    tables.abilityRows.push_back({kMark, cells({{"OnHit", 1.0F}, {"BaseDamage", 0.1F}}, {{"WeaponTag", "Bow"}, {"TargetStatusEffectCategory", "Debuff"}})});
    check(shop.host.abilities.set(tables), "setup: Mark has the condition and the bonus of the data");
    gamedata::ProgressionData progression = bow_progression();
    gamedata::AbilityNode node;
    node.id = kMark;
    node.table = gamedata::progression_id(kBowTable);
    node.column = 20;
    progression.nodes.push_back(node);
    shop.host.progression.set(progression);
    shop.arm(kCharacterA, {}, 100, {kMark});
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    world::Entity& target = *shop.world().creatures().find(wolf);
    world::HitContext applying;
    applying.raw = 100.0F;
    shop.world().gear().perk_status(*shop.host.world.find_character(kCharacterB), target, kRend, 1.0F, applying);
    check(target.creature.effects.size() == 1 && target.creature.effects.front().status == kRend, "setup: B gives the rend through the perk path");
    const float raw = shop.shot(target, "TestBowLight").damage.raw;
    note("raw damage of the arrow of A: " + std::to_string(raw));
    check(near(raw, 110.0F, 0.1F), "Mark gives its +0.1 on an arrow against a creature with the rend of a perk (category Debuff)");
}

/** BV4 (B2-R4): the rend of a perk does not come in a removal window of the target. */
void perk_rend_in_removal_window(const Options&) {
    constexpr const char* kRend = "Status_Perk_Hammer_Shockwave_Rend";
    constexpr const char* kWindow = "Test_BV4_Removal";
    constexpr const char* kPerk = "PerkID_Test_BV4_Rend";
    BoltShop shop;
    gamedata::AbilityTables tables = bow_abilities();
    tables.statusEffects.push_back(status(kWindow, 2.0F, cells({{"StackMax", 1.0F}}, {{"RemoveStatusEffects", kRend}})));
    tables.statusEffects = numbered(tables.statusEffects);
    check(shop.host.abilities.set(tables), "setup: a removal window of 2 s names the rend");
    gamedata::PerkTables perks = boltcaster_perks();
    perks.perks.push_back(perk(kPerk, "Generated", "OnEquip", "Bow", "", {}, {"Test_BV4_Rend"}));
    perks.abilities["test_bv4_rend"] = cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}}, {{"OtherApplyStatusEffect", kRend}});
    check(shop.host.perks.set(perks), "setup: a perk of a hit gives the rend");
    shop.arm(kCharacterA, {kPerk, "", "", "", ""});
    const world::EntityId wolf = shop.wolf_at(6.0F)->id;
    world::Entity& target = *shop.world().creatures().find(wolf);
    check(shop.world().creatures().give_status(target, *shop.host.abilities.status(kWindow), shop.character()->id), "setup: the removal window runs");
    check(!shop.world().creatures().give_status(target, *shop.host.abilities.status(kRend), shop.character()->id),
          "control: the one status path refuses the rend in the window");
    (void)shop.shot(target, "TestBowLight");
    note("stacks of the rend after the hit: " + std::to_string(shop.stacks(wolf, kRend)));
    check(shop.stacks(wolf, kRend) == 0, "the hit of the perk does not give the rend in the removal window");
}

/** BV5 (B2-R5): a shot item of a press that a weapon switch holds does not pass the weight gate of the bow. */
void held_press_weight(const Options&) {
    BowShop shop;
    shop.learn(*shop.character());
    world::GiveOptions options;
    options.gearScore = 100;
    const world::ItemResult staff = shop.world().inventory().give(kCharacterA, "TestStaff", 1, options);
    check(staff.ok() && !staff.slots.empty() && shop.world().inventory().equip(kCharacterA, staff.slots.front(), world::paperdoll::kMainHand1 + 1).ok(),
          "setup: a staff in weapon slot 2");
    shop.world().combat().select_weapon(1, 2);
    std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
    items.push_back(shop::definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
    items.push_back(shop::definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
    gamedata::ItemDefinition mass = shop::definition("TestMass", "Resource", "Resource", 5, 0, 0);
    mass.weight = 4000;
    items.push_back(mass);
    shop.host.catalog.set(items);
    check(shop.world().inventory().give(kCharacterA, "TestMass", 1).ok() && shop.world().inventory().encumbrance(*shop.character()).overencumbered,
          "setup: the bag is over 1.5 x its weight limit");
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    shop.walker.bits({kActionWeapon1});
    check(world::Combat::weapon_in_hand(*shop.character())->itemId == "TestBow" && world::Combat::switching(*shop.character()),
          "setup: the bow is in the hand and its switch holds the attacks");
    shop.send_item(BowShop::bow_item("Basic", "TestBowLight"), {kActionAttack, kActionAttackRelease});
    shop.walker.idle(10);
    note("damage of the item of the held press: " + std::to_string(shop.lost(wolf)));
    check(shop.lost(wolf) == 0.0F, "an item of a press that the switch holds does not pass the weight gate");
}

} // namespace

std::vector<Case> review_b2_cases() {
    return {
        {"BV1", "review of stage B2, finding B2-R1: a cancelled Rapid Shot does not refuse the next ability", cancelled_cast_frees_next},
        {"BV2", "review of stage B2, finding B2-R2: a hit after the end of the charges starts one new charge", ended_effect_starts_again},
        {"BV3", "review of stage B2, finding B2-R3: Mark against the rend of a perk", mark_against_perk_rend},
        {"BV4", "review of stage B2, finding B2-R4: the rend of a perk in a removal window", perk_rend_in_removal_window},
        {"BV5", "review of stage B2, finding B2-R5: the weight gate of a press that a weapon switch holds", held_press_weight},
    };
}

} // namespace soulwarden::backend::tests

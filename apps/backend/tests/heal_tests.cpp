// The cases of stage 3.3b-S6: the heals of an ability and of a friend (world/health.h, world/statuses.h, world/abilities.h). All ids,
// names, and values are made up, but for the names and the cells of the rows of the data that the rules read.
//
//   HE1   the heal path: the heal base (the base damage of the weapon at its gear score times the scaling of Focus), a self Divine
//         Embrace with its real gain after the cap of the maximum health, one heal, and one number (bit 1 of the vitals state of the
//         owner; the next state has no bit 1)
//   HE2   a heal of a friend (two sessions, MP-10): the health of the friend changes and is public to the observers (no number for
//         an observer), only the healer gets the notice (message 4298 with the id of the entity Stats of the friend and the gain),
//         and the friend gets its own number
//   HE3   the checks of a direct heal: a dead friend, a character that is not a friend, a friend out of the range, a friend behind a
//         wall: no heal of it (the caster heals itself); no target that the data permits: no heal and no payment; less mana than the
//         cost: the press is refused; Privilege selects the cost 20
//   HE4   Sacred Ground: the area at the caster at 0.333 s, its payment then, its regeneration (a heal over time each 1 s), Anointed
//         (NonConsumableHealMod), the Beacon potency (EffectPotencyMods), a received heal reduction (Disease), the outgoing and the
//         received parts of the arena debuff (multiplied), and Divine Blessing (an outgoing rule with OnHealed)
//   HE5   the Beacon: the heal amount of its pulses (HealthModifierDamageBased 0.16 of the heal base each 1 s)
//   HE6   a life steal: Critical Gains with the received part of the lifesteal potion (DmgPctToHealthHealMod), and its number
//   HE7   the consumables: a potion (its fixed amount and its part of the maximum, its number), a food tick (no number), a regen potion
//         tick (ShowUiDamageNumbersOnHeal: a number), a status of the gear (no number)
//   HE8   no heal two times: a replay of the press in the reports of the client gives one cast, and an observer that comes into the
//         interest again gets the health with no number and no heal
//   HE9   the load: 100 healers with their heals near them in a world of 4,000 creatures and 400 characters far away, with a time limit
//   HE10  the list of the rules with OnHealed: Divine Blessing applied, an effect after a heal not applied (with the cause)
//   HE11  the aim (live check of stage 3.3b-S6): the key of Divine Embrace starts only the aim of the client (no cast, no cost); the
//         confirm of the same session starts the cast (a click of B does not); the aim ends at its confirm, after 10 s, and at a change
//         of the weapon; the key with the self cast input held casts at once; a press with no session casts at once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "server/rep/health_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/heal_fixture.h"
#include "support/passive_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace heal;

/** HE1: the heal base and a self heal of Divine Embrace. */
void self_heal(const Options&) {
    HealShop shop;
    world::Entity& own = *shop.character();
    shop.staff(own, kDivineNode);
    const float base = shop.host.world.health().heal_base(*shop.character(), "TestLifeStaff", 100);
    note("the heal base: " + std::to_string(base) + " (expected " + std::to_string(kBase) + ")");
    check(near(base, kBase, 0.01F), "the heal base: the base damage of the staff at gear score 100 times 1 + ScalingFocus x the damage value of Focus 6");
    check(near(shop.host.world.health().heal_base(*shop.character(), "TestAxe", 100), kWeaponDamage, 0.01F),
          "a weapon with no ScalingFocus: the base damage at its gear score (no Focus part)");
    shop.walker.idle(5);
    world::Entity* character = shop.character();
    const float max = character->vitals.healthMax;
    character->vitals.health = max - 100.0F;
    shop.host.world.touch(character->id, world::ComponentKey::vitals);
    shop.walker.idle(3);
    const float mana = character->vitals.mana;
    const std::size_t from = shop.a.got.size();
    shop.cast(true);
    shop.walker.idle(2);
    check(near(shop.character()->vitals.mana, mana, 0.01F), "the press pays nothing: the cost comes with the heal");
    shop.pass(1.2F);
    character = shop.character();
    note("health " + std::to_string(character->vitals.health) + " of " + std::to_string(max) + ", heals " + std::to_string(shop.heals().heals)
         + ", mana " + std::to_string(character->vitals.mana));
    check(near(character->vitals.health, max, 0.01F) && shop.heals().heals == 1 && shop.ability_counts().healsSelf == 1,
          "the self heal: one heal, to the maximum (the real gain 100, not 1.2 x the base)");
    // The mana comes back after a spend (2 each second): a tolerance of 1.
    check(near(character->vitals.mana, mana - kDivineCost, 1.0F), "the heal pays the cost of the spell (25)");
    const auto states = vitals_of(shop.a, from);
    int numbers = 0;
    for (const auto& [health, flags] : states) {
        numbers += (flags & 0x02) != 0 ? 1 : 0;
    }
    check(numbers == 1 && !states.empty() && states.front().second == 0x01, "one vitals state of the owner has bit 1 (one number)");
    // A second heal with a known gain: Focus 6, 1.2 x the base.
    shop.pass(6.0F);
    character = shop.character();
    character->vitals.health = 100.0F;
    shop.host.world.touch(character->id, world::ComponentKey::vitals);
    // In a fight: the rest rule (3 percent each second after 6 s with no fight) adds nothing in the measurement.
    shop.host.world.health().fought(*character);
    shop.walker.idle(3);
    const std::size_t second = shop.a.got.size();
    shop.cast(true);
    shop.pass(1.2F);
    character = shop.character();
    const float gain = character->vitals.health - 100.0F;
    note("the second heal: " + std::to_string(gain) + " (1.2 x the base: " + std::to_string(1.2F * kBase) + ")");
    check(near(gain, std::min(1.2F * kBase, max - 100.0F), 0.05F), "the amount: HealthModifierDamageBased 1.2 x the heal base (no modifier)");
    const auto later = vitals_of(shop.a, second);
    check(std::count_if(later.begin(), later.end(), [](const auto& entry) { return (entry.second & 0x02) != 0; }) == 1,
          "the second heal: one number again");
    check(notices_of(shop.a).empty(), "a self heal sends no message 4298");
}

/** HE2: a heal of a friend, two sessions. */
void friend_heal(const Options&) {
    HealShop shop(true);
    const state::WorldPosition place = at(kPlace.x + 3.0F, kPlace.y);
    shop.enter_b_near(place);
    shop.staff(*shop.character(), kDivineNode);
    shop.host.world.set_test_friends(kCharacterA, kCharacterB);
    world::Entity* beta = shop.host.world.find_character(kCharacterB);
    check(beta != nullptr, "setup: B is in the world");
    if (beta == nullptr) {
        return;
    }
    const world::EntityId betaId = beta->id;
    const float max = beta->vitals.healthMax;
    (void)shop.host.world.health().hurt(kCharacterB, max * 0.5F);
    shop.b_steps(5, place);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    const float aBefore = shop.character()->vitals.health;
    shop.cast(false);
    shop.b_steps(45, place);
    beta = shop.host.world.find_character(kCharacterB);
    const float gain = beta->vitals.health - max * 0.5F;
    note("B: health " + std::to_string(beta->vitals.health) + " of " + std::to_string(max) + ", gain " + std::to_string(gain));
    check(near(gain, std::min(1.2F * kBase, max * 0.5F), 0.05F) && shop.ability_counts().healsFriend == 1,
          "Divine Embrace heals the nearest friend (B): 1.2 x the heal base of A");
    check(near(shop.character()->vitals.health, aBefore), "the caster keeps its health");
    const auto notices = notices_of(shop.a, fromA);
    check(notices.size() == 1 && notices.front().first == server::rep::player_stats_entity(betaId) && near(notices.front().second, gain, 0.05F),
          "only the healer gets the notice: message 4298 with the id of the entity Stats of B and the gain");
    check(notices_of(shop.b, fromB).empty(), "the friend gets no message 4298");
    const auto ownStates = vitals_of(shop.b, fromB);
    check(std::count_if(ownStates.begin(), ownStates.end(), [](const auto& entry) { return (entry.second & 0x02) != 0; }) == 1,
          "the friend gets its own number one time (bit 1 of its vitals state)");
    bool seen = false;
    bool observerNumber = false;
    for (const auto& [health, flags] : vitals_of(shop.a, fromA)) {
        if (near(health, beta->vitals.health, 0.01F)) {
            seen = true;
            observerNumber = observerNumber || (flags & 0x02) != 0;
        }
    }
    check(seen && !observerNumber, "the health of the friend is public: A (an observer) gets it, with bit 0 and no bit 1");
}

/** HE3: the checks of a direct heal. */
void heal_checks(const Options& options) {
    struct Variant {
        const char* name;
        bool friends;
        bool dead;
        float east;
        bool wall;
    };
    for (const Variant& variant : {Variant{"a dead friend", true, true, 3.0F, false}, Variant{"a character that is not a friend", false, false, 3.0F, false},
                                   Variant{"a friend out of the range (30 m)", true, false, 30.0F, false},
                                   Variant{"a friend behind a wall", true, false, -4.0F, true}}) {
        HealShop shop(true);
        if (variant.wall) {
            const auto folder = options.workDirectory / "heal-wall";
            TestNavRegion region;
            region.tiles[{31, 31}] = [](int cx, int, std::vector<TestFloor>& floors) {
                const float x = 1984.0F + (static_cast<float>(cx) + 0.5F) * 0.25F;
                if (x > 1998.0F && x < 1998.3F) {
                    floors = {{kHeight + 4.0F, gamedata::NavFloor::kBlocked}};
                }
            };
            region.write(folder);
            check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation data with a wall");
        }
        const state::WorldPosition there = at(kPlace.x + variant.east, kPlace.y);
        shop.enter_b_near(there);
        shop.staff(*shop.character(), kDivineNode);
        if (variant.friends) {
            shop.host.world.set_test_friends(kCharacterA, kCharacterB);
        }
        if (variant.wall) {
            check(!shop.host.world.navigation().sees(at(kPlace.x, kPlace.y, kHeight + 1.5F), at(there.x, there.y, kHeight + 1.5F)),
                  "setup: the wall is between A and B");
        }
        const float max = shop.host.world.find_character(kCharacterB)->vitals.healthMax;
        (void)shop.host.world.health().hurt(kCharacterB, variant.dead ? max : max * 0.5F);
        // The caster is hurt: a target at its full health takes no direct heal (code review of stage 3.3b-S6, finding S6-R3).
        shop.character()->vitals.health = shop.character()->vitals.healthMax - 100.0F;
        shop.host.world.health().fought(*shop.character());
        const float before = shop.host.world.find_character(kCharacterB)->vitals.health;
        const std::size_t from = shop.a.got.size();
        shop.cast(false);
        shop.b_steps(45, there);
        const world::Entity* beta = shop.host.world.find_character(kCharacterB);
        note(std::string(variant.name) + ": B " + std::to_string(beta->vitals.health) + " (before " + std::to_string(before) + "), self heals "
             + std::to_string(shop.ability_counts().healsSelf));
        check(near(beta->vitals.health, before) && notices_of(shop.a, from).empty() && shop.ability_counts().healsFriend == 0
                  && shop.ability_counts().healsSelf == 1,
              std::string(variant.name) + ": no heal of it and no notice; the caster is the target (the self form)");
    }
    {
        // No target that the data permits (the caster has the status of the heal: TargetStatusEffect NotEqual): no heal, no payment.
        HealShop shop;
        shop.staff(*shop.character(), kDivineNode);
        check(shop.give_status(*shop.character(), kDivineStatus, 30.0F) == world::StatusVerdict::applied, "setup: the caster has the status of the heal");
        const float mana = shop.character()->vitals.mana;
        shop.cast(false);
        shop.pass(1.2F);
        check(shop.ability_counts().healsRefused == 1 && near(shop.character()->vitals.mana, mana),
              "no target that the data permits: no heal and no payment (the choice of the target and the cost go together)");
    }
    {
        // A caster at its full health with no friend near: no target that can gain health, no heal and no payment (finding S6-R3).
        HealShop shop;
        shop.staff(*shop.character(), kDivineNode);
        const float mana = shop.character()->vitals.mana;
        shop.cast(true);
        shop.pass(1.2F);
        check(shop.ability_counts().healsRefused == 1 && shop.ability_counts().healsSelf == 0 && near(shop.character()->vitals.mana, mana),
              "a caster at its full health (the self cast): no heal and no payment (code review of stage 3.3b-S6, finding S6-R3)");
    }
    {
        // Less mana than the cost: the press is refused (no cast, no cooldown).
        HealShop shop;
        shop.staff(*shop.character(), kDivineNode);
        shop.character()->vitals.mana = kDivineCost - 1.0F;
        shop.cast(false);
        shop.pass(1.2F);
        check(shop.presses(world::CastVerdict::noMana) == 1 && shop.ability_counts().healsSelf == 0, "less mana than the cost (25): the press is refused");
    }
    {
        // Privilege: the spell _ReducedCost (20).
        HealShop shop;
        shop.staff(*shop.character(), kDivineNode, {kPrivilege});
        shop.character()->vitals.health = shop.character()->vitals.healthMax - 100.0F;
        shop.host.world.health().fought(*shop.character());
        const float mana = shop.character()->vitals.mana;
        shop.cast(true);
        shop.pass(1.2F);
        check(shop.ability_counts().healsSelf == 1 && near(shop.character()->vitals.mana, mana - kPrivilegeCost, 1.0F),
              "Privilege selects the spell _ReducedCost: the heal pays 20");
    }
}

/** @return The health that a character got in the next `seconds` (the ticks of the world). */
float gain_in(HealShop& shop, float seconds) {
    // In a fight: the rest rule (3 percent each second after 6 s with no fight) adds nothing in the measurement.
    shop.host.world.health().fought(*shop.character());
    const float before = shop.character()->vitals.health;
    shop.pass(seconds);
    return shop.character()->vitals.health - before;
}

/** HE4: Sacred Ground and the heal modifiers. */
void sacred_ground(const Options&) {
    const float tick = 0.16F * kBase;
    {
        HealShop shop;
        shop.staff(*shop.character(), kSacredNode);
        world::Entity* character = shop.character();
        character->vitals.health = 100.0F;
        const float mana = character->vitals.mana;
        shop.cast(true);
        shop.walker.idle(2);
        check(shop.spells().empty() && near(shop.character()->vitals.mana, mana), "the press pays nothing and makes no area at once");
        world::Entity* area = shop.wait_spell(20);
        check(area != nullptr && area->spell.role == world::SpellRole::aura && area->spell.spellId == kSacredSpell
                  && near(shop.character()->vitals.mana, mana - kSacredCost, 1.0F),
              "the area of its spell at 0.333 s, at the caster, with its payment (15)");
        if (area == nullptr) {
            return;
        }
        check(area->spell.ends - area->spell.started == milliseconds(12000), "its life: the Duration of the spell row (12 s)");
        shop.walker.idle(10);
        check(has_status(*shop.character(), kSacredStatus), "the caster in the area gets the status of its regeneration");
        const float gained = gain_in(shop, 3.0F);
        note("Sacred Ground: " + std::to_string(gained) + " in 3 s (one tick " + std::to_string(tick) + ")");
        check(near(gained, 3.0F * tick, 0.3F), "the regeneration: HealthModifierDamageBased 0.16 x the heal base each 1 s (a heal over time)");
        // A received heal reduction: Disease (HealMod -0.5).
        (void)shop.give_status(*shop.character(), kDisease, 60.0F);
        const float diseased = gain_in(shop, 2.0F);
        check(near(diseased, 2.0F * tick * 0.5F, 0.3F), "Disease (HealMod -0.5): each tick heals one half");
        // The arena debuff (two stacks): the outgoing part of the healer (-0.06) and the received part (-0.12), multiplied.
        shop.host.world.statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == kDisease; },
                                          world::StatusEnd::command);
        (void)shop.give_status(*shop.character(), kArenaDebuff, 60.0F, 2);
        const float debuffed = gain_in(shop, 2.0F);
        note("the arena debuff: " + std::to_string(debuffed) + " in 2 s (expected " + std::to_string(2.0F * tick * 0.94F * 0.88F) + ")");
        check(near(debuffed, 2.0F * tick * 0.94F * 0.88F, 0.3F), "the outgoing factor (0.94) times the received factor (0.88)");
        shop.host.world.statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == kArenaDebuff; },
                                          world::StatusEnd::command);
        // The potency of the categories: the status of the Beacon on the character (BeaconCat... SacredGroundCat=-0.1).
        (void)shop.give_status(*shop.character(), kBeaconPotency, 60.0F);
        const float potent = gain_in(shop, 2.0F);
        check(near(potent, 2.0F * tick * 0.9F, 0.3F), "the Beacon potency status (EffectPotencyMods SacredGroundCat=-0.1): each tick x 0.9");
    }
    {
        // Anointed: the spell _Self_Upgrade2 with NonConsumableHealMod 0.5 in the area; Divine Blessing below 50 percent (+0.3).
        HealShop shop;
        shop.staff(*shop.character(), kSacredNode, {kAnointed});
        shop.character()->vitals.health = 100.0F;
        shop.cast(true);
        world::Entity* area = shop.wait_spell(20);
        check(area != nullptr && area->spell.spellId == "Spell_LifeStaff_SacredGround_Self_Upgrade2", "Anointed selects the spell _Self_Upgrade2");
        shop.walker.idle(10);
        const float anointed = gain_in(shop, 2.0F);
        check(near(anointed, 2.0F * tick * 1.5F, 0.3F), "Anointed (NonConsumableHealMod 0.5): each tick x 1.5");
        // Divine Blessing: a learned node of the tree of the staff with OnHealed, TargetHealthPercent 50 (the blank comparator).
        world::Entity* character = shop.character();
        const std::uint32_t tree = gamedata::progression_id(kLifeMagic);
        character->skills.pools[tree][gamedata::progression_id(kBlessing)].level = 1;
        character->vitals.health = 100.0F;
        const float blessed = gain_in(shop, 2.0F);
        note("Anointed " + std::to_string(anointed) + ", with Divine Blessing below 50 percent " + std::to_string(blessed));
        check(near(blessed, 2.0F * tick * 1.3F * 1.5F, 0.4F), "Divine Blessing: a target at 50 percent or less: the outgoing factor 1.3");
    }
}

/** HE5: the heal of the pulses of the Beacon. */
void beacon_heal(const Options&) {
    HealShop shop;
    shop.staff(*shop.character(), kBeaconNode);
    world::Entity* wolf = shop.wolf_east(-2.5F);
    const world::EntityId wolfId = wolf->id;
    (void)wolfId;
    shop.character()->vitals.health = 100.0F;
    shop.cast(true);
    shop.walker.idle(3);
    shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
    world::Entity* aura = shop.wait_spell(10);
    check(aura != nullptr && aura->spell.role == world::SpellRole::aura, "setup: the Beacon on the wolf, the caster in its area");
    shop.walker.idle(5);
    check(has_status(*shop.character(), kBeaconStatus), "the caster has the status of the Beacon");
    const float gained = gain_in(shop, 3.0F);
    note("the Beacon: " + std::to_string(gained) + " in 3 s (one pulse " + std::to_string(0.16F * kBase) + ")");
    check(near(gained, 3.0F * 0.16F * kBase, 0.3F) && shop.host.world.statuses().counts().pulsesWithNoHeal == 0,
          "each pulse (1 s) heals HealthModifierDamageBased 0.16 x the heal base of the caster at its cast");
}

/** HE6: a life steal with a received modifier of its kind. */
void life_steal(const Options&) {
    using namespace passive;
    PassiveShop shop;
    auto tables = passive_tables();
    tables.statusEffects.push_back(status(kLifestealPotion, -1.0F, cells({{"DmgPctToHealthHealMod", 0.35F}})));
    tables.statusEffects.push_back(status(kDisease, -1.0F, cells({{"HealMod", -0.5F}})));
    tables.statusEffects = numbered(tables.statusEffects);
    check(shop.host.abilities.set(tables), "setup: the lifesteal potion and Disease");
    shop.hold("TestAxe");
    shop.learn("Passive_Greataxe_Reaper_OnCritHeal");
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    world::Entity& own = *shop.character();
    const auto heal_of = [&](world::Entity* wolf) {
        shop.character()->vitals.health = shop.character()->vitals.healthMax * 0.5F;
        const float before = shop.character()->vitals.health;
        const world::HitResult hit = shop.hit(*wolf, "Test_Light", "TestAxe");
        return std::pair<float, float>{shop.character()->vitals.health - before, hit.damage.dealt};
    };
    const auto [plain, dealt] = heal_of(shop.wolf(2));
    check(near(plain, 0.1F * dealt, 0.05F), "Critical Gains: 10 percent of the damage of the critical hit");
    shop.give(kLifestealPotion, 60.0F, 1);
    shop.pass(0.6F);
    const auto [potion, dealt2] = heal_of(shop.wolf(2, 3));
    note("life steal " + std::to_string(plain) + " of " + std::to_string(dealt) + ", with the potion " + std::to_string(potion) + " of "
         + std::to_string(dealt2));
    check(near(potion, 0.1F * dealt2 * 1.35F, 0.05F), "the lifesteal potion (DmgPctToHealthHealMod 0.35): x 1.35");
    shop.give(kDisease, 60.0F, 1);
    shop.pass(0.6F);
    const auto [sick, dealt3] = heal_of(shop.wolf(2, -3));
    check(near(sick, 0.1F * dealt3 * 0.85F, 0.05F), "Disease too (HealMod -0.5): x (1 - 0.5 + 0.35)");
    check(own.vitals.healShown != 0, "a life steal shows its number to the owner");
}

/** HE7: the numbers of the consumables and of the gear. */
void consumables(const Options&) {
    HealShop shop;
    world::Entity* character = shop.character();
    const float max = character->vitals.healthMax;
    const auto shown = [&] { return shop.character()->vitals.healShown; };
    character->vitals.health = 100.0F;
    world::Revision last = shown();
    (void)shop.give_status(*shop.character(), kPotion, 0.0F, 1, world::StatusSourceKind::consumable);
    check(near(shop.character()->vitals.health, std::min(100.0F + 400.0F + 0.1F * max, max), 0.05F) && shown() > last,
          "a potion: Health + HealthModifierPercent x the maximum, and its number");
    shop.character()->vitals.health = 100.0F;
    last = shown();
    (void)shop.give_status(*shop.character(), kFood, 0.0F, 1, world::StatusSourceKind::consumable);
    shop.pass(0.2F);
    const world::Revision afterFirst = shown();
    shop.pass(2.1F);
    check(shop.character()->vitals.health > 100.0F && shown() == last && afterFirst == last, "a food: its ticks heal with no number");
    shop.host.world.statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == kFood; }, world::StatusEnd::command);
    shop.character()->vitals.health = 100.0F;
    last = shown();
    (void)shop.give_status(*shop.character(), kRegenPotion, 0.0F, 1, world::StatusSourceKind::consumable);
    shop.pass(1.2F);
    check(shown() > last, "a regen potion (ShowUiDamageNumbersOnHeal): its ticks show the number");
    shop.host.world.statuses().remove(*shop.character(), [](const world::ActiveStatus& status) { return status.status == kRegenPotion; },
                                      world::StatusEnd::command);
    shop.character()->vitals.health = 100.0F;
    last = shown();
    (void)shop.give_status(*shop.character(), kFood, 0.0F, 1, world::StatusSourceKind::gear);
    shop.pass(2.2F);
    check(shop.character()->vitals.health > 100.0F && shown() == last, "a status of the gear (a regeneration): no number");
    // The received part of a consumable: ConsumableHealMod.
    HealShop other;
    gamedata::AbilityTables tables = heal_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    rows.push_back(status("Status_ItemPerk_Armor_ConsumableHealing", -1.0F, cells({{"ConsumableHealMod", 0.1F}})));
    tables.statusEffects = numbered(std::move(rows));
    check(other.host.abilities.set(tables), "setup: a status with ConsumableHealMod");
    (void)other.give_status(*other.character(), "Status_ItemPerk_Armor_ConsumableHealing", 60.0F);
    other.character()->vitals.health = 100.0F;
    (void)other.give_status(*other.character(), kPotion, 0.0F, 1, world::StatusSourceKind::consumable);
    const float gain = other.character()->vitals.health - 100.0F;
    check(near(gain, std::min((400.0F + 0.1F * other.character()->vitals.healthMax) * 1.1F, other.character()->vitals.healthMax - 100.0F), 0.05F),
          "ConsumableHealMod 0.1: a potion heals 1.1 times");
}

/** HE8: no heal two times. */
void no_double_heal(const Options&) {
    HealShop shop(true);
    const state::WorldPosition place = at(kPlace.x + 3.0F, kPlace.y);
    shop.enter_b_near(place);
    shop.staff(*shop.character(), kDivineNode);
    shop.host.world.set_test_friends(kCharacterA, kCharacterB);
    const float max = shop.host.world.find_character(kCharacterB)->vitals.healthMax;
    (void)shop.host.world.health().hurt(kCharacterB, max * 0.8F);
    shop.b_steps(3, place);
    // The press comes in five reports of the client (each report has the last five entries): one cast.
    shop.cast(false);
    shop.b_steps(45, place);
    const float healed = shop.host.world.find_character(kCharacterB)->vitals.health;
    check(shop.presses(world::CastVerdict::accepted) == 1 && shop.heals().heals == 1 && shop.ability_counts().healsFriend == 1,
          "a press that the reports of the client repeat: one cast, one heal");
    // B goes far from A (out of the interest of A) and comes back: the first record of B for A has its health, no number, and no heal.
    const state::WorldPosition far = at(kPlace.x + 300.0F, kPlace.y);
    check(shop.world().travel(kCharacterB, far, world::TravelScreen::off).error == world::TravelError::none, "setup: B travels away");
    shop.b_steps(10, far);
    const std::size_t from = shop.a.got.size();
    check(shop.world().travel(kCharacterB, place, world::TravelScreen::off).error == world::TravelError::none, "setup: B comes back");
    shop.b_steps(10, place);
    bool first = false;
    bool number = false;
    for (const auto& [health, flags] : vitals_of(shop.a, from)) {
        if (near(health, healed, 0.01F)) {
            first = true;
            number = number || (flags != 0xFF && (flags & 0x02) != 0);
        }
    }
    check(first && !number && shop.heals().heals == 1 && near(shop.host.world.find_character(kCharacterB)->vitals.health, healed),
          "an observer that comes into the interest again: the health of B with no number, and no second heal");
}

/**
 * HE9: the load. Code review of stage 3.3b-S6, finding S6-R9: the direct heals and the periodic heals of the areas are counted apart. Each
 * healer of Sacred Ground has a hurt friend 1 m from it (a pair), whose health only the pulses of the area can raise (the rest rule waits
 * for 6 s with no fight: each round marks a fight), and the case needs a gain of each such friend.
 */
void heal_load(const Options&) {
    HealShop shop;
    for (int index = 0; index < 4000; ++index) {
        (void)shop.world().creatures().spawn("Test_Wolf", at(5000.0F + static_cast<float>(index % 100) * 3.0F, 5000.0F + static_cast<float>(index / 100) * 3.0F), 0.0F);
    }
    std::vector<std::string> healers;
    std::vector<std::string> friends;
    const auto add = [&](int index, const state::WorldPosition& place) {
        char id[64];
        std::snprintf(id, sizeof(id), "eeeeeeee-3333-4000-8000-%012d", index);
        char steam[32];
        std::snprintf(steam, sizeof(steam), "765612022552%05d", 60000 + index);
        char name[32];
        std::snprintf(name, sizeof(name), "Healer%c%c%c", 'a' + index / 676, 'a' + (index / 26) % 26, 'a' + index % 26);
        (void)add_player(shop.host.store, steam, "Load", id, name, place);
        (void)shop.world().spawn_character(id, place);
        return std::string(id);
    };
    // 400 characters far away (a crowd that no heal sees), and 100 healers 60 m apart; each healer of Sacred Ground with a friend 1 m away.
    for (int index = 0; index < 400; ++index) {
        (void)add(index, at(8000.0F + static_cast<float>(index % 20) * 4.0F, 8000.0F + static_cast<float>(index / 20) * 4.0F));
    }
    for (int index = 0; index < 100; ++index) {
        const state::WorldPosition place = at(kPlace.x + static_cast<float>(index % 10) * 60.0F, kPlace.y + 100.0F + static_cast<float>(index / 10) * 60.0F);
        const std::string id = add(400 + index, place);
        world::Entity* healer = shop.world().find_character(id);
        shop.staff(*healer, index % 2 == 0 ? kDivineNode : kSacredNode);
        healers.push_back(id);
        if (index % 2 == 1) {
            const std::string friendId = add(600 + index, at(place.x + 1.0F, place.y));
            shop.world().set_test_friends(id, friendId);
            friends.push_back(friendId);
        }
    }
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t lookedBefore = shop.ability_counts().healLooked;
    float periodicGain = 0.0F;
    std::size_t friendsHealed = 0;
    for (int round = 0; round < 3; ++round) {
        std::vector<float> before;
        for (const std::string& id : friends) {
            world::Entity* friendly = shop.world().find_character(id);
            friendly->vitals.health = friendly->vitals.healthMax * 0.5F;
            shop.world().health().fought(*friendly);
            before.push_back(friendly->vitals.health);
        }
        for (const std::string& id : healers) {
            world::Entity* healer = shop.world().find_character(id);
            healer->vitals.health = healer->vitals.healthMax * 0.5F;
            shop.world().health().fought(*healer);
            world::AbilityPress press;
            press.place = 0;
            press.sequence = static_cast<std::uint32_t>(round + 1);
            (void)shop.world().abilities().reset_cooldowns(id);
            (void)shop.world().abilities().press_character(id, press);
        }
        shop.pass(5.0F);
        for (std::size_t index = 0; index < friends.size(); ++index) {
            const world::Entity* friendly = shop.world().find_character(friends[index]);
            const float gain = friendly->vitals.health - before[index];
            periodicGain += std::max(gain, 0.0F);
            friendsHealed += gain > 0.0F ? 1 : 0;
        }
        shop.pass(16.0F);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const std::uint64_t looked = shop.ability_counts().healLooked - lookedBefore;
    note("100 healers, 3 rounds of Divine Embrace and Sacred Ground in a world of 4,000 creatures and 400 far characters: " + std::to_string(seconds)
         + " s; heals " + std::to_string(shop.heals().heals) + ", direct self heals " + std::to_string(shop.ability_counts().healsSelf)
         + ", friends of the areas healed " + std::to_string(friendsHealed) + " of " + std::to_string(friends.size() * 3) + " (gain "
         + std::to_string(periodicGain) + "), friends looked at by the choice of a target " + std::to_string(looked));
    check(shop.ability_counts().healsSelf >= 140, "the direct heals: 50 healers of Divine Embrace in 3 rounds heal themselves");
    check(friendsHealed == friends.size() * 3 && periodicGain > 0.0F,
          "the periodic heals: in each round each hurt friend 1 m from a healer of Sacred Ground gains health from the pulses of the area");
    check(looked == 0, "the choice of a direct target looks only at the entities near the caster (no character is near a healer of Divine Embrace)");
    check(seconds < 5.0, "the far crowd adds no work: less than 5 s");
}

/** HE10: the list of the rules with OnHealed. */
void heal_rules(const Options&) {
    HealShop shop;
    const auto view_of = [&](const char* id) {
        const gamedata::AbilityRow* row = shop.host.abilities.ability_row(id);
        return shop.host.world.passives().describe_row(id, row != nullptr ? &row->cells : nullptr, world::RuleOrigin::node);
    };
    const world::RuleView blessing = view_of(kBlessing);
    const world::RuleView desperate = view_of(kDesperate);
    note("Divine Blessing: " + blessing.note + "; Desperate Speed: " + desperate.note);
    check(blessing.applied, "Divine Blessing (OnHealed, HealScalingValueMultiplier): applied (a part of the outgoing heal)");
    check(!desperate.applied && desperate.note.find("after a heal") != std::string::npos, "Desperate Speed (effects after a heal): not applied, with the cause");
    check(world::Abilities::has_plan(kDivineNode) && world::Abilities::has_plan(kSacredNode), "Divine Embrace and Sacred Ground have a plan");
}

/** HE11: the aim of an aimed heal and its confirm. */
void heal_aim(const Options&) {
    HealShop shop(true);
    const state::WorldPosition place = at(kPlace.x - 1.0F, kPlace.y + 1.0F);
    shop.enter_b_near(place);
    shop.staff(*shop.character(), kDivineNode);
    const float max = shop.character()->vitals.healthMax;
    const auto hurt = [&] {
        shop.character()->vitals.health = max - 200.0F;
        shop.host.world.health().fought(*shop.character());
    };
    const auto heals = [&] { return shop.ability_counts().healsSelf + shop.ability_counts().healsFriend; };
    // The key with no confirm: the aim of the client only (no cast, no cost, no cooldown, no heal).
    hurt();
    const float mana = shop.character()->vitals.mana;
    shop.walker.press(0);
    shop.walker.idle(45);
    check(heals() == 0 && near(shop.character()->vitals.mana, mana) && shop.ability_counts().aims == 1
              && shop.presses(world::CastVerdict::accepted) == 0,
          "the key with no confirm: an aim, no cast, no cost, no heal");
    // A click of a different session (B) does not confirm the aim of A (MP-10: the aim belongs to the session of its key).
    InputEntry click = report_entry(shop.bInput++, place.x, place.y, place.z);
    click.actionBits = {kActionAttack};
    shop.host.clock.advance(kTick);
    shop.host.datagram(shop.b.peer, shop.b.input_report({click}));
    shop.host.tick();
    shop.b_steps(30, place);
    check(heals() == 0, "a click of the session of B: no cast for the aim of A");
    // The self cast input of A confirms its aim: the cast, the heal of A, and the cost.
    hurt();
    shop.walker.bits({kSelfTargetBit});
    shop.walker.idle(45);
    check(shop.ability_counts().confirms == 1 && shop.ability_counts().healsSelf == 1
              && near(shop.character()->vitals.mana, mana - kDivineCost, 1.0F),
          "the self cast input of A confirms its aim: one cast, one heal of A, and its cost");
    // A second confirm with no new key: no cast (the confirm ends the aim).
    (void)shop.world().abilities().reset_cooldowns(kCharacterA, true);
    shop.walker.bits({kSelfTargetBit});
    shop.walker.idle(45);
    check(heals() == 1, "a second confirm with no new key: no cast");
    // The aim ends after ability_rules::kAimKeep: a late click is an attack, not a confirm.
    hurt();
    shop.walker.press(0);
    shop.pass(static_cast<float>(world::ability_rules::kAimKeep.count()) / 1000.0F + 0.5F);
    hurt();
    shop.walker.bits({kActionAttack});
    shop.walker.idle(45);
    check(heals() == 1, "a click after the time of the aim (10 s): no cast");
    // A change of the weapon ends the aim.
    shop.walker.press(0);
    shop.world().abilities().weapon_changed(*shop.character());
    shop.walker.bits({kActionAttack});
    shop.walker.idle(45);
    check(heals() == 1, "a change of the weapon ends the aim: no cast at the click");
    // The key with the self cast input held (the actions *_SelfCast of the data): the cast at once on the caster, with no confirm.
    hurt();
    shop.walker.bits({kAbilityBits[0], kVirtualBit, kSelfTargetBit});
    shop.walker.bits({kHoldBit});
    shop.walker.bits({kReleaseBit});
    shop.walker.idle(45);
    check(shop.ability_counts().healsSelf == 2, "the key with the self cast input held: the heal of the caster at once");
    // A character with no client (a command, the load case) has no aim: its press casts at once.
    (void)shop.world().abilities().reset_cooldowns(kCharacterA, true);
    hurt();
    world::AbilityPress press;
    press.place = 0;
    press.sequence = 99;
    check(shop.world().abilities().press_character(kCharacterA, press) == world::CastVerdict::accepted, "a press with no session: accepted");
    shop.walker.idle(45);
    check(shop.ability_counts().healsSelf == 3, "a press with no session: the cast at once (no aim)");
}

/** HE12: the level part of the heal base (a decision of the user of 2026-10-15): the level damage of the damage rule, and no Intelligence part. */
void heal_base_level(const Options&) {
    HealShop shop;
    world::Entity& own = *shop.character();
    shop.staff(own, kDivineNode);
    const float levelOne = shop.host.world.health().heal_base(own, "TestLifeStaff", 100);
    own.progression.level = 60;
    const float levelSixty = shop.host.world.health().heal_base(own, "TestLifeStaff", 100);
    const float levelDamage = shop.host.combat.constants().levelDamage;
    const float expected = kWeaponDamage * (1.0F + kScalingFocus * kFocusDamage + levelDamage * 59.0F);
    note("heal base: level 1 " + std::to_string(levelOne) + ", level 60 " + std::to_string(levelSixty) + " (expected " + std::to_string(expected)
         + ", level damage " + std::to_string(levelDamage) + ")");
    check(near(levelOne, kBase, 0.01F) && near(levelSixty, expected, 0.01F) && levelSixty > levelOne,
          "the heal base of a healer of the level 60: the base damage times (1 + the Focus part + the level damage x 59), the level part of the "
          "damage rule");
    const unsigned intelligence = own.attributes.value(world::attribute::kIntelligence);
    own.attributes.spent[world::attribute::kIntelligence] += 200;
    const float clever = shop.host.world.health().heal_base(own, "TestLifeStaff", 100);
    check(own.attributes.value(world::attribute::kIntelligence) > intelligence && near(clever, levelSixty, 0.001F),
          "200 more Intelligence: the same heal base (no Intelligence part; a heal scales with Focus)");
}

} // namespace

std::vector<Case> heal_cases() {
    return {{"HE1", "heals: the heal base, a self heal with its real gain, one heal and one number", self_heal},
            {"HE2", "heals: a heal of a friend, two sessions: the public health, the notice of the healer only (MP-10)", friend_heal},
            {"HE3", "heals: a dead, hostile, far, or hidden target gets no heal; no target: no payment; the cost", heal_checks},
            {"HE4", "heals: Sacred Ground: the area, its payment, its regeneration, the received and the outgoing modifiers", sacred_ground},
            {"HE5", "heals: the heal amount of the pulses of the Beacon", beacon_heal},
            {"HE6", "heals: a life steal with the received modifier of its kind", life_steal},
            {"HE7", "heals: the numbers of a potion, a food, a regen potion, and the gear; ConsumableHealMod", consumables},
            {"HE8", "heals: a repeated press and an observer that comes back: no heal two times (MP-10)", no_double_heal},
            {"HE9", "heals: the load of 100 healers in a world of 4,000 creatures and 400 far characters, with a time limit", heal_load},
            {"HE10", "heals: the list of the rules with OnHealed", heal_rules},
            {"HE11", "heals: the aim of Divine Embrace: the confirm of its session, its end, the key with the self cast input", heal_aim},
            {"HE12", "heals: the level part of the heal base, and no Intelligence part", heal_base_level}};
}

} // namespace soulwarden::backend::tests

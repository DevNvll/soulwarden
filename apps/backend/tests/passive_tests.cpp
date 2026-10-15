// The evaluator of the passive rules of the data (stage 3.3b-S5, world/passives.h). The rows of the nodes are the rows of the
// ability tables of the game (support/passive_fixture.h, data_rows): each case names them.
//
//   PS1  the kinds of the rules: a fixed stat (Keen Edge), a status on the caster (Mauler's Fury), a status on the target
//        (Exhaustive Attacks), a change of an ability (Crowded Well), a change of a basic attack (Contemption, Epitome Of Bonk!),
//        a change of a cooldown and a rule that counts hits (Quick Recovery, Stimulated Reduction), the targets of one attack
//        (Executioner's Speed), and a rule of an action (the spell of Shockwave by Meteoric Crater; Frailty and Aftershock)
//   PS2  the blank comparator of AbilityData at the limits: Executioner (50 percent), its critical node (30 percent), Leaping
//        Strike (30 percent), with the health just below, equal to, and just above the limit
//   PS3  a status while entities are near (Center of Attention: more than 2 creatures in the tracking spell of 4 m), its end
//   PS4  the list of the rules: what the evaluator applies, and each rule that it does not apply with its cause
//   PS5  two sessions (MP-10): only the character of a hit gets its rules; a status of a target goes through the normal path to
//        each observer; no activation cooldown and no hit count of one character changes the other
//   PS6  the load: the rules of the characters and the entities near them, not each row or each creature, with a time limit
//   PS7  a hit taken (Power Through Pain) and a critical hit (Critical Gains, Critical Condition)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/passive_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace passive;

/** @return A made-up hit of A for the evaluator (no strike): its row, its attack type, its table, and the health of the target before it. */
world::HitContext context(const char* row, const char* attackType, std::uint32_t table, float health = -1.0F, float healthMax = 0.0F) {
    world::HitContext hit;
    hit.row = row;
    hit.attackType = attackType;
    hit.table = table;
    hit.targetHealth = health;
    hit.targetHealthMax = healthMax;
    return hit;
}

world::AbilityPress press_of(unsigned place) {
    world::AbilityPress press;
    press.place = place;
    press.yaw = 1.5708F;
    return press;
}

/** PS1: the kinds of the rules of the data. */
void rule_kinds(const Options&) {
    PassiveShop shop;
    world::Entity& own = *shop.character();
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(2);
    // A status on the caster: Mauler's Fury (Ultimate_Greataxe_Mauler: OnHit, RequireReaction, SelfApplyStatusEffect).
    shop.learn("Ultimate_Greataxe_Mauler");
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    const world::ActiveStatus* fury = shop.find("Status_Ultimate_Greataxe_Mauler");
    check(fury != nullptr && fury->source.kind == world::StatusSourceKind::ability && fury->source.id == "Ultimate_Greataxe_Mauler",
          "Ultimate_Greataxe_Mauler: a hit of the great axe gives the caster Status_Ultimate_Greataxe_Mauler (source: the node)");
    // A status on the target: Exhaustive Attacks (Passive_WarHammer_Juggernaut_ExhaustOnHit: AttackType Ability, OtherApplyStatusEffect).
    shop.hold("TestHammer");
    shop.learn("Passive_WarHammer_Juggernaut_ExhaustOnHit");
    world::Entity* second = shop.wolf(-2);
    (void)shop.hit(*second, "Test_Light", "TestHammer");
    check(!shop.creature_has(*shop.world().creatures().find(second->id), "Warhammer_Pass_ExhaustOnHit"),
          "Passive_WarHammer_Juggernaut_ExhaustOnHit: a light attack of the hammer gives no exhaust (AttackType Ability)");
    (void)shop.hit(*second, "Warhammer_Damage_WreckingBall", "TestHammer", world::HitKind::contact, world::passive_rules::kAttackCast | 3,
                   "Ability_WarHammer_WreckingBall");
    check(shop.creature_has(*shop.world().creatures().find(second->id), "Warhammer_Pass_ExhaustOnHit"),
          "an ability hit of the hammer gives the creature Warhammer_Pass_ExhaustOnHit through the creature system");
    // A change of an ability: Crowded Well (Upgrade_Greataxe_GravityWell_DmgPerFoe: the burst row, BaseDamage 0.1 for each creature
    // of the tracking spell Spell_GreatAxe_GravityWell_Vortex, MaxNumAroundMe 5).
    shop.learn("Upgrade_Greataxe_GravityWell_DmgPerFoe");
    world::HitContext burst = context("GreatAxe_GravityWell_Burst", "Ability", kGreatAxe);
    burst.spell = "Spell_GreatAxe_GravityWell_Vortex";
    burst.areaCount = 3;
    check(near(shop.world().passives().before_hit(own, *wolf, burst).damage, 0.3F),
          "Crowded Well: a burst with 3 creatures in the vortex: BaseDamage 0.1 x 3 = 0.3");
    burst.areaCount = 8;
    check(near(shop.world().passives().before_hit(own, *wolf, burst).damage, 0.5F), "8 creatures in the vortex: 5 at most (MaxNumAroundMe): 0.5");
    burst.areaCount = 0;
    check(near(shop.world().passives().before_hit(own, *wolf, burst).damage, 0.0F), "no creature counted: NumAroundMe 1 (GreaterThanOrEqual) fails");
    // A change of a basic attack: Contemption (Passive_WarHammer_Juggernaut_HeavyvsLowHP: Heavy, below 30 percent, BaseDamage 0.15) and
    // Epitome Of Bonk! (Passive_WarHammer_Juggernaut_LightHeavyPenetrate: Light and Heavy, ArmorPenetration 0.1).
    shop.learn("Passive_WarHammer_Juggernaut_HeavyvsLowHP");
    shop.learn("Passive_WarHammer_Juggernaut_LightHeavyPenetrate");
    const float max = 1000.0F;
    const world::HitBonus heavyLow = shop.world().passives().before_hit(own, *wolf, context("Test_Heavy", "Heavy", kWarHammer, 290.0F, max));
    const world::HitBonus heavyAt = shop.world().passives().before_hit(own, *wolf, context("Test_Heavy", "Heavy", kWarHammer, 300.0F, max));
    const world::HitBonus lightLow = shop.world().passives().before_hit(own, *wolf, context("Test_Light", "Light", kWarHammer, 290.0F, max));
    const world::HitBonus ability = shop.world().passives().before_hit(own, *wolf, context("Warhammer_Damage_Shockwave", "Ability", kWarHammer, 290.0F, max));
    check(near(heavyLow.damage, 0.15F) && near(heavyAt.damage, 0.0F) && near(lightLow.damage, 0.0F),
          "Contemption: a heavy attack on a target at 29 percent: BaseDamage 0.15; at 30 percent none (LessThan); a light attack none");
    check(near(heavyLow.penetration, 0.1F) && near(lightLow.penetration, 0.1F) && near(ability.penetration, 0.0F),
          "Epitome Of Bonk!: the armor penetration 0.1 for the light and the heavy attacks, not for an ability");
    // The penetration in the damage rule: the mitigation 0.2 of the wolf times 0.9.
    const world::HitResult penetrated = shop.hit(*wolf, "Test_Light", "TestHammer");
    check(near(penetrated.damage.dealt, penetrated.damage.raw * (1.0F + own.gear.damage["Slash"]) * (1.0F - 0.2F * 0.9F), 0.05F),
          "the damage of a light attack with the penetration 0.1: raw times (1 + the DMG part) times (1 - 0.2 x 0.9)");
    // A change of a cooldown and a rule that counts hits: Quick Recovery (Passive_WarHammer_Juggernaut_CDROnHit: Heavy, RequireReaction,
    // TrackHitCount, NumberOfTrackedHits 5, ActiveWeapon, CooldownTimer -0.07).
    shop.learn("Passive_WarHammer_Juggernaut_CDROnHit");
    for (const char* node : {"Ability_WarHammer_WreckingBall", "Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny"}) {
        shop.learn(node);
    }
    own.skills.bars[kWarHammer] = {gamedata::progression_id("Ability_WarHammer_WreckingBall"), gamedata::progression_id("Ability_WarHammer_Shockwave"),
                                   gamedata::progression_id("Ability_WarHammer_PathOfDestiny")};
    shop.walker.idle(2);
    check(shop.world().abilities().press_character(kCharacterA, press_of(2)) == world::CastVerdict::accepted, "setup: Path of Destiny starts its cooldown (22 s)");
    shop.pass(2.0F);
    const auto cooldown = [&](const char* ability) {
        const auto* cooldowns = shop.world().abilities().cooldowns_of(kCharacterA);
        const auto found = cooldowns->find(gamedata::progression_id(ability));
        return found == cooldowns->end() ? -1.0F : std::chrono::duration<float>(found->second.end - shop.host.clock.now()).count();
    };
    const float before = cooldown("Ability_WarHammer_PathOfDestiny");
    std::vector<world::Entity*> wolves;
    for (int index = 0; index < 6; ++index) {
        wolves.push_back(shop.wolf(1.0F + static_cast<float>(index) * 0.3F, 2.0F));
    }
    const float start = cooldown("Ability_WarHammer_PathOfDestiny");
    for (world::Entity* target : wolves) {
        (void)shop.hit(*target, "Test_Heavy", "TestHammer", world::HitKind::contact, world::passive_rules::kAttackPress | 77);
    }
    const float afterOne = cooldown("Ability_WarHammer_PathOfDestiny");
    check(before > 15.0F && near(start - afterOne, 5 * 0.07F * 22.0F, 0.05F),
          "Quick Recovery: 6 heavy hits of one attack take 5 x 7 percent of the cooldown of Path of Destiny (22 s): 5 reductions at most for each attack");
    (void)shop.hit(*wolves[0], "Test_Heavy", "TestHammer", world::HitKind::contact, world::passive_rules::kAttackPress | 78);
    check(near(afterOne - cooldown("Ability_WarHammer_PathOfDestiny"), 0.07F * 22.0F, 0.05F), "a hit of the next attack takes 7 percent again");
    // Stimulated Reduction (Upgrade_WarHammer_PathOfDestiny_CDRPerHit: InAction Path of Destiny, TrackHitCount 5, AbilitySpecific).
    shop.learn("Upgrade_WarHammer_PathOfDestiny_CDRPerHit");
    const float path = cooldown("Ability_WarHammer_PathOfDestiny");
    for (world::Entity* target : wolves) {
        (void)shop.hit(*target, "Warhammer_Damage_PathOfDestiny", "TestHammer", world::HitKind::area, world::passive_rules::kAttackCast | 900,
                       "Ability_WarHammer_PathOfDestiny");
    }
    check(near(path - cooldown("Ability_WarHammer_PathOfDestiny"), 5 * 0.05F * 22.0F, 0.05F),
          "Stimulated Reduction: 6 hits of one cast take 5 x 5 percent of the cooldown of Path of Destiny (AbilitySpecific, 5 at most)");
    // The targets of one attack: Executioner's Speed (Passive_Greataxe_Reaper_HasteOnLight: NumSuccessfulHits 2, Light).
    shop.hold("TestAxe");
    shop.learn("Passive_Greataxe_Reaper_HasteOnLight");
    (void)shop.hit(*wolves[0], "Test_Light", "TestAxe", world::HitKind::contact, world::passive_rules::kAttackPress | 500);
    check(shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight") == nullptr, "Executioner's Speed: one target of a light attack: no haste");
    (void)shop.hit(*wolves[1], "Test_Light", "TestAxe", world::HitKind::contact, world::passive_rules::kAttackPress | 500);
    check(shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight") != nullptr, "the second target of the same light attack: the haste");
    // A rule of an action: the action of the data selects the spell of Shockwave (and its row) by Meteoric Crater.
    world::SkillSet skills;
    skills.pools[kWarHammer][gamedata::progression_id("Ability_WarHammer_Shockwave")].level = 1;
    const std::uint32_t shockwave = gamedata::progression_id("Ability_WarHammer_Shockwave");
    const auto plain = world::Abilities::plan(shockwave, skills, &shop.host.abilities);
    skills.pools[kWarHammer][gamedata::progression_id("Upgrade_WarHammer_Shockwave_SizeIncrease")].level = 1;
    const auto crater = world::Abilities::plan(shockwave, skills, &shop.host.abilities);
    check(plain && crater && plain->phases[0].spell == "WarHammer_Spell_Shockwave" && plain->phases[0].row == "Warhammer_Damage_Shockwave"
              && crater->phases[0].spell == "WarHammer_Spell_Shockwave_L2" && crater->phases[0].row == "Warhammer_Damage_Shockwave_L2",
          "Shockwave: the action of the data casts WarHammer_Spell_Shockwave_L2 with Meteoric Crater (its DamageTableRow), else the base spell");
    // Frailty and Aftershock: the evaluator applies their rows; the plan has no copy of them (the 3.3a rule stays for an older file).
    skills.pools[kWarHammer][gamedata::progression_id("Upgrade_WarHammer_Shockwave_Weaken")].level = 1;
    skills.pools[kWarHammer][gamedata::progression_id("Ult_Passive_WarHammer_SlowOnCC")].level = 1;
    const auto rules = world::Abilities::plan(shockwave, skills, &shop.host.abilities);
    const auto older = world::Abilities::plan(shockwave, skills, nullptr);
    check(rules && rules->phases[0].effects.size() == 1 && older && older->phases[0].effects.size() == 3,
          "with the rows of the data, the plan of Shockwave has only its stun; with no data the 3.3a effects (weaken, slow)");
    shop.hold("TestHammer");
    shop.learn("Upgrade_WarHammer_Shockwave_Weaken");
    shop.learn("Ult_Passive_WarHammer_SlowOnCC");
    world::Entity* third = shop.wolf(0.0F, -3.0F);
    (void)shop.hit(*third, "Warhammer_Damage_Shockwave", "TestHammer", world::HitKind::area, world::passive_rules::kAttackCast | 901,
                   "Ability_WarHammer_Shockwave");
    world::Entity* hurt = shop.world().creatures().find(third->id);
    check(shop.creature_has(*hurt, "WarHammer_Shockwave_Weaken") && shop.creature_has(*hurt, "WarHammer_Ult_SlowOnCC"),
          "a hit of Shockwave: Frailty (InAction Shockwave) and Aftershock (AttackType Ability) give the weaken and the slow from their rows");
}

/** PS2: the blank comparator at the limits (S5-I4). */
void comparator_limits(const Options&) {
    PassiveShop shop;
    world::Entity& own = *shop.character();
    world::Entity* wolf = shop.wolf(2);
    shop.learn("Ability_Greataxe_Executioner");
    shop.learn("Upgrade_Greataxe_Executioner_CritVsLowHP");
    shop.learn("Upgrade_Sword_LeapingStrike_Extra");
    const float max = 2000.0F;
    struct Limit {
        const char* name;
        const char* row;
        std::uint32_t table;
        float percent;
        bool critical;
    };
    for (const Limit& limit : {Limit{"Ability_Greataxe_Executioner (BaseDamage 0.5)", "GreatAxe_Executioner", kGreatAxe, 50.0F, false},
                               Limit{"Upgrade_Greataxe_Executioner_CritVsLowHP (CritChance 1)", "GreatAxe_Executioner", kGreatAxe, 30.0F, true},
                               Limit{"Upgrade_Sword_LeapingStrike_Extra (BaseDamage 0.5)", "1H_Sword_Attack_Charge", kSword, 30.0F, false}}) {
        const auto value = [&](float percent) {
            const world::HitBonus bonus =
                shop.world().passives().before_hit(own, *wolf, context(limit.row, "Ability", limit.table, max * percent / 100.0F, max));
            return limit.critical ? bonus.critChance : bonus.damage;
        };
        const float below = value(limit.percent - 0.01F);
        const float equal = value(limit.percent);
        const float above = value(limit.percent + 0.01F);
        const float part = limit.critical ? 1.0F : 0.5F;
        // The Executioner row also has BaseDamage 0.5 at 50 percent: its critical node is alone in the critical chance.
        check(near(below, part, 1e-4F) && near(equal, part, 1e-4F) && near(above, 0.0F, 1e-4F),
              std::string(limit.name) + ": the health " + std::to_string(limit.percent - 0.01F) + " and " + std::to_string(limit.percent)
                  + " percent pass, " + std::to_string(limit.percent + 0.01F) + " does not (the blank TargetComparisonType is LessThanOrEqual)");
    }
    // A full hit of Executioner on a wolf at 50 percent: the BaseDamage 0.5 is in the damage (the health before the hit).
    shop.hold("TestAxe");
    world::Entity* target = shop.world().creatures().find(wolf->id);
    target->vitals.health = target->vitals.healthMax * 0.5F;
    const world::HitResult at = shop.hit(*target, "GreatAxe_Executioner", "TestAxe");
    target->vitals.health = target->vitals.healthMax * 0.501F;
    const world::HitResult above = shop.hit(*target, "GreatAxe_Executioner", "TestAxe");
    check(at.damage.raw > above.damage.raw * 1.3F, "a hit of Executioner at 50 percent has the part 0.5 more than a hit just above 50 percent");
}

/** PS3: a status while creatures are near (Center of Attention). */
void around_rules(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    shop.learn("Passive_Greataxe_Mauler_DmgWhenFoesNear");
    world::Entity* one = shop.wolf(2);
    world::Entity* two = shop.wolf(-2);
    (void)shop.wolf(0.0F, 5.0F); // Out of the 4 m of the tracking spell.
    shop.pass(1.0F);
    check(shop.find("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear") == nullptr,
          "two creatures in 4 m (Spell_Passive_Greataxe_Mauler_DmgWhenFoesNear: radius 4) and one at 5 m: no status (NumAroundMe 2, GreaterThan)");
    world::Entity* three = shop.wolf(0.0F, 3.0F);
    shop.pass(1.0F);
    const world::ActiveStatus* status = shop.find("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear");
    check(status != nullptr && status->permanent && status->source.id == "Passive_Greataxe_Mauler_DmgWhenFoesNear"
              && near(shop.character()->gear.damage["Slash"], 0.1F),
          "three creatures in 4 m: the status of the rule with no end, and its DMG part 0.1 in the gear values");
    (void)shop.world().creatures().hurt(three->id, kWolfHealth * 2.0F, 0, 0);
    shop.pass(1.0F);
    check(shop.find("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear") == nullptr, "a creature died: two in 4 m, the status ends");
    (void)shop.wolf(0.0F, 3.0F);
    shop.pass(1.0F);
    check(shop.find("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear") != nullptr, "setup: three again, the status again");
    shop.hold("TestHammer");
    shop.pass(1.0F);
    check(shop.find("Status_Passive_Greataxe_Mauler_DmgWhenFoesNear") == nullptr,
          "the hammer in the hand: the rule of the great axe has no effect, the status ends (the action data: the tag GreatAxe)");
    (void)one;
    (void)two;
}

/** PS4: the list of the rules. */
void rule_list(const Options&) {
    PassiveShop shop;
    world::Entity& own = *shop.character();
    for (const char* node : {"Passive_Greataxe_Reaper_Crit", "Passive_Greataxe_Mauler_HeavyAddGrit", "Passive_Greataxe_Mauler_DmgOnBlock",
                             "Upgrade_Greataxe_Maelstorm_Rng", "Upgrade_WarHammer_Shockwave_SizeIncrease", "Ability_Warhammer_MightyGavel_Upg",
                             "Passive_Greataxe_Mauler_DmgWhenFoesNear"}) {
        shop.learn(node);
    }
    const std::vector<world::RuleView> views = shop.world().passives().describe(own);
    const auto view = [&](const char* id) -> const world::RuleView* {
        for (const world::RuleView& entry : views) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    };
    const auto has_kind = [](const world::RuleView* entry, const char* kind) {
        return entry != nullptr && std::find(entry->kinds.begin(), entry->kinds.end(), kind) != entry->kinds.end();
    };
    check(view("Passive_Greataxe_Reaper_Crit") != nullptr && view("Passive_Greataxe_Reaper_Crit")->applied && has_kind(view("Passive_Greataxe_Reaper_Crit"), "fixed stat"),
          "Keen Edge: applied, a fixed stat");
    check(view("Passive_Greataxe_Mauler_HeavyAddGrit") != nullptr && !view("Passive_Greataxe_Mauler_HeavyAddGrit")->applied
              && view("Passive_Greataxe_Mauler_HeavyAddGrit")->note.find("grit") != std::string::npos,
          "Enduring Strike (HasGritActive): NOT APPLIED, the backend has no grit");
    check(view("Passive_Greataxe_Mauler_DmgOnBlock") != nullptr && !view("Passive_Greataxe_Mauler_DmgOnBlock")->applied
              && view("Passive_Greataxe_Mauler_DmgOnBlock")->note.find("OnBlockedHitTaken") != std::string::npos,
          "Passive_Greataxe_Mauler_DmgOnBlock (OnBlockedHitTaken): NOT APPLIED, with the event in the cause");
    check(view("Upgrade_Greataxe_Maelstorm_Rng") != nullptr && view("Upgrade_Greataxe_Maelstorm_Rng")->applied
              && has_kind(view("Upgrade_Greataxe_Maelstorm_Rng"), "rule of an action"),
          "Storm's Reach (no cell of a rule, the action list tests it): a rule of an action that the plan of Maelstrom applies");
    check(view("Upgrade_WarHammer_Shockwave_SizeIncrease") != nullptr && view("Upgrade_WarHammer_Shockwave_SizeIncrease")->applied
              && view("Upgrade_WarHammer_Shockwave_SizeIncrease")->note.find("WarHammer_Spell_Shockwave_L2") != std::string::npos,
          "Meteoric Crater: applied, the action of the data selects WarHammer_Spell_Shockwave_L2");
    check(view("Ability_Warhammer_MightyGavel_Upg") != nullptr && !view("Ability_Warhammer_MightyGavel_Upg")->applied
              && view("Ability_Warhammer_MightyGavel_Upg")->note.find("no plan") != std::string::npos
              && has_kind(view("Ability_Warhammer_MightyGavel_Upg"), "change of a cooldown") && has_kind(view("Ability_Warhammer_MightyGavel_Upg"), "critical hit"),
          "Justice For All: a critical chance and a change of the other cooldowns (AllExcept), NOT APPLIED yet: Mighty Gavel has no plan here");
    check(view("Passive_Greataxe_Mauler_DmgWhenFoesNear") != nullptr && view("Passive_Greataxe_Mauler_DmgWhenFoesNear")->applied
              && has_kind(view("Passive_Greataxe_Mauler_DmgWhenFoesNear"), "entities near"),
          "Center of Attention: applied, a status while entities are near");
    const std::vector<world::RuleView> table = shop.world().passives().describe_table(kGreatAxe);
    check(table.size() >= 15, "the list of a whole tree: each node of the great axe of the progression data (" + std::to_string(table.size()) + ")");
}

/** PS5: two sessions (MP-10). */
void two_sessions(const Options&) {
    PassiveShop shop(true);
    shop.enter_b();
    world::Entity& a = *shop.character();
    world::Entity* b = shop.world().find_character(kCharacterB);
    check(b != nullptr, "setup: B is in the world");
    shop.hold("TestAxe");
    world::GiveOptions options;
    (void)shop.world().inventory().give(kCharacterB, "TestAxe", 1, options);
    (void)shop.world().inventory().equip(kCharacterB, 0, world::paperdoll::kMainHand1);
    b->combat.selectedWeapon = world::paperdoll::kMainHand1;
    for (world::Entity* who : {&a, b}) {
        shop.learn("Ultimate_Greataxe_Mauler", who);
        shop.learn("Passive_Greataxe_Reaper_OnCritHeal", who);
        shop.learn("Passive_Greataxe_Reaper_HasteOnLight", who);
    }
    world::Entity* wolf = shop.wolf(1.5F);
    world::Entity* other = shop.wolf(1.5F, 1.0F);
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    check(shop.find("Status_Ultimate_Greataxe_Mauler", &a) != nullptr && shop.find("Status_Ultimate_Greataxe_Mauler", b) == nullptr,
          "a hit of A gives Mauler's Fury to A only");
    // One activation cooldown for each character: Critical Gains (ActivationCooldown 0.1) of A does not stop the heal of B.
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10, &a);
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10, b);
    a.vitals.health = a.vitals.healthMax * 0.5F;
    b->vitals.health = b->vitals.healthMax * 0.5F;
    const float aBefore = a.vitals.health;
    const float bBefore = b->vitals.health;
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    (void)shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, 0, "", false, nullptr, b);
    check(a.vitals.health > aBefore && b->vitals.health > bBefore, "Critical Gains: the critical hits of A and B in the same tick heal each one");
    // The hit counts of one attack are values of each character: one target of A and one target of B with the same attack number.
    const std::uint64_t attack = world::passive_rules::kAttackPress | 40;
    (void)shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, attack);
    (void)shop.hit(*other, "Test_Light", "TestAxe", world::HitKind::contact, attack, "", false, nullptr, b);
    check(shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight", &a) == nullptr && shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight", b) == nullptr,
          "Executioner's Speed: one target of A and one target of B with the same attack number: no haste (no shared count)");
    (void)shop.hit(*other, "Test_Light", "TestAxe", world::HitKind::contact, attack);
    check(shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight", &a) != nullptr && shop.find("Status_Passive_Greataxe_Reaper_HasteOnLight", b) == nullptr,
          "the second target of A: A gets the haste, B not");
    // A status of a target goes to each observer through the status state of the creature (the normal path).
    shop.learn("Passive_WarHammer_Juggernaut_ExhaustOnHit");
    shop.hold("TestHammer");
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    const std::size_t fromB = shop.b.got.size();
    (void)shop.hit(*other, "Warhammer_Damage_WreckingBall", "TestHammer", world::HitKind::contact, world::passive_rules::kAttackCast | 2,
                   "Ability_WarHammer_WreckingBall");
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    check(shop.creature_has(*shop.world().creatures().find(other->id), "Warhammer_Pass_ExhaustOnHit")
              && !fragments_of(shop.b, kStatusSlot, 4236, fromB).empty(),
          "the exhaust of A on a creature: B, an observer of the creature, gets its status state");
    check(damage_entries(shop.b, fromB).empty(), "B gets no damage number of a hit of A");
}

/** PS6: the load. */
void passive_load(const Options&) {
    PassiveShop shop;
    for (int index = 0; index < 4000; ++index) {
        (void)shop.world().creatures().spawn("Test_Wolf", at(5000.0F + static_cast<float>(index % 100) * 3.0F, 5000.0F + static_cast<float>(index / 100) * 3.0F), 0.0F);
    }
    std::vector<std::string> characters;
    for (int index = 0; index < 50; ++index) {
        char id[64];
        std::snprintf(id, sizeof(id), "dddddddd-2222-4000-8000-%012d", index);
        char steam[32];
        std::snprintf(steam, sizeof(steam), "765612022552%05d", 50000 + index);
        char name[32];
        std::snprintf(name, sizeof(name), "Ruler%c%c", 'a' + index / 26, 'a' + index % 26);
        const state::WorldPosition place = at(kPlace.x + static_cast<float>(index % 10) * 30.0F, kPlace.y + static_cast<float>(index / 10) * 30.0F);
        (void)add_player(shop.host.store, steam, "Load", id, name, place);
        (void)shop.world().spawn_character(id, place);
        world::Entity* character = shop.world().find_character(id);
        (void)shop.world().inventory().give(id, "TestAxe", 1, world::GiveOptions{});
        (void)shop.world().inventory().equip(id, 0, world::paperdoll::kMainHand1);
        for (const gamedata::AbilityRow& row : data_rows()) {
            shop.learn(row.id.c_str(), character);
        }
        for (int wolf = 0; wolf < 4; ++wolf) {
            (void)shop.world().creatures().spawn("Test_Wolf", at(place.x + 1.0F + static_cast<float>(wolf % 2), place.y + 1.0F + static_cast<float>(wolf / 2)), 0.0F);
        }
        characters.emplace_back(id);
    }
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t lookedBefore = shop.world().passives().counts().rulesLooked;
    const gamedata::WeaponRow* weapon = shop.host.combat.weapon_of("TestAxe");
    const std::uint32_t row = *shop.host.combat.row_index("Test_Light");
    std::uint64_t hits = 0;
    for (int round = 0; round < 40; ++round) {
        for (const std::string& id : characters) {
            world::Entity* character = shop.world().find_character(id);
            world::Entity* target = nullptr;
            shop.world().for_each_near(character->position, 3.0F, [&](world::Entity& entity) {
                if (target == nullptr && entity.kind == world::EntityKind::creature && !entity.creature.dead) {
                    target = &entity;
                }
            });
            if (target != nullptr) {
                world::HitWeapon use{"TestAxe", world::paperdoll::kMainHand1, weapon, 100, {}};
                use.attack = world::passive_rules::kAttackPress | static_cast<std::uint64_t>(round);
                (void)shop.world().combat().strike(*character, *target, use, row, 1.0F, "a load hit");
                ++hits;
            }
        }
        for (int tick = 0; tick < 10; ++tick) {
            shop.host.clock.advance(kTick);
            shop.host.tick();
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const world::PassiveCounts& counts = shop.world().passives().counts();
    const double perHit = hits > 0 ? static_cast<double>(counts.rulesLooked - lookedBefore) / static_cast<double>(hits) : 0.0;
    note("50 characters with " + std::to_string(data_rows().size()) + " learned rows each, " + std::to_string(hits) + " hits, "
         + std::to_string(counts.aroundSteps) + " steps of the rules near a character in a world of 4,200 creatures: " + std::to_string(seconds)
         + " s; rules looked at for each hit: " + std::to_string(perHit));
    check(hits >= 1000 && perHit <= static_cast<double>(data_rows().size()) && counts.aroundSteps > 0 && seconds < 5.0,
          "the evaluator looks only at the rules of the tree of the weapon of a hit (and the perks), and the steps near a character "
          "look at the position index: 50 characters in 4,200 creatures in less than 5 s");
}

/** PS7: a hit taken and a critical hit. */
void events(const Options&) {
    PassiveShop shop;
    world::Entity& own = *shop.character();
    shop.hold("TestHammer");
    shop.learn("Passive_WarHammer_Juggernaut_DmgAfterTakingDmg");
    shop.world().passives().hit_taken(own);
    const world::ActiveStatus* pain = shop.find("WarHammer_Pass_EmpowerOnDmgTaken");
    check(pain != nullptr && pain->source.id == "Passive_WarHammer_Juggernaut_DmgAfterTakingDmg",
          "Power Through Pain (OnHitTaken, NumSuccessfulHits 1): a hit taken with the hammer in the hand gives WarHammer_Pass_EmpowerOnDmgTaken");
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(2);
    // Critical Condition (Passive_Greataxe_Reaper_CritVsLowHP: OnCrit, TargetHealthPercent 30, CritChance 0.15): the roll of a hit
    // on a target at 30 percent or less.
    shop.learn("Passive_Greataxe_Reaper_CritVsLowHP");
    const float max = 1000.0F;
    const world::HitBonus low = shop.world().passives().before_hit(own, *wolf, context("Test_Light", "Light", kGreatAxe, 300.0F, max));
    const world::HitBonus high = shop.world().passives().before_hit(own, *wolf, context("Test_Light", "Light", kGreatAxe, 301.0F, max));
    check(near(low.critChance, 0.15F) && near(high.critChance, 0.0F), "Critical Condition: +0.15 critical chance at 30 percent health, none above");
    // Critical Gains (Passive_Greataxe_Reaper_OnCritHeal: OnCrit, DmgPctToHealth 0.1): a heal of 10 percent of the damage of a critical hit.
    shop.learn("Passive_Greataxe_Reaper_OnCritHeal");
    own.vitals.health = own.vitals.healthMax * 0.5F;
    const float before = own.vitals.health;
    gamedata::CombatData data = crit_combat();
    data.weapons[0].critChance = 0.0F;
    check(shop.host.combat.set(data), "setup: the axe with the chance 0");
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    check(near(own.vitals.health, before), "a hit that is not critical: no heal");
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    const world::HitResult critical = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(critical.critical.critical && near(own.vitals.health - before, critical.damage.dealt * 0.1F * shop.world().gear().heal_factor(own), 0.05F),
          "a critical hit: a heal of 10 percent of its damage (with the heal factor of the equip load)");
}

} // namespace

std::vector<Case> passive_cases() {
    return {{"PS1", "passive rules: a fixed stat, the statuses, a change of an ability, a basic attack, a cooldown, the hit counts, an action", rule_kinds},
            {"PS2", "passive rules: the blank comparator at 50 and 30 percent (Executioner, its critical node, Leaping Strike)", comparator_limits},
            {"PS3", "passive rules: a status while creatures are near (the tracking spell of the rule)", around_rules},
            {"PS4", "passive rules: the list of the rules that the evaluator applies and does not apply", rule_list},
            {"PS5", "passive rules: two sessions: the rules, cooldowns, and counts of each character (MP-10)", two_sessions},
            {"PS6", "passive rules: the load of 50 characters with their rules in a world of 4,200 creatures, with a time limit", passive_load},
            {"PS7", "passive rules: a hit taken (Power Through Pain) and a critical hit (Critical Condition, Critical Gains)", events}};
}

} // namespace soulwarden::backend::tests

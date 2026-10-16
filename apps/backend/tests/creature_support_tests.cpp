// The cases of stage 3.4f-D: the heals and the buffs of the creatures (the status actions of the slices), and the ground areas at the caster.
//
//   CRH1  two recipients and two sessions: two Clerics heal themselves (InqCleric_Rekindle_Self, the mark at 0.633 s): 8 ticks of 4.5 percent
//         of the maximum health each; each session gets the health of each Cleric and its icon; the heal of one does not change the other
//   CRH2  the cap and a duplicate: the health stops at the maximum; the same row again gives no second tick; the behavior heals at the
//         low-health part of the slice (0.15), not above it, and not again before the repeat time
//   CRH3  the buff (the Howl of the hound, the mark at 1.2 s): the damage of a hit of the hound is 1.1 times (DMG 0.1 of its type), its speed
//         1.2 times; a real hit on A; the icon for both sessions; the end at 10 s
//   CRH4  the ends: the expiry (no tick after 8 s, the icon goes for both sessions), the dispel (RemoveStatusEffectCategories Buff), the death
//         of the source (no tick, no effect), a knockdown before the mark (no status)
//   CRH5  the place rule of the client (0x679DBB0): the plague nova and the shockwave at the caster, the Radius as the distance of a cast at
//         the caster, the rotation of the caster in the Spell state (the same bytes for two sessions), a cast of an unsupported rule
//   CRH6  the loader of the creature data: the status actions and the low-health part
//   CRH9  the load: the due list of the effects with 50 characters, 50 Clerics that heal, 50 hounds that howl, 1,000 creatures with a long
//         effect, and 32,840 far spawn points; the same work with 840 points and no long effects; a time limit
// Made-up accounts, ids, names, and slices only.

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/support_fixture.h"
#include "world/combat.h"

namespace soulwarden::backend::tests {
namespace {

using namespace support;
using std::chrono::milliseconds;

/** @return The fragments of a slot and a type of the records of an entity in a session from the message `from` on. */
std::vector<Fragment> fragments_of(Support& world, const FakeClient& client, std::uint32_t session, world::EntityId id, std::uint32_t slot,
                                   std::uint32_t type, std::size_t from = 0) {
    std::vector<Fragment> found;
    const std::uint32_t interest = interest_in(world.host, session, id);
    if (interest == 0) {
        return found;
    }
    for (const Fragment& fragment : fragments_for(client, interest, from)) {
        if (fragment.slot == slot && fragment.type == type) {
            found.push_back(fragment);
        }
    }
    return found;
}

/** @return HealthAmount of a vitals state (group 0, field 0: a big-endian float), or -1. */
float health_of(const Fragment& fragment) {
    const auto found = fragment.fields.find({0, 0});
    if (found == fragment.fields.end() || found->second.size() != 4) {
        return -1.0F;
    }
    const std::uint32_t bits = (static_cast<std::uint32_t>(found->second[0]) << 24) | (static_cast<std::uint32_t>(found->second[1]) << 16)
                               | (static_cast<std::uint32_t>(found->second[2]) << 8) | found->second[3];
    return std::bit_cast<float>(bits);
}

/** @return The health of the last vitals state of a creature in a session, or -1. */
float sent_health(Support& world, const FakeClient& client, std::uint32_t session, const world::Entity& creature, std::size_t from = 0) {
    const auto states = fragments_of(world, client, session, creature.id, creature.creature.body->vitalsSlot, 15, from);
    return states.empty() ? -1.0F : health_of(states.back());
}

/** @return The bash contact of the slices of the cases (the hit of the hound). */
const gamedata::CreatureContact& bash_of(const world::Entity& creature) {
    return creature.creature.body->attacks.front().contacts.front();
}

/** CRH1: two Clerics heal themselves; two sessions. */
void two_recipients(const Options&) {
    Support world(true);
    world.enter_both();
    // The Clerics look away from A and B: no fight. The command starts the status action (the rules of the behavior).
    world::Entity* first = world.make("Test_Cleric", 6.0F, 0.0F, 270.0F);
    world::Entity* second = world.make("Test_Cleric", -6.0F, 2.0F, 90.0F);
    const float max = first->vitals.healthMax;
    check(world.host.world.creatures().set_health(first->id, 0.1F * max).error == world::CreatureError::none
              && world.host.world.creatures().set_health(second->id, 0.1F * max).error == world::CreatureError::none,
          "setup: each Cleric has 10 percent of its health");
    world.step(6);
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    const auto start = world.now();
    check(world.behavior().command_status_action(*first, "InqCleric_Rekindle_Self") && first->creature.action
              && first->creature.action->action == kSupportAction,
          "the first Cleric starts its status action InqCleric_Rekindle_Self (the action of the data)");
    world.to(start + milliseconds(400));
    check(world.behavior().command_status_action(*second, ""), "the second Cleric starts its status action 0.4 s later");
    world.to(start + milliseconds(580));
    note("time from the start: " + std::to_string(std::chrono::duration_cast<milliseconds>(world.now() - start).count()) + " ms");
    check(Support::effect(*first, kRekindle) == nullptr, "no heal before the mark of the data (0.633 s)");
    world.to(start + milliseconds(700));
    const world::CreatureEffect* heal = Support::effect(*first, kRekindle);
    check(heal != nullptr && Support::effect(*second, kRekindle) == nullptr && heal->source == first->id,
          "the mark (0.633 s) gives AI_InqCleric_Rekindle to the Cleric itself (its source); the second Cleric has none yet");
    world.to(start + milliseconds(1200));
    const float afterOne = first->vitals.health;
    check(std::fabs(afterOne - (0.1F + kHealPart) * max) < 0.5F && std::fabs(second->vitals.health - (0.1F + kHealPart) * max) < 0.5F,
          "the first tick (the next tick of the world after the mark): 4.5 percent of the maximum health for each Cleric");
    world.to(start + milliseconds(9500));
    const float expected = (0.1F + 8.0F * kHealPart) * max;
    note("the first Cleric: " + std::to_string(first->vitals.health) + " of " + std::to_string(max) + " (expected " + std::to_string(expected)
         + "); ticks " + std::to_string(world.counts().effectTicks) + ", heals " + std::to_string(world.counts().heals));
    check(std::fabs(first->vitals.health - expected) < 1.0F && std::fabs(second->vitals.health - expected) < 1.0F
              && world.counts().effectTicks == 16 && world.counts().heals == 16,
          "8 ticks of 4.5 percent for each Cleric (8 s, TickRate 1 s): 10 to 46 percent; 16 ticks, 16 heals");
    world.step(4);
    // Each session gets the vitals state of each Cleric with its health (the observer view: the creature vitals of the slice), and the
    // status state with the icon of the heal (ShowInNameplates).
    for (const world::Entity* cleric : {first, second}) {
        const float seenA = sent_health(world, world.a, 1, *cleric, fromA);
        const float seenB = sent_health(world, world.b, 2, *cleric, fromB);
        check(std::fabs(seenA - cleric->vitals.health) < 0.01F && std::fabs(seenB - cleric->vitals.health) < 0.01F,
              "A and B get the health of each Cleric after its heal (" + std::to_string(seenA) + ", " + std::to_string(seenB) + ")");
        check(!fragments_of(world, world.a, 1, cleric->id, kStatusSlot, 4236, fromA).empty()
                  && !fragments_of(world, world.b, 2, cleric->id, kStatusSlot, 4236, fromB).empty(),
              "A and B get the status state of each Cleric (the icon of the heal at its name plate)");
    }
    // The tick status of the row (OnTickStatusEffect, KeepOnTickEffectOnEnd): it stays after the end of the heal.
    check(Support::effect(*first, kRampant) != nullptr && Support::effect(*first, kRekindle) == nullptr,
          "the heal ended at 8 s; the status of its ticks (OnTickStatusEffect, KeepOnTickEffectOnEnd) stays");
}

/** CRH2: the cap, a duplicate, and the rules of the behavior. */
void cap_and_duplicate(const Options&) {
    Support world(true);
    world.enter_both();
    world::Entity* cleric = world.make("Test_Cleric", 6.0F, 0.0F, 270.0F);
    const float max = cleric->vitals.healthMax;
    (void)world.host.world.creatures().set_health(cleric->id, 0.95F * max);
    world.step(3);
    const auto start = world.now();
    check(world.behavior().command_status_action(*cleric, ""), "the Cleric at 95 percent starts its heal (a command)");
    world.to(start + milliseconds(1700));
    check(cleric->vitals.health == max, "the cap: the second tick takes the health to its maximum and not above it");
    // A duplicate: the same row again while it runs gives no second effect and no second tick.
    const std::uint64_t ticks = world.counts().effectTicks;
    const gamedata::StatusRow* row = world.host.abilities.status(kRekindle);
    check(row != nullptr && world.host.world.creatures().give_status(*cleric, *row, cleric->id), "the same row again (a duplicate)");
    world.to(start + milliseconds(2750));
    const auto count = std::count_if(cleric->creature.effects.begin(), cleric->creature.effects.end(),
                                     [](const world::CreatureEffect& effect) { return effect.status == kRekindle; });
    check(count == 1 && world.counts().effectTicks == ticks + 1,
          "a duplicate keeps one effect (StackMax 1, the later end) and one tick in each second");
    // The behavior: a Cleric in a fight above the low-health part does not heal; at it, it heals.
    world::Entity* fighter = world.make("Test_Cleric", 5.0F, 3.0F, 90.0F);
    (void)world.host.world.creatures().set_health(fighter->id, 0.5F * max);
    world.behavior().hurt_by(*fighter, world.character()->id);
    const std::uint64_t before = world.behavior().counts().statusActions;
    world.to(world.now() + milliseconds(3000));
    check(world.behavior().counts().statusActions == before && Support::effect(*fighter, kRekindle) == nullptr,
          "a Cleric in a fight at 50 percent (above 0.15 of the slice) starts no heal");
    (void)world.host.world.creatures().set_health(fighter->id, 0.1F * max);
    const auto low = world.now();
    check(world.until([&] { return world.behavior().counts().statusActions == before + 1; }, 80),
          "at 10 percent (not above the low-health part 0.15 of the slice) it starts its heal in its fight");
    world.to(low + milliseconds(10500));
    check(Support::effect(*fighter, kRekindle) == nullptr, "the heal ended (8 s)");
    (void)world.host.world.creatures().set_health(fighter->id, 0.1F * max);
    world.to(low + milliseconds(15000));
    check(world.behavior().counts().statusActions == before + 1, "no second heal before the repeat time (20 s from the start of the first)");
    const bool second = world.until([&] { return world.behavior().counts().statusActions == before + 2; }, 450);
    note("the fighter: health " + std::to_string(fighter->vitals.health) + ", mode " + std::to_string(static_cast<int>(fighter->creature.mind.mode))
         + ", dead " + std::to_string(fighter->creature.dead) + ", seconds from the low health "
         + std::to_string(std::chrono::duration_cast<milliseconds>(world.now() - low).count()));
    check(second, "a second heal after the repeat time");
}

/** CRH3: the buff of the hound and its effect on a hit. */
void buff(const Options&) {
    Support world(true);
    world.enter_both();
    world::Entity& a = *world.character();
    world::Entity* hound = world.make("Test_Hound", 1.6F, 0.0F, 270.0F);
    world.step(3);
    const float plain = world.host.world.combat().creature_damage(*hound, bash_of(*hound), a).dealt;
    const float speed = hound->creature.speed_factor(world.now());
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    const auto start = world.now();
    check(world.behavior().command_status_action(*hound, "Howl"), "the hound starts its status action Howl");
    world.to(start + milliseconds(1150));
    check(Support::effect(*hound, kHaste) == nullptr, "no buff before the mark ApplyHaste (1.2 s)");
    world.to(start + milliseconds(1250));
    const float buffed = world.host.world.combat().creature_damage(*hound, bash_of(*hound), a).dealt;
    note("the bash of the hound: " + std::to_string(plain) + " with no buff, " + std::to_string(buffed) + " with the buff");
    check(Support::effect(*hound, kHaste) != nullptr && std::fabs(buffed - plain * (1.0F + kHastePart)) < 0.01F,
          "the mark gives the buff to the hound: the damage of its hit (the type Strike) is 1.1 times (DMG 0.1 of the type)");
    check(std::fabs(hound->creature.speed_factor(world.now()) - speed * (1.0F + kHasteSpeed)) < 0.001F,
          "its speed is 1.2 times (MoveSpeedMod 0.2)");
    world.step(4);
    check(!fragments_of(world, world.a, 1, hound->id, kStatusSlot, 4236, fromA).empty()
              && !fragments_of(world, world.b, 2, hound->id, kStatusSlot, 4236, fromB).empty(),
          "A and B get the status state of the hound (the icon of the buff)");
    // A real hit: the hound takes A as its target and bashes it; the loss of A is the damage with the buff.
    world.to(start + milliseconds(2200));
    world.behavior().hurt_by(*hound, a.id);
    const float health = a.vitals.health;
    check(world.until([&] { return a.vitals.health < health; }, 120), "the hound bashes A");
    note("the loss of A: " + std::to_string(health - a.vitals.health));
    check(std::fabs((health - a.vitals.health) - buffed) < 0.05F, "the loss of A is the damage of the bash with the buff");
    world.to(start + milliseconds(1250 + 10050));
    check(Support::effect(*hound, kHaste) == nullptr
              && std::fabs(world.host.world.combat().creature_damage(*hound, bash_of(*hound), a).dealt - plain) < 0.01F
              && std::fabs(hound->creature.speed_factor(world.now()) - speed) < 0.001F,
          "the end at 10 s (BaseDuration): the damage and the speed of the hound are as before");
}

/** CRH4: the expiry, the dispel, the death of the source, and a knockdown before the mark. */
void ends(const Options&) {
    Support world(true);
    world.enter_both();
    world::Entity* cleric = world.make("Test_Cleric", 6.0F, 0.0F, 270.0F);
    world::Entity* hound = world.make("Test_Hound", -6.0F, 0.0F, 90.0F);
    const float max = cleric->vitals.healthMax;
    (void)world.host.world.creatures().set_health(cleric->id, 0.1F * max);
    world.step(3);
    // The expiry: no tick after the end, and the icon goes for both sessions.
    const auto start = world.now();
    (void)world.behavior().command_status_action(*cleric, "");
    world.to(start + milliseconds(9000));
    const std::uint64_t ticks = world.counts().effectTicks;
    const float health = cleric->vitals.health;
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    world.to(start + milliseconds(12000));
    check(Support::effect(*cleric, kRekindle) == nullptr && world.counts().effectTicks == ticks && cleric->vitals.health == health && ticks == 8,
          "the expiry at 8 s: 8 ticks, no tick after it");
    (void)fromA;
    (void)fromB;
    // The dispel: a row with RemoveStatusEffectCategories Buff ends the buff of the hound, and not the tick status of the Cleric.
    (void)world.behavior().command_status_action(*hound, "Howl");
    world.to(world.now() + milliseconds(1300));
    const gamedata::StatusRow* nullify = world.host.abilities.status(kNullify);
    check(Support::effect(*hound, kHaste) != nullptr && nullify != nullptr, "setup: the hound has its buff");
    (void)world.host.world.creatures().give_status(*hound, *nullify, world.character()->id);
    (void)world.host.world.creatures().give_status(*cleric, *nullify, world.character()->id);
    check(Support::effect(*hound, kHaste) == nullptr && world.counts().dispelled == 1 && Support::effect(*cleric, kRampant) != nullptr,
          "the dispel (category Buff) ends the buff of the hound (one effect); the RampantFlame of the Cleric (not Buff) stays");
    // The death of the source: a Cleric that dies in its heal gets no tick more, and its effects end.
    world::Entity* dying = world.make("Test_Cleric", 0.0F, 6.0F, 0.0F);
    (void)world.host.world.creatures().set_health(dying->id, 0.1F * max);
    const auto again = world.now();
    (void)world.behavior().command_status_action(*dying, "");
    world.to(again + milliseconds(2800));
    const std::uint64_t before = world.counts().effectTicks;
    check(Support::effect(*dying, kRekindle) != nullptr, "setup: the third Cleric heals");
    (void)world.host.world.creatures().kill(dying->id);
    world.to(again + milliseconds(6000));
    check(dying->creature.dead && dying->creature.effects.empty() && world.counts().effectTicks == before && dying->vitals.health == 0.0F,
          "the death of the source (the Cleric itself): its effects end (RemoveOnDeath), no tick and no heal after it");
    // A knockdown before the mark: the action is replaced, and the mark gives no status.
    world::Entity* knocked = world.make("Test_Hound", 0.0F, -6.0F, 180.0F);
    const std::uint64_t cancelled = world.behavior().counts().statusMarksCancelled;
    const auto howl = world.now();
    (void)world.behavior().command_status_action(*knocked, "Howl");
    world.to(howl + milliseconds(500));
    check(world.host.world.creatures().knock(*knocked, 4, milliseconds(2000)), "a knockdown 0.5 s after the start of the Howl");
    world.to(howl + milliseconds(1500));
    check(Support::effect(*knocked, kHaste) == nullptr && world.behavior().counts().statusMarksCancelled == cancelled + 1,
          "the knockdown replaced the action: its mark gives no buff");
}

/** CRH5: the place rule of a cast of an AI (client code 0x679DBB0). */
void caster_place(const Options&) {
    Support world(true);
    world.enter_both();
    world::Entity& a = *world.character();
    world::Behavior& rules = world.behavior();
    const gamedata::AreaCast& nova = world.host.creatures.slices()[2].areas.front().casts.front();
    const gamedata::AreaCast& wave = world.host.creatures.slices()[3].areas.front().casts.front();
    const gamedata::AreaCast mushroom = area::bomb_cast(1.2F);
    check(rules.place_rule(nova) == world::CastPlace::caster && rules.place_rule(wave) == world::CastPlace::caster
              && rules.place_rule(mushroom) == world::CastPlace::aim,
          "the rule: the nova (AIAimAtTarget, UseAllAvailableTargets, no ranged attack data) and the shockwave (AIAimAtTarget false) at "
          "the caster; the mushroom (with ranged attack data) at the aim point");
    check(std::fabs(rules.area_range(nova) - kNovaRadius) < 0.001F && std::fabs(rules.area_range(wave) - kWaveRadius) < 0.001F
              && std::fabs(rules.area_range(mushroom) - 30.0F) < 0.001F,
          "the distance of a cast at the caster is the Radius of its row (CastDistance 0); of a cast at a target its CastDistance");
    gamedata::AreaCast shot = wave;
    shot.spell = kShotSpell;
    gamedata::AreaCast single = nova;
    single.allTargets = false;
    check(rules.place_rule(shot) == world::CastPlace::unsupported && rules.place_rule(single) == world::CastPlace::target,
          "AIAimAtTarget false with ranged attack data: a shot of the data (no rule of the backend); AIAimAtTarget alone: the target");
    // The plague nova: the area at the ghost, and its hit on A in its sphere (3 m).
    world::Entity* ghost = world.make("Test_Ghost", 3.0F, 0.0F, 90.0F);
    world.step(3);
    const auto start = world.now();
    const float health = a.vitals.health;
    check(rules.command_area(*ghost, a.id), "the ghost casts its plague nova at A");
    world.to(start + milliseconds(1000));
    const std::vector<world::EntityId> strikes = world.areas_of(world::SpellRole::strike);
    const world::Entity* area = strikes.size() == 1 ? world.host.world.find(strikes.front()) : nullptr;
    check(area != nullptr && world::horizontal_distance(area->position, ghost->position) < 0.05F
              && world::horizontal_distance(area->position, a.position) > 2.9F && rules.counts().castsAtCaster == 1,
          "the area is at the ghost (the caster), not at A (3 m away)");
    world.to(start + milliseconds(1500));
    check(a.vitals.health < health && world.spells().counts().strikeHits >= 1, "A in the sphere of 4 m takes the hit of the window (0 to 0.5 s)");
    // The shockwave: at the knight, with its rotation (yaw 90) in the Spell state, the same bytes for A and B.
    world::Entity* knight = world.make("Test_Knight", -2.0F, 0.0F, 270.0F);
    world.step(3);
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    const auto wave_start = world.now();
    check(rules.command_area(*knight, a.id), "the knight casts its shockwave");
    world.to(wave_start + milliseconds(1100));
    world.step(3);
    world::Entity* waveArea = nullptr;
    for (const world::EntityId id : world.areas_of(world::SpellRole::strike)) {
        world::Entity* entity = world.host.world.find(id);
        if (entity != nullptr && entity->spell.caster == knight->id) {
            waveArea = entity;
        }
    }
    check(waveArea != nullptr && world::horizontal_distance(waveArea->position, knight->position) < 0.05F && waveArea->spell.yaw
              && std::fabs(*waveArea->spell.yaw - knight->creature.yaw) < 0.01F,
          "the shockwave is at the knight, with the direction of the knight");
    const auto statesA = waveArea != nullptr ? fragments_of(world, world.a, 1, waveArea->id, kWaveSpellSlot, 2912, fromA) : std::vector<Fragment>{};
    const auto statesB = waveArea != nullptr ? fragments_of(world, world.b, 2, waveArea->id, kWaveSpellSlot, 2912, fromB) : std::vector<Fragment>{};
    const auto rotation = statesA.empty() ? Bytes{} : statesA.front().fields.at({0, 2});
    check(statesA.size() == 1 && statesB.size() == 1 && statesA.front().fields == statesB.front().fields && rotation.size() == 3
              && rotation[0] == 0x03,
          "A and B get the same Spell state; its rotation is the yaw of the knight (header 03: x and y are 0, then z; not the identity 07)");
}

/** CRH6: the loader of the creature data (status actions, the low-health part). */
void loaders(const Options&) {
    const auto load = [](const std::function<void(gamedata::CreatureSlice&)>& change) {
        std::vector<gamedata::CreatureSlice> slices = support_slices();
        change(slices[0]);
        gamedata::CreatureData data;
        return data.set_full(slices, support_kinds(), {}, archer_levels(), {}, {arrow_slice()}, std::nullopt);
    };
    check(load([](gamedata::CreatureSlice&) {}), "control: the slices of the cases load");
    check(load([](gamedata::CreatureSlice& slice) {
              slice.statusActions.clear();
              slice.lowHealth.reset();
          }),
          "a file of an older tool (no status action, no low-health part) loads: no creature heals");
    check(!load([](gamedata::CreatureSlice& slice) { slice.statusActions[0].marks[0].time = 2.0F; }), "a mark after the end of its action is refused");
    check(!load([](gamedata::CreatureSlice& slice) { slice.statusActions[0].action = 99; }), "a status action of an action that is not there is refused");
    check(!load([](gamedata::CreatureSlice& slice) { slice.statusActions[0].marks[0].statuses.clear(); }), "a mark with no status is refused");
    check(!load([](gamedata::CreatureSlice& slice) { slice.lowHealth = 1.5F; }), "a low-health part above 1 is refused");
}

/** CRH9: the load of the effects of the creatures. */
void support_load(const Options&) {
    const auto points = [](std::size_t count) {
        std::vector<gamedata::SpawnPoint> list;
        for (std::size_t index = 0; index < count; ++index) {
            gamedata::SpawnPoint point;
            point.id = "p" + std::to_string(index);
            point.kind = 0;
            point.level = 1;
            point.position = state::WorldPosition{10000.0F + static_cast<float>(index % 200) * 20.0F,
                                                  10000.0F + static_cast<float>(index / 200) * 20.0F, kHeight};
            point.ground = kHeight;
            list.push_back(point);
        }
        return list;
    };
    struct Result {
        double milliseconds{0.0};
        world::CreatureCounts counts;
        std::uint64_t statusActions{0};
        std::size_t due{0};
    };
    const auto run = [&](std::size_t count, unsigned idle) {
        ServerHost host;
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        check(host.creatures.set_full(support_slices(), support_kinds(), points(count), archer_levels(), {}, {arrow_slice()},
                                      gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}),
              "setup: the slices with " + std::to_string(count) + " spawn points");
        check(host.combat.set(shop::shop_combat()) && host.abilities.set(support_abilities()) && host.spells.set(support_spells()),
              "setup: the combat tables, the ability data, and the spell data");
        stamina::set_stamina(host);
        const unsigned kPlayers = 50;
        for (unsigned index = 0; index < kPlayers; ++index) {
            char id[40];
            std::snprintf(id, sizeof(id), "%08x-3333-4000-8000-%012x", 0x40000000U + index, index);
            add_player(host.store, "765612022553" + std::to_string(40000 + index), "Tester " + std::to_string(index), id,
                       "Heal" + std::to_string(index), std::nullopt);
            const state::WorldPosition place{500.0F + 120.0F * static_cast<float>(index % 10), 500.0F + 120.0F * static_cast<float>(index / 10),
                                             kHeight};
            (void)host.world.spawn_character(id, place);
            world::Entity* character = host.world.find_character(id);
            // A Cleric at 10 percent and a hound near each character, in a fight with it: the Cleric heals, the hound howls (again and again).
            world::Entity* cleric = host.world.creatures().spawn("Test_Cleric", {place.x + 8.0F, place.y, kHeight}, 90.0F).entity;
            world::Entity* hound = host.world.creatures().spawn("Test_Hound", {place.x - 8.0F, place.y, kHeight}, 270.0F).entity;
            (void)host.world.creatures().set_health(cleric->id, 0.1F * cleric->vitals.healthMax);
            host.world.creatures().behavior().hurt_by(*cleric, character->id);
            host.world.creatures().behavior().hurt_by(*hound, character->id);
        }
        // Creatures far from each character with a long effect (600 s): one due entry each, which does not come in the run.
        const gamedata::StatusRow* haste = host.abilities.status(kHaste);
        for (unsigned index = 0; index < idle; ++index) {
            world::Entity* far = host.world.creatures()
                                     .spawn("Test_Hound", {3000.0F + static_cast<float>(index % 40) * 5.0F, 3000.0F + static_cast<float>(index / 40) * 5.0F, kHeight}, 0.0F)
                                     .entity;
            (void)host.world.creatures().give_status(*far, *haste, 0, milliseconds(600000));
        }
        Result result;
        const auto started = std::chrono::steady_clock::now();
        for (int tick = 0; tick < 1000; ++tick) {
            host.clock.advance(std::chrono::milliseconds(33));
            host.world.tick();
        }
        result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        result.counts = host.world.creatures().counts();
        result.statusActions = host.world.creatures().behavior().counts().statusActions;
        result.due = host.world.creatures().effect_due_count();
        return result;
    };
    const Result full = run(32840, 1000);
    const Result few = run(840, 0);
    note("32,840 points, 50 characters, 50 Clerics, 50 hounds, 1,000 far creatures with a long effect: 1000 ticks need "
         + std::to_string(full.milliseconds) + " ms; " + std::to_string(full.statusActions) + " status actions, "
         + std::to_string(full.counts.effectTicks) + " ticks, " + std::to_string(full.counts.heals) + " heals, "
         + std::to_string(full.counts.effectDueTaken) + " due entries taken, " + std::to_string(full.counts.effectsLooked)
         + " effects looked at, " + std::to_string(full.due) + " entries waiting; with 840 points and no far effect: "
         + std::to_string(few.milliseconds) + " ms, " + std::to_string(few.counts.effectDueTaken) + " due entries, "
         + std::to_string(few.counts.effectsLooked) + " looks");
    check(full.statusActions >= 100 && full.counts.effectTicks >= 50 * 8 && full.counts.heals >= 50 * 8,
          "each Cleric heals (8 ticks or more each) and each hound howls");
    check(full.counts.effectDueTaken == few.counts.effectDueTaken && full.counts.effectsLooked == few.counts.effectsLooked
              && full.counts.effectTicks == few.counts.effectTicks && full.statusActions == few.statusActions,
          "the work depends on the due entries, not on the far spawn points and not on the creatures whose effects are not due: the same "
          "entries and looks with 840 points and no far effect");
    check(full.due >= 1000, "the far creatures each wait in the due list (their end), with no look in the run");
    check(full.milliseconds < 3000.0, "1000 ticks with 100 creatures that heal and howl and 1,000 long effects need less than 3 s");
}

} // namespace

std::vector<Case> support_cases() {
    return {{"CRH1", "creature heals (3.4f-D): two Clerics heal themselves; the health and the icon of each for two sessions", two_recipients},
            {"CRH2", "creature heals (3.4f-D): the cap, a duplicate, the low-health part, the repeat", cap_and_duplicate},
            {"CRH3", "creature buffs (3.4f-D): the Howl of the hound: DMG 0.1 of a hit, the speed, a real hit, the icon, the end", buff},
            {"CRH4", "creature heals and buffs (3.4f-D): the expiry, the dispel, the death of the source, a knockdown before the mark", ends},
            {"CRH5", "areas at the caster (3.4f-D): the place rule of the client, the Radius, the rotation of the caster, two sessions", caster_place},
            {"CRH6", "creature heals and buffs (3.4f-D): the loader of the status actions and of the low-health part", loaders},
            {"CRH9", "creature heals and buffs (3.4f-D): the due list of the effects, far points and far effects, a time limit", support_load}};
}

} // namespace soulwarden::backend::tests

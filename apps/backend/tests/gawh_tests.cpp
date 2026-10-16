// Cases for the other abilities of the great axe and the war hammer (stage 3.3b-M2): the fragments of the abilities in the sequence of
// the character (Reap and its pull, Execute, Whirlwind and its exit by the hits, Charge and its transitions, Mighty Gavel, Armor Breaker,
// Clear Out and its push), the start of an ability in a fragment that plays, the status effects of the damage rows, the spells of an
// action, of a fragment, and of a rule, the shield of Maelstrom, the fortify in the well, Guarded Sprint, the list of the nodes, two
// sessions, the load, and the loaders of the new file formats. The graphs have the form and the times of the data of the game with
// made-up rows and values (support/gawh_fixture.h): no file of the game and no value of a capture is here.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/gawh_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gawh;
using world::CastVerdict;
using json = nlohmann::json;

float away(const world::Entity& one, const world::Entity& other) {
    return world::horizontal_distance(one.position, other.position);
}

/** The amounts of the damage numbers that a client got from the message `from` on (the first entry of each number). */
std::vector<float> amounts(const FakeClient& client, std::size_t from) {
    std::vector<float> values;
    for (const auto& entries : damage_entries(client, from)) {
        if (!entries.empty()) {
            values.push_back(entries.front().second);
        }
    }
    return values;
}

/** GH1: Reap: the hit of its first strike in its window, the pull of its second strike on the creatures that the cast hit, the cooldown. */
void reap_pull(const Options&) {
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        world::Entity* hit = shop.wolf(-2.5F);
        world::Entity* other = shop.wolf(-4.0F, 1.0F);
        const state::WorldPosition otherBefore = other->position;
        const std::uint64_t started = shop.host.world.creatures().forced().counts().started;
        const std::size_t from = shop.a.got.size();
        const std::uint16_t pressed = shop.press(0);
        check(shop.accepted() == 1 && !shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_JudgementsReach",
              "the press of Reap starts the fragment of its action (Ability_GreatAxe_JudgementsReach) in the sequence of A");
        check(shop.contact_at(static_cast<std::uint16_t>(pressed + 3), *hit) == 0,
              "a contact 3 steps after the press (before the window of the first strike, 0.3 s): no hit");
        check(shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *hit) == 1, "a contact in the window of the first strike: one hit");
        check(shop.contact_at(static_cast<std::uint16_t>(pressed + 9), *hit) == 1, "a second contact of the same strike on the same wolf: no second hit");
        shop.until(static_cast<std::uint16_t>(pressed + 15));
        check(shop.abilities().counts().follows == 1 && shop.host.world.creatures().forced().counts().started == started + 1,
              "at the time of the second strike (0.433 s, no damage, no contact of the client) the wolf that the cast hit moves");
        shop.pass(2.0F);
        note("the wolf of the hit is " + std::to_string(away(*shop.character(), *hit)) + " m from A, the other wolf moved "
             + std::to_string(world::horizontal_distance(other->position, otherBefore)) + " m; numbers " + std::to_string(amounts(shop.a, from).size()));
        check(near(away(*shop.character(), *hit), 1.5F, 0.1F), "the row of the nearest shape (1.2 m: _Pull, 1 m to A): the wolf is 1.5 m from A");
        check(near(world::horizontal_distance(other->position, otherBefore), 0.0F, 0.001F),
              "a wolf out of the shape of the pull row (1.2 m with no node) does not move");
        check(amounts(shop.a, from).size() == 1, "one damage number: the pull row does no damage");
        shop.press(0);
        check(shop.refused(CastVerdict::cooldown) == 1, "a second press in the cooldown of the data (10 s): refused");
    }
    {
        // The Collector: the longer reach of the first strike, and the rows of the pull for each shape (2.7 m: 2 m, 4.3 m: 3 m).
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.learn({kCollector});
        world::Entity* middle = shop.wolf(-2.6F);
        world::Entity* end = shop.wolf(-4.3F, 0.5F);
        const std::uint16_t pressed = shop.press(0);
        shop.until(static_cast<std::uint16_t>(pressed + 8));
        shop.step({}, {touch(*middle), touch(*end)});
        check(shop.abilities().counts().strikeHits == 2, "with The Collector both wolves (2.6 m and 4.3 m) are in the reach of the first strike");
        shop.pass(2.5F);
        note("The Collector: the wolves are " + std::to_string(away(*shop.character(), *middle)) + " m and "
             + std::to_string(away(*shop.character(), *end)) + " m from A");
        check(near(away(*shop.character(), *middle), 0.6F, 0.12F) && near(away(*shop.character(), *end), 1.33F, 0.12F),
              "each wolf has the pull row of its nearest shape: _upgrade (2 m) at 2.7 m, _upgrade2 (3 m) at 4.3 m");
    }
    {
        // A wolf in a shape of the pull that the client did not touch (the client reports no contact for the pull: the server tests the
        // shapes), and a wolf behind the character.
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.learn({kCollector});
        world::Entity* ahead = shop.wolf(-4.3F);
        world::Entity* behind = shop.wolf(4.0F);
        const state::WorldPosition behindBefore = behind->position;
        shop.press(0);
        shop.pass(2.5F);
        note("no contact: the wolf in front is " + std::to_string(away(*shop.character(), *ahead)) + " m from A");
        check(shop.abilities().counts().strikeHits == 0 && near(away(*shop.character(), *ahead), 1.3F, 0.12F),
              "the wolf in the shape at 4.3 m (in front, the direction of the press) is pulled 3 m with no contact of the client");
        check(near(world::horizontal_distance(behind->position, behindBefore), 0.0F, 0.001F), "the wolf behind the character does not move");
    }
}

/** GH2: the status effect of a damage row (format 3): the root of Gravity on the pull rows, ApplicationCooldown, OnEndStatusEffect. */
void gravity_root(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    shop.learn({kGravity});
    world::Entity* wolf = shop.wolf(-2.5F);
    const std::uint16_t pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *wolf);
    shop.until(static_cast<std::uint16_t>(pressed + 14));
    check(shop.creature_has(*wolf, kGravityRoot), "Gravity: the pull row _RP2 gives the status of its row (Status_Passive_GreatAxe_Gravity_Root)");
    shop.pass(0.45F);
    check(shop.creature_has(*wolf, kGravityHold) && !shop.creature_has(*wolf, kGravityRoot),
          "at the end of the status (0.33 s) its OnEndStatusEffect comes: the root of 0.4 s");
    // ApplicationCooldown 1 s: one application for each creature in that time (the rows of the next pulls).
    const gamedata::DamageRow& row = shop.host.combat.damage_row(*shop.host.combat.row_index("GreatAxe_JudgementsReach_Pull_RP2"));
    shop.pass(1.2F);
    std::uint64_t effects = shop.abilities().counts().effects;
    shop.abilities().row_effects(*wolf, row, 0.0001F, shop.character()->id);
    shop.abilities().row_effects(*wolf, row, 0.0001F, shop.character()->id);
    check(shop.abilities().counts().effects == effects + 1, "two pulls in 1 s give the status one time (ApplicationCooldown of the status row)");
    shop.pass(1.1F);
    effects = shop.abilities().counts().effects; // With the end status of the first one (the root).
    shop.abilities().row_effects(*wolf, row, 0.0F, shop.character()->id);
    check(shop.abilities().counts().effects == effects, "EffectOnlyOnDamage: a hit with no damage gives no status");
    shop.abilities().row_effects(*wolf, row, 0.0001F, shop.character()->id);
    check(shop.abilities().counts().effects == effects + 1, "after the cooldown a hit with damage gives the status again");
}

/** GH3: the selection of the fragment of an action by a node (Fatal Attraction): the strikes of the selected fragment. */
void fatal_attraction(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    shop.learn({kFatalAttraction});
    world::Entity* wolf = shop.wolf(-1.5F);
    const std::size_t from = shop.a.got.size();
    const std::uint16_t pressed = shop.press(0);
    check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_JudgementsReach_Upgrade",
          "with Fatal Attraction the action selects its fragment Ability_GreatAxe_JudgementsReach_Upgrade (ChangeActionFragment)");
    shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *wolf);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 29), *wolf);
    const std::vector<float> values = amounts(shop.a, from);
    note("Fatal Attraction: " + std::to_string(values.size()) + " numbers");
    check(values.size() == 2 && near(values[1] / values[0], 1.15F / 0.8F, 0.002F),
          "two hits: the first strike (the row of Reap, 0.8) and the third strike at 0.967 s (the row _FA, 1.15)");
}

/** GH4: Execute: its strike, its rule (BaseDamage 0.5 below 50 % health, the ability row of the data), and a press that no window takes. */
void execute(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Executioner", "Ability_Greataxe_Reap", nullptr});
    world::Entity* full = shop.wolf(-1.5F, 0.5F);
    world::Entity* low = shop.wolf(-1.5F, -0.5F);
    low->vitals.health = low->vitals.healthMax * 0.4F;
    const std::size_t from = shop.a.got.size();
    const std::uint16_t pressed = shop.press(0);
    shop.until(static_cast<std::uint16_t>(pressed + 4));
    shop.press(1);
    check(shop.refused(CastVerdict::casting) == 1,
          "a press of Reap 4 steps into Execute (no window of Reap, the mark CanTransition at 1.533 s): refused, the client does not play it");
    shop.until(static_cast<std::uint16_t>(pressed + 20));
    shop.step({}, {touch(*full), touch(*low)});
    const auto numbers = damage_entries(shop.a, from);
    note("Execute: " + std::to_string(numbers.size()) + " numbers");
    check(numbers.size() == 2, "the contact in the window of the strike (0.667 s) hits both wolves");
    if (numbers.size() == 2) {
        check(near(numbers[1].front().second / numbers[0].front().second, 1.5F, 0.002F),
              "the wolf below 50 % health takes 1.5 times the damage (BaseDamage 0.5 of the rule of the data)");
    }
}

/** GH5: a press of an ability in a buffered window of the fragment that plays starts its fragment at the end of the window. */
void buffered_start(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    world::Entity* wolf = shop.wolf(-1.5F);
    shop.step({kActionAttack});
    const std::uint16_t released = shop.step({kActionAttackRelease});
    check(!shop.visits().empty() && shop.visits().back() == "Attack_Primary", "setup: the click starts the light swing at the release");
    const std::uint64_t later = shop.abilities().counts().delayedStarts;
    shop.press(0);
    const world::AttackSequences::State* state = shop.host.world.combat().sequences().state_of(kCharacterA);
    const world::AttackVisit* reap = state != nullptr && !state->visits.empty() ? &state->visits.back() : nullptr;
    check(shop.abilities().counts().delayedStarts == later + 1 && reap != nullptr
              && state->actions->fragments[reap->fragment].name == "Ability_GreatAxe_JudgementsReach"
              && reap->start == static_cast<std::uint16_t>(released + 2),
          "the press at 0.067 s (the buffered window to 0.1 s) starts Reap at the end of the window: 2 steps after the swing started");
    const std::uint16_t start = reap != nullptr ? reap->start : 0;
    check(shop.contact_at(static_cast<std::uint16_t>(start + 8), *wolf) == 1, "the strike of Reap has its window from the start of its fragment");
}

/** GH6: Whirlwind: the spins of its fragment, and the exit by the hits of the cast (Unending Winds: IfAbilityNumHits). */
void whirlwind(const Options&) {
    const std::uint16_t spins[] = {15, 27, 38, 48, 58, 69, 80};
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        for (int spin = 0; spin < 4; ++spin) {
            shop.contact_at(static_cast<std::uint16_t>(pressed + spins[spin]), *wolf);
        }
        shop.until(static_cast<std::uint16_t>(pressed + 60));
        note("Whirlwind: the claims " + claim_counts(shop.host));
        check(shop.abilities().counts().strikeHits == 4, "the fragment of the data: four spins, one hit in each");
        check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_Whirlwind_Exit_Short",
              "after 1.967 s the fragment goes to its exit (a transition with no condition)");
    }
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        shop.learn({kUnendingWinds});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_Whirlwind_Upgrade", "Unending Winds selects the long fragment");
        shop.until(static_cast<std::uint16_t>(pressed + 58));
        const world::AttackVisit* exit = shop.host.world.combat().sequences().newest(kCharacterA);
        check(shop.visits().back() == "Ability_GreatAxe_Whirlwind_Exit_Short" && exit != nullptr && exit->start == static_cast<std::uint16_t>(pressed + 57),
              "no hit in the first four spins: the exit at 1.933 s (IfAbilityNumHits LessThan 1)");
        check(shop.contact_at(static_cast<std::uint16_t>(pressed + 60), *wolf) == 0, "a contact at the time of the fifth spin after the exit: no hit");
    }
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        shop.learn({kUnendingWinds});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        for (const std::uint16_t spin : spins) {
            shop.contact_at(static_cast<std::uint16_t>(pressed + spin), *wolf);
        }
        shop.until(static_cast<std::uint16_t>(pressed + 92));
        const world::AttackVisit* exit = shop.host.world.combat().sequences().newest(kCharacterA);
        note("Unending Winds with hits: " + std::to_string(shop.abilities().counts().strikeHits) + " hits, the exit at step "
             + std::to_string(exit != nullptr ? static_cast<std::uint16_t>(exit->start - pressed) : 0));
        check(shop.abilities().counts().strikeHits == 7, "a hit in each spin: the fragment goes on to the seventh spin (7 hits)");
        check(exit != nullptr && shop.visits().back() == "Ability_GreatAxe_Whirlwind_Exit_Short" && exit->start == static_cast<std::uint16_t>(pressed + 90),
              "the exit at the end of the seventh spin (3.033 s)");
    }
}

/** GH7: Charge: the uppercut at a collision that a contact shows, and the swing at the block press (Unpredictable Strike). */
void charge_branches(const Options&) {
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Charge", nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-2.0F);
        shop.walker.idle(20); // The session observes the new wolf.
        const std::size_t from = shop.a.got.size();
        const std::uint16_t pressed = shop.press(0);
        check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_Engage", "the press starts the charge (Ability_GreatAxe_Engage)");
        shop.contact_at(static_cast<std::uint16_t>(pressed + 5), *wolf);
        note("Charge: the claims " + claim_counts(shop.host) + "; the visits " + af::joined(shop.visits()));
        const world::AttackVisit* visit = shop.host.world.combat().sequences().newest(kCharacterA);
        check(shop.abilities().counts().strikeHits == 1 && shop.visits().back() == "Ability_GreatAxe_Engage_Uppercut" && visit != nullptr
                  && visit->start == static_cast<std::uint16_t>(pressed + 5) && amounts(shop.a, from).size() == 1,
              "a contact at 0.2 s (the collision of the client, which the server does not see) is the strike of the uppercut: one hit");
    }
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Charge", nullptr, nullptr});
        shop.learn({kSwingAttack});
        world::Entity* wolf = shop.wolf(-2.0F);
        shop.walker.idle(20);
        const std::size_t from = shop.a.got.size();
        const std::uint16_t pressed = shop.press(0);
        shop.until(static_cast<std::uint16_t>(pressed + 4));
        shop.step({kBlockBit});
        check(!shop.visits().empty() && shop.visits().back() == "Ability_GreatAxe_Engage_Swing",
              "the block press at 0.167 s with Unpredictable Strike: the swing (IfInput block_press_virtual)");
        shop.contact_at(static_cast<std::uint16_t>(pressed + 7), *wolf);
        const std::vector<float> values = amounts(shop.a, from);
        check(values.size() == 1 && near(values[0], 100.0F * 1.4F * 0.8F, 0.05F), "the contact in its window hits with the row of the swing (1.4)");
    }
}

/** GH8: Mighty Gavel: its strike, and the spell of its fragment (a picture of the ground: no hit). */
void mighty_gavel(const Options&) {
    GawhShop shop;
    shop.hold("TestHammer");
    shop.bar(kWarHammer, {"Ability_WarHammer_MightyGavel", nullptr, nullptr});
    world::Entity* wolf = shop.wolf(-1.5F);
    const std::size_t from = shop.a.got.size();
    const std::uint16_t pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 18), *wolf);
    shop.until(static_cast<std::uint16_t>(pressed + 20));
    const std::vector<const world::Entity*> made = shop.spells();
    check(made.size() == 1 && made[0]->spell.role == world::SpellRole::visual && made[0]->spell.spellId == "WarHammer_Spell_MightyGavel",
          "at 0.638 s the fragment casts its spell (CAGE-CastSpell): a spell entity with no rule of the server (the picture of the ground)");
    shop.pass(1.0F);
    check(shop.abilities().counts().strikeHits == 1 && amounts(shop.a, from).size() == 1 && shop.spells().empty(),
          "the strike hits one time; the spell does no hit and ends after its Duration (600 ms)");
}

/** GH9: Armor Breaker: the rule of its row (ArmorPenetration 0.35) passes with InSequence, the fragment of its action. */
void armor_breaker(const Options&) {
    float dealt[2] = {0.0F, 0.0F};
    for (const bool rule : {true, false}) {
        GawhShop shop(rule);
        shop.hold("TestHammer");
        shop.bar(kWarHammer, {nullptr, "Ability_Warhammer_ArmorBreaker", nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::size_t from = shop.a.got.size();
        const std::uint16_t pressed = shop.press(1);
        shop.contact_at(static_cast<std::uint16_t>(pressed + 14), *wolf);
        const std::vector<float> values = amounts(shop.a, from);
        dealt[rule ? 0 : 1] = values.size() == 1 ? values[0] : 0.0F;
        if (rule) {
            const world::RuleView view = shop.host.world.passives().describe_row(
                "Ability_Warhammer_ArmorBreaker", &shop.host.abilities.ability_row("Ability_Warhammer_ArmorBreaker")->cells, world::RuleOrigin::node);
            note("the rule of Armor Breaker: " + view.note);
            check(view.applied, "the list names the rule as applied (its InSequence is the fragment of an ability with a plan)");
        }
    }
    note("Armor Breaker: " + std::to_string(dealt[0]) + " with the rule, " + std::to_string(dealt[1]) + " with no rule");
    check(dealt[0] > 0.0F && dealt[1] > 0.0F && near(dealt[0] / dealt[1], (1.0F - 0.2F * 0.65F) / 0.8F, 0.002F),
          "the penetration 0.35 takes 35 % of the physical mitigation of the wolf (0.2)");
}

/** GH10: Clear Out: the push of its row, and the fortify of Power Cleaner (the CastSpell of the rule) on the friends in 6 m. */
void clear_out(const Options&) {
    GawhShop shop;
    shop.enter_b();
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.hold("TestHammer");
    shop.bar(kWarHammer, {nullptr, nullptr, "Ability_WarHammer_ClearOut"});
    shop.learn({kFortifyAoe});
    world::Entity* wolf = shop.wolf(-2.0F);
    const std::uint64_t ruleSpells = shop.abilities().counts().ruleSpells;
    const std::uint16_t pressed = shop.press(2);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 23), *wolf);
    shop.walker.idle(2);
    check(shop.abilities().counts().ruleSpells == ruleSpells + 1, "the hit of Clear Out passes the rule of Power Cleaner: its CastSpell");
    check(shop.status_of(kFortifyBuff) != nullptr && shop.status_of(kFortifyBuff, kCharacterB) != nullptr,
          "the spell of the rule (a friend area of 6 m) gives its status to A and to the friend B");
    shop.pass(1.5F);
    note("Clear Out: the wolf is " + std::to_string(away(*shop.character(), *wolf)) + " m from A");
    check(near(away(*shop.character(), *wolf), 2.0F + 3.8F, 0.15F), "the row of Clear Out pushes the wolf 3.8 m away");
    check(shop.status_of(kFortifyBuff) != nullptr && shop.spells().empty(),
          "the spell ended (100 ms), and the status keeps its time (the row has no ClearOnExit and no ClearOnEnd)");
}

/** GH11: the shield of Maelstrom (Absorb): the spell of the action at its start, and its sphere for the projectiles of an enemy. */
void maelstrom_shield(const Options&) {
    for (const bool absorb : {false, true}) {
        GawhShop shop;
        shop.enter_b();
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {nullptr, "Ability_Greataxe_Maelstorm", nullptr});
        if (absorb) {
            shop.learn({kAbsorb});
        }
        world::Entity* wolf = shop.wolf(-6.0F);
        shop.press(1);
        const std::vector<const world::Entity*> made = shop.spells();
        if (!absorb) {
            check(made.empty(), "with no node Absorb the action casts no spell");
            continue;
        }
        check(made.size() == 1 && made[0]->spell.role == world::SpellRole::shield && made[0]->spell.spellId == kShieldSpell,
              "with Absorb the action casts its spell at its start: a shield at A (AttachToOwner, TargetTypes Friend)");
        if (made.size() != 1) {
            return;
        }
        const state::WorldPosition& at = shop.character()->position;
        const state::WorldPosition from{at.x - 5.0F, at.y, at.z + 1.0F};
        const state::WorldPosition to{at.x + 5.0F, at.y, at.z + 1.0F};
        const world::Entity* b = shop.world().find_character(kCharacterB);
        const std::optional<float> enemy = shop.world().spells().absorbs(from, to, wolf->id);
        check(enemy && near(*enemy, 0.3F, 0.01F), "the segment of a projectile of a creature enters the sphere (2 m) of the shield: it ends there");
        check(!shop.world().spells().absorbs(from, to, b->id) && !shop.world().spells().absorbs(from, to, shop.character()->id),
              "a projectile of a character (no enemy) passes");
        check(!shop.world().spells().absorbs({at.x - 5.0F, at.y + 6.0F, at.z + 1.0F}, {at.x + 5.0F, at.y + 6.0F, at.z + 1.0F}, wolf->id),
              "a segment 6 m away passes");
        check(shop.world().hostile(*wolf, *shop.character()) && !shop.world().hostile(*b, *shop.character()) && shop.world().hostile(*made[0], *wolf),
              "a creature and a character are enemies; two characters are not");
        shop.pass(1.3F);
        check(shop.spells().empty() && !shop.world().spells().absorbs(from, to, wolf->id), "the shield ends after its Duration (1125 ms)");
    }
}

/** GH12: the fortify in the well (Unyielding): the area status of the slice for each friend in it, its exit, and its end. */
void well_fortify(const Options&) {
    GawhShop shop;
    gamedata::AbilityTables tables = gawh_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    gamedata::StatusRow root = status("GreatAxe_Vortex_Root", 1.0F, {}, true);
    root.root = true;
    rows.push_back(root);
    rows.push_back(status("GreatAxe_Vortex", 3.0F, {}));
    tables.statusEffects = numbered(std::move(rows));
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Projectile", 0.0F, 0.0F, 10.0F, 0, 0.0F, 2000.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    tables.abilityRows.push_back({"Upgrade_Greataxe_GravityWell_ArmorInWell",
                                  cells({}, {{"WeaponTag", "GreatAxe"}, {"RequiredAbilityId", "Ability_Greataxe_GravityWell"},
                                             {"SelfApplyStatusEffect", "Status_Upgrade_Greataxe_GravityWell_ArmorInWell"}})});
    check(shop.host.abilities.set(tables), "setup: the rows of Gravity Well");
    gamedata::SpellSlice slice;
    slice.path = "slices/items/weapons/test_vortex.dynamicslice";
    slice.asset.fill(0x52);
    slice.subId = 2;
    slice.root = 0x0000025ACFDAA309ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = 1;
    slice.slots = {{1, "Spell", 2912}, {2, "Ownership", 3217}, {3, "TemporaryAffiliation", 3563}, {4, "DelayedEvent", 3139}, {5, "DelayedEvent", 3139}};
    slice.events = {{4, 3.5F, gamedata::spell_event::kTrigger, "Destroy", "", true, "DestroyDelay", true, true},
                    {5, 3.0F, gamedata::spell_event::kActivate, "Burst", "GreatAxe_GravityWell_Burst", false, "DestroyDelay", true, true}};
    gamedata::SpellAreaStatus area;
    area.entity = "Upgrade";
    area.detection = "DetectionVolume";
    area.radius = 4.0F;
    area.statuses = {"Status_Upgrade_Greataxe_GravityWell_ArmorInWell"};
    area.targets = 1;
    slice.areaStatuses = {area};
    gamedata::SpellTables spells;
    spells.slices = {slice};
    spells.spells = {{"Spell_GreatAxe_GravityWell_Vortex", 0}, {"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 0}};
    check(shop.host.spells.set(spells), "setup: the slice of the vortex with its AreaStatusEffectComponent (the friends, 4 m)");
    shop.hold("TestAxe");
    world::Entity& own = *shop.character();
    own.skills.pools[kGreatAxe][gamedata::progression_id("Ability_Greataxe_GravityWell")].level = 1;
    own.skills.pools[kGreatAxe][gamedata::progression_id("Upgrade_Greataxe_GravityWell_ArmorInWell")].level = 1;
    own.skills.bars[kGreatAxe] = {0, 0, gamedata::progression_id("Ability_Greataxe_GravityWell")};
    shop.world().creatures().behavior().set_ignored(kCharacterA, true);
    world::Entity* wolf = shop.wolf(-6.0F);
    const state::WorldPosition inside{wolf->position.x, wolf->position.y + 3.0F, kHeight};
    // B enters in the place of the well (a friend of A), and walks with the speed of a run (4.5 m/s).
    shop.beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", inside);
    shop.host.enter(shop.b, shop.beta.registration());
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    Walker bWalker{shop.host, shop.b, inside};
    bWalker.input = 300;
    const auto b_steps = [&](int count, float north = 0.0F) {
        for (int index = 0; index < count; ++index) {
            bWalker.position.y += north;
            bWalker.idle(1);
            shop.walker.idle(1);
        }
    };
    b_steps(20);
    shop.press(2);
    for (int index = 0; index < 60 && shop.spells().empty(); ++index) {
        b_steps(1);
    }
    b_steps(3);
    check(!shop.spells().empty(), "setup: the vortex of the upgrade is in the world");
    if (!shop.spells().empty()) {
        const world::Entity* vortex = shop.spells().front();
        const world::Entity* b = shop.world().find_character(kCharacterB);
        note("the vortex " + vortex->spell.spellId + " (" + world::spell_role_text(vortex->spell.role) + ") at "
             + std::to_string(vortex->position.x - kPlace.x) + " " + std::to_string(vortex->position.y - kPlace.y) + "; B at "
             + std::to_string(b->position.x - kPlace.x) + " " + std::to_string(b->position.y - kPlace.y));
    }
    const char* armor = "Status_Upgrade_Greataxe_GravityWell_ArmorInWell";
    const bool given = shop.status_of(armor, kCharacterB) != nullptr;
    b_steps(10, 0.15F);
    b_steps(4);
    const bool left = shop.status_of(armor, kCharacterB) == nullptr;
    const float outside = shop.world().find_character(kCharacterB)->position.y - wolf->position.y;
    b_steps(10, -0.15F);
    b_steps(4);
    const bool again = shop.status_of(armor, kCharacterB) != nullptr;
    note("B left to " + std::to_string(outside) + " m and came back to " + std::to_string(shop.world().find_character(kCharacterB)->position.y - wolf->position.y)
         + " m; spells then " + std::to_string(shop.spells().size()));
    b_steps(120);
    note("the well: given " + std::to_string(given) + ", gone at the exit " + std::to_string(left) + ", again " + std::to_string(again)
         + ", spells at the end " + std::to_string(shop.spells().size()));
    check(given, "a friend in 4 m of the vortex of Unyielding gets Status_Upgrade_Greataxe_GravityWell_ArmorInWell");
    check(left && again, "the status leaves the friend at its exit (m_removeOnExit) and comes again at its entry");
    check(shop.spells().empty() && shop.status_of(armor, kCharacterB) == nullptr, "the end of the vortex takes the status from each friend");
    const world::RuleView view = shop.host.world.passives().describe_row(
        "Upgrade_Greataxe_GravityWell_ArmorInWell", &shop.host.abilities.ability_row("Upgrade_Greataxe_GravityWell_ArmorInWell")->cells,
        world::RuleOrigin::node);
    note("Unyielding in the list: " + view.note);
    check(view.applied && view.note.find("area of the spell slice") != std::string::npos,
          "the list names Unyielding as applied: the area of the slice of the vortex gives its status");
}

/** GH13: Guarded Sprint: the status action of the hammer (the node, a movement, the hammer in the hand). */
void guarded_sprint(const Options&) {
    GawhShop shop;
    shop.hold("TestHammer");
    shop.learn({kGuardedSprint});
    shop.pass(1.0F);
    check(shop.status_of(kSprintStatus) == nullptr, "at rest: no status");
    for (int step = 0; step < 30; ++step) {
        shop.walker.position.y += 0.12F;
        shop.walker.idle(1);
    }
    check(shop.status_of(kSprintStatus) != nullptr, "the character moves with the hammer in its hand: WarHammer_Pass_SprintDmgReduction");
    shop.pass(1.2F);
    check(shop.status_of(kSprintStatus) == nullptr, "the character stops: the status goes");
    shop.hold("TestAxe");
    for (int step = 0; step < 30; ++step) {
        shop.walker.position.y += 0.12F;
        shop.walker.idle(1);
    }
    check(shop.status_of(kSprintStatus) == nullptr, "with the axe in the hand: no status (the tag Warhammer of the condition)");
}

/** GH14: the list of the passive rules names each use of a node in the weapon actions (format 2). */
void node_list(const Options&) {
    GawhShop shop;
    gamedata::AbilityTables tables = gawh_abilities();
    for (const char* node : {kCollector, kAddGrit, kSpeed, kGuardedSprint}) {
        tables.abilityRows.push_back({node, cells({}, {{"WeaponTag", "GreatAxe"}, {"RequiredAbilityId", "Ability_Greataxe_Reap"}})});
    }
    check(shop.host.abilities.set(tables), "setup: the rows of the nodes with no effect of their own");
    const auto view = [&](const char* id) {
        const world::RuleView result =
            shop.host.world.passives().describe_row(id, &shop.host.abilities.ability_row(id)->cells, world::RuleOrigin::node);
        note(std::string(id) + ": " + (result.applied ? "applied, " : "not applied, ") + result.note);
        return result;
    };
    const world::RuleView collector = view(kCollector);
    check(collector.applied && collector.note.find("row") != std::string::npos, "a node that selects a row of a strike: applied by the data");
    const world::RuleView grit = view(kAddGrit);
    check(!grit.applied && grit.note.find("grit") != std::string::npos, "a node that gives grit: not applied, with the cause (no grit)");
    const world::RuleView speed = view(kSpeed);
    check(speed.applied && speed.note.find("client") != std::string::npos, "a node of the movement of an action: applied by the client");
    const world::RuleView sprint = view(kGuardedSprint);
    check(sprint.applied && sprint.note.find("status action") != std::string::npos, "the node of a status action: applied by the evaluator");
}

/** GH15: two sessions (rule MP-10): each caster has its own cast, its own hits, its own pull, and its own cooldown. */
void two_casters(const Options&) {
    GawhShop shop;
    shop.enter_b();
    shop.hold("TestAxe");
    shop.hold("TestAxe", kCharacterB);
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr}, kCharacterB);
    world::Entity* wolfA = shop.wolf(-2.5F, -2.0F);
    world::Entity* wolfB = shop.wolf(-3.5F, 1.0F);
    world::Entity* b = shop.world().find_character(kCharacterB);
    Walker bWalker{shop.host, shop.b, b->position};
    bWalker.input = 300;
    bWalker.idle(2);
    const std::uint16_t pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *wolfA);
    shop.walker.idle(2);
    // B presses Reap while the cooldown of A runs, and hits its own wolf.
    bWalker.bits({kAbilityBits[0], kVirtualBit});
    const std::uint16_t bPressed = bWalker.input;
    bWalker.bits({kHoldBit});
    bWalker.bits({kReleaseBit});
    while (static_cast<std::uint16_t>(bPressed + 8 - bWalker.input) > 1) {
        bWalker.idle(1);
    }
    InputEntry contact = bWalker.entry();
    contact.contacts = {touch(*wolfB)};
    bWalker.send(contact);
    for (int index = 0; index < 60; ++index) {
        bWalker.idle(1);
        shop.walker.idle(1);
    }
    const auto* cooldownsA = shop.abilities().cooldowns_of(kCharacterA);
    const auto* cooldownsB = shop.abilities().cooldowns_of(kCharacterB);
    note("two casters: " + std::to_string(shop.accepted()) + " casts, " + std::to_string(shop.abilities().counts().strikeHits) + " hits, "
         + std::to_string(shop.abilities().counts().follows) + " pulls; the wolves are " + std::to_string(away(*shop.character(), *wolfA))
         + " m from A and " + std::to_string(away(*b, *wolfB)) + " m from B");
    check(shop.accepted() == 2 && shop.abilities().counts().strikeHits == 2 && shop.abilities().counts().follows == 2,
          "each caster has its cast, its hit, and its pull (the cooldown of A does not refuse the press of B)");
    check(near(away(*shop.character(), *wolfA), std::hypot(2.5F, 2.0F) - 1.0F, 0.12F), "the wolf of A moves 1 m to A (the row _Pull)");
    check(near(away(*b, *wolfB), 1.5F, 0.12F), "the wolf of B moves 1 m to B, not to A");
    check(cooldownsA != nullptr && cooldownsB != nullptr && cooldownsA->count(kReap) == 1 && cooldownsB->count(kReap) == 1,
          "each character has its own cooldown of Reap");
}

/** GH16: the load: the work of a cast of the sequence does not grow with the creatures far from the caster. */
void load(const Options&) {
    GawhShop shop;
    gamedata::AbilityTables tables = gawh_abilities();
    for (gamedata::CooldownRow& row : tables.cooldowns) {
        if (row.id == "Ability_Greataxe_Reap") {
            row.time = 1.5F; // A short cooldown: one cast after each end of the fragment.
        }
    }
    check(shop.host.abilities.set(tables), "setup: the cooldown of Reap 1.5 s");
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    shop.learn({kGravity, kCollector});
    world::Entity* wolf = shop.wolf(-2.5F);
    for (int index = 0; index < 4000; ++index) {
        shop.world().creatures().spawn("Test_Wolf", at(kPlace.x + 400.0F + static_cast<float>(index % 80) * 3.0F,
                                                        kPlace.y + 400.0F + static_cast<float>(index / 80) * 3.0F), 0.0F);
    }
    const auto started = std::chrono::steady_clock::now();
    const std::uint64_t looked = shop.abilities().counts().followLooked;
    constexpr int kCasts = 20;
    for (int cast = 0; cast < kCasts; ++cast) {
        const std::uint16_t pressed = shop.press(0);
        shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *wolf);
        shop.until(static_cast<std::uint16_t>(pressed + 56));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    note(std::to_string(kCasts) + " casts of Reap in a world of 4,001 creatures: " + std::to_string(seconds) + " s, "
         + std::to_string(shop.abilities().counts().followLooked - looked) + " creatures looked at by the pulls");
    check(shop.abilities().counts().strikeHits == kCasts && shop.abilities().counts().follows == kCasts, "each cast hits and pulls its wolf");
    check(shop.abilities().counts().followLooked - looked == kCasts, "a pull looks only at the creatures that its cast hit (1 for each cast)");
    check(shop.host.world.combat().sequences().size() == 1, "one sequence state: the character");
    check(seconds < 4.0, "20 casts with the passive rules of the axe in less than 4 s");
}

/** GH19: the hits of a cast for the client: the ability state of the owner (slot 46, hitDataNumHits and hitDataAbilityIds). */
void hit_counts(const Options&) {
    GawhShop shop(true, true);
    gamedata::AbilityTables tables = gawh_abilities();
    tables.abilityRows.push_back({"Ability_Greataxe_Whirlwind", cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"TrackHitCount", 1.0F}},
                                                                     {{"DamageTableRow", "GreatAxe_Whirlwind"}, {"WeaponTag", "GreatAxe"}})});
    check(shop.host.abilities.set(tables), "setup: the row of Whirlwind with TrackHitCount (game data)");
    shop.enter_b();
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", "Ability_Greataxe_Reap", nullptr});
    world::Entity* wolf = shop.wolf(-1.5F);
    const std::size_t from = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    const std::uint16_t pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 15), *wolf);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 27), *wolf);
    shop.walker.idle(3);
    const auto& hits = shop.character()->combat.abilityHits;
    check(hits.size() == 1 && hits[0].first == kWhirlwind && hits[0].second == 2, "two hits of the cast: the count 2 for Whirlwind");
    // The state of the owner (the encoder of the session), read with the state decoder of the cases.
    server::rep::PartsRequest request{1, 100000, nullptr};
    const std::vector<Bytes> parts = server::rep::encode_ability_state(*shop.character(), server::rep::View::owner, request);
    bool sent = false;
    if (parts.size() == 1) {
        server::rep::RecordWriter record(1);
        record.fragment(server::rep::kAbilitySlot, server::rep::kTypeAbilityState, parts[0]);
        server::hub::Writer body;
        const Bytes payload = record.finish();
        body.u8(1);
        body.size64(1);
        body.u8(1);
        body.u8(0);
        body.u8(0);
        body.u8(0);
        body.size(static_cast<std::uint32_t>(payload.size()));
        body.raw(payload);
        const Bundle bundle = read_bundle(body.data());
        if (bundle.complete && bundle.records.size() == 1 && bundle.records[0].fragments.size() == 1) {
            const Fragment& state = bundle.records[0].fragments[0];
            server::hub::Writer id;
            id.u32(kWhirlwind);
            const auto counts = state.fields.find({0, 1});
            const auto ids = state.fields.find({0, 2});
            sent = counts != state.fields.end() && ids != state.fields.end() && !counts->second.empty() && counts->second.back() == 2
                   && ids->second.size() >= 4 && std::equal(id.data().begin(), id.data().end(), ids->second.end() - 4);
            note("the field 1 has " + std::to_string(counts != state.fields.end() ? counts->second.size() : 0) + " bytes, the field 2 "
                 + std::to_string(ids != state.fields.end() ? ids->second.size() : 0));
        }
    }
    std::size_t states = 0;
    for (std::size_t index = from; index < shop.a.got.size(); ++index) {
        states += shop.a.got[index].type == 8 && shop.a.got[index].body.size() > 800 ? 1 : 0;
    }
    check(states >= 2, "the session of A sends a bundle with the ability state (850 bytes) after each hit");
    check(sent, "the owner gets the ability state with the field 1 (the count 2, u8) and the field 2 (the id of Whirlwind, u32)");
    bool observed = false;
    for (std::size_t index = fromB; index < shop.b.got.size(); ++index) {
        if (shop.b.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(shop.b.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                observed = observed || (fragment.slot == server::rep::kAbilitySlot && fragment.fields.count({0, 1}) != 0);
            }
        }
    }
    check(!observed, "an observer (B) gets no hit count");
    shop.pass(3.0F);
    shop.press(0);
    check((shop.character()->combat.abilityHits.size() == 1 && shop.character()->combat.abilityHits[0].second == 0)
              || shop.refused(CastVerdict::cooldown) == 1,
          "a new cast starts at 0 (or the press is in the cooldown)");
    shop.host.world.abilities().count_hit(*shop.character(), kReap, false);
    check(shop.character()->combat.abilityHits.size() == 1, "an ability with no TrackHitCount in its row (Reap of the fixture) has no count");
}

/** GH17: the loader of the weapon actions of format 2: the abilities, the conditions, the spells, the marks, the uses of the nodes. */
void loader_actions(const Options& options) {
    const json start{{"name", "Attack_Start_Charge"}, {"tags", "2H_Melee+TestAxe"}, {"file", "x"}, {"length", 0.7}, {"canTransition", nullptr},
                     {"transitions", json::array({json::array({"instant", 0.0, nullptr, "notHeld", "Attack_Start_Charge", "Attack_Heavy_NotHeld", nullptr}),
                                                  json::array({"buffered", 0.0, 0.1, "ability", "Ability_Test", "CanAttack_Test", "Ability_Greataxe_Reap"})})},
                     {"strikes", json::array()}, {"payments", json::array()}, {"dodge", json::array()}, {"casts", json::array()}, {"markers", json::array()}};
    const json shape = json::array({"MeleeAttackShapeCastTypeCapsule", 0.75, 0.3, 0, 0, 0, 0, 0, 1.2, 1, 1, 1, "master_root"});
    const json ability{{"name", "Ability_Test"}, {"tags", "2H_Melee+TestAxe"}, {"file", "x"}, {"length", 1.6}, {"canTransition", 0.9},
                       {"transitions", json::array({json::array({"instant", 1.9, nullptr, "terms", "Attack_Start_Charge", "Test_LessThan_1",
                                                                 json::array({json::array({json::array({"hits", 1, false}),
                                                                                           json::array({"node", "Test_Node", true}),
                                                                                           json::array({"input", "block", false})})})})})},
                       {"strikes", json::array({json::array({"atk1", 0.3, 0.4, json::array({json::array({"Test_Row", json::array(), json::array(), 0})}),
                                                             json::array({shape})})})},
                       {"payments", json::array()}, {"dodge", json::array()},
                       {"casts", json::array({json::array({0.6, "Test_Spell", json::array(), json::array({"Test_Node"})})})},
                       {"markers", json::array({json::array({"Test_Mark", 0.5})})}};
    const json good{{"format", 2},
                    {"counts", {{"weapons", 1}, {"fragments", 2}, {"transitions", 3}, {"strikes", 1}}},
                    {"weapons", json::array({{{"tags", {"2H_Melee", "TestAxe"}}, {"files", json::array()}, {"start", "Attack_Start_Charge"},
                                              {"fragments", {start, ability}},
                                              {"abilities", json::array({{{"ability", "Ability_Greataxe_Reap"}, {"action", "Test_Action"},
                                                                          {"fragment", "Ability_Test"},
                                                                          {"selections", json::array({json::array({"Ability_Test", json::array({"Test_Node"}),
                                                                                                                   json::array(), json::array(), json::array()})})},
                                                                          {"casts", json::array({json::array({"Test_Spell", json::array(), json::array(),
                                                                                                              json::array(), json::array(), "Test_Mark"})})}}})},
                                              {"nodeUses", json::array({json::array({"Test_Node", "row", "Ability_Test atk1"})})}}})}};
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "gawh-weapon-actions.json";
    const auto load = [&](const json& document, gamedata::WeaponActions& actions) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        const bool loaded = actions.load(path.string());
        std::filesystem::remove(path);
        return loaded;
    };
    gamedata::WeaponActions actions;
    const bool loaded = load(good, actions);
    if (!loaded) {
        note("the file of format 2: " + actions.error());
    }
    const gamedata::WeaponActionSet* axe = loaded ? actions.of_tags("2H_Melee,TestAxe") : nullptr;
    check(axe != nullptr && axe->abilities.size() == 1 && axe->ability(kReap) != nullptr && axe->ability(kReap)->selections.size() == 1
              && axe->ability(kReap)->casts[0].marker == "Test_Mark" && axe->fragments[0].transitions[1].input == gamedata::AttackInput::ability
              && axe->fragments[0].transitions[1].abilityId == kReap && axe->fragments[1].transitions[0].terms.size() == 1
              && axe->fragments[1].transitions[0].terms[0].size() == 3 && axe->fragments[1].transitions[0].terms[0][0].kind == gamedata::LiteralKind::hits
              && axe->fragments[1].transitions[0].terms[0][2].kind == gamedata::LiteralKind::block && axe->fragments[1].casts.size() == 1
              && axe->fragments[1].markers.size() == 1 && near(axe->fragments[1].strikes[0].shapes[0].offset[2], 1.2F)
              && actions.uses_of(gamedata::progression_id("Test_Node")).size() == 1,
          "a file of format 2 loads: the abilities, the start windows, the conditions, the spell casts, the marks, the shapes, the node uses");
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json copy = good;
        change(copy);
        gamedata::WeaponActions other;
        const bool bad = !load(copy, other);
        check(bad, std::string("refused: ") + what);
        return bad;
    };
    refused("a literal of an unknown input", [](json& d) { d["weapons"][0]["fragments"][1]["transitions"][0][6][0][2] = json::array({"input", "jump", false}); });
    refused("a selection of a fragment that the weapon does not have",
            [](json& d) { d["weapons"][0]["abilities"][0]["selections"][0][0] = "Ability_Nowhere"; });
    refused("a row with a shape that the strike does not have", [](json& d) { d["weapons"][0]["fragments"][1]["strikes"][0][3][0][3] = 4; });
    refused("a file of format 2 with no list of abilities", [](json& d) { d["weapons"][0].erase("abilities"); });
    refused("a spell cast with a negative time", [](json& d) { d["weapons"][0]["fragments"][1]["casts"][0][0] = -1.0; });
}

/** GH18: the loaders of the other new formats: the combat tables (format 3), the spell data (format 3), the ability data (format 5). */
void loader_formats(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "gawh-format.json";
    const auto write = [&](const json& document) { std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump(); };
    // The combat tables of format 3: the status effects of the damage rows.
    const json combat{{"format", 3},
                      {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreInterval", 5}, {"gearScoreMinimum", 100},
                                     {"gearScoreLimit", 500}, {"gearScoreSmallerRate", 0.6667}}},
                      {"damageTypes", {{"Slash", 4, "Physical"}}},
                      {"attributes", {{"minimum", 5}, {"strength", {0.0, 1.25}}, {"dexterity", {0.0, 1.25}}, {"intelligence", {0.0, 1.25}},
                                      {"focus", {0.0, 1.25}}}},
                      {"damageRows", {{"Light", "Sword", "Light", "Slash", 1.0, 0.0, 0.0, 0.0}, {"Pull_RP2", "Sword", "Ability", "Slash", 0.0001, 0.0, 0.0, 0.0}}},
                      {"weapons", {{"SwordT1", "Combat Melee", "Sword", 64.0, 0.0, 0.0, 0.0, 0.0, "Light", "Light", 0.0, "SwordAbilityTable"}}},
                      {"items", {{"1hswordt1", 0}}},
                      {"defenses", {{0.2, 0.1, json::object(), json::object()}}},
                      {"vitals", {{"wolf", 0}}},
                      {"rowEffects", {{"Pull_RP2", {"Status_Test_Root"}, true}}}};
    gamedata::CombatTables tables;
    write(combat);
    const bool combatLoaded = tables.load(path.string());
    if (!combatLoaded) {
        note("the combat tables of format 3: " + tables.error());
    }
    check(combatLoaded && tables.has_row_effects() && tables.damage_row(*tables.row_index("Pull_RP2")).effects.size() == 1
              && tables.damage_row(*tables.row_index("Pull_RP2")).effectOnlyOnDamage && tables.damage_row(*tables.row_index("Light")).effects.empty(),
          "the combat tables of format 3 load with the status effects of a row and EffectOnlyOnDamage");
    for (const auto& [what, change] : std::vector<std::pair<const char*, std::function<void(json&)>>>{
             {"a status effect of a row that the file does not have", [](json& d) { d["rowEffects"][0][0] = "Nowhere"; }},
             {"a row two times", [](json& d) { d["rowEffects"].push_back(d["rowEffects"][0]); }},
             {"no list rowEffects in format 3", [](json& d) { d.erase("rowEffects"); }}}) {
        json copy = combat;
        change(copy);
        write(copy);
        gamedata::CombatTables other;
        check(!other.load(path.string()), std::string("the combat tables are refused: ") + what);
    }
    // The spell data of format 3: the AreaStatusEffectComponents of a slice.
    const json slice{{"path", "slices/test/well.dynamicslice"},
                     {"asset", "62626262-6262-6262-6262-626262626202"},
                     {"sub", 2},
                     {"root", "0000030000000063"},
                     {"interestRadius", 50.0},
                     {"spellSlot", 1},
                     {"positionSlot", 0},
                     {"slots", json::array({json::array({1, "Spell", 2912}), json::array({11, "DelayedEvent", 3139})})},
                     {"events", json::array({json::array({11, 3.5, 2, "Destroy", "", true, "DestroyDelay", true, true})})},
                     {"components", json::array()},
                     {"vitals", nullptr},
                     {"autoSpell", nullptr},
                     {"volumes", json::array()},
                     {"offenses", json::array()},
                     {"detectionEvents", json::array()},
                     {"timelines", json::array()},
                     {"areaStatuses", json::array({json::array({"Upgrade", "DetectionVolume", 4.0, json::array({"Status_Test_Armor"}), true, 1})})}};
    const json spellDocument{{"format", 3},
                             {"counts", {{"slices", 1}, {"spells", 1}}},
                             {"slices", {slice}},
                             {"spells", json::array({json::array({"Test_Well", 0})})},
                             {"vitals", json::array()}};
    gamedata::SpellData spellData;
    write(spellDocument);
    const bool spellsLoaded = spellData.load(path.string());
    if (!spellsLoaded) {
        note("the spell data of format 3: " + spellData.error());
    }
    check(spellsLoaded && spellData.slice_of("Test_Well") != nullptr && spellData.slice_of("Test_Well")->areaStatuses.size() == 1
              && spellData.slice_of("Test_Well")->areaStatuses[0].targets == 1 && near(spellData.slice_of("Test_Well")->areaStatuses[0].radius, 4.0F),
          "the spell data of format 3 load with the area statuses of a slice (the targets, the radius, the statuses)");
    for (const auto& [what, change] : std::vector<std::pair<const char*, std::function<void(json&)>>>{
             {"no list areaStatuses in format 3", [](json& d) { d["slices"][0].erase("areaStatuses"); }},
             {"a negative radius", [](json& d) { d["slices"][0]["areaStatuses"][0][2] = -1.0; }},
             {"an area with no status", [](json& d) { d["slices"][0]["areaStatuses"][0][3] = json::array(); }}}) {
        json copy = spellDocument;
        change(copy);
        write(copy);
        gamedata::SpellData other;
        check(!other.load(path.string()), std::string("the spell data are refused: ") + what);
    }
    // The ability data of format 5: the status actions.
    const bool aFirst = gamedata::progression_id("statusa") < gamedata::progression_id("statusb");
    const json abilityDocument{
        {"format", 5},
        {"statusRows", 2},
        {"counts", {{"cooldowns", 1}, {"statusEffects", 2}, {"spells", 0}, {"manaCosts", 0}, {"staminaCosts", 0}, {"abilityCosts", 0},
                    {"abilityRows", 0}, {"statusCategories", 0}, {"actionSpells", 0}, {"actionNodes", 0}, {"actionStatuses", 1}}},
        {"cooldowns", {{"Test", 1, 0}}},
        {"statusEffects", {{"StatusA", aFirst ? 0 : 1, 7, 0, false, false, 0, true, 0, {{"PhysicalArmor", 0.2}}},
                           {"StatusB", aFirst ? 1 : 0, 3, 0.2, false, false, 0, false, 0, {{"EffectCategories", "Haste"}}}}},
        {"spells", json::array()},
        {"manaCosts", json::array()},
        {"staminaCosts", json::array()},
        {"abilityCosts", json::array()},
        {"abilityRows", json::array()},
        {"statusCategories", json::array()},
        {"actionSpells", json::array()},
        {"actionNodes", json::array()},
        {"actionStatuses", json::array({json::array({"playeractions_test", "Test_Action",
                                                     json::array({json::array({json::array({"node", "Test_Node", false}),
                                                                               json::array({"tag", "ForwardNav", false}),
                                                                               json::array({"tag", "Dodging", true})})}),
                                                     json::array({"StatusA"})})})}};
    gamedata::AbilityData abilityData;
    write(abilityDocument);
    const bool abilitiesLoaded = abilityData.load(path.string());
    if (!abilitiesLoaded) {
        note("the ability data of format 5: " + abilityData.error());
    }
    const auto& statuses = abilityData.tables().actionStatuses;
    check(abilitiesLoaded && statuses.size() == 1 && statuses[0].terms.size() == 1 && statuses[0].terms[0].size() == 3 && !statuses[0].terms[0][0].tag
              && statuses[0].terms[0][0].id == gamedata::progression_id("Test_Node") && statuses[0].terms[0][2].negated
              && statuses[0].statuses == std::vector<std::string>{"StatusA"},
          "the ability data of format 5 load with a status action (its terms and its statuses)");
    for (const auto& [what, change] : std::vector<std::pair<const char*, std::function<void(json&)>>>{
             {"a literal of an unknown kind", [](json& d) { d["actionStatuses"][0][2][0][0][0] = "slot"; }},
             {"a status action with no status", [](json& d) { d["actionStatuses"][0][3] = json::array(); }},
             {"no list actionStatuses in format 5", [](json& d) { d.erase("actionStatuses"); }}}) {
        json copy = abilityDocument;
        change(copy);
        write(copy);
        gamedata::AbilityData other;
        check(!other.load(path.string()), std::string("the ability data are refused: ") + what);
    }
    std::filesystem::remove(path);
}

} // namespace

std::vector<Case> gawh_cases() {
    return {{"GH1", "abilities 3.3b-M2: Reap, its strike, the pull of its second strike (the row of the nearest shape), The Collector, the cooldown", reap_pull},
            {"GH2", "abilities 3.3b-M2: the status of a damage row (the root of Gravity), ApplicationCooldown, OnEndStatusEffect", gravity_root},
            {"GH3", "abilities 3.3b-M2: the selection of a fragment by a node (Fatal Attraction) and its third strike", fatal_attraction},
            {"GH4", "abilities 3.3b-M2: Execute, the rule below 50 % health, a press that no window of the fragment takes", execute},
            {"GH5", "abilities 3.3b-M2: a press in a buffered window starts the ability at the end of the window", buffered_start},
            {"GH6", "abilities 3.3b-M2: Whirlwind, its spins, and its exit by the hits of the cast (Unending Winds)", whirlwind},
            {"GH7", "abilities 3.3b-M2: Charge, the uppercut at a collision that a contact shows, the swing at the block press", charge_branches},
            {"GH8", "abilities 3.3b-M2: Mighty Gavel, its strike, and the spell of its fragment (a picture)", mighty_gavel},
            {"GH9", "abilities 3.3b-M2: Armor Breaker, the penetration of its rule (InSequence: the fragment of the action)", armor_breaker},
            {"GH10", "abilities 3.3b-M2: Clear Out, its push, the fortify of Power Cleaner on the friends (two sessions)", clear_out},
            {"GH11", "abilities 3.3b-M2: the shield of Maelstrom with Absorb, the projectiles of an enemy", maelstrom_shield},
            {"GH12", "abilities 3.3b-M2: the fortify in the well of Unyielding (two sessions): the entry, the exit, the end", well_fortify},
            {"GH13", "abilities 3.3b-M2: Guarded Sprint, the status action of the hammer while the character moves", guarded_sprint},
            {"GH14", "abilities 3.3b-M2: the list of the passive rules names each use of a node in the weapon actions", node_list},
            {"GH15", "abilities 3.3b-M2: two casters of Reap (rule MP-10): own casts, hits, pulls, and cooldowns", two_casters},
            {"GH16", "abilities 3.3b-M2: the load: 20 casts in a world of 4,001 creatures look only at the creatures near the caster", load},
            {"GH17", "abilities 3.3b-M2: the loader of the weapon actions of format 2", loader_actions},
            {"GH19", "abilities 3.3b-M2: the hits of a cast in the ability state of the owner (slot 46, fields 1 and 2), two sessions", hit_counts},
            {"GH18", "abilities 3.3b-M2: the loaders of the combat tables (format 3), the spell data (format 3), the ability data (format 5)",
             loader_formats}};
}

} // namespace soulwarden::backend::tests

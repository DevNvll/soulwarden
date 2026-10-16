// Cases for the basic attacks of the melee weapons (stage 3.3b-M1): the sequence of a character (the light chain, the partial and
// the full charge, the fast sub-hits), the strike ledger, the ends of a sequence, two sessions, a replay, a wrap of the input
// number, a flood of claims, a reconnect, the load, the loader of the weapon actions, and the check of the data tool. The weapon
// graphs have the form of the data of the game (tools/gamedata/weapon-actions.sh: the great axe and the rapier) with made-up ids
// and values: no file of the game and no value of a capture is here.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gamedata/progression_tables.h"
#include "server/rep/combat_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "support/attack_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using world::HitVerdict;

using namespace attack_fixture;

/** MB1: the light chain of the axe: each step its row, a contact out of its window, a press before the buffered window. */
void light_chain(const Options&) {
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    const std::size_t from = range.a.got.size();
    // The click: the light swing starts at the release step (Attack_Heavy_NotHeld). Its window 0.3 to 0.4 s: the contact 9 steps after.
    const std::uint16_t light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 3));
    attacker.step({}, {contact_with(*wolf)});
    check(range.track().count(HitVerdict::noStrike) == 1 && amounts_of(range.a, from).empty(),
          "a contact 3 steps after the start of the light swing (before its window): refused (no strike fits), no number");
    attacker.until(static_cast<std::uint16_t>(light + 9));
    attacker.step({}, {contact_with(*wolf)});
    // The second click at the step 16 of the swing (0.567 s: its instant window): Attack_Start_Charge_C2, then the second swing at the release.
    attacker.until(static_cast<std::uint16_t>(light + 16));
    const std::uint16_t second = attacker.click();
    attacker.until(static_cast<std::uint16_t>(second + 11));
    attacker.step({}, {contact_with(*wolf)});
    // A press at the step 14 of the second swing (its buffered window): the next swing starts at 1.133 s of it (the step 33).
    attacker.until(static_cast<std::uint16_t>(second + 14));
    attacker.click();
    attacker.until(static_cast<std::uint16_t>(second + 33 + 9));
    attacker.step({}, {contact_with(*wolf)});
    const std::vector<float> amounts = amounts_of(range.a, from);
    note("visits: " + joined(range.visits()) + "; numbers " + std::to_string(amounts.size()));
    check(amounts.size() == 3, "three contacts in the windows of three swings: three hits");
    if (amounts.size() == 3) {
        check(near(amounts[1] / amounts[0], 1.05F, 0.0005F) && near(amounts[2] / amounts[0], 1.0F, 0.0005F),
              "the first swing has the row of the first step (factor 1), the second the row of the ender (1.05), the third the first row "
              "again: the chain of the data (Attack_Primary, Attack_Primary_Strike2, Attack_Primary)");
    }
    check(joined(range.visits()) == "Attack_Start_Charge_C2 Attack_Primary_Strike2 Attack_Start_Charge Attack_Primary",
          "the visits follow the graph of the data (the last 4 of the history)");

    // A second click 3 steps after the first one (before the buffered window at 0.1875 s): the press is lost, no second swing.
    attacker.idle(80);
    const std::size_t before = range.a.got.size();
    const std::uint16_t once = attacker.click();
    attacker.until(static_cast<std::uint16_t>(once + 3));
    attacker.click();
    attacker.until(static_cast<std::uint16_t>(once + 18 + 11));
    attacker.step({}, {contact_with(*wolf)});
    check(range.visits().back() == "Attack_Primary" && amounts_of(range.a, before).empty(),
          "a second press before the buffered window is lost: no second swing, and a contact at its time is no hit");
}

/** MB2: the partial and the full charge of a heavy attack, the pull node, the repeat of a long hold, and its end at a release. */
void charge(const Options&) {
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    std::size_t from = range.a.got.size();
    // The light reference.
    const std::uint16_t light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 9));
    attacker.step({}, {contact_with(*wolf)});
    attacker.idle(70);
    // A partial charge: the release 14 steps after the press (0.5 s): Attack_Heavy at the release; the contact 14 steps after it.
    const std::uint16_t partial = attacker.hold(13);
    attacker.until(static_cast<std::uint16_t>(partial + 14 + 14));
    attacker.step({}, {contact_with(*wolf)});
    attacker.idle(90);
    // A full charge: the hold to 0.667 s gives Attack_Heavy_Charged by itself (the step 19); the contact 9 steps after it.
    const std::uint16_t full = attacker.step({kActionAttack});
    for (int index = 0; index < 26; ++index) {
        attacker.step({kActionAttackHold});
    }
    attacker.step({kActionAttackRelease});
    attacker.until(static_cast<std::uint16_t>(full + 19 + 9));
    attacker.step({}, {contact_with(*wolf)});
    std::vector<float> amounts = amounts_of(range.a, from);
    check(amounts.size() == 3, "a light, a partial, and a full charge: three hits");
    if (amounts.size() == 3) {
        check(near(amounts[1] / amounts[0], 1.2F, 0.0005F) && near(amounts[2] / amounts[0], 1.6F, 0.0005F),
              "the partial charge has the heavy row (1.2), the full charge the charged row (1.6)");
    }
    // With the pull node the heavy strike is the second key of the same window (the row with the pull).
    world::Entity* own = range.host.world.find_character(kCharacterA);
    own->skills.pools[gamedata::progression_id("TestAxeTable")][gamedata::progression_id(kPullNode)].level = 1;
    attacker.idle(90);
    from = range.a.got.size();
    const std::uint16_t pulled = attacker.hold(13);
    attacker.until(static_cast<std::uint16_t>(pulled + 14 + 14));
    attacker.step({}, {contact_with(*wolf)});
    amounts = amounts_of(range.a, from);
    check(amounts.size() == 1 && near(amounts[0] / (amounts_of(range.a).front()), 1.25F, 0.0005F),
          "with the node of the condition: the strike atk2 with the pull row (1.25), not atk1 (its condition forbids the node)");
    own->skills.pools.clear();

    // A long hold: the charged swing repeats (its instant window at 0.733 s with the hold), each swing a new visit and a new hit.
    attacker.idle(90);
    from = range.a.got.size();
    const std::uint64_t visitsBefore = range.host.world.combat().sequences().counts().visits;
    const std::uint16_t held = attacker.step({kActionAttack});
    for (int index = 0; index < 95; ++index) {
        attacker.step({kActionAttackHold});
    }
    attacker.step({kActionAttackRelease});
    attacker.idle(5);
    const std::uint64_t visits = range.host.world.combat().sequences().counts().visits - visitsBefore;
    note("long hold: " + std::to_string(visits) + " visits, the last 4: " + joined(range.visits()));
    // The swings: charged at the step 19; Attack_Start_Charge_C2 at 19 + 21 (0.733 s); charged again at 40 + 19 = 59; C2 again at 80;
    // the release at the step 96 (0.567 s of that charge): the partial heavy.
    check(visits == 6 && joined(range.visits()) == "Attack_Start_Charge_C2 Attack_Heavy_Charged Attack_Start_Charge_C2 Attack_Heavy",
          "a long hold of 3.2 s: Attack_Start_Charge, the charged swing, Attack_Start_Charge_C2 at its instant window with the hold, the "
          "charged swing again, C2 again, and at the release the partial heavy");
    (void)held;
    // A release in the buffered window of the charged swing: no repeat (the hold does not count in a buffered window).
    attacker.idle(90);
    const std::uint16_t shortHold = attacker.step({kActionAttack});
    for (int index = 0; index < 29; ++index) {
        attacker.step({kActionAttackHold});
    }
    attacker.step({kActionAttackRelease});
    attacker.idle(40);
    (void)shortHold;
    check(range.visits().back() == "Attack_Heavy_Charged", "a release in the buffered window of the charged swing: no second charge");
}

/** MB3: the fast sub-hits of the second step of the rapier: two strikes 7 steps apart, each one hit; a third contact is the same strike. */
void sub_hits(const Options&) {
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.step({kActionWeapon2});
    attacker.idle(5);
    const std::size_t from = range.a.got.size();
    const std::uint16_t first = attacker.click();
    attacker.until(static_cast<std::uint16_t>(first + 10));
    attacker.step({}, {contact_with(*wolf)});
    // The press at the step 12 of the first thrust (its instant window from 0.3 s, CanAttack_Primary_Rapier): the second step at the release.
    attacker.until(static_cast<std::uint16_t>(first + 12));
    const std::uint16_t second = attacker.click();
    attacker.until(static_cast<std::uint16_t>(second + 9));
    attacker.step({}, {contact_with(*wolf)});
    attacker.until(static_cast<std::uint16_t>(second + 16));
    attacker.step({}, {contact_with(*wolf)});
    attacker.step({}, {contact_with(*wolf)});
    const std::vector<float> amounts = amounts_of(range.a, from);
    note("rapier: " + joined(range.visits()) + "; numbers " + std::to_string(amounts.size()));
    check(amounts.size() == 3, "the first thrust, and the two sub-hits of the second step (7 steps, 0.233 s apart): three hits");
    if (amounts.size() == 3) {
        check(near(amounts[1] / amounts[0], 0.66F, 0.0005F) && near(amounts[2] / amounts[0], 0.66F, 0.0005F),
              "each sub-hit has the row of two thirds (0.66)");
    }
    check(range.track().count(HitVerdict::sameAttack) == 1, "a second contact in the window of the second sub-hit: the same strike (refused)");
}

/** MB4: the ends of a sequence: a weapon change, a dodge, a travel, an accepted ability, a death. */
void sequence_ends(const Options&) {
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    const auto refused_at = [&](const char* what, auto between) {
        const std::size_t from = range.a.got.size();
        const std::uint64_t refusedBefore = range.track().count(HitVerdict::noAttack) + range.track().count(HitVerdict::noStrike);
        const std::uint16_t light = attacker.click();
        between(light);
        attacker.until(static_cast<std::uint16_t>(light + 9));
        attacker.step({}, {contact_with(*wolf)});
        check(amounts_of(range.a, from).empty()
                  && range.track().count(HitVerdict::noAttack) + range.track().count(HitVerdict::noStrike) == refusedBefore + 1,
              std::string(what) + ": the swing ends, a contact in its window is no hit (refused: no attack, or no strike)");
        attacker.idle(70);
    };
    refused_at("a weapon change (key 2) in the swing", [&](std::uint16_t) {
        attacker.step({kActionWeapon2});
    });
    attacker.step({kActionWeapon1});
    attacker.idle(10);
    // A dodge in the cancel window that starts with the swing: no end (the client plays no dodge there); the strike hits.
    {
        const std::size_t from = range.a.got.size();
        const std::uint16_t light = attacker.click();
        attacker.until(static_cast<std::uint16_t>(light + 1));
        attacker.step({kActionDodge});
        attacker.until(static_cast<std::uint16_t>(light + 9));
        attacker.step({}, {contact_with(*wolf)});
        check(amounts_of(range.a, from).size() == 1, "a dodge in the cancel window from the start of the swing: no end, the strike hits");
        attacker.idle(70);
    }
    refused_at("a dodge in the buffer window (the end at 0.333 s, the step of the strike)", [&](std::uint16_t light) {
        attacker.until(static_cast<std::uint16_t>(light + 4));
        attacker.step({kActionDodge});
    });
    // A dodge in the later cancel window: the swing ends at once; a contact after it in the window of the strike is no hit.
    {
        const std::size_t from = range.a.got.size();
        const std::uint64_t refusedBefore = range.track().count(HitVerdict::noAttack) + range.track().count(HitVerdict::noStrike);
        const std::uint16_t light = attacker.click();
        attacker.until(static_cast<std::uint16_t>(light + 10));
        attacker.step({kActionDodge});
        attacker.step({}, {contact_with(*wolf)});
        check(amounts_of(range.a, from).empty() && range.track().count(HitVerdict::noAttack) + range.track().count(HitVerdict::noStrike) == refusedBefore + 1
                  && range.state()->visits.back().interrupted,
              "a dodge in the later cancel window: the swing ends at once, a contact after it in the window of the strike is no hit");
        attacker.idle(70);
    }
    refused_at("a travel", [&](std::uint16_t) {
        (void)range.host.world.travel(kCharacterA, state::WorldPosition{kPlace.x, kPlace.y + 0.5F, kHeight}, world::TravelScreen::off);
    });
    refused_at("an accepted ability (the end of the world)", [&](std::uint16_t) {
        range.host.world.combat().end_sequence(kCharacterA, world::SequenceEnd::ability);
    });
    const auto& counts = range.host.world.combat().sequences().counts();
    note("ends: weapon " + std::to_string(counts.ends[static_cast<std::size_t>(world::SequenceEnd::weapon)]) + ", dodge "
         + std::to_string(counts.ends[static_cast<std::size_t>(world::SequenceEnd::dodge)]) + ", travel "
         + std::to_string(counts.ends[static_cast<std::size_t>(world::SequenceEnd::travel)]));
    check(counts.ends[static_cast<std::size_t>(world::SequenceEnd::weapon)] >= 1 && counts.ends[static_cast<std::size_t>(world::SequenceEnd::dodge)] == 2
              && counts.ends[static_cast<std::size_t>(world::SequenceEnd::travel)] == 1
              && counts.ends[static_cast<std::size_t>(world::SequenceEnd::ability)] == 1,
          "the counts of the ends: a weapon change, two dodges, a travel, an ability");
    // A death: the sequence ends at the next step, and a dead character makes no hit.
    const std::uint16_t light = attacker.click();
    (void)range.host.world.health().hurt(kCharacterA, 1.0e9F);
    check(range.host.world.find_character(kCharacterA)->life.dead, "control: the character is dead");
    attacker.until(static_cast<std::uint16_t>(light + 9));
    attacker.step({}, {contact_with(*wolf)});
    check(range.state()->visits.back().interrupted && counts.ends[static_cast<std::size_t>(world::SequenceEnd::death)] == 1,
          "a death ends the sequence");
}

/** MB5: two sessions (rule MP-10): two characters attack one wolf; each sequence is its own; each number goes to its own session. */
void two_sessions(const Options&) {
    Range range;
    FakeClient b{27001};
    const Player beta = add_player(range.host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kPlace);
    range.host.enter(b, beta.registration());
    range.host.datagram(b.peer, b.report_at(1, kPlace.x, kPlace.y + 1.0F, kPlace.z));
    range.host.steps(60, {&range.a, &b});
    range.arm(kCharacterB, "TestAxe", world::paperdoll::kMainHand1);
    world::Entity* wolf = range.wolf(1.5F);
    range.host.steps(30, {&range.a, &b});
    Attacker one{range.host, range.a, kPlace};
    Attacker two{range.host, b, state::WorldPosition{kPlace.x, kPlace.y + 1.0F, kPlace.z}};
    one.others = {&b};
    two.others = {&range.a};
    one.idle(3);
    two.idle(3);
    const std::size_t fromA = range.a.got.size();
    const std::size_t fromB = b.got.size();
    // A swings; B sends a contact at the time of the strike of A with no attack of its own: refused for B.
    const std::uint16_t light = one.click();
    for (int index = 0; index < 8; ++index) {
        one.step();
        two.step();
    }
    one.step({}, {contact_with(*wolf)});
    two.step({}, {contact_with(*wolf)});
    check(amounts_of(range.a, fromA).size() == 1 && amounts_of(b, fromB).empty() && range.track(2).count(HitVerdict::noAttack) == 1,
          "the strike of A hits for A; the same contact of B, with no attack of B, is no hit (the sequence of B has no visit)");
    (void)light;
    // B swings in the second swing of A: two sequences at one time, each with its own fragments.
    one.click();
    two.click();
    for (int index = 0; index < 8; ++index) {
        one.step();
        two.step();
    }
    two.step({}, {contact_with(*wolf)});
    note("A: " + joined(range.visits(kCharacterA)) + "; B: " + joined(range.visits(kCharacterB)));
    check(amounts_of(b, fromB).size() == 1 && amounts_of(range.a, fromA).size() == 1,
          "the first swing of B hits for B; the number goes only to the session of B");
    check(range.visits(kCharacterA).back() == "Attack_Primary_Strike2" && range.visits(kCharacterB).back() == "Attack_Primary",
          "the sequence of each character is its own: A is in its second swing, B in its first");
    check(range.host.world.combat().sequences().size() == 2, "two states: one for each character (the character id is the key)");
}

/** MB6: a replay, a wrap of the input number, a flood of claims, and a reconnect. */
void abuse(const Options&) {
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    // A replay: the same report again (the session takes each input number one time), and a contact item two times in one step.
    std::size_t from = range.a.got.size();
    std::uint16_t light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 9));
    InputEntry made = attacker.entry();
    made.contacts = {contact_with(*wolf), contact_with(*wolf)};
    attacker.send(made);
    range.host.datagram(range.a.peer, range.a.input_report(std::vector<InputEntry>(attacker.history.begin(), attacker.history.end())));
    range.host.tick();
    check(amounts_of(range.a, from).size() == 1 && range.track().count(HitVerdict::sameAttack) == 1,
          "a replay of the report and a second item in the step: one hit (the second item: the same strike)");
    // A wrap of the input number: the input numbers of the client start again near 65535 (the track takes the new sequence after
    // 2 s), and the chain goes on across 65535 to 0.
    attacker.idle(70);
    attacker.input = 65440;
    attacker.history.clear();
    attacker.idle(70);
    from = range.a.got.size();
    light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 9));
    attacker.step({}, {contact_with(*wolf)});
    attacker.until(static_cast<std::uint16_t>(light + 16));
    const std::uint16_t second = attacker.click();
    attacker.until(static_cast<std::uint16_t>(second + 11));
    attacker.step({}, {contact_with(*wolf)});
    std::vector<float> amounts = amounts_of(range.a, from);
    note("wrap: the first swing at " + std::to_string(light) + ", the second at " + std::to_string(second) + ": " + joined(range.visits()));
    check(amounts.size() == 2 && light > 65000 && static_cast<std::uint16_t>(second + 11) < 100 && near(amounts[1] / amounts[0], 1.05F, 0.0005F),
          "a chain across the wrap of the input number (the contact of the second swing after 65535): two hits, the second with the ender row");
    // A flood: the limit and 20 more contacts in one step of a strike: one hit, the rest refused (the same strike, then too many).
    attacker.idle(70);
    from = range.a.got.size();
    light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 9));
    const std::uint64_t tooMany = range.track().count(HitVerdict::tooMany);
    attacker.step({}, std::vector<InputContactItem>(world::combat_limits::kClaimsInWindow + 20, contact_with(*wolf)));
    check(amounts_of(range.a, from).size() == 1 && range.track().count(HitVerdict::tooMany) == tooMany + 20,
          "a flood of claims in one step: one hit, the claims above the limit refused (too many claims)");
    // A reconnect: a new session of the character in a swing of the old one: the old visits do not serve the new session.
    attacker.idle(80);
    attacker.click();
    attacker.idle(2);
    range.host.disconnect(range.a.peer);
    range.host.steps(5, {});
    FakeClient again{27002};
    range.host.enter(again, range.alpha.registration());
    range.host.datagram(again.peer, again.report_at(1, kPlace.x, kPlace.y, kPlace.z));
    range.host.steps(30, {&again});
    range.arm(kCharacterA, "TestAxe", world::paperdoll::kMainHand1);
    Attacker renewed{range.host, again, kPlace};
    renewed.input = static_cast<std::uint16_t>(attacker.input + 1);
    from = again.got.size();
    renewed.idle(3);
    renewed.step({}, {contact_with(*wolf)});
    check(amounts_of(again, from).empty(), "a reconnect: a contact of the new session in the time of the old swing is no hit");
    const std::uint16_t fresh = renewed.click();
    renewed.until(static_cast<std::uint16_t>(fresh + 9));
    renewed.step({}, {contact_with(*wolf)});
    check(amounts_of(again, from).size() == 1 && range.state()->session != 1, "the first swing of the new session hits; the state has the new session");
}

/** MB7: the load: 2 attackers in chains near 8 wolves, in a world with 4,000 far creatures, with a time limit. */
void sequence_load(const Options&) {
    Range range;
    for (int index = 0; index < 4000; ++index) {
        (void)range.host.world.creatures().spawn(
            "Test_Wolf", state::WorldPosition{5000.0F + static_cast<float>(index % 100) * 3.0F, 5000.0F + static_cast<float>(index / 100) * 3.0F, kHeight}, 0.0F);
    }
    std::vector<world::Entity*> near;
    for (int index = 0; index < 8; ++index) {
        near.push_back(range.wolf(1.2F + 0.1F * static_cast<float>(index)));
    }
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    std::vector<InputContactItem> all;
    for (const world::Entity* wolf : near) {
        all.push_back(contact_with(*wolf));
    }
    const std::size_t from = range.a.got.size();
    const auto started = std::chrono::steady_clock::now();
    int swings = 0;
    for (int round = 0; round < 30; ++round) {
        const std::uint16_t light = attacker.click();
        attacker.until(static_cast<std::uint16_t>(light + 9));
        attacker.step({}, all);
        attacker.until(static_cast<std::uint16_t>(light + 16));
        const std::uint16_t second = attacker.click();
        attacker.until(static_cast<std::uint16_t>(second + 11));
        attacker.step({}, all);
        attacker.idle(70);
        swings += 2;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const std::size_t hits = amounts_of(range.a, from).size();
    note(std::to_string(swings) + " swings on 8 wolves in a world of 4,008 creatures: " + std::to_string(seconds) + " s, " + std::to_string(hits) + " hits");
    check(hits == static_cast<std::size_t>(swings) * 8, "each swing hits each of the 8 wolves of its contacts one time");
    check(seconds < 4.0, "the sequence and the claims look only at the claimed creatures: less than 4 s for 60 swings and 7,000 steps");
}

/** MB8: the loader of the weapon actions: a file of the tool, the checks, and the match of the tags of a weapon. */
void loader(const Options& options) {
    gamedata::WeaponActions actions;
    check(actions.set(test_actions()), "the graphs of the test are valid");
    check(actions.of_tags("2H_Melee,TestAxe") != nullptr && actions.of_tags("testaxe, 2h_melee") != nullptr && actions.of_tags("1H_Melee,Sword") == nullptr,
          "the tags of a weapon find its graph with no regard to the case and the order of the tags; a weapon with no graph has none");
    gamedata::WeaponActionTables broken = test_actions();
    broken.weapons[0].fragments[1].transitions[0].toName = "Attack_Nowhere";
    check(!actions.set(broken) && !actions.loaded(), "a transition to a fragment that the weapon does not have: the data are refused");
    broken = test_actions();
    broken.weapons[0].fragments[1].strikes[0].rows.clear();
    check(!actions.set(broken), "a strike with no row: refused");
    broken = test_actions();
    broken.weapons[0].fragments[1].strikes[0].end = 0.1F;
    check(!actions.set(broken), "a strike window with its end before its start: refused");
    broken = test_actions();
    broken.weapons[1].tags = {"testaxe", "2H_MELEE"};
    check(!actions.set(broken), "two graphs with one tag set: refused");
    const std::filesystem::path file = options.workDirectory / "weapon-actions-test.json";
    std::filesystem::create_directories(options.workDirectory);
    const auto write = [&](const std::string& text) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    };
    const std::string fragmentJson = R"({"name":"Attack_Start_Charge","tags":"2H_Melee+TestAxe","file":"x","length":0.7,"canTransition":null,)"
                                     R"("transitions":[["instant",0.0,null,"notHeld","Attack_Primary","Attack_Heavy_NotHeld"]],"strikes":[],"payments":[],"dodge":[]})";
    const std::string primaryJson = R"({"name":"Attack_Primary","tags":"2H_Melee+TestAxe","file":"x","length":1.6,"canTransition":1.1,)"
                                    R"("transitions":[],"strikes":[["atk1",0.3,0.4,[["Test_Axe_Primary",[],["Test_Node_HeavyPull"]]],)"
                                    R"([["MeleeAttackShapeCastTypeCapsule",0.25,0.7,0,0,0,0,0,0,1,1,1,"master_root"]]]],)"
                                    R"("payments":[[0.0,"stamina","Test_Cost",1.0,""]],"dodge":[[0.0,0.26,false]]})";
    const std::string good = R"({"format":1,"counts":{"weapons":1,"fragments":2,"transitions":1,"strikes":1},"weapons":[{"tags":["2H_Melee","TestAxe"],)"
                             R"("files":[],"start":"Attack_Start_Charge","fragments":[)" + fragmentJson + "," + primaryJson + "]}]}";
    write(good);
    check(actions.load(file.string()) && actions.of_tags("2H_Melee,TestAxe") != nullptr
              && actions.of_tags("2H_Melee,TestAxe")->fragments[1].strikes[0].rows[0].forbids.size() == 1
              && actions.of_tags("2H_Melee,TestAxe")->fragments[1].payments.size() == 1,
          "a file of the tool (format 1) loads: its fragments, strikes, conditions, and payments");
    std::string wrongCount = good;
    wrongCount.replace(wrongCount.find("\"strikes\":1"), 11, "\"strikes\":2");
    write(wrongCount);
    check(!actions.load(file.string()) && !actions.loaded(), "a count that is not the size of its list: the file is refused");
    std::string notFinite = good;
    notFinite.replace(notFinite.find("\"length\":1.6"), 12, "\"length\":\"x\"");
    write(notFinite);
    check(!actions.load(file.string()), "a length that is not a number: the file is refused");
    check(!actions.load((options.workDirectory / "no-such-file.json").string()) && actions.error() == "the file is not there",
          "no file: not loaded (the backend then uses the rule of stage 3.2a)");

    // With no weapon actions: the rule of stage 3.2a (the light row of the weapon table) for the same click.
    Range plain(false);
    world::Entity* wolf = plain.wolf(1.5F);
    Attacker attacker{plain.host, plain.a, kPlace};
    attacker.idle(5);
    const std::uint16_t light = attacker.click();
    attacker.until(static_cast<std::uint16_t>(light + 9));
    attacker.step({}, {contact_with(*wolf)});
    check(amounts_of(plain.a).size() == 1 && plain.state() == nullptr,
          "no weapon actions: a click hits with the rule of stage 3.2a, and no character has a sequence");
}

/** MB9: the check of the data tool (tools/gamedata/check_weapon_actions_tool.py, a path below the repository). */
void tool_check(const Options& options) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools/gamedata/check_weapon_actions_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto log = options.workDirectory / "check_weapon_actions_tool-mb9.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + path.lexically_normal().string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream input(log);
    std::size_t oks = 0;
    std::size_t fails = 0;
    for (std::string line; std::getline(input, line);) {
        oks += line.rfind("ok ", 0) == 0 ? 1 : 0;
        fails += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    std::filesystem::remove(log);
    note("tool check: " + std::to_string(oks) + " ok, " + std::to_string(fails) + " FAIL, status " + std::to_string(status));
    check(status == 0 && oks >= 8 && fails == 0,
          "each case of the tool check passes (the variant, the length, the conditions, the graph, the refusals), status 0");
}

} // namespace

std::vector<Case> attack_sequence_cases() {
    return {{"MB1", "basic attacks: the light chain of the axe, a contact out of its window, a press before the buffered window", light_chain},
            {"MB2", "basic attacks: the partial and the full charge, the pull node, the repeat of a long hold and its end", charge},
            {"MB3", "basic attacks: the two sub-hits of the second step of the rapier, and the same strike", sub_hits},
            {"MB4", "basic attacks: the ends of a sequence (a weapon change, a dodge, a travel, an ability, a death)", sequence_ends},
            {"MB5", "basic attacks: two sessions on one wolf (rule MP-10)", two_sessions},
            {"MB6", "basic attacks: a replay, a wrap of the input number, a flood of claims, a reconnect", abuse},
            {"MB7", "basic attacks: the load of 60 swings on 8 wolves in a world of 4,000 far creatures, with a time limit", sequence_load},
            {"MB8", "basic attacks: the loader of the weapon actions, and the rule of stage 3.2a with no file", loader},
            {"MB9", "basic attacks: the check of the data tool (check_weapon_actions_tool.py)", tool_check}};
}

} // namespace soulwarden::backend::tests

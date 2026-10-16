// Cases for the corrections of the code review of stage 3.3b-M1 (stage 3.3b-M2): the findings M1-R1 to M1-R8, each with the proof of
// the reviewer in the form of the fixtures of this tree (the attack graphs of the test with the form and the times of the great axe and
// of the rapier of the game data, made-up accounts and characters; no file of the game). Each case fails on the tree of stage 3.3b-M1
// (commit 90012b3) and passes with the correction.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/heal_fixture.h"

namespace soulwarden::backend::tests {
namespace {

/** @return The visits of a character as one text (for a note). */
std::string visits_text(attack_fixture::Range& range) {
    return attack_fixture::joined(range.visits());
}

/** @return True when a fragment with this name is in the visits of the character. */
bool visited(attack_fixture::Range& range, const char* name) {
    const std::vector<std::string> names = range.visits();
    return std::find(names.begin(), names.end(), name) != names.end();
}

/**
 * RB1 (M1-R1): input numbers that come faster than 30 in a second do not skip the time of a charge. The press, then the steps s + 19
 * and s + 26 (with the hold and a contact in the window of the charged strike) in three reports, 0.067 s from the press to the contact:
 * no hit. The control: the same hold at the speed of the client (one step each 1/30 s) gives the charged hit.
 */
void fast_charge(const Options&) {
    using namespace attack_fixture;
    float charged = 0.0F;
    {
        Range range;
        world::Entity* wolf = range.wolf(1.5F);
        Attacker attacker{range.host, range.a, kPlace};
        attacker.idle(5);
        const std::size_t from = range.a.got.size();
        const std::uint16_t start = attacker.step({kActionAttack});
        while (static_cast<std::uint16_t>(attacker.input - start) < 25) {
            attacker.step({kActionAttackHold});
        }
        attacker.step({kActionAttackHold}, {contact_with(*wolf)});
        const std::vector<float> amounts = amounts_of(range.a, from);
        check(amounts.size() == 1 && visited(range, "Attack_Heavy_Charged"),
              "control: a hold at the speed of the client (26 steps, 0.87 s) gives one charged hit (" + visits_text(range) + ")");
        charged = amounts.empty() ? 0.0F : amounts.front();
    }
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    const std::size_t from = range.a.got.size();
    const std::uint16_t start = attacker.step({kActionAttack});
    const auto began = range.host.clock.now();
    // Each number is newer than the last one (the rule of the session); three reports, one simulation step apart.
    attacker.input = static_cast<std::uint16_t>(start + 18);
    attacker.step({kActionAttackHold});
    attacker.input = static_cast<std::uint16_t>(start + 25);
    attacker.step({kActionAttackHold}, {contact_with(*wolf)});
    const double elapsed = std::chrono::duration<double>(range.host.clock.now() - began).count();
    note("RB1: " + std::to_string(elapsed) + " s from the press to the contact, " + std::to_string(amounts_of(range.a, from).size())
         + " damage number(s), the charged amount " + std::to_string(charged) + ", visits " + visits_text(range));
    check(amounts_of(range.a, from).empty(), "M1-R1: a full charge does not hit 0.067 s after its press (fast input numbers)");
}

/**
 * RB2 (M1-R2): a hold that goes away before the full charge (no hold, no press, no release from 0.333 s on) is a release: the partial
 * heavy attack (Attack_Heavy), not the full charge.
 */
void absent_hold(const Options&) {
    using namespace attack_fixture;
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    const std::size_t from = range.a.got.size();
    const std::uint16_t start = attacker.step({kActionAttack});
    for (int index = 0; index < 8; ++index) {
        attacker.step({kActionAttackHold});
    }
    attacker.until(static_cast<std::uint16_t>(start + 26));
    attacker.step({}, {contact_with(*wolf)});
    note("RB2: visits " + visits_text(range) + ", " + std::to_string(amounts_of(range.a, from).size()) + " damage number(s)");
    check(!visited(range, "Attack_Heavy_Charged"), "M1-R2: the loss of the hold before the full charge gives no charged fragment");
    check(visited(range, "Attack_Heavy"), "the loss of the hold after the light window is a release: the partial heavy attack");
}

/**
 * RB3 (M1-R3): a press just after the end of the first rapier swing (the band of the length): a fresh first swing, with the second step
 * of the chain as its other fragment. The first contact hits with the first swing; a contact at the time of the second thrust of the
 * second step does not hit with the other fragment (the first hit committed the fragment).
 */
void both_alternates(const Options&) {
    using namespace attack_fixture;
    Range range;
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.step({kActionWeapon2});
    attacker.idle(5);
    const std::uint16_t first = attacker.click();
    // The first swing of the test rapier has the length 1.2 s: the step first + 40 has the clock 1.367 s (in the band of 0.25 s).
    attacker.until(static_cast<std::uint16_t>(first + 40));
    const std::uint16_t second = attacker.click();
    const std::size_t from = range.a.got.size();
    attacker.until(static_cast<std::uint16_t>(second + 10));
    attacker.step({}, {contact_with(*wolf)});
    attacker.until(static_cast<std::uint16_t>(second + 16));
    attacker.step({}, {contact_with(*wolf)});
    const std::vector<float> amounts = amounts_of(range.a, from);
    note("RB3: " + std::to_string(amounts.size()) + " damage number(s), visits " + visits_text(range));
    check(amounts.size() == 1, "M1-R3: a fresh first swing does not also hit the second thrust of the other fragment");
}

/**
 * RB4 (M1-R4): a second click in the buffered window of the first swing, then the reports of 17 steps are lost (the world clock goes
 * on). The second swing is due at the end of the window (0.567 s); its contact at the right time of the client (the step first + 26)
 * hits. The control: the same input with no loss.
 */
void lost_buffered_steps(const Options&) {
    using namespace attack_fixture;
    const auto run = [](bool lose, std::uint16_t& visitStart, std::uint16_t& light) {
        Range range;
        world::Entity* wolf = range.wolf(1.5F);
        Attacker attacker{range.host, range.a, kPlace};
        attacker.idle(5);
        const std::size_t from = range.a.got.size();
        light = attacker.click();
        attacker.until(static_cast<std::uint16_t>(light + 7));
        attacker.click();
        while (static_cast<std::uint16_t>(attacker.input - light) < 25) {
            if (!lose) {
                attacker.step();
                continue;
            }
            // A lost report: the entry is in the history of the client (the next report has it if it is one of the newest five).
            InputEntry entry = attacker.entry();
            attacker.history.push_front(entry);
            if (attacker.history.size() > 5) {
                attacker.history.pop_back();
            }
            range.host.clock.advance(kTick);
            range.host.tick();
            range.a.exchange(range.host);
        }
        attacker.step({}, {contact_with(*wolf)});
        visitStart = range.state()->visits.back().start;
        return amounts_of(range.a, from).size();
    };
    std::uint16_t start = 0;
    std::uint16_t light = 0;
    check(run(false, start, light) == 1, "control: the second swing hits when each report comes");
    const std::size_t hits = run(true, start, light);
    note("RB4: with the loss " + std::to_string(hits) + " hit(s), the second swing starts at the step light + "
         + std::to_string(static_cast<std::uint16_t>(start - light)) + " (due: light + 16)");
    check(hits == 1, "M1-R4: lost reports after an accepted buffered press do not move the clock of the next fragment");
}

/**
 * RB5 (M1-R5): the loader refuses a strike that starts after the end of its fragment (a known length), and a contact that comes after
 * the end of a fragment (its length, with the late tolerance of 2 steps) is no hit of it.
 */
void ended_fragment(const Options&) {
    using namespace attack_fixture;
    gamedata::WeaponActionTables bad = test_actions();
    bad.weapons[0].fragments[1].length = 0.1F; // Attack_Primary: its strike 0.3 to 0.4 s
    gamedata::WeaponActions loader;
    check(!loader.set(std::move(bad)), "M1-R5: the loader refuses a strike that starts after the end of its fragment");
    gamedata::WeaponActionTables tables = test_actions();
    tables.weapons[0].fragments[1].length = 0.35F; // the strike 0.3 to 0.4 s starts before the end: a file that loads
    Range range;
    check(range.host.weaponActions.set(std::move(tables)), "the loader takes a strike that starts before the end of its fragment");
    world::Entity* wolf = range.wolf(1.5F);
    Attacker attacker{range.host, range.a, kPlace};
    attacker.idle(5);
    const std::size_t from = range.a.got.size();
    const std::uint16_t light = attacker.click();
    // The swing ends at the step light + 10 (its clock 0.367 s is after 0.35 s); a contact 3 steps later (clock 0.467 s, in the window of
    // the strike with its late tolerance) is after the end.
    attacker.until(static_cast<std::uint16_t>(light + 13));
    attacker.step({}, {contact_with(*wolf)});
    note("RB5: " + std::to_string(amounts_of(range.a, from).size()) + " damage number(s), visits " + visits_text(range));
    check(amounts_of(range.a, from).empty(), "M1-R5: a contact 3 steps after the end of a fragment is no hit of it");
}

/** RB6 (M1-R6): the check of the data tool, case "incomplete": a missing fragment and an empty attachment file stop the tool. */
void incomplete_sources(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools/gamedata/check_weapon_actions_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path log = options.workDirectory / "rb6-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream in(log);
    std::string line;
    std::string incomplete;
    while (std::getline(in, line)) {
        if (line.find(" incomplete:") != std::string::npos) {
            incomplete = line;
        }
    }
    std::filesystem::remove(log);
    note("RB6: " + incomplete);
    check(status == 0 && incomplete.rfind("ok incomplete:", 0) == 0,
          "M1-R6: a missing fragment of a graph and an empty attachment file stop the tool (status 1), and the old file stays");
}

/**
 * RB7 (M1-R7): a hurt friend with Disease and 9 stacks of the arena heal debuff (its received factor of an ability heal is 0) is no target
 * of Divine Embrace: the cast goes on to the caster (hurt), which gains health, and the cost is the cost of a real heal.
 */
void zero_gain_friend(const Options&) {
    using namespace heal;
    HealShop shop(true);
    shop.enter_b_near(at(kPlace.x - 1.0F, kPlace.y));
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.staff(*shop.character(), kDivineNode);
    world::Entity* friendTarget = shop.world().find_character(kCharacterB);
    shop.character()->vitals.health = 100.0F;
    friendTarget->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.world().health().fought(*friendTarget);
    shop.give_status(*friendTarget, kDisease, 60.0F);
    shop.give_status(*friendTarget, kArenaDebuff, 60.0F, 9);
    const float received = shop.world().health().received(*friendTarget, world::HealKind::ability);
    check(near(received, 0.0F), "control: Disease and 9 arena stacks make the received ability heal of the friend 0");
    const float friendBefore = friendTarget->vitals.health;
    const float casterBefore = shop.character()->vitals.health;
    const std::uint64_t friendCasts = shop.ability_counts().healsFriend;
    shop.cast(false);
    shop.pass(1.2F);
    note("RB7: friend " + std::to_string(friendBefore) + " to " + std::to_string(friendTarget->vitals.health) + ", caster "
         + std::to_string(casterBefore) + " to " + std::to_string(shop.character()->vitals.health) + ", friend casts "
         + std::to_string(shop.ability_counts().healsFriend - friendCasts));
    check(shop.ability_counts().healsFriend == friendCasts, "M1-R7: a friend that cannot gain health is no target of the direct heal");
    check(shop.character()->vitals.health > casterBefore, "the cast goes on to the hurt caster, which gains health");
}

/** RB8 (M1-R8): nine heals with a number before one state of the owner: each one reaches the owner as its own number. */
void heal_history(const Options&) {
    using namespace heal;
    HealShop shop;
    shop.character()->vitals.health = 100.0F;
    shop.world().health().fought(*shop.character());
    shop.world().touch(shop.character()->id, world::ComponentKey::vitals);
    shop.walker.idle(3);
    const std::size_t from = shop.a.got.size();
    float real = 0.0F;
    for (int index = 0; index < 9; ++index) {
        world::HealRequest request;
        request.amount = 10.0F;
        request.kind = world::HealKind::lifeSteal;
        request.what = "a life steal of the test";
        real += shop.world().health().heal(*shop.character(), request).gained;
    }
    shop.walker.idle(3);
    float prior = 100.0F;
    float shown = 0.0F;
    unsigned numbers = 0;
    for (const auto& [health, flags] : vitals_of(shop.a, from)) {
        if ((flags & 2) != 0) {
            shown += std::max(health - prior, 0.0F);
            ++numbers;
        }
        prior = health;
    }
    note("RB8: real gain " + std::to_string(real) + ", " + std::to_string(numbers) + " number(s) with the gain " + std::to_string(shown));
    check(near(real, 90.0F), "control: nine heals of 10 before the state of the owner");
    check(numbers == 9 && near(shown, real), "M1-R8: each heal with a number reaches the owner as its own number");
}

} // namespace

std::vector<Case> review_combos_cases() {
    return {{"RB1", "review of 3.3b-M1, M1-R1: fast input numbers do not skip the time of a charge", fast_charge},
            {"RB2", "review of 3.3b-M1, M1-R2: a hold that goes away before the full charge is a release", absent_hold},
            {"RB3", "review of 3.3b-M1, M1-R3: one visit hits with one fragment (the first hit commits it)", both_alternates},
            {"RB4", "review of 3.3b-M1, M1-R4: lost reports do not move the clock of a buffered fragment", lost_buffered_steps},
            {"RB5", "review of 3.3b-M1, M1-R5: a strike after the end of its fragment (the loader, the contact)", ended_fragment},
            {"RB6", "review of 3.3b-M1, M1-R6: incomplete sources stop the data tool (check_weapon_actions_tool.py)", incomplete_sources},
            {"RB7", "review of 3.3b-M1, M1-R7: a friend with no gain is no target of a direct heal (two sessions)", zero_gain_friend},
            {"RB8", "review of 3.3b-M1, M1-R8: nine numbered heals before one owner state keep their numbers", heal_history}};
}

} // namespace soulwarden::backend::tests

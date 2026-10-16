// Cases for the corrections of the code review of stage 3.3b-M2 (stage 3.3b-M2b): the findings M2-R1 to M2-R10, each with the proof of
// the reviewer in the form of the fixtures of this tree (the axe and hammer graphs of the test with the form and the times of the game
// data, made-up accounts and characters; no file of the game). Each case fails on the tree of stage 3.3b-M2 (commit 5ed8e22) and passes
// with the correction.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gawh;
using json = nlohmann::json;

std::uint64_t pulls(GawhShop& shop) {
    return shop.host.world.creatures().forced().counts().started;
}

/**
 * RH1 (finding M2-R1, high): an item of the fragment of an ability runs at its due time on the world clock. A client that sends the
 * input number of the pull of Reap (0.433 s) or of the ground of Mighty Gavel (0.638 s) after only 0.1 s gets neither at that step;
 * each one runs one time at its due time.
 */
void events_wait_for_their_time(const Options&) {
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.wolf(-1.2F);
        const std::uint64_t before = pulls(shop);
        const std::uint16_t pressed = shop.press(0);
        const auto began = shop.host.clock.now() - kTick * 2;
        shop.walker.input = static_cast<std::uint16_t>(pressed + 12);
        shop.step();
        const double elapsed = std::chrono::duration<double>(shop.host.clock.now() - began).count();
        note("Reap: " + std::to_string(elapsed) + " s after the press, input step +13: " + std::to_string(pulls(shop) - before) + " pull(s)");
        check(pulls(shop) == before, "Reap: an input number that reaches the pull (0.433 s) 0.1 s after the press runs no pull");
        shop.pass(0.6F);
        check(pulls(shop) == before + 1, "Reap: the pull waits for its due time on the world clock and runs one time");
    }
    {
        GawhShop shop;
        shop.hold("TestHammer");
        shop.bar(kWarHammer, {"Ability_WarHammer_MightyGavel", nullptr, nullptr});
        const std::uint16_t pressed = shop.press(0);
        shop.walker.input = static_cast<std::uint16_t>(pressed + 19);
        shop.step();
        check(shop.spells().empty(), "Mighty Gavel: an input number that reaches the ground (0.638 s) 0.1 s after the press casts no spell");
        shop.walker.idle(20);
        check(shop.spells().size() == 1, "Mighty Gavel: the ground waits for its due time and comes one time");
    }
    {
        // Control: the normal input runs the pull one time, at its step.
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.wolf(-1.2F);
        const std::uint64_t before = pulls(shop);
        const std::uint16_t pressed = shop.press(0);
        shop.until(static_cast<std::uint16_t>(pressed + 10));
        check(pulls(shop) == before, "control: the normal input runs no pull before the time of its strike");
        shop.until(static_cast<std::uint16_t>(pressed + 16));
        shop.walker.idle(2);
        check(pulls(shop) == before + 1, "control: the normal input runs the pull one time");
    }
}

/**
 * RH2 (finding M2-R2): one input step with two identical contact items for one creature, at the end of the first spin of Whirlwind (the
 * next spin is in its early tolerance), is one hit: the strike of the time window, then the ledger (a repeat). Control: four separate
 * contacts in the four spins are four hits.
 */
void one_contact_one_strike(const Options&) {
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        shop.until(static_cast<std::uint16_t>(pressed + 24));
        const std::uint64_t before = shop.abilities().counts().strikeHits;
        const float health = wolf->vitals.health;
        const InputContactItem contact = touch(*wolf);
        shop.step({}, {contact, contact});
        const std::uint64_t hits = shop.abilities().counts().strikeHits - before;
        note("two identical contacts in one step: " + std::to_string(hits) + " hit(s), damage " + std::to_string(health - wolf->vitals.health)
             + "; claims of A: " + claim_counts(shop.host));
        check(hits == 1, "two identical contacts of one input step are one hit (the second is a repeat of the same strike)");
    }
    {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        for (const unsigned spin : {15U, 27U, 38U, 48U}) {
            shop.contact_at(static_cast<std::uint16_t>(pressed + spin), *wolf);
        }
        check(shop.abilities().counts().strikeHits == 4, "control: four contacts in the four spins are four hits");
    }
}

/**
 * RH3 (finding M2-R3, two sessions): Clear Out with Power Cleaner gives the fortify to A and to the friend B; after the end of the
 * spell, of the status, and of the cooldown a second cast gives the fortify again to both (the spell row has no ClearOnEnd: the end of
 * the first spell left its aura rows).
 */
void second_fortify(const Options&) {
    GawhShop shop;
    shop.enter_b();
    shop.world().set_test_friends(kCharacterA, kCharacterB);
    shop.hold("TestHammer");
    shop.bar(kWarHammer, {"Ability_WarHammer_ClearOut", nullptr, nullptr});
    shop.learn({kFortifyAoe});
    world::Entity* wolf = shop.wolf(-2.0F);
    std::uint16_t pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 23), *wolf);
    shop.walker.idle(2);
    check(shop.status_of(kFortifyBuff) != nullptr && shop.status_of(kFortifyBuff, kCharacterB) != nullptr,
          "control: the first Clear Out gives the fortify to A and to B");
    shop.pass(12.0F);
    check(shop.spells().empty() && shop.status_of(kFortifyBuff) == nullptr && shop.status_of(kFortifyBuff, kCharacterB) == nullptr,
          "control: the first spell, the statuses, and the cooldown ended");
    wolf = shop.wolf(-2.0F, 0.5F);
    pressed = shop.press(0);
    shop.contact_at(static_cast<std::uint16_t>(pressed + 23), *wolf);
    shop.walker.idle(2);
    note("second cast: accepted casts " + std::to_string(shop.accepted()) + ", rule spells " + std::to_string(shop.abilities().counts().ruleSpells)
         + ", fortify A " + std::to_string(shop.status_of(kFortifyBuff) != nullptr) + ", B "
         + std::to_string(shop.status_of(kFortifyBuff, kCharacterB) != nullptr));
    check(shop.accepted() == 2 && shop.abilities().counts().ruleSpells == 2, "control: two casts and two spells of the rule");
    check(shop.status_of(kFortifyBuff) != nullptr && shop.status_of(kFortifyBuff, kCharacterB) != nullptr,
          "the second Clear Out gives the fortify again to A and to B");
}

/**
 * RH4 (finding M2-R4): the first strike of Reap hits a wolf in front; before the pull the wolf is 2 m behind the caster. The pull tests
 * the shape, the height, and the sight for each target: the wolf gets no pull and no root of Gravity. Controls: a wolf in front that the
 * first strike hit gets the pull; a wolf that the first strike hit at 45 degrees gets the pull on the line to it (the homing of the client
 * turns the body to its target, ability_rules::kHomingTurnCosine).
 */
void earlier_target_in_shape(const Options&) {
    for (const int variant : {0, 1, 2}) {
        const bool behind = variant == 1;
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.learn({kGravity});
        // Variant 2: the wolf is 2 m away at 45 degrees (1.41 m to the side of the camera axis: out of the shapes on that axis).
        world::Entity* wolf = variant == 2 ? shop.wolf(-1.414F, -1.414F) : shop.wolf(-2.0F);
        const std::uint16_t pressed = shop.press(0);
        shop.contact_at(static_cast<std::uint16_t>(pressed + 8), *wolf);
        check(shop.abilities().counts().strikeHits == 1, "control: the first strike hits the wolf in front");
        if (behind) {
            const state::WorldPosition own = shop.character()->position;
            shop.world().place_creature(*wolf, {own.x + 2.0F, own.y, own.z});
        }
        const std::uint64_t before = pulls(shop);
        shop.until(static_cast<std::uint16_t>(pressed + 16));
        shop.walker.idle(2);
        const std::uint64_t moved = pulls(shop) - before;
        note(std::string(variant == 0 ? "in front" : behind ? "behind" : "hit at 45 degrees") + ": " + std::to_string(moved) + " pull(s), root "
             + std::to_string(shop.creature_has(*wolf, kGravityRoot)));
        if (behind) {
            check(moved == 0 && !shop.creature_has(*wolf, kGravityRoot), "an earlier hit gives no pull to a wolf 2 m behind the caster");
        } else if (variant == 0) {
            check(moved == 1, "control: the wolf in front gets the pull");
        } else {
            check(moved == 1, "a wolf that the first strike hit at 45 degrees gets the pull on the line to it (the turn of the body by the homing)");
        }
    }
}

/**
 * RH4, second part (the axis of the shapes, stage 3.3b-M2b): a wolf 3 m away at 60 degrees from the direction of the camera at the press.
 * The caster moves 1.2 m to it before the pull (the root motion of Reap with the homing of the client): on the camera axis the wolf is
 * then 1.56 m to the side, out of the shapes; the pull takes the direction of the move, and the wolf is straight ahead: it gets the pull.
 */
void pull_axis_follows_the_move(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    const float east = -3.0F * 0.5F;
    const float north = -3.0F * 0.8660254F;
    world::Entity* wolf = shop.wolf(east, north);
    const std::uint64_t before = pulls(shop);
    const std::uint16_t pressed = shop.press(0);
    for (int step = 0; step < 8; ++step) {
        shop.walker.position.x += 0.15F * east / 3.0F;
        shop.walker.position.y += 0.15F * north / 3.0F;
        shop.walker.idle(1);
    }
    shop.until(static_cast<std::uint16_t>(pressed + 16));
    shop.walker.idle(2);
    note("the caster moved 1.2 m to the wolf at 60 degrees: " + std::to_string(pulls(shop) - before) + " pull(s), the wolf is "
         + std::to_string(world::horizontal_distance(shop.character()->position, wolf->position)) + " m from A");
    check(pulls(shop) == before + 1, "the axis of the pull is the move of the caster since the press: the wolf at 60 degrees gets the pull");
}

/**
 * RH10 (finding M2-R10): 40 creatures in a pull shape of Reap, no contact: the server pulls the 32 nearest ones (kAreaMost, the limit of
 * an area phase of the server).
 */
void pull_budget(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    std::vector<world::Entity*> wolves;
    for (int index = 0; index < 40; ++index) {
        // From 1.0 m to 1.39 m in front: the nearest ones first.
        wolves.push_back(shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x - 1.0F - 0.01F * index, kPlace.y), 90.0F).entity);
    }
    shop.walker.idle(5);
    const std::uint64_t before = pulls(shop);
    const std::uint16_t pressed = shop.press(0);
    shop.until(static_cast<std::uint16_t>(pressed + 16));
    shop.walker.idle(2);
    const std::uint64_t moved = pulls(shop) - before;
    shop.pass(1.0F);
    // The 8 farthest creatures did not move (their place is the place of their spawn).
    std::size_t farthestMoved = 0;
    for (std::size_t index = world::ability_rules::kAreaMost; index < wolves.size(); ++index) {
        const float spawnX = kPlace.x - 1.0F - 0.01F * static_cast<float>(index);
        farthestMoved += std::fabs(wolves[index]->position.x - spawnX) > 0.01F ? 1 : 0;
    }
    note("40 creatures in the shape: " + std::to_string(moved) + " pull(s); the 8 farthest moved: " + std::to_string(farthestMoved));
    check(moved == world::ability_rules::kAreaMost && farthestMoved == 0, "the pull takes the 32 nearest creatures, not more");
}

/**
 * RH5 (finding M2-R5): Maelstrom with Absorb, pressed in the buffered window of Reap, waits for the end of that window. A change to the
 * hammer before its start cancels the start: no shield. Control: with no change the shield comes at the start of the fragment.
 */
void cancelled_start_no_shield(const Options&) {
    for (const bool change : {false, true}) {
        GawhShop shop;
        shop.hold("TestAxe");
        check(shop.wear("TestHammer", world::paperdoll::kMainHand1 + 1).ok(), "setup: a hammer in the other weapon slot");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Maelstorm", nullptr});
        shop.learn({kAbsorb});
        const std::uint16_t reap = shop.press(0);
        shop.until(static_cast<std::uint16_t>(reap + 8));
        shop.press(1);
        check(shop.accepted() == 2 && shop.abilities().counts().delayedStarts == 1 && shop.spells().empty(),
              "setup: Maelstrom waits for the end of the buffered window of Reap, with no shield yet");
        if (change) {
            shop.world().combat().select_weapon(1, 2);
        }
        shop.pass(1.0F);
        note(std::string(change ? "weapon change" : "no change") + ": " + std::to_string(shop.spells().size()) + " shield(s)");
        if (change) {
            check(shop.spells().empty(), "a cancelled buffered Maelstrom makes no shield");
        } else {
            check(shop.spells().size() == 1, "control: the buffered Maelstrom makes its shield at its start");
        }
    }
}

/**
 * RH6 (finding M2-R6): a press of an empty place of the bar after Reap (refused) closes the strikes of the cast: the later contact of
 * the first strike is no hit. Controls: with no second press the contact is a hit; a press of Maelstrom in the buffered window of Reap
 * (accepted, it starts at the end of the window) leaves the strike of Reap that still plays open.
 */
void refused_press_closes_strikes(const Options&) {
    for (int variant = 0; variant < 3; ++variant) {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", variant == 2 ? "Ability_Greataxe_Maelstorm" : nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        if (variant == 1) {
            shop.press(1);
            check(shop.refused(world::CastVerdict::emptyPlace) == 1, "setup: the second press names an empty place");
        } else if (variant == 2) {
            shop.until(static_cast<std::uint16_t>(pressed + 9));
            shop.press(1);
            check(shop.accepted() == 2 && shop.abilities().counts().delayedStarts == 1, "setup: Maelstrom waits for the buffered window");
        }
        shop.contact_at(static_cast<std::uint16_t>(pressed + 11), *wolf);
        const std::uint64_t hits = shop.abilities().counts().strikeHits;
        note(std::string(variant == 0 ? "no second press" : variant == 1 ? "empty place" : "accepted buffered press") + ": "
             + std::to_string(hits) + " hit(s); claims: " + claim_counts(shop.host));
        if (variant == 1) {
            check(hits == 0, "a refused press closes the strikes of the earlier cast: the later contact is no hit");
        } else {
            check(hits == 1, variant == 0 ? "control: the contact of the first strike is a hit"
                                          : "control: an accepted buffered press leaves the strike of Reap that plays open");
        }
    }
}

/**
 * RH8 (finding M2-R8): the combat tables have no row Warhammer_Damage_ClearOut (the row of the strike of the fragment of Clear Out): the
 * press is refused (noRule) before the cooldown, and a contact hurts nothing. Control: with the row the press is accepted.
 */
void missing_fragment_row(const Options&) {
    for (const bool complete : {true, false}) {
        GawhShop shop;
        if (!complete) {
            gamedata::CombatData data = gawh_combat();
            std::erase_if(data.damageRows, [](const gamedata::DamageRow& row) { return row.id == "Warhammer_Damage_ClearOut"; });
            check(shop.host.combat.set(data), "setup: the combat tables are valid with no strike row of Clear Out");
        }
        shop.hold("TestHammer");
        shop.bar(kWarHammer, {"Ability_WarHammer_ClearOut", nullptr, nullptr});
        world::Entity* wolf = shop.wolf(-1.5F);
        const std::uint16_t pressed = shop.press(0);
        shop.contact_at(static_cast<std::uint16_t>(pressed + 23), *wolf);
        note(std::string(complete ? "complete" : "no strike row") + ": accepted " + std::to_string(shop.accepted()) + ", noRule "
             + std::to_string(shop.refused(world::CastVerdict::noRule)) + ", hits " + std::to_string(shop.abilities().counts().strikeHits)
             + ", cooldowns " + std::to_string(shop.character()->cooldowns.size()));
        if (complete) {
            check(shop.accepted() == 1 && shop.abilities().counts().strikeHits == 1, "control: with the row the press is accepted and hits");
        } else {
            check(shop.accepted() == 0 && shop.refused(world::CastVerdict::noRule) == 1 && shop.character()->cooldowns.empty(),
                  "a fragment whose strike row is not in the data refuses the press before the cooldown");
        }
    }
}

/**
 * RH9 (finding M2-R9): the loader of the ability data (format 5) refuses a file whose status action names a status that the file does not
 * have. Control: the same file with no status action loads.
 */
void action_status_link(const Options& options) {
    const json good{{"format", 5},
                    {"statusRows", 0},
                    {"cooldowns", {{"Review_Cooldown", 1, 0}}},
                    {"statusEffects", json::array()},
                    {"spells", json::array()},
                    {"manaCosts", json::array()},
                    {"staminaCosts", json::array()},
                    {"abilityCosts", json::array()},
                    {"abilityRows", json::array()},
                    {"statusCategories", json::array()},
                    {"actionSpells", json::array()},
                    {"actionNodes", json::array()},
                    {"actionStatuses", json::array()}};
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "review-action-status.json";
    const auto load = [&](const json& document, gamedata::AbilityData& data) {
        std::ofstream(path) << document.dump();
        return data.load(path.string());
    };
    gamedata::AbilityData control;
    check(load(good, control), "control: a complete file with no status action loads");
    json bad = good;
    bad["actionStatuses"].push_back(json::array(
        {"playeractions_ability_warhammer", "Warhammer_Passve_SprintDmgRed",
         json::array({json::array({json::array({"node", kGuardedSprint, false}), json::array({"tag", "Warhammer", false})})}),
         json::array({"Review_Status_Not_In_File"})}));
    gamedata::AbilityData data;
    const bool accepted = load(bad, data);
    note("a status action with no status row: accepted " + std::to_string(accepted) + "; " + data.error());
    check(!accepted, "a status action that names a status with no row makes the loader refuse the file");
    std::filesystem::remove(path);
}

/**
 * RH7 (finding M2-R7): the check of the data tool (tools/gamedata/check_weapon_actions_tool.py), case "incomplete abilities": a missing
 * fragment of an ability of the great axe, a missing fragment that its action selects, and an attachment file with no damage clip of the
 * ability stop the tool (status 1), and the old file stays; the complete sources write the ability, its selection, and its strike.
 */
void incomplete_ability_sources(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools/gamedata/check_weapon_actions_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path log = options.workDirectory / "rh7-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" > \"" + log.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream in(log);
    std::string line;
    std::string found;
    while (std::getline(in, line)) {
        if (line.find(" incomplete abilities:") != std::string::npos) {
            found = line;
        }
    }
    std::filesystem::remove(log);
    note("RH7: " + (found.empty() ? std::string("no line of the case") : found));
    check(status == 0 && found.rfind("ok incomplete abilities:", 0) == 0,
          "M2-R7: incomplete sources of an ability stop the data tool (status 1), and the old file stays");
}

} // namespace

std::vector<Case> review_gawh_cases() {
    return {{"RH1", "review 3.3b-M2, M2-R1: an item of the fragment of an ability waits for its due time on the world clock",
             events_wait_for_their_time},
            {"RH2", "review 3.3b-M2, M2-R2: one contact of one step is a hit of one strike (the time window, then the ledger)",
             one_contact_one_strike},
            {"RH3", "review 3.3b-M2, M2-R3: a second Clear Out gives the fortify again (two sessions)", second_fortify},
            {"RH4", "review 3.3b-M2, M2-R4: an earlier target of Reap gets the pull only in a pull shape", earlier_target_in_shape},
            {"RH11", "review 3.3b-M2, M2-R4: the axis of the pull shapes is the move of the caster since the press", pull_axis_follows_the_move},
            {"RH5", "review 3.3b-M2, M2-R5: a cancelled buffered Maelstrom makes no shield", cancelled_start_no_shield},
            {"RH6", "review 3.3b-M2, M2-R6: a refused later press closes the strikes of the fragment of a cast", refused_press_closes_strikes},
            {"RH7", "review 3.3b-M2, M2-R7: incomplete sources of an ability stop the data tool (check_weapon_actions_tool.py)",
             incomplete_ability_sources},
            {"RH8", "review 3.3b-M2, M2-R8: a fragment with a missing part refuses the press before the cooldown", missing_fragment_row},
            {"RH9", "review 3.3b-M2, M2-R9: the ability data loader refuses a status action with no status row", action_status_link},
            {"RH10", "review 3.3b-M2, M2-R10: the server pull of Reap takes 32 creatures at most, the nearest", pull_budget}};
}

} // namespace soulwarden::backend::tests

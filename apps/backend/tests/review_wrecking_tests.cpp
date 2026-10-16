// Cases of the corrections of the code review of stage 3.3b-M2b (findings M2b-R1 to M2b-R5), in stage 2.5a. Each case is the proof
// case of the reviewer (the independent cases WB1 to WB5 of the review) with its controls; each one fails on the tree of stage
// 3.3b-M2b and passes with the correction. The fixture is the shop of the great axe and the war hammer (support/gawh_fixture.h):
// made-up ids, the form of the game data.
//
//   RX1  M2b-R3: a later refused press closes the fragment that plays, also after a press in the client cooldown and with a buffered cast
//   RX2  M2b-R2: an end of the sequence cancels each item that waits for its due time, also after the end of its visit (a weapon change,
//        a later basic attack, a travel)
//   RX3  M2b-R5: an action whose spell is not in the data refuses the cast before the cooldown
//   RX4  M2b-R4: an ability of the great axe with no damage clip in one choice, or with no action list, stops the data tool
//        (check_weapon_actions_tool.py, case "incomplete abilities")
//   RX5  M2b-R1: the spell of the action of an immediate start waits for the due time of its visit on the world clock

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gawh;

/** RX1: two refused presses close the old cast (finding M2b-R3). */
void repeated_refusal(const Options&) {
    for (int variant : {0, 1, 2, 3}) {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", variant == 3 ? "Ability_Greataxe_Maelstorm" : nullptr, nullptr});
        auto* wolf = shop.wolf(-1.5F);
        const auto pressed = shop.press(0);
        if (variant == 3) {
            shop.until(static_cast<std::uint16_t>(pressed + 9));
            shop.step({kAbilityBits[1], kVirtualBit});
            check(shop.accepted() == 2 && shop.abilities().counts().delayedStarts == 1, "control: Maelstrom is buffered while Reap still plays");
        } else if (variant != 1) {
            shop.press(0);
            check(shop.refused(world::CastVerdict::cooldown) == 1, "control: Reap again is in the client cooldown");
        }
        if (variant == 3) {
            shop.step({kAbilityBits[2]});
            check(shop.refused(world::CastVerdict::emptyPlace) == 1, "control: a press of an empty place after the buffered cast is refused");
        } else if (variant != 0) {
            shop.press(1);
            check(shop.refused(world::CastVerdict::emptyPlace) == 1, "control: a press of an empty place is refused");
        }
        shop.contact_at(static_cast<std::uint16_t>(pressed + 11), *wolf);
        const auto hits = shop.abilities().counts().strikeHits;
        const char* what[] = {"only the cooldown refusal: the strike of Reap stays open (1 hit)",
                              "only the empty place refusal: Reap is closed (no hit)",
                              "the cooldown refusal, then the empty place refusal: Reap is closed (no hit)",
                              "a buffered Maelstrom, then the empty place refusal while Reap plays: Reap is closed (no hit)"};
        check(hits == (variant == 0 ? 1U : 0U), std::string(what[variant]) + " (hits " + std::to_string(hits) + ")");
    }
}

/** RX2: an end of the sequence cancels each waiting item (finding M2b-R2). */
void delayed_event_cancel(const Options&) {
    for (int variant : {0, 1, 2}) {
        GawhShop shop;
        shop.hold("TestHammer");
        shop.bar(kWarHammer, {"Ability_WarHammer_MightyGavel", nullptr, nullptr});
        check(shop.wear("TestAxe", world::paperdoll::kMainHand1 + 1).ok(), "setup: the axe in the second weapon slot");
        const auto pressed = shop.press(0);
        // Input numbers that run ahead of the world clock: the ground of Mighty Gavel waits for its due time.
        shop.walker.input = static_cast<std::uint16_t>(pressed + (variant == 0 ? 19 : 59));
        shop.step();
        check(shop.spells().empty(), "control: the ground of the early Gavel is not there before its due time");
        if (variant == 2) {
            // A new basic attack plays when the weapon changes: the visit of Gavel is history.
            shop.step({kVirtualBit});
        }
        shop.world().combat().select_weapon(1, 2);
        shop.host.advance(std::chrono::milliseconds(700));
        shop.host.tick();
        const char* what[] = {"a weapon change while the visit of Gavel plays: no ground",
                              "a weapon change after the end of the visit of Gavel (at rest): no ground",
                              "a weapon change while a later basic attack plays: no ground of the old visit"};
        check(shop.spells().empty(), what[variant]);
    }
    for (bool ended : {false, true}) {
        GawhShop shop;
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
        shop.learn({kGravity});
        const auto pressed = shop.press(0);
        shop.walker.input = static_cast<std::uint16_t>(pressed + (ended ? 59 : 12));
        shop.step();
        check(shop.world().travel(kCharacterA, at(kPlace.x - 100.0F, kPlace.y)).error == world::TravelError::none,
              "control: the travel before the due time of the pull");
        const auto place = shop.character()->position;
        auto* wolf = shop.world().creatures().spawn("Test_Wolf", {place.x - 1.2F, place.y, place.z}, 90.0F).entity;
        check(wolf != nullptr, "setup: a wolf at the place of the travel");
        const auto before = shop.world().creatures().forced().counts().started;
        shop.host.advance(std::chrono::milliseconds(400));
        shop.host.tick();
        const auto pulls = shop.world().creatures().forced().counts().started - before;
        check(pulls == 0 && !shop.creature_has(*wolf, kGravityRoot),
              std::string("a travel ") + (ended ? "after the end of the visit of Reap" : "while Reap plays")
                  + ": no pull and no root of the old Reap at the new place (pulls " + std::to_string(pulls) + ")");
    }
}

/** RX3: an action spell that the data does not have refuses the cast before the cooldown (finding M2b-R5). */
void missing_action_spell(const Options&) {
    for (bool complete : {true, false}) {
        GawhShop shop;
        if (!complete) {
            auto data = gawh_abilities();
            std::erase_if(data.spells, [](const auto& spell) { return spell.id == kShieldSpell; });
            check(shop.host.abilities.set(data), "control: the tables with no row of the shield load");
        }
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {"Ability_Greataxe_Maelstorm", nullptr, nullptr});
        shop.learn({kAbsorb});
        shop.press(0);
        if (complete) {
            check(shop.accepted() == 1 && shop.spells().size() == 1, "control: Maelstrom with Absorb starts its shield");
        } else {
            check(shop.accepted() == 0 && shop.refused(world::CastVerdict::noRule) == 1 && shop.character()->cooldowns.empty(),
                  "an action whose spell is not in the data: the press is refused (noRule) before the cooldown");
        }
    }
}

/** RX4: the data tool stops for an incomplete ability (finding M2b-R4). */
void incomplete_ability_sources(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata/check_weapon_actions_tool.py";
    const std::filesystem::path output = options.workDirectory / "check-weapon-actions-rx4.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + script.lexically_normal().string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    bool abilities = false;
    for (std::string line; std::getline(log, line);) {
        if (line.find("incomplete abilities") != std::string::npos) {
            note(line.substr(0, 300));
            abilities = line.rfind("ok", 0) == 0 && line.find("missing base fragment damage: status 1, old file kept") != std::string::npos
                        && line.find("missing selected fragment damage: status 1, old file kept") != std::string::npos
                        && line.find("missing ability action list: status 1, old file kept") != std::string::npos;
        }
    }
    std::filesystem::remove(output);
    check(status == 0 && abilities,
          "check_weapon_actions_tool.py: no damage clip of the base fragment, no damage clip of the selected fragment, and no ability "
          "action list each stop the tool (status 1, the old file stays)");
}

/** RX5: the spell of the action of an immediate start waits for the due time of its visit (finding M2b-R1). */
void immediate_spell_world_time(const Options&) {
    GawhShop shop;
    shop.hold("TestAxe");
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Maelstorm", nullptr});
    shop.learn({kAbsorb});
    const auto pressed = shop.press(0);
    // Input numbers that reach the instant window of Reap after 0.1 s of the world clock.
    shop.walker.input = static_cast<std::uint16_t>(pressed + 26);
    shop.step();
    shop.press(1);
    const auto* visit = shop.world().combat().sequences().newest(kCharacterA);
    const double untilDue = std::chrono::duration<double>(visit->dueAt - shop.host.clock.now()).count();
    check(shop.accepted() == 2 && untilDue > 0.5, "control: the immediate start of Maelstrom has a due time " + std::to_string(untilDue) + " s ahead");
    check(shop.spells().empty(), "the shield does not start before the due time of the visit");
    shop.host.advance(std::chrono::milliseconds(static_cast<long long>(untilDue * 1000.0) + 50));
    shop.host.tick();
    check(shop.spells().size() == 1, "at the due time of the visit: one shield");
    GawhShop normal;
    normal.hold("TestAxe");
    normal.bar(kGreatAxe, {"Ability_Greataxe_Reap", "Ability_Greataxe_Maelstorm", nullptr});
    normal.learn({kAbsorb});
    const auto reap = normal.press(0);
    normal.until(static_cast<std::uint16_t>(reap + 27));
    normal.step();
    normal.press(1);
    check(normal.accepted() == 2 && normal.spells().size() == 1, "control: Maelstrom at the normal time starts one shield at once");
}

} // namespace

std::vector<Case> review_wrecking_cases() {
    return {{"RX1", "review 3.3b-M2b, M2b-R3: a later refused press closes the fragment that plays (a cooldown press before, a buffered cast)",
             repeated_refusal},
            {"RX2", "review 3.3b-M2b, M2b-R2: an end of the sequence cancels each waiting item, also after the end of its visit", delayed_event_cancel},
            {"RX3", "review 3.3b-M2b, M2b-R5: an action spell that the data does not have refuses the cast before the cooldown", missing_action_spell},
            {"RX4", "review 3.3b-M2b, M2b-R4: no damage clip of one choice, or no ability action list, stops the data tool", incomplete_ability_sources},
            {"RX5", "review 3.3b-M2b, M2b-R1: the spell of an immediate start waits for the due time of its visit", immediate_spell_world_time}};
}

} // namespace soulwarden::backend::tests

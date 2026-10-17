// The corrections of the code review of stage B3 (~/Games/soulwarden/work/evidence/bow-build/b3/code-review/FINDINGS.md), corrected in stage B4.
// Made-up ids and the rows of the game data (support/bow_rules_fixture.h, support/haste_fixture.h); no capture and no id of an account. The
// proof that a case fails on the tree before stage B4: build this file with SOULWARDEN_B4_BASELINE.
//
//   BY1  B3-R1: an accepted basic shot from the mark CanAttackCancel of a dodge on ends the dodge action: the haste of Dodge and Weave and the
//        empower of Evasive Tactics come at once, and the mark CanTransition gives them no second time; a shot before the mark does not end it
//   BY2  B3-R2: the time factor and the rules after a dodge take the cap of the sources of a row (Alacrity II IsStackableMax 5 with six sources,
//        Refreshing Evasion IsStackableMax 1 with two)
//   BY3  B3-R3: an OnStackStatusEffect that names no status row: the loader and the set of the ability data refuse it; the tool checks the link
//        and the three columns of the stack chain
// The correction B3-R4 is in case HS7 (tests/haste_tests.cpp): the path of the run of A needs the haste allowance (the same path with no haste
// is refused).

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/bow_rules_fixture.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {

using namespace rules;

/** BY1 (B3-R1): an attack that replaces the dodge action. */
void attack_ends_dodge(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {kDodgeWeave, kEvasive});
    (void)shop.wolf_at(6.0F);
    const auto launches = [&] { return shop.world().projectiles().counts().shots[static_cast<unsigned>(world::ShotVerdict::accepted)]; };
    const auto started = shop.host.clock.now();
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge at full stamina with the bow");
    shop.wait(300);
    const auto before = launches();
    shop.send_item(BowShop::bow_item("Basic", "TestBowLight"), {kActionAttack, kActionAttackRelease});
    const float elapsed = std::chrono::duration<float>(shop.host.clock.now() - started).count();
    note("the shot at " + std::to_string(elapsed) + " s of the dodge: haste " + std::to_string(shop.find(kDodgeHaste) != nullptr) + ", empower "
         + std::to_string(shop.find(kEvasiveEmpower) != nullptr));
    check(launches() == before + 1, "setup: the basic shot is accepted after the mark CanAttackCancel (0.2667 s)");
    check(shop.find(kDodgeHaste) != nullptr && shop.find(kEvasiveEmpower) != nullptr && shop.attack_cancels() == 1,
          "the shot ends the dodge action: the haste of Dodge and Weave and the empower of Evasive Tactics come at once");
    const float haste = shop.left(kDodgeHaste);
    shop.wait(300);
    check(shop.dodge_statuses() == 1 && shop.after_dodge() == 1 && near(shop.left(kDodgeHaste), haste - 0.3F, 0.05F),
          "the mark CanTransition (0.5 s) gives the exit effects no second time");
    // A shot before the mark does not end the action: the haste comes at the mark CanTransition.
    RulesShop early;
    early.ready(kCharacterA, {}, {kDodgeWeave});
    (void)early.wolf_at(6.0F);
    check(early.dodge() == world::DodgeVerdict::accepted, "setup: a second world, a dodge");
    early.wait(150);
    early.send_item(BowShop::bow_item("Basic", "TestBowLight"), {kActionAttack, kActionAttackRelease});
    check(early.find(kDodgeHaste) == nullptr && early.attack_cancels() == 0, "a shot before the mark CanAttackCancel does not end the action");
    early.wait(450);
    check(early.find(kDodgeHaste) != nullptr && early.dodge_statuses() == 1, "the haste comes at the mark CanTransition");
}

/** BY2 (B3-R2): the cap of the sources of a row in the time factor and in the rules after a dodge. */
void source_caps(const Options&) {
    // Six permitted items with Alacrity II (IsStackableMax 5): the factor of five sources (the retail charm permits armor, jewelry, weapons).
    HasteShop shop;
    gamedata::PerkTables perks = haste_perks();
    for (gamedata::PerkRow& perk : perks.perks) {
        if (perk.id == kAlacrity || perk.id == kDodgeCdr) {
            perk.itemClass = "EquippableTwoHand+Armor+Jewelry+Bow";
        }
    }
    check(shop.host.perks.set(perks), "setup: Alacrity II and Refreshing Evasion with their retail item classes");
    std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
    items.push_back(shop::definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
    items.push_back(shop::definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
    items.push_back(shop::definition("TestHead", "Armor", "Armor+EquippableHead+Light", 5, 100, 600));
    shop.host.catalog.set(items);
    shop.ready(kCharacterA, {kAlacrity, kDodgeCdr, "", "", ""}, {kArcherSpeed}, 200);
    const float potency = shop.potency(kAlacrity);
    const auto give = [&](const char* item, std::uint32_t slot) {
        world::GiveOptions how;
        how.gearScore = 200;
        how.perks = {kAlacrity, kDodgeCdr, "", "", ""};
        const world::ItemResult result = shop.world().inventory().give(kCharacterA, item, 1, how);
        check(result.ok() && !result.slots.empty() && shop.world().inventory().equip(kCharacterA, result.slots.front(), slot).ok(),
              std::string("setup: ") + item + " with Alacrity II");
    };
    give("TestHead", world::paperdoll::kHead);
    give("TestChest", world::paperdoll::kChest);
    give("TestLegs", world::paperdoll::kLegs);
    give("TestAmulet", world::paperdoll::kAmulet);
    const gamedata::StatusRow* row = shop.host.abilities.status(kSwapHaste);
    const float five = shop.world().passives().duration_factor(*shop.character(), *row);
    give("TestBow", world::paperdoll::kMainHand1 + 2);
    unsigned copies = 0;
    for (const world::GearPerk& entry : shop.world().gear().perks_of(*shop.character())) {
        copies += entry.perk->id == kAlacrity ? 1U : 0U;
    }
    const float six = shop.world().passives().duration_factor(*shop.character(), *row);
    note("the time factor with five sources " + std::to_string(five) + ", with six " + std::to_string(six));
    check(copies == 6, "setup: six items with Alacrity II");
    check(near(five, 1.0F + 5.0F * 0.094F * potency, 0.001F) && near(six, five, 0.0001F), "a sixth source does not exceed IsStackableMax 5");
    // Refreshing Evasion (IsStackableMax 1) on six items: one reduction of 0.005 of the cooldown at the end of one dodge.
    shop.press(2);
    shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.walker.idle(90);
    const float running = shop.remaining(kRapid);
    const auto start = shop.host.clock.now();
    (void)shop.world().stamina().change(*shop.character(), 100.0F, "a test");
    check(shop.dodge() == world::DodgeVerdict::accepted, "setup: a dodge");
    shop.wait(520);
    const float elapsed = std::chrono::duration<float>(shop.host.clock.now() - start).count();
    const float reduction = running - elapsed - shop.remaining(kRapid);
    note("the reduction of the cooldown at the end of the dodge: " + std::to_string(reduction) + " s");
    check(shop.after_dodge() == 1 && near(reduction, 0.005F * shop.potency(kDodgeCdr) * 14.0F, 0.01F),
          "the end of one dodge gives Refreshing Evasion one time (IsStackableMax 1)");
}

/** BY3 (B3-R3): an OnStackStatusEffect link that resolves to no status row. */
void stack_link(const Options& options) {
    const auto path = options.workDirectory / "review-b3-stack-link.json";
    std::filesystem::create_directories(options.workDirectory);
    const auto document = [](const char* link) {
        return nlohmann::json{
            {"format", 7}, {"statusRows", 2},
            {"counts", {{"cooldowns", 1}, {"statusEffects", 2}, {"spells", 0}, {"manaCosts", 0}, {"staminaCosts", 0}, {"abilityCosts", 0},
                        {"abilityRows", 0}, {"statusCategories", 0}, {"actionSpells", 0}, {"actionNodes", 0}, {"actionStatuses", 0},
                        {"actionHitStatuses", 0}, {"actionDodgeStatuses", 0}, {"actionTagRemovals", 0}}},
            {"cooldowns", {{"Test", 1, 0}}},
            // The number of a row is the place of the CRC-32 of its lower-case id: test_timer 0x610E7811, test_haste 0xC71802D0.
            {"statusEffects", {{"Test_Haste", 1, 5, 0.07, false, false, 0, false, 0,
                                {{"OnStackStatusEffect", link}, {"AddOnStackSize", 1}, {"AddOnStackSizeComparison", "Equal"}}},
                               {"Test_Timer", 0, 10, 0, false, false, 0, false, 0, nlohmann::json::object()}}},
            {"spells", nlohmann::json::array()}, {"manaCosts", nlohmann::json::array()}, {"staminaCosts", nlohmann::json::array()},
            {"abilityCosts", nlohmann::json::array()}, {"abilityRows", nlohmann::json::array()}, {"statusCategories", nlohmann::json::array()},
            {"actionSpells", nlohmann::json::array()}, {"actionNodes", nlohmann::json::array()}, {"actionStatuses", nlohmann::json::array()},
            {"actionHitStatuses", nlohmann::json::array()}, {"actionDodgeStatuses", nlohmann::json::array()},
            {"actionTagRemovals", nlohmann::json::array()}};
    };
    const auto load = [&](const char* link) {
        std::ofstream(path, std::ios::trunc) << document(link).dump();
        gamedata::AbilityData data;
        const bool loaded = data.load(path.string());
        if (!loaded) {
            note("refused: " + data.error());
        }
        return loaded;
    };
    const bool control = load("Test_Timer");
    const bool broken = load("Test_Missing_Timer");
    note("the control file " + std::string(control ? "loads" : "does not load") + ", the broken link " + (broken ? "loads" : "does not load"));
    check(control && !broken, "a file whose OnStackStatusEffect names a row loads; a file with an OnStackStatusEffect of no status row does not load");
    // The set of the ability data (the tables of a test) refuses the broken chain of Archer's Speed too.
    HasteShop shop;
    gamedata::AbilityTables tables = shop.host.abilities.tables();
    for (gamedata::StatusRow& row : tables.statusEffects) {
        if (row.id == kSwapHaste) {
            row.cells.texts["OnStackStatusEffect"] = "Test_Missing_Timer";
        }
    }
    gamedata::AbilityData other;
    check(!other.set(tables), "the set of the ability data refuses the haste of Archer's Speed with a timer link of no row");
    // The tool: the link and the three columns of the stack chain (check_ability_tool.py).
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_ability_tool.py";
    const auto output = options.workDirectory / "review-b3-ability-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + script.lexically_normal().string()
                                + "\" \"" + (options.workDirectory / "review-b3-ability").string()
                                + "\" stack-link missing-stack-link missing-OnStackStatusEffect missing-AddOnStackSize missing-AddOnStackSizeComparison > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    note(text.substr(0, 600));
    check(status == 0 && text.find("FAIL") == std::string::npos && text.find("ok    missing-stack-link") != std::string::npos,
          "the ability tool stops for a stack link of no row and for each missing column of the stack chain (the old file stays)");
}

} // namespace

std::vector<Case> review_b3_cases() {
    return {
        {"BY1", "review of stage B3, finding B3-R1: an attack after the mark CanAttackCancel ends the dodge action (Dodge and Weave, Evasive Tactics)",
         attack_ends_dodge},
        {"BY2", "review of stage B3, finding B3-R2: the cap of the sources of a row (Alacrity II, Refreshing Evasion)", source_caps},
        {"BY3", "review of stage B3, finding B3-R3: an OnStackStatusEffect of no status row (the loader, the set, the tool)", stack_link},
    };
}

} // namespace soulwarden::backend::tests

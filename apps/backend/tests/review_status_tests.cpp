// Cases for the corrections of the code review of stage 3.3b-S1 (findings F1 to F13), stage 3.3b-S2a. Each case fails on
// the tree before the corrections (~/Games/soulwarden/work/trees/baseline-spell, with the new check scripts of the tools) and
// passes after them. The proofs of the reviewer (code-review/work/apps/backend/tests/review_status_faults.cpp and
// work/tools/gamedata/review_status_tools.py) are the start of most cases. All ids, names, and values are made up.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;

/** The shop with the made-up status rows of the findings. */
struct StatusShop : Shop {
    StatusShop() {
        gamedata::AbilityTables data = shop_abilities();
        std::vector<gamedata::StatusRow> rows = data.statusEffects;
        rows.push_back(status("Review_Refresh", 4.0F, cells({{"RefreshDuration", 1.0F}, {"StackMax", 2.0F}, {"PhysicalArmor", 0.2F}})));
        rows.push_back(status("Review_Gear", 10.0F, cells({{"ABSSlash", 0.25F}, {"RemoveOnDeath", 1.0F}})));
        rows.push_back(status("Review_Full", 10.0F, cells({{"DontReplaceStack", 1.0F}, {"StackMax", 1.0F}, {"ApplicationCooldown", 1.0F}})));
        rows.push_back(status("Review_Haste", 10.0F, {}));
        rows.back().moveSpeed = 0.4F;
        rows.push_back(status("Review_Caster", 10.0F, cells({{"PhysicalArmor", 0.2F}})));
        data.statusEffects = numbered(std::move(rows));
        if (!host.abilities.set(std::move(data))) {
            throw std::runtime_error("review test: the ability data is not valid: " + host.abilities.error());
        }
    }
    world::StatusVerdict apply(const char* name, float seconds = 0.0F) {
        world::StatusApply how;
        how.seconds = seconds;
        how.source = {world::StatusSourceKind::command, "review", 0, character()->id};
        return world().statuses().apply(*character(), name, how);
    }
    const world::ActiveStatus* find(const char* name) {
        for (const world::ActiveStatus& entry : character()->statuses) {
            if (entry.status == name) {
                return &entry;
            }
        }
        return nullptr;
    }
    void advance(std::chrono::milliseconds by) {
        host.clock.advance(by);
        host.tick();
    }
    /** An amulet whose affix keeps the status Review_Gear (RemoveOnDeath). */
    void gear() {
        gamedata::PerkTables perks = shop_perks();
        perks.affixes["test_affix_slashward"] = cells({}, {{"StatusEffect", "Review_Gear"}});
        check(host.perks.set(std::move(perks)), "setup: a ward perk keeps a status");
        check(wear("TestAmulet", world::paperdoll::kAmulet, {"PerkID_Gem_SlashWard"}).ok(), "setup: equip the ward amulet");
    }
};

/** @return The names of the cases of a check script of a tool (tools/gamedata) that passed. */
std::set<std::string> passed_cases(const Options& options, const char* tool, const char* folder) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / tool;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::string(folder) + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / folder).string() + "\" > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok  ", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
        }
    }
    return passed;
}

/** A file of format 3 with one status row whose cells are `cells` (the cases of the loader). */
nlohmann::json one_status_file(const nlohmann::json& cells, float duration = 1.0F) {
    return nlohmann::json{{"format", 3},
                          {"statusRows", 1},
                          {"cooldowns", {{"Test", 1, 0}}},
                          {"statusEffects", {{"Review_A", 0, duration, 0, false, false, 0, false, 0, cells}}},
                          {"spells", {{"Review_Spell", 1, 0, 0, 0, 0, 0, nlohmann::json::object()}}},
                          {"manaCosts", nlohmann::json::array()},
                          {"staminaCosts", nlohmann::json::array()},
                          {"abilityCosts", nlohmann::json::array()},
                          {"abilityRows", nlohmann::json::array()},
                          {"statusCategories", nlohmann::json::array()}};
}

bool loads(const Options& options, const nlohmann::json& document, const char* name) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / name;
    std::ofstream(path) << document.dump();
    gamedata::AbilityData data;
    return data.load(path.string());
}

/** RS1 (F1): a haste of the server alone does not widen the movement check: the client has no speed multiplier. */
void haste_without_table(const Options&) {
    const auto run = [](bool haste) {
        StatusShop shop;
        if (haste) {
            shop.apply("Review_Haste");
        }
        world::PositionTrack track;
        unsigned refused = 0;
        for (unsigned index = 0; index <= 60; ++index) {
            const world::PositionReport report{static_cast<std::uint16_t>(100 + index), at(kPlace.x + 11.0F * index / 30.0F, kPlace.y),
                                               wire_height(kHeight)};
            if (shop.world().report_position(shop.character()->owner, report, track) != world::PositionVerdict::accepted) {
                ++refused;
            }
            shop.host.clock.advance(std::chrono::microseconds(33333));
        }
        return refused;
    };
    const unsigned plain = run(false);
    const unsigned haste = run(true);
    note("11 m/s for 2 s through World::report_position: refusals with no status " + std::to_string(plain) + ", with the haste "
         + std::to_string(haste));
    check(plain > 0, "control: the movement check refuses 11 m/s");
    check(haste > 0, "a haste of the server whose speed table the client did not get does not permit 11 m/s");
}

/** RS2 (F2): a new session of a character with a status gets the clock pair and the status map in its first record. */
void reconnect_status(const Options&) {
    StatusShop shop;
    shop.apply("Review_Refresh");
    check(shop.world().leave(shop.character()->owner), "setup: the character leaves with a status that runs");
    FakeClient next{27102};
    shop.host.enter(next, shop.alpha.registration());
    Walker again{shop.host, next, kPlace};
    again.idle(20);
    bool pair = false;
    bool status = false;
    bool ordered = true;
    for (const auto& message : next.got) {
        if (message.type != 8) {
            continue;
        }
        for (const auto& record : read_bundle(message.body).records) {
            for (const auto& fragment : record.fragments) {
                if (fragment.type == server::rep::kTypePlayerComponentState && fragment.fields.contains({1, 6}) && fragment.fields.contains({1, 7})) {
                    pair = true;
                }
                if (fragment.type == server::rep::kTypeStatusEffectsState) {
                    status = true;
                    ordered = ordered && pair;
                }
            }
        }
    }
    note("the new session: clock pair " + std::to_string(pair) + ", status map " + std::to_string(status) + ", the pair first "
         + std::to_string(ordered));
    check(pair && status && ordered, "the new session gets the clock pair and then the status map of the status that runs");
}

/** RS3 (F3): a hit of a cast uses the gem of the weapon instance of the press, not of a later instance in the slot. */
void cast_keeps_instance(const Options&) {
    for (const bool replace : {false, true}) {
        Shop shop;
        check(shop.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: a hammer with no gem");
        world::Entity& own = *shop.character();
        const std::uint32_t node = gamedata::progression_id("Ability_WarHammer_Shockwave");
        own.skills.pools[kWarHammer][node].level = 1;
        own.skills.bars[kWarHammer] = {node, 0, 0};
        shop.wolf(2.0F);
        world::AbilityPress press;
        press.input = 100;
        press.sequence = 1;
        check(shop.world().abilities().press(own.owner, press) == world::CastVerdict::accepted, "setup: Shockwave waits for its slam");
        if (replace) {
            check(shop.world().inventory().unequip(kCharacterA, world::paperdoll::kMainHand1).ok(), "setup: the hammer of the press goes off");
            check(shop.wear("TestHammer", world::paperdoll::kMainHand1, {"PerkID_Gem_VoidDMG4"}).ok(), "setup: a hammer with a gem goes on");
        }
        const std::size_t from = shop.a.got.size();
        shop.pass(1.1F);
        const auto entries = damage_entries(shop.a, from);
        const std::size_t parts = entries.empty() ? 0 : entries.front().size();
        note(std::string(replace ? "a different hammer in the slot at the slam" : "the hammer of the press") + ": parts of the damage number "
             + std::to_string(parts));
        check(parts == 1, "the hit of the cast has no conversion of a gem that the weapon of the press did not have");
    }
}

/** RS4 (F4): an application of a command on a status of the gear does not keep the status after the item goes off. */
void gear_source_kept(const Options&) {
    StatusShop shop;
    shop.gear();
    check(shop.find("Review_Gear") != nullptr && shop.find("Review_Gear")->permanent, "control: the status of the gear has no end");
    shop.apply("Review_Gear", 2.0F);
    check(shop.world().inventory().unequip(kCharacterA, world::paperdoll::kAmulet).ok(), "setup: the amulet goes off");
    shop.advance(std::chrono::seconds(11));
    const world::ActiveStatus* left = shop.find("Review_Gear");
    note("after the unequip and 11 s: status " + std::to_string(left != nullptr) + ", Slash absorption "
         + std::to_string(shop.character()->gear.absorption["Slash"]));
    check(left == nullptr && near(shop.character()->gear.absorption["Slash"], 0.0F), "the status and its value end with the item that kept it");
}

/** RS5 (F5): the gear does not give a dead character again a status that the death removed (RemoveOnDeath); the revival does. */
void gear_status_death(const Options&) {
    StatusShop shop;
    shop.gear();
    shop.world().health().hurt(kCharacterA, 1.0e7F);
    note("dead " + std::to_string(shop.character()->life.dead) + ", the RemoveOnDeath status of the gear " + std::to_string(shop.find("Review_Gear") != nullptr));
    check(shop.character()->life.dead, "control: the character died");
    check(shop.find("Review_Gear") == nullptr, "the gear refresh does not give the dead character its RemoveOnDeath status again");
    shop.world().health().heal(kCharacterA);
    check(!shop.character()->life.dead && shop.find("Review_Gear") != nullptr, "the revival gives the status of the gear again");
}

/** RS6 (F6): a shorter refresh of a RefreshDuration row keeps the later end. */
void refresh_keeps_end(const Options&) {
    StatusShop shop;
    shop.apply("Review_Refresh", 10.0F);
    const auto before = shop.find("Review_Refresh")->ends;
    shop.advance(std::chrono::seconds(1));
    shop.apply("Review_Refresh", 1.0F);
    const auto after = shop.find("Review_Refresh")->ends;
    note("the refresh moves the end by " + std::to_string(std::chrono::duration<float>(after - before).count()) + " s");
    check(after >= before, "a shorter refresh keeps the later end");
}

/** RS7 (F7): an application that DontReplaceStack refuses does not start the ApplicationCooldown again. */
void refused_keeps_cooldown(const Options&) {
    StatusShop shop;
    check(shop.apply("Review_Full") == world::StatusVerdict::applied, "setup: the first application");
    shop.advance(std::chrono::milliseconds(1100));
    check(shop.apply("Review_Full") == world::StatusVerdict::full, "setup: DontReplaceStack refuses the full status");
    shop.world().statuses().remove(*shop.character(), [](const world::ActiveStatus& entry) { return entry.status == "Review_Full"; },
                                   world::StatusEnd::command);
    const world::StatusVerdict verdict = shop.apply("Review_Full");
    note(std::string("after a refused application and a removal: ") + world::status_verdict_text(verdict));
    check(verdict == world::StatusVerdict::applied, "a refused application did not start the cooldown of the row");
}

/** RS8 (F8): each end status and CastSpell link resolves (the loader and the tool), and the rune checks its chain before its payment. */
void links_resolve(const Options& options) {
    check(loads(options, one_status_file({{"OnEndStatusEffect", "Review_A"}}), "rs8-control.json"), "control: a file with a link that resolves loads");
    check(!loads(options, one_status_file({{"OnEndStatusEffect", "Review_Missing"}}), "rs8-end.json"), "the loader refuses an end status that the data does not have");
    check(!loads(options, one_status_file({{"CastSpell", "Review_No_Spell"}}), "rs8-spell.json"), "the loader refuses a CastSpell that the data does not have");
    nlohmann::json game = one_status_file({{"OnEndStatusEffect", "AI_IceDragon_EmpoweringLink_Stack"}});
    game["statusEffects"][0][0] = "AI_IceDragon_EmpoweringLink_Stack_Apply";
    check(loads(options, game, "rs8-game.json"), "the one link of the game with no row loads (kMissingEndLinks)");
    // The rune: an end status with no CastSpell (the loader accepts it): the activation is refused and the charge stays.
    StatusShop shop;
    gamedata::AbilityTables tables = shop_abilities();
    for (gamedata::StatusRow& row : tables.statusEffects) {
        if (row.id == "Rune_Detonate_CastAoE") {
            row.cells.texts.erase("CastSpell");
        }
    }
    check(shop.host.abilities.set(std::move(tables)), "setup: the end status of the rune has no CastSpell");
    check(shop.wear("TestRune", world::paperdoll::kHeartRune).ok(), "setup: the heart rune");
    shop.world().runes().set_charge(kCharacterA, 100.0F);
    const world::RuneVerdict verdict = shop.world().runes().activate_character(kCharacterA);
    shop.advance(std::chrono::milliseconds(3200));
    note(std::string("a chain with no explosion: ") + world::rune_verdict_text(verdict) + ", charge " + std::to_string(shop.character()->runeCharge)
         + ", explosions " + std::to_string(shop.world().runes().counts().explosions));
    check(verdict != world::RuneVerdict::activated && near(shop.character()->runeCharge, 100.0F),
          "an activation whose data has no explosion is refused before its payment");
    const std::set<std::string> passed = passed_cases(options, "check_ability_tool.py", "review-status-links");
    for (const char* name : {"missing-end-link", "game-end-link", "missing-cast-spell"}) {
        check(passed.count(name) == 1, std::string("the ability tool, case ") + name);
    }
}

/** RS9 (F9): a positive timer below 1 ms is refused by the loader; a timer of 1 ms advances, and the tick returns. */
void timers_advance(const Options& options) {
    check(!loads(options, one_status_file({{"Health", 1}, {"TickRate", 0.0001}}, 5.0F), "rs9-tick.json"),
          "the loader refuses a TickRate above 0 and below 1 ms");
    check(!loads(options, one_status_file({{"OnEndStatusEffect", "Review_A"}}, 0.0001F), "rs9-end.json"),
          "the loader refuses a BaseDuration above 0 and below 1 ms");
    StatusShop shop;
    gamedata::AbilityTables tables = shop_abilities();
    tables.statusEffects.push_back(status("Review_Tick", 0.05F, cells({{"TickRate", 0.001F}, {"Health", 1.0F}})));
    tables.statusEffects.push_back(status("Review_Loop", 0.001F, cells({}, {{"OnEndStatusEffect", "Review_Loop"}})));
    tables.statusEffects = numbered(std::move(tables.statusEffects));
    check(shop.host.abilities.set(std::move(tables)), "setup: a tick of 1 ms and an end chain of 1 ms");
    shop.apply("Review_Tick");
    shop.apply("Review_Loop");
    for (int step = 0; step < 5; ++step) {
        shop.advance(std::chrono::milliseconds(10));
    }
    note("ticks " + std::to_string(shop.world().statuses().counts().ticks) + ", due entries taken " + std::to_string(shop.world().statuses().counts().dueTaken));
    check(shop.world().statuses().counts().ticks > 0, "the ticks of 1 ms run, and each tick of the world returns");
}

/** RS10 (F10): a caster effect has the time of SelfApplyStatusEffectDurations. */
void caster_duration(const Options&) {
    StatusShop shop;
    gamedata::PerkTables perks = shop_perks();
    perks.abilities["test_ability_unknowncondition"] =
        cells({{"OnHit", 1.0F}}, {{"SelfApplyStatusEffect", "Review_Caster"}, {"SelfApplyStatusEffectDurations", "2"}});
    check(shop.host.perks.set(std::move(perks)), "setup: a caster row with a duration of 2 s");
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1, {"PerkID_Test_Unknown_Condition"}).ok(), "setup: the caster perk");
    shop.strike(*shop.wolf(2.0F), "Test_Light");
    const world::ActiveStatus* found = shop.find("Review_Caster");
    const float left = found != nullptr ? std::chrono::duration<float>(found->ends - shop.host.clock.now()).count() : -1.0F;
    note("seconds of the caster status: " + std::to_string(left) + " (the row asks for 2)");
    check(found != nullptr && near(left, 2.0F), "the caster effect has the time of SelfApplyStatusEffectDurations");
}

/** RS11 (F11): the perk tool stops for a master item table with no RequiredLevel, and has no row for a consumable with no master row. */
void required_level(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_perk_tool.py", "review-status-level");
    check(passed.count("valid") == 1, "control: the complete tables give the level 55");
    check(passed.count("missing-RequiredLevel") == 1, "a master item table with no RequiredLevel stops the tool and keeps the old file");
    check(passed.count("consumable-no-item") == 1, "a consumable with no master item row has no row in the file (not the level 0)");
}

/** RS12 (F12): the perk tool requires the damage type columns of an affix (the affix table of the game has no armor column). */
void affix_columns(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_perk_tool.py", "review-status-affix");
    check(passed.count("missing-ABSSlash") == 1, "an affix table with no ABSSlash stops the tool and keeps the old file");
    check(passed.count("affix-no-armor") == 1, "an affix table with no PhysicalArmor (as the table of the game) gives the file");
}

/** RS13 (F13): the ability tool requires the columns of the status rules and of a caster effect, with the differences of the game. */
void rule_columns(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_ability_tool.py", "review-status-columns");
    check(passed.count("valid") == 1, "control: the complete tables give the StackMax 3");
    check(passed.count("missing-StackMax") == 1, "a status table with no StackMax stops the tool and keeps the old file");
    check(passed.count("missing-caster-OnHit") == 1, "an ability table of the players with no OnHit stops the tool and keeps the old file");
    check(passed.count("game-table-difference") == 1, "a status table of the creatures with no ApplicationCooldown (a difference of the game) gives the file");
}

} // namespace

std::vector<Case> review_status_cases() {
    return {{"RS1", "review of 3.3b-S1, F1: a haste of the server alone does not widen the movement check", haste_without_table},
            {"RS2", "review of 3.3b-S1, F2: a new session gets the clock pair and the status map of the character", reconnect_status},
            {"RS3", "review of 3.3b-S1, F3: a cast keeps the gem of the weapon instance of its press", cast_keeps_instance},
            {"RS4", "review of 3.3b-S1, F4: a command on a status of the gear does not keep it after the item", gear_source_kept},
            {"RS5", "review of 3.3b-S1, F5: the gear gives no RemoveOnDeath status to a dead character", gear_status_death},
            {"RS6", "review of 3.3b-S1, F6: a shorter refresh keeps the later end", refresh_keeps_end},
            {"RS7", "review of 3.3b-S1, F7: a refused application does not start ApplicationCooldown", refused_keeps_cooldown},
            {"RS8", "review of 3.3b-S1, F8: the links of a status row resolve; the rune checks its chain (check_ability_tool.py)", links_resolve},
            {"RS9", "review of 3.3b-S1, F9: no positive timer below 1 ms; a timer of 1 ms returns", timers_advance},
            {"RS10", "review of 3.3b-S1, F10: a caster effect has its duration override", caster_duration},
            {"RS11", "review of 3.3b-S1, F11: the RequiredLevel of a consumable (check_perk_tool.py)", required_level},
            {"RS12", "review of 3.3b-S1, F12: the damage type columns of an affix (check_perk_tool.py)", affix_columns},
            {"RS13", "review of 3.3b-S1, F13: the columns of the status rules and caster effects (check_ability_tool.py)", rule_columns}};
}

} // namespace soulwarden::backend::tests

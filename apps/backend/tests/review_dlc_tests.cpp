// The corrections of the code review of stage 1.1b (the DLC for each player), made in stage 3.3b-S5. Each case fails on the
// tree of stage 1.1b (commit 264c945) and passes after its correction.
//
//   RD1   F1: the mute of a development run: a move of the savedata folder and a link at its old place between the check and
//         the write changes no settings file of the user (tools/check_dev_mute.py, the swap and hard link cases)
//   RD2   F2: a Vitals slot needs its vitals row: the spell loader refuses it, the spell tool keeps the old file, and a Pylon
//         of a slice with a Vitals slot and no row is not made and not paid
//   RD3   F3: the progression tool keeps the old file for a rank table shorter than the cap (check_progression_tool.py)
//   RD4   F4: the projectile tool keeps the old file when a required ability spell is not a row of the source
//         (check_projectile_tool.py)
//   RD5   F5: the progression loader reads the format first: format 2 needs the expansion rank of each row and the three
//         lists of the ownership; format 1 has none of them
//   RD6   F6: the progression loader refuses a root of a DLC pack whose child entitlement is not in the file

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/spell_kind_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace shop;
using namespace projectile;
using namespace spell_kind;

/** Runs a check script of the tools (a path below the repository) and @return its output lines. */
std::vector<std::string> run_check(const Options& options, const std::string& script, const std::string& argument) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-rl.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + path.lexically_normal().string() + "\"" + (argument.empty() ? "" : " \"" + argument + "\"") + " > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 150));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** @return True when a line of the output starts with "ok" and names the case (the text before its first colon), and no line FAILs it. */
bool passed(const std::vector<std::string>& lines, const std::string& name) {
    const bool ok = std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        if (line.rfind("ok", 0) != 0) {
            return false;
        }
        const std::size_t start = line.find_first_not_of(' ', 2);
        return start != std::string::npos && line.compare(start, name.size() + 1, name + ":") == 0;
    });
    const bool failed = std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.rfind("FAIL", 0) == 0 && line.find(name + ":") != std::string::npos;
    });
    return ok && !failed;
}

/** RD1 (F1): the mute against a move of the savedata folder at the write, after the checks, after the folder open, and a hard link. */
void mute_swap(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/check_dev_mute.py", "");
    for (const char* name : {"swap at the write (sed shim)", "swap after the checks", "swap after the folder open", "hard link"}) {
        check(passed(lines, name), std::string("mute: ") + name + ": the settings file of the user stays, each check of the case passes");
    }
    check(passed(lines, "separate prefix with spaces"), "control: the settings file of a separate development prefix gets the volume 0");
}

/** RD2 (F2): a Vitals slot with no vitals row. */
void vitals_slot(const Options& options) {
    KindShop shop;
    gamedata::SpellTables tables = kind_tables();
    // The slice of the Pylon (the first slice of the fixture) keeps its Vitals slot (type 15) and loses its vitals row.
    tables.slices[0].vitals.reset();
    check(!shop.host.spells.set(tables), "format 2: the spell data refuses a slice with a Vitals slot and no vitals row");
    // A file of format 1 has no vitals rows: the loader takes it, and the press makes no Pylon and pays nothing.
    tables.format = 1;
    check(shop.host.spells.set(tables), "control: the same slice in a file of format 1 loads");
    shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
    const float mana = shop.character()->vitals.mana;
    world::Entity* pylon = shop.cast(30);
    check(pylon == nullptr && near(shop.character()->vitals.mana, mana, 0.5F),
          "a Pylon of a slice with a Vitals slot and no vitals row is not made and not paid");
    const std::vector<std::string> lines =
        run_check(options, "tools/gamedata/check_spell_tool.py", (options.workDirectory / "review-dlc-spells").string());
    check(passed(lines, "vitals-no-row"), "the spell tool: a Vitals component with no row reference: the old file stays");
    check(passed(lines, "valid"), "control: the spell tool writes the valid slice");
}

/** RD3 (F3): the progression tool and a short rank table. */
void short_ranks(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/gamedata/check_progression_tool.py", "");
    check(passed(lines, "rank-control"), "control: the complete rank table gives the Mining cap 250 with the expansion");
    check(passed(lines, "short-rank-source"), "a rank table with the ranks 0 to 200 for the cap 250: the tool stops, the old file stays");
}

/** RD4 (F4): the projectile tool and a source with no row of a required ability spell. */
void required_spell_row(const Options& options) {
    const std::vector<std::string> lines =
        run_check(options, "tools/gamedata/check_projectile_tool.py", (options.workDirectory / "review-dlc-projectiles").string());
    check(passed(lines, "valid"), "control: the valid source gives the basic shots and the seven ability shots");
    check(passed(lines, "missing-required-row"), "no row of IceMagic_Pylon_Projectile in the source: the tool stops, the old file stays");
}

/** A complete progression file of format 2 (made-up rows: the review probe RDL1 of stage 1.1b). */
json progression_document() {
    json levels = json::array();
    for (unsigned level = 0; level < 70; ++level) {
        levels.push_back({level * 100, 0, 0, 0});
    }
    return {{"format", 2},
            {"levelCap", 60},
            {"levels", levels},
            {"attributes", {{"minimum", 5}, {"constitutionHealth", {0}}}},
            {"vitals", {{"health", 800}, {"stamina", 100}, {"mana", 100}}},
            {"rankTables", {{"Mining", {{"experience", std::vector<unsigned>(251, 100)}}}}},
            {"progressions", json::array({json::array({"Mining", "tradeskill", 200, "Mining", 250})})},
            {"abilities", json::array()},
            {"expansions", json::array({json::array({"None", 60, 625, 625, 200, ""}),
                                        json::array({"Expansion2023", 70, 700, 800, 250, "TestExpansion"})})},
            {"entitlements", json::array({json::array({"TestExpansion", 1484, "Expansion2023", "", "TestUnlock"}),
                                          json::array({"TestRoot", 220, "Entitlement", "TestChild+TestSecond", ""}),
                                          json::array({"TestChild", 300, "Misc", "TestArt", ""}),
                                          json::array({"TestSecond", 301, "Misc", "TestArt", ""})})},
            {"achievements", json::array({json::array({"TestUnlock", 11979})})}};
}

bool load_progression(const Options& options, const json& document, gamedata::ProgressionTables& tables, const char* name) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / (std::string("review-dlc-") + name + ".json");
    std::ofstream(path) << document.dump();
    const bool loaded = tables.load(path.string());
    std::filesystem::remove(path);
    return loaded;
}

/** RD5 (F5): the format first. */
void progression_format(const Options& options) {
    gamedata::ProgressionTables control;
    check(load_progression(options, progression_document(), control, "control") && control.level_cap() == 70
              && control.rank_cap(*control.progression("Mining")) == 250,
          "control: the complete file of format 2 loads (level 70, Mining 250)");
    json four = progression_document();
    four["progressions"][0].erase(4);
    gamedata::ProgressionTables fourValues;
    check(!load_progression(options, four, fourValues, "four"), "format 2: a progression row with no expansion rank is refused");
    json noLists = progression_document();
    for (const char* member : {"expansions", "entitlements", "achievements"}) {
        noLists.erase(member);
    }
    gamedata::ProgressionTables noOwnership;
    check(!load_progression(options, noLists, noOwnership, "no-lists"), "format 2: a file with no ownership lists is refused");
    json old = noLists;
    old["format"] = 1;
    old["progressions"][0].erase(4);
    gamedata::ProgressionTables oldTables;
    check(load_progression(options, old, oldTables, "old") && oldTables.level_cap() == 60 && oldTables.owned_expansion() == nullptr
              && oldTables.rank_cap(*oldTables.progression("Mining")) == 200,
          "control: format 1 (rows of four values, no lists) loads with the base limits");
    json oldFive = old;
    oldFive["progressions"][0].push_back(250);
    gamedata::ProgressionTables oldFiveTables;
    check(!load_progression(options, oldFive, oldFiveTables, "old-five"), "format 1: a row of five values is refused");
    json oldLists = progression_document();
    oldLists["format"] = 1;
    gamedata::ProgressionTables oldListTables;
    check(!load_progression(options, oldLists, oldListTables, "old-lists"), "format 1: a file with the ownership lists is refused");
    json other = progression_document();
    other["format"] = 3;
    gamedata::ProgressionTables otherTables;
    check(!load_progression(options, other, otherTables, "format-3"), "a format that is not 1 or 2 is refused");
}

/** RD6 (F6): the children of a root of a DLC pack. */
void reward_children(const Options& options) {
    for (const std::size_t missing : {std::size_t{2}, std::size_t{3}}) {
        json document = progression_document();
        document["entitlements"].erase(missing);
        gamedata::ProgressionTables tables;
        check(!load_progression(options, document, tables, "missing-child"),
              std::string("a root whose child ") + (missing == 2 ? "TestChild" : "TestSecond") + " is not in the file is refused");
    }
    json empty = progression_document();
    empty["entitlements"][1][3] = "";
    gamedata::ProgressionTables emptyTables;
    check(!load_progression(options, empty, emptyTables, "empty-child"), "a root of the type Entitlement with no child is refused");
    gamedata::ProgressionTables control;
    check(load_progression(options, progression_document(), control, "children") && control.entitlement("TestChild") != nullptr
              && control.entitlement("TestSecond") != nullptr,
          "control: a root with its two children loads");
}

} // namespace

std::vector<Case> review_dlc_cases() {
    return {{"RD1", "review of 1.1b, F1: the mute changes no settings file of the user at a move of the savedata folder (check_dev_mute.py)", mute_swap},
            {"RD2", "review of 1.1b, F2: a Vitals slot needs its vitals row: loader, spell tool, no paid Pylon", vitals_slot},
            {"RD3", "review of 1.1b, F3: the progression tool stops at a rank table shorter than the cap", short_ranks},
            {"RD4", "review of 1.1b, F4: the projectile tool stops when a required ability spell is not a row of the source", required_spell_row},
            {"RD5", "review of 1.1b, F5: the progression loader reads the format first", progression_format},
            {"RD6", "review of 1.1b, F6: the progression loader refuses a DLC root whose child is not in the file", reward_children}};
}

} // namespace soulwarden::backend::tests

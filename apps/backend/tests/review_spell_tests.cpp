// Cases of the corrections of the code review of stage 3.3b-S2a (made in stage 3.3b-S4). Each case fails on the tree of
// stage 3.3b-S2a (commit f63df14, with this file and the check scripts of the tools) and passes after the correction. All
// ids, names, and values are made up.
//
//   RG1  F1: a later lower speed table does not keep an expired haste allowance of the position check
//   RG2  F2: an ability file with no speed limits gives the limits of the supported client (1.5), not a faster run
//   RG3  F3: the spell loader refuses a DelayedEvent slot with no event row
//   RG4  F4: the ability tool requires ABSStandard and the condition columns of a caster row (check_ability_tool.py)
//   RG5  F5: the spell tool refuses a delay that is not a 32-bit float (check_spell_tool.py)

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using std::chrono::milliseconds;

/** Runs a check script of the tools; @return the names of its cases that passed (lines "ok  NAME: ..."). */
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
            const std::size_t start = line.find_first_not_of(' ', 4);
            passed.insert(line.substr(start, line.find(':') - start));
        }
        note(line.substr(0, 150));
    }
    std::filesystem::remove(output);
    return passed;
}

/** RG1 (F1): the old maximum of the speed tables ends one window after its own change. */
void haste_allowance(const Options&) {
    world::PositionTrack track;
    const auto start = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    track.set_client_speed(1.5F, start);
    track.set_client_speed(1.0F, start + milliseconds(100));
    check(near(track.client_speed(start + milliseconds(1000)), 1.5F), "control: the table 1.5 counts one window after its change");
    // Lower tables each 400 ms: 1.1, 1.0, 1.1, ...
    float factor = 1.0F;
    for (int step = 1; step <= 8; ++step) {
        factor = step % 2 == 1 ? 1.1F : 1.0F;
        track.set_client_speed(factor, start + milliseconds(100 + 400 * step));
    }
    const float atEnd = track.client_speed(start + milliseconds(3300));
    note("3.2 s after the end of the table 1.5: the check takes " + std::to_string(atEnd));
    check(atEnd <= 1.1F + 0.001F, "3.2 s later, with only the tables 1.0 and 1.1 since, the check takes 1.1 at most (not 1.5)");
}

/** RG2 (F2): with no speed limits in the ability data the multiplier has the limits of the client (1.5). */
void speed_limit(const Options&) {
    Shop shop;
    gamedata::AbilityTables tables = shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    for (const char* id : {"Review_SpeedA", "Review_SpeedB"}) {
        gamedata::StatusRow row = status(id, 30.0F, cells({{"SprintSpeedMod", 0.4F}}));
        row.moveSpeed = 0.4F;
        rows.push_back(row);
    }
    tables.statusEffects = numbered(std::move(rows));
    tables.statMultipliers.clear(); // A file of the ability tool from before stage 3.3b-S2a: no limits.
    check(shop.host.abilities.set(tables), "setup: an ability file with no statMultipliers");
    world::StatusApply how;
    how.source = {world::StatusSourceKind::command, "test", 0, shop.character()->id};
    (void)shop.world().statuses().apply(*shop.character(), "Review_SpeedA", how);
    (void)shop.world().statuses().apply(*shop.character(), "Review_SpeedB", how);
    shop.walker.idle(3);
    const float move = shop.character()->moveMultiplier;
    const float sprint = shop.character()->sprintMultiplier;
    const float guard = shop.host.server->session(1)->position_track().client_speed(shop.host.clock.now());
    note("two speed statuses of 0.4: the multipliers " + std::to_string(move) + " and " + std::to_string(sprint) + ", the check "
         + std::to_string(guard));
    check(move <= 1.5F + 0.001F && sprint <= 1.5F + 0.001F, "the multipliers are 1.5 at most (the limit of the client, StatMultiplierData)");
    check(guard <= 1.5F + 0.001F, "the position check takes 1.5 at most");
}

/** RG3 (F3): a DelayedEvent slot with no event row is refused. */
void delayed_slot(const Options&) {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/review_vortex.dynamicslice";
    slice.asset.fill(0x5A);
    slice.subId = 2;
    slice.root = 0x0000025ACFDAA308ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = 1;
    slice.slots = {{1, "Spell", 2912}, {6, "DelayedEvent", 3139}, {7, "DelayedEvent", 3139}};
    slice.events = {{6, 3.1F, gamedata::spell_event::kDeactivate, "DetectionVolume", "", false},
                    {7, 3.0F, gamedata::spell_event::kActivate, "Burst", "GreatAxe_GravityWell_Burst", false}};
    gamedata::SpellTables complete;
    complete.slices = {slice};
    complete.spells = {{"Review_Spell", 0}};
    gamedata::SpellData data;
    check(data.set(complete), "control: each DelayedEvent slot has its event row");
    gamedata::SpellTables missing = complete;
    missing.slices[0].events.pop_back(); // Slot 7 stays, its event row goes.
    check(!data.set(missing), "a DelayedEvent slot (7) with no event row: the file is refused");
}

/** RG4 (F4): the ability tool requires ABSStandard and the condition columns of a caster row. */
void ability_columns(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_ability_tool.py", "review-spell-columns");
    check(passed.count("valid") == 1, "control: the complete tables give the file");
    for (const char* name : {"missing-ABSStandard", "missing-RemoteDamageTableRow", "missing-NumAroundMe"}) {
        check(passed.count(name) == 1, std::string("the tool stops and keeps the old file: ") + name);
    }
}

/** RG5 (F5): the spell tool refuses a delay that is not a 32-bit float. */
void spell_delay(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_spell_tool.py", "review-spell-delay");
    check(passed.count("valid") == 1, "control: a delay of 4 bytes gives the event of 3 s");
    check(passed.count("bad-delay-width") == 1 && passed.count("delay-not-finite") == 1,
          "a delay of 1 byte, and a delay that is not finite: the tool stops and keeps the old file");
}

} // namespace

std::vector<Case> review_spell_cases() {
    return {{"RG1", "review of 3.3b-S2a, F1: an old haste allowance ends one window after its change", haste_allowance},
            {"RG2", "review of 3.3b-S2a, F2: no speed limits in the data give the limits of the client (1.5)", speed_limit},
            {"RG3", "review of 3.3b-S2a, F3: the spell loader refuses a DelayedEvent slot with no event row", delayed_slot},
            {"RG4", "review of 3.3b-S2a, F4: the ability tool requires ABSStandard and the caster condition columns", ability_columns},
            {"RG5", "review of 3.3b-S2a, F5: the spell tool refuses a delay that is not a 32-bit float", spell_delay}};
}

} // namespace soulwarden::backend::tests

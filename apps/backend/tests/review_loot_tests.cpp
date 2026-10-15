// Cases for the corrections of the code review of stage 3.5a (findings F1, F2, F3, F4, F8, F9, F10,
// and F11; ~/Games/soulwarden/work/evidence/loot/review/FINDINGS.md). Each case starts from the
// proof test of the review, and it fails with the code of stage 3.5a. The fixture of the loot cases
// is in loot_tests.cpp: this file takes it with its helpers. All values are made up.

#define loot_cases review_loot_fixture_cases
#include "loot_tests.cpp"
#undef loot_cases

#include <cstdio>

#include <sqlite3.h>

#include "server/http/character_commands.h"

namespace soulwarden::backend::tests {
namespace {

/** RL1 (F1): the store refuses one write of the reward of a kill: no part of the reward is given. */
void reward_one_change(const Options& options) {
    for (const std::string tableName : {"control", "character_progress", "progressions", "currencies", "items"}) {
        LootHost fixture(true, {});
        gamedata::LootData data = loot_data();
        data.bases[0].dropChance = 1;
        data.bases[0].table = kAll;
        check(fixture.host.loot.set(data), "setup: rewards with items");
        const auto path = options.workDirectory / ("reward-failure-" + tableName + ".sqlite3");
        std::filesystem::remove(path);
        state::Store store;
        check(store.open(path.string()), "setup: a database file of the case");
        (void)add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
        ManualClock clock;
        auto settings = ServerHost::with_game_data(fixture.host.catalog, fixture.host.progression, fixture.host.creatures,
                                                  fixture.host.combat, fixture.host.places, fixture.host.terrain,
                                                  fixture.host.navigation, fixture.host.loot);
        world::World target(clock, store, fixture.host.tables, settings);
        target.set_creature_behavior(false);
        check(target.spawn_character(kCharacterA, at(1000, 1000)) == world::SpawnError::none, "setup: the character is in the world");
        world::Entity* own = target.find_character(kCharacterA);
        arm_sword(*own);
        target.rewards().set_forced(kCharacterA, true);
        if (tableName != "control") {
            sqlite3* connection = nullptr;
            const std::string sql = "CREATE TRIGGER refuse_reward BEFORE INSERT ON " + tableName
                                    + " BEGIN SELECT RAISE(FAIL, 'review write failure'); END";
            check(sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK
                      && sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK,
                  "setup: the database refuses one kind of write");
            sqlite3_close(connection);
        }
        const world::EntityId creature = target.creatures().spawn("Test_Wolf", at(1003, 1000), 0).entity->id;
        check(target.creatures().hurt(creature, kHealth, own->id).error == world::CreatureError::none, "setup: a kill");
        const auto savedSkills = store.list_skills(kCharacterA);
        const auto savedItems = store.list_items(kCharacterA);
        const auto savedProgress = store.progress(kCharacterA);
        const auto savedCoin = store.currency(kCharacterA, state::kCurrencyCoin);
        note(tableName + ": experience=" + std::to_string(own->progression.experience) + ", mastery rows="
             + std::to_string(savedSkills.ranks.size()) + ", coin=" + std::to_string(own->wallet.coin)
             + ", item rows=" + std::to_string(savedItems.size()));
        check(savedProgress.experience == own->progression.experience && savedCoin == own->wallet.coin,
              tableName + ": the rows agree with the entity");
        if (tableName == "control") {
            check(own->progression.experience == 17 && savedSkills.ranks.size() == 1 && savedCoin == 380 && savedItems.size() == 2,
                  "control: the whole reward is written");
        } else {
            check(own->progression.experience == 0 && savedSkills.ranks.empty() && savedCoin == 0 && savedItems.empty()
                      && own->bag.slots.empty() && own->skills.rank(kSword) == 0 && target.rewards().counts().refused == 1,
                  tableName + ": a refused write gives no part of the reward (one change)");
        }
    }
}

json complete_loot_document() {
    return json{{"format", 1}, {"rewards", {{1, 10, 310, 1}}},
                {"modifiers", {{"Solo", {2, 3, 0, 1, nullptr}}}}, {"disparity", json::array()},
                {"events", {{"killevent", 0.05}, {"reviewevent", 0}}},
                {"credit", {{"timeLimit", 120}, {"healthShare", 0.01}, {"range", 200}, {"receivers", 1}, {"modifiers", {{1, 1, 1}}}}},
                {"gearScore", {{100, 100, 100, 100, 100, 100, 100, 100, 100, 100}}}, {"lootCeiling", {{1, 100}}},
                {"bases", {{"Test_Wolf", "Solo", false, 0, false, "", json::array(), false, "reviewevent"}}},
                {"vitals", {{"test_wolf", 0}}}, {"tables", json::object()}, {"buckets", json::object()}};
}

/** RL2 (F2): the loader refuses a loot file with a reference that does not resolve. */
void loader_references(const Options& options) {
    const auto path = options.workDirectory / "review-incomplete-loot.json";
    for (const std::string variant : {"control", "missing-modifier", "missing-event", "missing-credit-factor"}) {
        json document = complete_loot_document();
        if (variant == "missing-modifier") document["modifiers"] = json::object();
        if (variant == "missing-event") document["events"].erase("reviewevent");
        if (variant == "missing-credit-factor") document["credit"]["receivers"] = 10;
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        gamedata::LootTables tables;
        const bool loaded = tables.load(path.string());
        note(variant + ": loaded=" + std::to_string(loaded) + (loaded ? "" : ", " + tables.error()));
        if (variant == "control") {
            check(loaded && tables.coin_chance("reviewevent") == 0 && tables.modifier("Solo")->experience == 2,
                  "control: a complete loot file loads");
        } else {
            check(!loaded && !tables.loaded(), variant + ": a loot file with a part that is not there is refused as a whole");
        }
    }
}

/** RL3 (F3, F4): the exporter stops with no file for a reference that does not resolve and for a value out of the range. */
void exporter_checks(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_loot_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the loot tool is in the tree");
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "review-loot-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "review-loot-tool").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok  ", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
        }
    }
    for (const char* name : {"variant-base", "event-override", "creature-type"}) {
        check(passed.count(name) == 1, std::string("F3, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
    for (const char* name : {"negative-experience", "negative-factor", "bad-flag"}) {
        check(passed.count(name) == 1, std::string("F4, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
}

/**
 * Runs a check tool of tools/gamedata (made-up tables) and gives the cases whose line starts with "ok".
 * @param cases The cases to run (empty: each case of the tool).
 */
std::set<std::string> tool_check(const Options& options, const char* tool, const char* folder, const std::vector<std::string>& cases) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / tool;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::string(folder) + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                          + script.lexically_normal().string() + "\" \"" + (options.workDirectory / folder).string() + "\"";
    for (const std::string& name : cases) {
        command += " " + name;
    }
    command += " > \"" + output.string() + "\" 2>&1";
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

/** RA12 (review of stage 3.3a, F11 and F12): the ability data tool refuses an absent column and two rows of one id. */
void ability_exporter_checks(const Options& options) {
    const std::set<std::string> passed = tool_check(options, "check_ability_tool.py", "review-ability-tool", {});
    check(passed.count("valid") == 1 && passed.count("game-duplicate") == 1,
          "complete tables give the file of format 2; the duplicate of the game (Sanctuary) keeps its first row");
    for (const char* name : {"missing-Time", "missing-Stunned", "missing-Radius"}) {
        check(passed.count(name) == 1, std::string("F11, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
    for (const char* name : {"conflicting-status", "conflicting-spell"}) {
        check(passed.count(name) == 1, std::string("F12, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
    for (const char* name : {"missing-cost", "partial-cost-columns", "negative-cost"}) {
        check(passed.count(name) == 1, std::string("I14, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
}

/** RA13 (review of stage 3.3a, F13): the loot tool writes no file with a value that the backend refuses. */
void loot_exporter_ranges(const Options& options) {
    const std::set<std::string> passed =
        tool_check(options, "check_loot_tool.py", "review-loot-ranges", {"negative-disparity", "large-disparity", "negative-bucket-odds"});
    for (const char* name : {"negative-disparity", "large-disparity", "negative-bucket-odds"}) {
        check(passed.count(name) == 1, std::string("F13, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
}

/** RL8 (F8): a command removes the body of a dead creature of a spawn point: no automatic respawn. */
void removal_clears_respawn(const Options&) {
    LootHost fixture(true, {loot_point("review-point", 1010, 1000, 2, 2)});
    world::Entity* own = fixture.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    fixture.pass(0.5F);
    auto& system = fixture.creatures();
    const auto first = system.spawn_points()[0].entity;
    check(first != 0 && system.hurt(first, kHealth, own->id).error == world::CreatureError::none,
          "setup: a creature of a spawn point died and has a respawn time");
    check(system.remove(first).error == world::CreatureError::none, "setup: a command removes its body");
    const auto& point = system.spawn_points()[0];
    check(point.waiting && !point.respawnAt && point.entity == 0, "the removal by a command clears the respawn time");
    fixture.pass(3);
    check(point.waiting && point.entity == 0, "the point stays empty after the old respawn time, until the reset");
    (void)system.reset_spawn_points();
    fixture.pass(0.5F);
    check(point.entity != 0, "control: the reset makes the point free");
}

/** RL9 (F9): the reference states the credit rule of the code. */
void reference_credit_rule(const Options& options) {
    const auto path = options.goldenDirectory / ".." / ".." / ".." / ".." / "docs" / "REFERENCE.md";
    std::ifstream input(path);
    const std::string reference((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    check(!reference.empty(), "setup: the reference document");
    check(reference.find("in the last 120 s") == std::string::npos && reference.find("its last hit was 120 s or less") != std::string::npos,
          "the reference says the rule of the code: all hits did 1 percent, and the last hit was 120 s or less before the death");
}

/** RL10 (F10): the roll command counts a roll that gave an item more than one time as one roll. */
void roll_counts(const Options&) {
    LootHost fixture;
    auto data = loot_data();
    data.bases[0].table = kBuckets;
    check(fixture.host.loot.set(data), "setup: one roll gives the same item more than one time");
    const json values{{"kind", "Test_Wolf"}, {"count", 1}, {"forced", 1}};
    const server::http::commands::JsonArguments arguments(values);
    service::HttpResponse refusal;
    const auto step = server::http::commands::roll_loot("", arguments, refusal);
    check(static_cast<bool>(step), "setup: the roll command");
    if (!step) return;
    const auto reply = step(fixture.world());
    const auto document = json::parse(reply.body);
    std::uint64_t sword = 0;
    std::uint64_t hide = 0;
    std::uint64_t hideQuantity = 0;
    for (const auto& row : document["items"]) {
        if (row["item"] == "TestSword") sword = row["rolls"].get<std::uint64_t>();
        if (row["item"] == "TestHide") {
            hide = row["rolls"].get<std::uint64_t>();
            hideQuantity = row["quantity"].get<std::uint64_t>();
        }
    }
    note("one roll: sword rolls " + std::to_string(sword) + ", hide rolls " + std::to_string(hide) + ", hide quantity "
         + std::to_string(hideQuantity));
    check(reply.status == 200 && document["rolls"] == 1 && sword <= 1 && hide <= 1 && hideQuantity > 1,
          "the rolls of an item are not more than the rolls; the quantity counts each drop");
}

/** RL11 (F11): a drop that the bag has space for is given whole, also above the limit of the give command. */
void large_drop(const Options&) {
    LootHost fixture(true, {});
    auto data = loot_data();
    data.bases[0].table = kAll;
    data.tables[kAll].entries = {entry(gamedata::LootEntryKind::item, "TestHide", 0, 200000, 200000)};
    check(fixture.host.loot.set(data), "setup: a table gives 200,000 resources");
    auto* own = fixture.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    fixture.rewards().set_forced(kCharacterA, true);
    auto* wolf = fixture.wolf(at(1003, 1000));
    (void)fixture.creatures().hurt(wolf->id, kHealth, own->id);
    const auto given = count_of(own->bag, "TestHide");
    note("given " + std::to_string(given) + ", lost " + std::to_string(fixture.rewards().counts().itemsLost));
    check(given == 200000 && fixture.rewards().counts().itemsLost == 0 && own->bag.slots.size() == 200,
          "the whole drop goes into the bag: the limit of the give command is not a limit of a drop");
}

} // namespace

std::vector<Case> review_loot_cases() {
    return {{"RL1", "review of 3.5a, F1: the reward of a kill is one change (a refused write gives no part)", reward_one_change},
            {"RL2", "review of 3.5a, F2: the loader refuses a loot file with a reference that does not resolve", loader_references},
            {"RL3", "review of 3.5a, F3 and F4: the loot tool stops for a reference that does not resolve and a value out of the range",
             exporter_checks},
            {"RL8", "review of 3.5a, F8: a command removal of a dead creature clears its respawn time", removal_clears_respawn},
            {"RL9", "review of 3.5a, F9: the reference states the credit rule of the code", reference_credit_rule},
            {"RL10", "review of 3.5a, F10: the roll command counts each roll of an item one time", roll_counts},
            {"RL11", "review of 3.5a, F11: a drop that fits in the bag is not cut at the limit of the give command", large_drop},
            {"RA12", "review of 3.3a, F11 and F12: the ability data tool stops for an absent column and for two rows of one id",
             ability_exporter_checks},
            {"RA13", "review of 3.3a, F13: the loot tool writes no file with a value that the backend refuses", loot_exporter_ranges}};
}

} // namespace soulwarden::backend::tests

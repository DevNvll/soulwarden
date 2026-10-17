// Cases from the code review of the stages 1.5 and 1.6 (level, attributes, skills). Each one
// is a fault that the review found, and the case passes only with the correction. The findings
// 2 and 3 of that review (the stamps of a point state in more than one fragment, and a refused
// bar request) are in the cases K1, K3, and K6, whose client models have the state merge.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "gamedata/progression_tables.h"
#include "middleware/compression/zlib.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "state/creation_data.h"
#include "state/world.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/progress.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::ProgressError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr float kHeight = 50.0F;

/** @return A tables document that the loader accepts (the form of tools/gamedata/progression.sh). */
json valid_tables() {
    return {
        {"format", 1},
        {"levelCap", 3},
        {"levels", json::array({json::array({0, 0, 0, 0}), json::array({100, 5, 50, 0}), json::array({300, 6, 100, 250})})},
        {"attributes", {{"minimum", 5}, {"maximum", 8}, {"constitutionHealth", json::array({0, 40, 80, 120})}}},
        {"vitals", {{"health", 800}, {"stamina", 100}, {"mana", 100}}},
        {"rankTables",
         {{"TestWeaponRanks", {{"experience", json::array({10, 50, 70})}, {"resetCost", json::array({0, 0, 6})}}},
          {"TestMiningRanks", {{"experience", json::array({100, 105})}}}}},
        {"progressions", json::array({json::array({"SwordAbilityTable", "weapon", 2, "TestWeaponRanks"}),
                                      json::array({"Mining", "tradeskill", 1, "TestMiningRanks"})})},
        {"abilities", json::array({json::array({"SwordAbilityTable", "Test_Sword_Slash", 0, 0, 2, true, ""})})},
    };
}

/** Writes a tables document to a file of the work folder and loads it. @return True when the loader accepts it. */
bool loads(const Options& options, const json& document, gamedata::ProgressionTables* tables = nullptr) {
    const std::filesystem::path path = options.workDirectory / "review-progression-tables.json";
    std::ofstream(path, std::ios::binary) << document.dump();
    gamedata::ProgressionTables local;
    return (tables != nullptr ? *tables : local).load(path.string());
}

/** Q1: review finding 1, a rank table that is not valid. */
void rank_table(const Options& options) {
    gamedata::ProgressionTables tables;
    const bool base = loads(options, valid_tables(), &tables);
    // The file of the review: the largest rank 65,536 with 65,536 limits of 0.
    json wrap = valid_tables();
    wrap["rankTables"]["TestWeaponRanks"]["experience"] = std::vector<std::uint32_t>(65536, 0);
    wrap["rankTables"]["TestWeaponRanks"]["resetCost"] = std::vector<std::uint32_t>(65537, 0);
    wrap["progressions"][0][2] = 65536;
    json wide = wrap;
    wide["rankTables"]["TestWeaponRanks"]["experience"] = std::vector<std::uint32_t>(65536, 10);
    json shortTable = valid_tables();
    shortTable["progressions"][1][2] = 3; // Mining: the largest rank 3 with the experience of 2 ranks.
    json zero = valid_tables();
    zero["rankTables"]["TestMiningRanks"]["experience"] = json::array({0, 105});
    json zeroAtCap = valid_tables();
    zeroAtCap["rankTables"]["TestMiningRanks"]["experience"] = json::array({100, 0});
    check(base && tables.loaded() && !loads(options, wrap, &tables) && !loads(options, wide) && !loads(options, shortTable)
              && !loads(options, zero) && loads(options, zeroAtCap) && tables.loaded() && tables.level_cap() == 3
              && tables.progression("SwordAbilityTable")->maxRank == 2,
          "the loader refuses the file of the review (the largest rank 65,536 with limits of 0), a largest rank that has "
          "more than 16 bits, a rank table that is shorter than the largest rank, and a limit of 0 below the largest "
          "rank; it accepts a 0 at the largest rank (a table of the game has one). After a refused file the tables are "
          "the tables from before");

    // Tables that no loader checked (a test sets them): the gain of experience has an end.
    ServerHost host;
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight});
    gamedata::ProgressionData data = test_progression();
    gamedata::RankTable endless;
    endless.experience.assign(65536, 0);
    endless.resetCost.assign(65537, 0);
    data.progressions[0].maxRank = 65536;
    data.progressions[0].ranks = endless;
    const bool valid = gamedata::valid_ranks(test_progression().progressions[0]) && !gamedata::valid_ranks(data.progressions[0]);
    host.progression.set(std::move(data));
    // With the fault of the review this call does not return: the case then stops the test
    // program, so that a run has an end.
    std::promise<world::SkillResult> promise;
    std::future<world::SkillResult> future = promise.get_future();
    std::thread worker([&] { promise.set_value(host.world.skills().add_experience(kCharacterA, "SwordAbilityTable", 1)); });
    if (future.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        check(false, "the gain of experience with a rank table of limits of 0 did not return in 10 s (review finding 1)");
        std::fflush(stdout);
        std::quick_exit(1);
    }
    worker.join();
    const world::SkillResult result = future.get();
    check(valid && result.ok() && result.rank == 65535 && result.points == 0,
          "with tables that are not valid and that no loader checked, a gain of 1 experience returns: the rank stops at "
          "the largest rank on the wire (65,535)");
}

/** Q2: review finding 4, a weapon rank with no reset cost row. */
void reset_cost(const Options& options) {
    json none = valid_tables();
    none["rankTables"]["TestWeaponRanks"].erase("resetCost");
    json shortCost = valid_tables();
    shortCost["rankTables"]["TestWeaponRanks"]["resetCost"] = json::array({0, 0});
    json tradeSkill = valid_tables();
    tradeSkill["rankTables"]["TestMiningRanks"].erase("resetCost");
    check(!loads(options, none) && !loads(options, shortCost) && loads(options, tradeSkill),
          "the loader refuses a weapon whose rank table has no reset cost, or a cost for fewer ranks than the weapon "
          "has; a trade skill needs no cost");

    // Tables that no loader checked: a reset at a rank with no cost row is refused.
    ServerHost host;
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight});
    gamedata::ProgressionData data = test_progression();
    data.progressions[0].ranks.resetCost.resize(3); // The ranks 0 to 2 have a cost row (0). The original table has 6 at rank 10.
    host.progression.set(std::move(data));
    world::Skills& skills = host.world.skills();
    const std::uint32_t sword = gamedata::progression_id("SwordAbilityTable");
    const std::uint32_t slash = gamedata::progression_id("Test_Sword_Slash");
    const bool setup = skills.set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 10).ok()
                       && skills.assign(kCharacterA, sword, slash, 1).ok();
    const world::SkillResult refused = skills.reset_tree(kCharacterA, sword);
    const std::size_t nodesAfter = host.store.list_skills(kCharacterA).nodes.size();
    const bool lower = skills.set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 2).ok()
                       && skills.reset_tree(kCharacterA, sword).ok();
    check(setup && refused.error == ProgressError::costNotPaid && nodesAfter == 1 && lower
              && host.store.list_skills(kCharacterA).nodes.empty(),
          "a tree reset at rank 10 with no cost row for that rank is refused (the cost is not known), and the node "
          "stays; a reset at a rank whose row has the cost 0 is done");
}

/** Q3: review finding 5, a level table whose experience does not go up. */
void level_table(const Options& options) {
    json down = valid_tables();
    down["levels"] = json::array({json::array({0, 0, 0, 0}), json::array({100, 5, 50, 0}), json::array({50, 6, 100, 250})});
    json equal = valid_tables();
    equal["levels"] = json::array({json::array({0, 0, 0, 0}), json::array({100, 5, 50, 0}), json::array({100, 6, 100, 250})});
    json first = valid_tables();
    first["levels"] = json::array({json::array({10, 0, 0, 0}), json::array({100, 5, 50, 0}), json::array({300, 6, 100, 250})});
    gamedata::ProgressionTables tables;
    check(!loads(options, down, &tables) && !tables.loaded() && !loads(options, equal) && !loads(options, first)
              && loads(options, valid_tables(), &tables) && tables.loaded(),
          "the loader refuses a level table whose experience goes down (the file of the review: 0, 100, 50), stays the "
          "same, or does not start at 0: the backend then has no tables, which is its default");
}

/** @return The document of creation data as text, or an empty text. */
std::string document_of(const std::string& creationData) {
    std::string compressed;
    std::vector<std::uint8_t> document;
    if (!encoding::base64_decode(creationData, compressed)
        || !compression::zlib_decode(std::span(reinterpret_cast<const std::uint8_t*>(compressed.data()), compressed.size()),
                                     1 << 20, document)) {
        return {};
    }
    return std::string(document.begin(), document.end());
}

/**
 * Q4: review finding 6, white space at the member of the level. From stage U4 the backend writes the menu document with its own
 * writer: creation data of the client with white space at the colon of the member currentLevel reads as before, and the character
 * list gives the level of the row (the backend does not read the member of the stored text).
 */
void menu_level_space(const Options&) {
    state::MenuDocument values;
    values.name = "Spacetest";
    values.appearance.fill("none");
    values.appearance[state::kAppearanceGender] = "Male";
    values.backstory = "Archetype_Test";
    values.worldId = state::local_world().worldId;
    const std::string plain = document_of(state::make_menu_document(values));
    const std::string member = "\"currentLevel\":0";
    const std::size_t at = plain.find(member);
    bool allOk = at != std::string::npos;
    ServerHost host;
    state::AccountOrigin origin{};
    const auto account = host.store.login_account(kSteamIdA, "Tester A", false, origin);
    allOk = allOk && account.has_value();
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_gateway_routes(router, services);
    int number = 0;
    for (const char* form : {"\"currentLevel\": 0", "\"currentLevel\" : 0", "\"currentLevel\"\t:\r\n  0", "\"currentLevel\" :0"}) {
        if (!allOk) {
            break;
        }
        std::string spaced = plain;
        spaced.replace(at, member.size(), form);
        const std::vector<std::uint8_t> stored =
            compression::zlib_store(std::span(reinterpret_cast<const std::uint8_t*>(spaced.data()), spaced.size()));
        state::Character character;
        character.characterId = "eeeeeeee-0000-4000-8000-00000000000" + std::to_string(++number);
        character.personaId = account->personaId;
        character.worldId = state::local_world().worldId;
        character.name = "Spacetest" + std::to_string(number);
        character.createdDate = "2026-01-01T00:00:00.000Z";
        character.modifiedDate = character.createdDate;
        character.creationParams = encoding::base64(std::string(stored.begin(), stored.end()));
        state::ProgressRow row;
        row.level = 20;
        allOk = allOk && state::read_creation_data(character.creationParams).has_value()
                && host.store.insert_character(character) == state::InsertResult::inserted
                && host.store.set_progress(character.characterId, row);
    }
    service::HttpRequest request;
    request.method = "GET";
    request.host = "gateway.test";
    request.path = "/prod/game/getlogininfo";
    request.target = request.path;
    request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", allOk ? account->personaId : ""}}.dump()));
    std::string route;
    const service::HttpResponse reply = router.dispatch(request, route);
    const json body = json::parse(reply.body, nullptr, false);
    int levels = 0;
    if (allOk && reply.status == 200 && body.is_object()) {
        for (const json& entry : body["LoginInfoList"]["Characters"]) {
            const std::string document = entry.contains("PublishedData") ? document_of(entry["PublishedData"].get<std::string>()) : "";
            levels += document.find("\"currentLevel\":19,") != std::string::npos ? 1 : 0;
        }
    }
    check(allOk && levels == 4,
          "creation data with white space before or after the colon of the member currentLevel (a space, a tab, a line "
          "end) reads as before, and the character list gives each such character the level of its row minus 1 (19 for 20)");
}

} // namespace

std::vector<Case> review_progress_cases() {
    return {
        {"Q1", "a rank table that is not valid: the loader refuses it, and a gain of experience has an end (review finding 1 of stage 1.6)", rank_table},
        {"Q2", "a weapon rank with no reset cost row: no tree reset with no cost (review finding 4)", reset_cost},
        {"Q3", "a level table whose experience does not go up is refused (review finding 5)", level_table},
        {"Q4", "white space at the level member of the menu document (review finding 6)", menu_level_space},
    };
}

} // namespace soulwarden::backend::tests

// The cases of the corrections of the code review of stage QG1 (findings QG1R-R1 to QG1R-R3,
// ~/Games/soulwarden/work/evidence/quests/qg1/code-review/FINDINGS.md), corrected in stage QG2a. Each case fails on the tree of stage QG1
// and passes after the correction. Made-up ids and made-up tables with the shape of the game data.
//
//   QGR1  QG1R-R1: the stored cards of a territory pool stay when a start with data that has no card (a file of the format 3) writes the
//         skills of the character; the next start with the cards has them and their spent tokens again
//   QGR2  QG1R-R2: the loader refuses a file of the format 4 whose standing rank table has no card offers, or no rank experience
//   QGR3  QG1R-R3: a standing command that pays no rank experience reports the level and the experience of the character

#include <filesystem>
#include <fstream>
#include <string>

#include "gamedata/progression_tables.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

constexpr const char* kReviewCharacter = "dddddddd-0000-4000-8000-0000000000d1";

gamedata::TerritoryCard card(const char* id, const char* category, gamedata::TerritoryBonus kind, std::uint32_t maxLevel, bool doNotSpend) {
    gamedata::TerritoryCard row;
    row.id = id;
    row.category = category;
    row.kind = kind;
    row.maxLevel = maxLevel;
    row.doNotSpend = doNotSpend;
    row.initialBonus = 0.05F;
    row.reduction = 0.135F;
    row.minValue = 0.0025F;
    return row;
}

/** A standing of the territory 6 with 4 ranks (150, 375, 600 points) and the same four cards at each rank; with no card data (`cards`). */
gamedata::ProgressionData review_tables(bool cards) {
    gamedata::ProgressionData data = test_progression();
    gamedata::StandingRow row;
    row.territory = 6;
    row.id = "6";
    row.maxRank = 3;
    row.pool = "TerritoryRankBonus_6";
    row.autoRankUp = true;
    row.xpModifier = 1.0F;
    row.ranks.experience = {150, 375, 600, 0};
    row.xpReward = {0, 100, 150, 200};
    if (cards) {
        for (int rank = 0; rank < 4; ++rank) {
            row.offers.push_back({gamedata::progression_id("TB_GainStanding"), gamedata::progression_id("TB_GainStorage"),
                                  gamedata::progression_id("TB_GainGatherRate"), gamedata::progression_id("TB_PurchaseHouse")});
        }
        data.territoryCards = {card("TB_GainStanding", "GainStanding", gamedata::TerritoryBonus::gainStanding, 102, false),
                               card("TB_GainStorage", "GainStorage", gamedata::TerritoryBonus::gainStorage, 100, false),
                               card("TB_GainGatherRate", "GainGatherRate", gamedata::TerritoryBonus::gainGatherRate, 101, false),
                               card("TB_PurchaseHouse", "HousePurchase", gamedata::TerritoryBonus::housePurchase, 1, true)};
    }
    data.standings = {row};
    return data;
}

/** QGR1 (QG1R-R1): three starts with one database: the cards, a start with no card data and a write of a weapon mastery, the cards again. */
void kept_cards(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path database = options.workDirectory / "review-qg1-cards.sqlite3";
    std::filesystem::remove(database);
    const std::uint32_t pool = gamedata::progression_id("TerritoryRankBonus_6");
    const std::uint32_t territory = gamedata::progression_id("6");
    const auto stored = [](state::Store& store) { return store.list_skills(kReviewCharacter).nodes.size(); };
    {
        ServerHost host(service::RepServerOptions{}, database.string());
        host.progression.set(review_tables(true)); // the tables with the cards
        (void)add_player(host.store, kSteamIdA, "Review tester", kReviewCharacter, "Reviewer", std::nullopt);
        check(host.world.skills().give_standing(kReviewCharacter, 6, 1125).ok(), "control: the standing reaches the rank 3 (3 tokens)");
        for (const char* id : {"TB_GainStanding", "TB_GainStorage", "TB_GainGatherRate"}) {
            check(host.world.skills().spend_territory_point(kReviewCharacter, territory, gamedata::progression_id(id)).ok(),
                  std::string("control: the choice of ") + id);
        }
        check(stored(host.store) == 3 && host.world.skills().view(kReviewCharacter)->skills.free_tokens(pool) == 0,
              "control: three card rows, no free token");
    }
    {
        // A start with tables that have the standing and no card (the content of a file of the format 3), then a write of the skills.
        ServerHost host(service::RepServerOptions{}, database.string());
        host.progression.set(review_tables(false)); // the tables with no card
        check(host.world.skills().set_rank(kReviewCharacter, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 1).ok(),
              "control: a change of the sword mastery only");
        note("QGR1 card rows after the sword write with no card data: " + std::to_string(stored(host.store)));
        check(stored(host.store) == 3, "the stored cards stay after a write of the skills with data that has no card");
    }
    {
        ServerHost host(service::RepServerOptions{}, database.string());
        host.progression.set(review_tables(true)); // the tables with the cards again
        const std::optional<world::SkillView> view = host.world.skills().view(kReviewCharacter);
        const std::size_t cards = view && view->skills.territoryPools.count(pool) != 0 ? view->skills.territoryPools.at(pool).cards.size() : 0;
        const std::uint32_t tokens = view ? view->skills.free_tokens(pool) : 99;
        note("QGR1 with the cards again: " + std::to_string(cards) + " card(s), " + std::to_string(tokens) + " free token(s)");
        check(cards == 3 && tokens == 0, "the next start with the card data has the three cards and their spent tokens");
    }
}

/** QGR2 (QG1R-R2): a format 4 file needs the card offers and the rank experience of each standing. */
void complete_standing(const Options& options) {
    const std::string base = R"("levelCap":2,"levels":[[0,0,0,0],[100,1,0,0]],"attributes":{"minimum":5,"constitutionHealth":[0]},
        "vitals":{"health":800,"stamina":100,"mana":100},"progressions":[],"abilities":[],"expansions":[],"entitlements":[],"achievements":[])";
    const std::string tail = R"("standings":[["6",2,"Territory_Standing","TerritoryRankBonus_6",true,1.25]],
        "territoryCards":[["TB_GainStanding","GainStanding",1,102,false,0,0.05,0.135,0.0025]]})";
    const std::string offers = R"("cards":[["TB_GainStanding","","",""],["TB_GainStanding","","",""],["","","",""]])";
    const std::string experience = R"("xpReward":[0,100,150])";
    const auto document = [&](bool withOffers, bool withExperience) {
        std::string table = R"("experience":[150,375,0])";
        if (withExperience) {
            table += "," + experience;
        }
        if (withOffers) {
            table += "," + offers;
        }
        return "{\"format\":4," + base + ",\"rankTables\":{\"Territory_Standing\":{" + table + "}}," + tail;
    };
    const auto load = [&](const char* name, const std::string& text) {
        std::filesystem::create_directories(options.workDirectory);
        const std::filesystem::path path = options.workDirectory / name;
        std::ofstream(path) << text;
        gamedata::ProgressionTables tables;
        return tables.load(path.string());
    };
    check(load("review-qg1-complete.json", document(true, true)), "control: a complete file of the format 4 loads");
    const bool noOffers = load("review-qg1-no-offers.json", document(false, true));
    const bool noExperience = load("review-qg1-no-experience.json", document(true, false));
    note(std::string("QGR2 loaded with no offers: ") + (noOffers ? "yes" : "no") + ", with no rank experience: " + (noExperience ? "yes" : "no"));
    check(!noOffers, "the format 4 refuses a standing rank table with no card offers");
    check(!noExperience, "the format 4 refuses a standing rank table with no rank experience");
}

/** QGR3 (QG1R-R3): a standing gain with no new rank reports the level and the experience of the character. */
void standing_level(const Options&) {
    ServerHost host;
    host.progression.set(review_tables(true)); // the tables
    (void)add_player(host.store, kSteamIdA, "Review tester", kReviewCharacter, "Reviewer", std::nullopt);
    check(host.world.progress().set_level(kReviewCharacter, 2).ok(), "control: the character has the level 2");
    const world::StandingResult gain = host.world.skills().give_standing(kReviewCharacter, 6, 10);
    const state::ProgressRow row = host.store.progress(kReviewCharacter);
    note("QGR3 the command: level " + std::to_string(gain.level) + ", experience " + std::to_string(gain.levelExperience) + "; the row: level "
         + std::to_string(row.level) + ", experience " + std::to_string(row.experience));
    check(gain.ok() && gain.gain.ranks == 0 && row.level == 2, "control: the gain of 10 at the rank 0 gives no rank");
    check(gain.level == row.level && gain.levelExperience == row.experience,
          "the result of a gain with no rank experience has the level and the experience of the character");
}

} // namespace

std::vector<Case> review_qg1_cases() {
    return {{"QGR1", "review of QG1, QG1R-R1: the stored cards stay through a start with no card data", kept_cards},
            {"QGR2", "review of QG1, QG1R-R2: the format 4 needs the card offers and the rank experience of a standing", complete_standing},
            {"QGR3", "review of QG1, QG1R-R3: a standing gain with no rank experience reports the level of the character", standing_level}};
}

} // namespace soulwarden::backend::tests

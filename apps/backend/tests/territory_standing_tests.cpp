// The cases of stage QG1 (docs/systems/progression-and-skills.md, the territory standing): the ranks and the rank-up, the tokens of the
// territory pools, the card state (slot 67), the card choice (message 4040), the card values (client 0x5C51100), the cards that a system
// applies, the territory of a character, the territory of a quest standing, the loaders of the game data, and two sessions (rule MP-10).
// Made-up ids and made-up tables with the shape and the values of the game data (the standing table, the 10 cards).
//
//   QG1A  the rank-up: one rank, more than one rank in one gain, equality, the clamp at 300, the points above a threshold, the experience
//         of a rank with TerritoryStandingXpModifier, the GainStanding card (f32, truncation), the card values
//   QG1B  the points state: each weapon pool and each territory pool in one fragment, the tokens, the weapon nodes stay; the bytes of the
//         fixtures of the research (a fresh pool with one token: 28 bytes; one GainStanding choice: 38 bytes)
//   QG1C  the card choice 4040: the body, a choice, each refusal (no change, the state again), the house card, a restart keeps the
//         standing, the tokens, and the cards
//   QG1D  the cards that a system applies: GainGatherRate on the speed of a gather in the territory of the character, GainXP on the
//         experience of a game event (Skills::card_experience: the hand-in and the kill), the territory of the character; the
//         other cards are stored only
//   QG1F  two sessions: two characters with different standings, tokens, and cards; each session gets only its own state (MP-10)
//   QG1G  the loaders: the progression file of the format 4 (the cards, the offers, the modifier; a format 3 file), the bounds of the
//         territories of the POI data of the format 2, the territory of a character

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/poi_data.h"
#include "gamedata/progression_tables.h"
#include "server/rep/progress_requests.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using world::ProgressError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-0000000000a1";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-0000000000b1";
constexpr float kHeight = 50.0F;
/** The ids on the wire (research territories.md section 5): the standing of Windsward ("6") and its pool, and of Everfall ("4"). */
constexpr std::uint32_t kWindsward = 0x1DB87A14U;
constexpr std::uint32_t kWindswardPool = 0xCCDDF19BU;
constexpr std::uint32_t kEverfall = 0xF3B61B38U;
constexpr std::uint32_t kEverfallPool = 0x22D390B7U;
constexpr std::uint32_t kGainStanding = 0x68C1F9AAU;

std::uint32_t id_of(const char* text) {
    return gamedata::progression_id(text);
}

/** @return An f32 of its bits (the values of the Territory Bonus Data of the game: 0x3D4CCCCD is 0.05). */
float bits(std::uint32_t value) {
    float number = 0.0F;
    std::memcpy(&number, &value, sizeof(number));
    return number;
}

gamedata::TerritoryCard make_card(const char* id, const char* category, gamedata::TerritoryBonus kind, std::uint32_t maxLevel, bool doNotSpend,
                                  std::uint32_t requiredRank, float initial, float reduction, float minimum) {
    gamedata::TerritoryCard card;
    card.id = id;
    card.category = category;
    card.kind = kind;
    card.maxLevel = maxLevel;
    card.doNotSpend = doNotSpend;
    card.requiredRank = requiredRank;
    card.initialBonus = initial;
    card.reduction = reduction;
    card.minValue = minimum;
    return card;
}

/** The 10 cards of the game data (javelindata_progressionpointdata, the Territory Bonus Data of playerbaseattributes.pbadb). */
std::vector<gamedata::TerritoryCard> game_cards() {
    using gamedata::TerritoryBonus;
    const float five = bits(0x3D4CCCCDU);
    const float reduction = bits(0x3E0A3D71U);
    const float least = bits(0x3B23D70AU);
    return {make_card("TB_GainStanding", "GainStanding", TerritoryBonus::gainStanding, 102, false, 0, five, reduction, least),
            make_card("TB_GainXP", "GainXP", TerritoryBonus::gainXp, 100, false, 0, bits(0x3CF5C28FU), bits(0x3DCCCCCDU), least),
            make_card("TB_GainStorage", "GainStorage", TerritoryBonus::gainStorage, 100, false, 0, 250.0F, 0.0F, 1.0F),
            make_card("TB_GainGatherRate", "GainGatherRate", TerritoryBonus::gainGatherRate, 101, false, 0, five, reduction, least),
            make_card("TB_GainFactionRep", "GainFactionRep", TerritoryBonus::gainFactionRep, 101, false, 0, five, reduction, least),
            make_card("TB_GainHousePoints", "GainHousePoints", TerritoryBonus::gainHousePoints, 97, false, 0, 5.0F, 0.0F, 0.0F),
            make_card("TB_TaxTradingPost", "TradingTax", TerritoryBonus::tradingTax, 101, false, 0, five, reduction, least),
            make_card("TB_TaxStations", "StationTax", TerritoryBonus::stationTax, 101, false, 0, five, reduction, least),
            make_card("TB_TaxProperty", "PropertyTax", TerritoryBonus::propertyTax, 97, false, 0, five, reduction, least),
            make_card("TB_PurchaseHouse", "HousePurchase", TerritoryBonus::housePurchase, 1, true, 1, 1.0F, 0.0F, 0.0F)};
}

/**
 * A standing row with the shape of the game data: MaxLevel 300, MaximumInfluence 150 + 225 R up to 22,500 (rank 99: 22,425; rank 300:
 * 0), XpReward 50 + 50 R up to 2,000 (rank 1: 100), the cards of the first two rows of the data (the even rows: GainStanding, GainXP,
 * GainGatherRate, PurchaseHouse; the odd rows: TaxStations, TaxTradingPost, GainStorage, PurchaseHouse), no card at the rank 300.
 */
gamedata::StandingRow standing_row(std::uint16_t territory, float modifier) {
    gamedata::StandingRow row;
    row.territory = territory;
    row.id = std::to_string(territory);
    row.maxRank = 300;
    row.pool = "TerritoryRankBonus_" + std::to_string(territory);
    row.autoRankUp = true;
    row.xpModifier = modifier;
    for (std::uint32_t rank = 0; rank <= 300; ++rank) {
        row.ranks.experience.push_back(rank == 300 ? 0 : std::min(150U + 225U * rank, 22500U));
        row.xpReward.push_back(rank == 0 || rank == 300 ? 0 : std::min(50U + 50U * rank, 2000U));
        if (rank == 300) {
            row.offers.push_back({0, 0, 0, 0});
        } else if (rank % 2 == 0) {
            row.offers.push_back({id_of("TB_GainStanding"), id_of("TB_GainXP"), id_of("TB_GainGatherRate"), id_of("TB_PurchaseHouse")});
        } else {
            row.offers.push_back({id_of("TB_TaxStations"), id_of("TB_TaxTradingPost"), id_of("TB_GainStorage"), id_of("TB_PurchaseHouse")});
        }
    }
    return row;
}

/** The tables of the tests with the standings of Windsward (6, modifier 1.25 of the data) and of a territory 4 with the modifier 0.5. */
gamedata::ProgressionData standing_tables() {
    gamedata::ProgressionData data = test_progression();
    data.standings = {standing_row(6, 1.25F), standing_row(4, 0.5F)};
    data.territoryCards = game_cards();
    return data;
}

/** @return The standing of a skill set (rank and points). */
world::ProgressionRank standing_of(const world::SkillSet& skills, std::uint32_t id) {
    const auto found = skills.progressions.find(id);
    return found != skills.progressions.end() ? found->second : world::ProgressionRank{};
}

/** Reads a state with one group as a fragment (the record of one slot). */
Fragment decode_points(const Bytes& state, bool& ok) {
    server::rep::RecordWriter record(1);
    record.fragment(kPointsSlot, server::rep::kTypePointsState, state);
    server::hub::Writer body;
    const Bytes payload = record.finish();
    body.u8(1);
    body.size64(1);
    body.u8(1);
    body.u8(0);
    body.u8(0);
    body.u8(0);
    body.size(static_cast<std::uint32_t>(payload.size()));
    body.raw(payload);
    const Bundle bundle = read_bundle(body.data());
    ok = bundle.complete && bundle.records.size() == 1 && bundle.records[0].fragments.size() == 1;
    return ok ? bundle.records[0].fragments[0] : Fragment{};
}

/** Body of ProgressionPointComponentServerFacet_RequestSpendTerritoryStandingPoint (type 4040): progressionId first (writer 0x3274450). */
Bytes card_body(std::uint64_t entity, std::uint32_t progressionId, std::uint32_t pointId) {
    server::hub::Writer body;
    body.u64(target_of(entity, kPointsSlot));
    body.u64(entity);
    body.u32(progressionId);
    body.u32(pointId);
    return body.data();
}

/** @return The rows of the cards of a character as text (card:level, in the sequence of the letters). */
std::string card_rows(state::Store& store, const std::string& character) {
    std::vector<std::string> names;
    for (const auto& row : store.list_skills(character).nodes) {
        names.push_back(row.name + ":" + std::to_string(row.level));
    }
    std::sort(names.begin(), names.end());
    std::string text;
    for (const std::string& name : names) {
        text += (text.empty() ? "" : " ") + name;
    }
    return text;
}

struct Players {
    Player alpha;
    Player beta;
    explicit Players(state::Store& store)
        : alpha(add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{9622.0F, 2306.0F, kHeight})),
          beta(add_player(store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{9624.0F, 2306.0F, kHeight})) {}
};

/** QG1A: the rank-up rules. */
void rank_up(const Options&) {
    ServerHost host;
    host.progression.set(standing_tables());
    const Players players(host.store);
    world::Skills& skills = host.world.skills();
    world::StandingGain gain;

    world::Skills::Plan plan = skills.plan_none(kCharacterA);
    const bool below = skills.add_standing(plan, 6, 140, &gain) == ProgressError::none && gain.rank == 0 && gain.rankPoints == 140
                       && gain.ranks == 0 && gain.experience == 0 && plan.rankExperience == 0;
    check(below && skills.add_standing(plan, 6, 20, &gain) == ProgressError::none && gain.rank == 1 && gain.rankPoints == 10 && gain.ranks == 1
              && gain.experience == 125 && plan.rankExperience == 125 && plan.skills.free_tokens(kWindswardPool) == 1
              && plan.touched.ranks && plan.touched.tokens,
          "one rank (research quests-18d rules Q8, client 0x3CAACF0): 140 + 20 is rank 1 with 10 points, one token, and the experience "
          "XpReward 100 times TerritoryStandingXpModifier 1.25 = 125");
    world::Skills::Plan equal = skills.plan_none(kCharacterA);
    check(skills.add_standing(equal, 6, 150, &gain) == ProgressError::none && gain.rank == 1 && gain.rankPoints == 0 && gain.ranks == 1,
          "equality enters the rank path (0x3CAAF15): 150 points of rank 0 is rank 1 with 0 points");
    world::Skills::Plan many = skills.plan_none(kCharacterA);
    check(skills.add_standing(many, 6, 1130, &gain) == ProgressError::none && gain.rank == 3 && gain.rankPoints == 5 && gain.ranks == 3
              && gain.experience == 125 + 187 + 250 && many.skills.free_tokens(kWindswardPool) == 3,
          "more than one rank in one gain: 1,130 = 150 + 375 + 600 + 5 is rank 3 with 5 points, three tokens, and 125 + 187 + 250 "
          "experience (150 times 1.25 is truncated to 187)");
    world::Skills::Plan other = skills.plan_none(kCharacterA);
    check(skills.add_standing(other, 4, 1130, &gain) == ProgressError::none && gain.experience == 50 + 75 + 100
              && other.skills.free_tokens(kEverfallPool) == 3 && other.skills.free_tokens(kWindswardPool) == 0,
          "the modifier of the territory: 0.5 for the territory 4 gives 50 + 75 + 100 experience, and the tokens go to its own pool");

    world::Skills::Plan top = skills.plan_none(kCharacterA);
    top.skills.progressions[kWindsward] = world::ProgressionRank{299, 22000};
    const bool clamp = skills.add_standing(top, 6, 1000, &gain) == ProgressError::none && gain.rank == 300 && gain.rankPoints == 0
                       && gain.ranks == 1 && gain.experience == 0 && top.skills.free_tokens(kWindswardPool) == 300;
    world::StandingGain after;
    const std::uint64_t experience = top.rankExperience;
    check(clamp && skills.add_standing(top, 6, 5000, &after) == ProgressError::none && after.rank == 300 && after.rankPoints == 0
              && after.ranks == 0 && after.points == 0 && top.rankExperience == experience,
          "the clamp at 300 (MaxLevel 300, MaximumInfluence 0, AllowOverflow false): rank 299 with 22,000 and 1,000 more is rank 300 "
          "with 0 points (the other 500 go), 300 tokens, no experience (XpReward 0 of the rank 300); a gain at the rank 300 adds nothing");
    world::Skills::Plan whole = skills.plan_none(kCharacterA);
    check(skills.add_standing(whole, 6, 10000000, &gain) == ProgressError::none && gain.rank == 300 && gain.ranks == 300
              && gain.rankPoints == 0 && whole.skills.free_tokens(kWindswardPool) == 300,
          "one gain of 10,000,000 passes each rank to 300: 300 ranks, 300 tokens");

    // The GainStanding card of the territory pool (client 0x31F7D19 to 0x31F7D37): (bonus + 1) times the standing in f32, truncated.
    world::Skills::Plan carded = skills.plan_none(kCharacterA);
    carded.skills.territoryPools[kWindswardPool].cards[kGainStanding] = 1;
    const bool first = skills.add_standing(carded, 6, 100, &gain) == ProgressError::none && gain.base == 100 && gain.points == 104
                       && std::fabs(gain.bonus - 0.05F) < 1e-9F;
    carded.skills.territoryPools[kWindswardPool].cards[kGainStanding] = 2;
    const bool second = skills.add_standing(carded, 6, 100, &gain) == ProgressError::none && gain.points == 109;
    const bool none = skills.add_standing(carded, 4, 100, &gain) == ProgressError::none && gain.points == 100;
    check(first && second && none,
          "GainStanding level 1 (0.05): 100 standing is 104 (1.05f times 100 is 104.99999 in f32, then the truncation of the client); "
          "level 2 (0.09325): 109; the card of Windsward does not change the standing of the territory 4");

    // The card values (client 0x5C51100), against values of an f32 calculation of the research data.
    const std::vector<gamedata::TerritoryCard> cards = game_cards();
    const auto value = [&](std::size_t card, std::uint32_t level) {
        const float number = gamedata::card_bonus(cards[card], level);
        std::uint32_t raw = 0;
        std::memcpy(&raw, &number, sizeof(raw));
        return raw;
    };
    check(value(0, 0) == 0 && value(0, 1) == 0x3D4CCCCDU && value(0, 2) == 0x3DBEF9DCU && value(0, 3) == 0x3E05CC10U && value(0, 20) == 0x3EB33374U
              && value(0, 102) == 0x3F0E24F6U && value(1, 1) == 0x3CF5C28FU && value(1, 2) == 0x3D6978D4U && value(1, 100) == 0x3EEEA0B4U
              && gamedata::card_bonus(cards[2], 3) == 750.0F && gamedata::card_bonus(cards[9], 1) == 1.0F,
          "the card values of the client formula (f32 subtraction, f64 power and product, an f32 increment with Min Value as its least "
          "value, an f32 sum): GainStanding 0.05, 0.09325, 0.13066 at the levels 1 to 3, 0.55525 at 102; GainXP 0.03, 0.057, 0.46607 at "
          "100; GainStorage 750 at 3; PurchaseHouse 1");

    world::Skills::Plan unknown = skills.plan_none(kCharacterA);
    ServerHost bare;
    const Players others(bare.store);
    world::Skills::Plan noTables = bare.world.skills().plan_none(kCharacterA);
    check(skills.add_standing(unknown, 9, 100) == ProgressError::unknownProgression
              && bare.world.skills().add_standing(noTables, 6, 100) == ProgressError::noTables,
          "a territory with no standing row, and no tables: refused");

    // The command: one transaction with the experience of the progress system (the character is not in the world: the rows).
    const world::StandingResult command = skills.give_standing(kCharacterA, 6, 1130);
    const state::SkillRows rows = host.store.list_skills(kCharacterA);
    bool stored = false;
    for (const auto& row : rows.ranks) {
        stored = stored || (row.id == kWindsward && row.rank == 3 && row.points == 5 && row.name == "6");
    }
    check(command.ok() && !command.inWorld && command.tokens == 3 && command.gain.experience == 562 && stored
              && host.store.progress(kCharacterA).experience == 562,
          "the standing command: rank 3 with 5 points in the row, 3 tokens, and the 562 experience of the ranks in the progress row");
}

/** QG1B: the points state with the territory pools. */
void points_state(const Options&) {
    using namespace server::rep;
    bool ok = false;
    // The fixtures of the research (territories.md section 10, verified by an independent encoder): the bytes after the slot and the type.
    world::SkillSet fresh;
    fresh.progressions[kWindsward] = world::ProgressionRank{1, 0};
    fresh.territoryPools[kWindswardPool].standing = kWindsward;
    const Bytes token = encode_points(fresh, 1, 0);
    check(hex_text(token) == "010f" "00010101ccddf19b" "000101010001" "0001010102" "00010100",
          "a fresh pool with one token: the 25 bytes of the 28-byte fixture standing-pool.hex after its slot and type (pool CCDDF19B, one "
          "token, the category 2 of a territory, no entry)");
    fresh.territoryPools[kWindswardPool].cards[kGainStanding] = 1;
    fresh.territoryPools[kWindswardPool].spent = 1;
    check(hex_text(encode_points(fresh, 1, 0)) == "010f" "00010101ccddf19b" "000101010000" "0001010102" "00010101ccddf19b68c1f9aa0001",
          "one GainStanding choice: the 35 bytes of the 38-byte fixture standing-card-selected.hex after its slot and type (no free "
          "token, the entry CCDDF19B 68C1F9AA level 1)");

    // A character with weapon nodes and two territory pools: one fragment has each pool and each entry.
    world::SkillSet skills;
    skills.progressions[kSword] = world::ProgressionRank{5, 0};
    skills.pools[kSword][node_id("Test_Sword_Slash")].level = 1;
    skills.pools[kSword][node_id("Test_Sword_P0a")].level = 1;
    skills.pools[kBow];
    skills.progressions[kWindsward] = world::ProgressionRank{3, 7};
    skills.territoryPools[kWindswardPool] = world::TerritoryPool{kWindsward, {{kGainStanding, 2}, {id_of("TB_PurchaseHouse"), 1}}, 2};
    skills.territoryPools[kEverfallPool] = world::TerritoryPool{kEverfall, {}, 0};
    const WirePools wire = pools_of(decode_points(encode_points(skills, 7, 0), ok));
    const auto pool = [&](std::uint32_t id) {
        for (const WirePool& entry : wire.pools) {
            if (entry.id == id) {
                return entry;
            }
        }
        return WirePool{};
    };
    check(ok && wire.ok && wire.pools.size() == 4 && pool(kSword) == WirePool{kSword, 3, 1} && pool(kBow) == WirePool{kBow, 0, 1}
              && pool(kWindswardPool) == WirePool{kWindswardPool, 1, 2} && pool(kEverfallPool) == WirePool{kEverfallPool, 0, 2}
              && wire.nodes.size() == 4 && wire.poolStamp == 7 && wire.entryStamp == 7,
          "one fragment has the two weapon pools (category 1) and the two territory pools (category 2): Windsward rank 3 with "
          "GainStanding 2 and the house (no token) has 1 free token, Everfall 0; the 2 nodes and the 2 cards are the 4 entries");
    ClientTrees client;
    client.apply(decode_points(encode_points(skills, 7, 0), ok));
    skills.territoryPools[kWindswardPool].cards[kGainStanding] = 3;
    skills.territoryPools[kWindswardPool].spent = 3;
    client.apply(decode_points(encode_points(skills, 8, 0), ok));
    check(!client.fault && client.refused == 0 && client.has(kSword, node_id("Test_Sword_Slash")) && client.has(kSword, node_id("Test_Sword_P0a"))
              && client.free(kSword) == 3 && client.free(kWindswardPool) == 0 && client.pools.at(kWindswardPool).nodes.at(kGainStanding) == 3
              && client.pools.count(kEverfallPool) == 1,
          "the client model (the list of a state replaces its lists): after a card choice the weapon nodes and the other territory "
          "pool stay, and the card has the level 3");

    // A session: a character in the world gets the territory pools of the tables in its first state, with no card.
    ServerHost host;
    host.progression.set(standing_tables());
    const Players players(host.store);
    FakeClient a(27100);
    host.enter(a, players.alpha.registration());
    host.steps(2, {&a});
    ClientTrees trees;
    std::size_t from = 0;
    follow_trees(trees, a, from);
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    check(trees.fragments >= 1 && !trees.fault && trees.pools.count(kWindswardPool) == 1 && trees.pools.count(kEverfallPool) == 1
              && trees.free(kWindswardPool) == 0 && trees.pools.count(kSword) == 1 && trees.pools.count(kBow) == 1 && alpha != nullptr
              && alpha->skills.territoryPools.size() == 2,
          "the first state of a session has each weapon pool and each territory pool of the tables (the client removes a pool that a "
          "state does not have)");
    host.world.skills().set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 3);
    host.world.skills().assign(kCharacterA, kSword, node_id("Test_Sword_Slash"), 1);
    host.steps(2, {&a});
    follow_trees(trees, a, from);
    const world::StandingResult gained = host.world.skills().give_standing(kCharacterA, 6, 400);
    host.steps(2, {&a});
    const std::size_t fragments = follow_trees(trees, a, from);
    const SlotUpdates ranks = slot_updates(a, kCategoricalSlot, 0);
    const WireRanks lastRanks = ranks.fragments.empty() ? WireRanks{} : ranks_of(ranks.fragments.back());
    bool rankEntry = false;
    for (const WireRank& entry : lastRanks.entries) {
        rankEntry = rankEntry || (entry.id == kWindsward && entry.rank == 1 && entry.points == 250);
    }
    check(gained.ok() && gained.inWorld && gained.tokens == 1 && fragments >= 1 && trees.free(kWindswardPool) == 1
              && trees.has(kSword, node_id("Test_Sword_Slash")) && trees.free(kSword) == 2 && !trees.fault && rankEntry
              && alpha->progression.experience == 125,
          "a rank-up in the world: the categorical state has rank 1 with 250 points, the points state has the token, the weapon node "
          "stays, and the experience of the rank (125) is on the character");
}

/** QG1C: the card choice 4040. */
void card_choice(const Options&) {
    using namespace server::rep;
    ServerHost host;
    host.progression.set(standing_tables());
    const Players players(host.store);
    FakeClient a(27110);
    host.enter(a, players.alpha.registration());
    host.steps(2, {&a});
    world::Entity* alpha = host.world.find_character(kCharacterA);
    ClientTrees trees;
    std::size_t from = 0;
    follow_trees(trees, a, from);
    const auto send = [&](const Bytes& body) {
        host.datagram(a.peer, a.component_message(kTypeSpendTerritoryStandingPoint, alpha->actor.bytes, body));
        host.steps(2, {&a});
        return follow_trees(trees, a, from);
    };
    const Bytes body = card_body(alpha->id, kWindsward, kGainStanding);
    check(body.size() == 24 && hex_text(body).substr(32) == "1db87a14" "68c1f9aa" && read_territory_point_request(body)->progressionId == kWindsward
              && read_territory_point_request(body)->pointId == kGainStanding,
          "the body of 4040 (24 bytes): target, entity, the territory CRC 1DB87A14 (\"6\", not the pool), then the card 68C1F9AA "
          "(TB_GainStanding)");

    // Each refusal changes nothing and sends the state again.
    const auto refused = [&](const Bytes& request, const char* error) {
        // The line of the session (the skill system writes its own line with "refused").
        const std::string text = std::string("): ") + error + ", card level 0";
        const std::string rows = card_rows(host.store, kCharacterA);
        const std::size_t lines = log_count(text);
        const world::TerritoryPool pool = alpha->skills.territoryPools.at(kWindswardPool);
        const std::size_t fragments = send(request);
        const world::TerritoryPool now = alpha->skills.territoryPools.at(kWindswardPool);
        return card_rows(host.store, kCharacterA) == rows && log_count(text) == lines + 1 && now.cards == pool.cards && now.spent == pool.spent
               && fragments >= 1;
    };
    const bool noToken = refused(body, "the territory pool has no free token");
    host.world.skills().give_standing(kCharacterA, 6, 150);
    host.steps(2, {&a});
    follow_trees(trees, a, from);
    const bool tokenThere = trees.free(kWindswardPool) == 1;
    const bool notOffered = refused(card_body(alpha->id, kWindsward, id_of("TB_TaxStations")), "the card screen does not offer this card for the spent tokens of the pool");
    const bool unknownCard = refused(card_body(alpha->id, kWindsward, 0x12345678U), "the id is not a card of the territory pools");
    const bool unknownStanding =
        refused(card_body(alpha->id, kWindswardPool, kGainStanding), "the game data has no weapon mastery and no trade skill with this id");
    Bytes longer = body;
    longer.push_back(0);
    const std::size_t malformed = log_count("territory card request (type 4040, 25 bytes) does not have the form");
    send(longer);
    Bytes foreign = card_body(alpha->id + 1, kWindsward, kGainStanding);
    const std::size_t wrongEntity = log_count("territory card request (type 4040) names the entity");
    send(foreign);
    check(noToken && tokenThere && notOffered && unknownCard && unknownStanding
              && log_count("territory card request (type 4040, 25 bytes) does not have the form") == malformed + 1
              && log_count("territory card request (type 4040) names the entity") == wrongEntity + 1
              && alpha->skills.territoryPools.at(kWindswardPool).cards.empty(),
          "refused with no change, and the points state goes again: no free token (rank 0), a card of a different rank row (TaxStations "
          "at 0 spent tokens), an id that is not a card, the pool id in place of the territory CRC; a body with one byte more and the "
          "entity of a different character: no change");

    // The house card: it needs the rank 1 and spends no token (DoNotSpendPoint).
    check(send(card_body(alpha->id, kWindsward, id_of("TB_PurchaseHouse"))) >= 1 && trees.free(kWindswardPool) == 1
              && trees.pools.at(kWindswardPool).nodes.at(id_of("TB_PurchaseHouse")) == 1
              && refused(card_body(alpha->id, kWindsward, id_of("TB_PurchaseHouse")), "the card has its largest level"),
          "TB_PurchaseHouse at rank 1: level 1 and the token stays (DoNotSpendPoint); a second choice: refused (MaxLevel 1)");
    // A choice: the level, the token, the row, the state.
    check(send(body) >= 1 && trees.free(kWindswardPool) == 0 && trees.pools.at(kWindswardPool).nodes.at(kGainStanding) == 1
              && card_rows(host.store, kCharacterA) == "TB_GainStanding:1 TB_PurchaseHouse:1"
              && log_count("rep: territory card request (type 4040, standing 1DB87A14, card 68C1F9AA): done, card level 1, 0 free token(s)") == 1
              && host.world.skills().territory_bonus(alpha->skills, 6, gamedata::TerritoryBonus::gainStanding) == bits(0x3D4CCCCDU),
          "a choice of GainStanding: level 1, no free token, the row TB_GainStanding:1, the state; the card gives 0.05");
    // The next choice uses the next rank row (1 spent token: TaxStations, TaxTradingPost, GainStorage, PurchaseHouse).
    host.world.skills().give_standing(kCharacterA, 6, 375);
    host.steps(2, {&a});
    follow_trees(trees, a, from);
    const bool second = refused(body, "the card screen does not offer this card for the spent tokens of the pool");
    check(second && send(card_body(alpha->id, kWindsward, id_of("TB_GainStorage"))) >= 1 && trees.free(kWindswardPool) == 0
              && card_rows(host.store, kCharacterA) == "TB_GainStanding:1 TB_GainStorage:1 TB_PurchaseHouse:1",
          "with one spent token the screen offers the row 1: GainStanding is refused, GainStorage is level 1");

    // A restart: the standing, the tokens, and the cards come from the rows.
    host.disconnect(a.peer);
    host.steps(2, {});
    FakeClient again(27111);
    host.enter(again, players.alpha.registration());
    host.steps(2, {&again});
    ClientTrees reloaded;
    std::size_t start = 0;
    follow_trees(reloaded, again, start);
    const world::Entity* back = host.world.find_character(kCharacterA);
    check(back != nullptr && standing_of(back->skills, kWindsward).rank == 2 && standing_of(back->skills, kWindsward).points == 18
              && back->skills.free_tokens(kWindswardPool) == 0 && reloaded.pools.at(kWindswardPool).nodes.size() == 3
              && back->skills.territoryPools.at(kWindswardPool).spent == 2,
          "a new world entry: rank 2 with 18 points (the gain of 375 had the GainStanding card: 393), the three cards (the house "
          "spends no token: 2 spent of 2), no free token, in the first state");
}

/** QG1D: the cards that a system applies. */
void applied_cards(const Options&) {
    gather::GatherShop shop;
    gamedata::ProgressionData data = gather::gathering_progression();
    data.standings = {standing_row(6, 1.25F), standing_row(4, 0.5F)};
    data.territoryCards = game_cards();
    shop.host.progression.set(data);
    shop.enter();
    world::Entity& a = *shop.entity(interaction::kCharacterA);
    check(shop.equip(interaction::kCharacterA, "TestPick", gather::kPickSlot, 150), "setup: the pick");
    const gamedata::GatherableRow& ore = shop.host.gathering.rows()[gather::kOreRow];
    const world::GatherParts plain = shop.gathering().parts_of(a, ore);
    // The bounds of Windsward (6) around A, and the card GainGatherRate 1 of its pool: one more part of the speed sum (an agent rule).
    const std::string bounds = nlohmann::json{{"format", 2},
                                              {"pois", nlohmann::json::array()},
                                              {"territories",
                                               nlohmann::json::array({{{"id", 6},
                                                                       {"shape", nlohmann::json::array({nlohmann::json::array({a.position.x - 50.0F, a.position.y - 50.0F}),
                                                                                                        nlohmann::json::array({a.position.x + 50.0F, a.position.y - 50.0F}),
                                                                                                        nlohmann::json::array({a.position.x + 50.0F, a.position.y + 50.0F}),
                                                                                                        nlohmann::json::array({a.position.x - 50.0F, a.position.y + 50.0F})})}}})}}
                                   .dump();
    check(shop.host.pois.load_text(bounds) && shop.host.world.territory_of(a) == 6, "setup: A stands in the bounds of Windsward");
    const world::GatherParts outsideCard = shop.gathering().parts_of(a, ore);
    a.skills.territoryPools[kWindswardPool].cards[id_of("TB_GainGatherRate")] = 1;
    const world::GatherParts carded = shop.gathering().parts_of(a, ore);
    const float expected = carded.toolSpeed * (1.0F + carded.rankEfficiency + carded.effects + bits(0x3D4CCCCDU));
    check(outsideCard.territory == 0.0F && outsideCard.speed == plain.speed && carded.territory == bits(0x3D4CCCCDU)
              && std::fabs(carded.speed - expected) < 1e-6F && carded.speed > plain.speed,
          "GainGatherRate level 1 of Windsward while A is in Windsward: the part 0.05 of the speed sum (tool x (1 + rank + effects + 0.05))");
    a.skills.territoryPools.erase(kWindswardPool);
    a.skills.territoryPools[kEverfallPool].cards[id_of("TB_GainGatherRate")] = 3;
    check(shop.gathering().parts_of(a, ore).territory == 0.0F,
          "a card of a different territory (4) does not act in Windsward");
    // GainXP: the experience of a game event in the territory of the character (client 0x3CEC0CA, 0x3CF04A9).
    world::Skills& skills = shop.host.world.skills();
    a.skills.territoryPools[kWindswardPool].cards[id_of("TB_GainXP")] = 2;
    check(skills.card_experience(interaction::kCharacterA, 1000) == 1057 && skills.card_experience(interaction::kCharacterA, 40) == 42
              && skills.card_experience(interaction::kCharacterB, 1000) == 1000,
          "GainXP level 2 (0.057): 1,000 experience of a game event are 1,057 (f32, truncated), 40 are 42; B (no card) keeps 1,000");
    check(shop.host.pois.load_text(R"({"format":2,"pois":[],"territories":[]})") && skills.card_experience(interaction::kCharacterA, 1000) == 1000,
          "outside each territory (no detected territory) the card has no effect (client 0x3CF0AE0: a zero territory gives no bonus)");
}

/** QG1F: two sessions (MP-10). */
void two_sessions(const Options&) {
    using namespace server::rep;
    service::RepServerOptions observing;
    observing.serverVersion = "test-version";
    observing.observePlayers = true;
    ServerHost host(observing);
    host.progression.set(standing_tables());
    const Players players(host.store);
    FakeClient a(27120);
    FakeClient b(27121);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.steps(3, {&a, &b});
    world::Skills& skills = host.world.skills();
    skills.give_standing(kCharacterA, 6, 1130);
    skills.give_standing(kCharacterB, 4, 160);
    host.steps(2, {&a, &b});
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    host.datagram(a.peer, a.component_message(kTypeSpendTerritoryStandingPoint, alpha->actor.bytes, card_body(alpha->id, kWindsward, kGainStanding)));
    host.datagram(b.peer, b.component_message(kTypeSpendTerritoryStandingPoint, beta->actor.bytes,
                                              card_body(beta->id, kEverfall, id_of("TB_GainGatherRate"))));
    // B names the entity of A: no change of A.
    host.datagram(b.peer, b.component_message(kTypeSpendTerritoryStandingPoint, beta->actor.bytes, card_body(alpha->id, kWindsward, kGainStanding)));
    host.steps(3, {&a, &b});
    ClientTrees treesA;
    ClientTrees treesB;
    std::size_t fromA = 0;
    std::size_t fromB = 0;
    follow_trees(treesA, a, fromA);
    follow_trees(treesB, b, fromB);
    check(standing_of(alpha->skills, kWindsward).rank == 3 && standing_of(alpha->skills, kEverfall).rank == 0
              && standing_of(beta->skills, kEverfall).rank == 1 && standing_of(beta->skills, kWindsward).rank == 0
              && alpha->skills.territoryPools.at(kWindswardPool).cards.at(kGainStanding) == 1 && alpha->skills.free_tokens(kWindswardPool) == 2
              && beta->skills.territoryPools.at(kEverfallPool).cards.at(id_of("TB_GainGatherRate")) == 1 && beta->skills.free_tokens(kEverfallPool) == 0
              && beta->skills.territoryPools.at(kWindswardPool).cards.empty() && alpha->skills.territoryPools.at(kEverfallPool).cards.empty(),
          "A: Windsward rank 3, GainStanding 1, 2 free tokens; B: the territory 4 rank 1, GainGatherRate 1, 0 free tokens; the request "
          "of B with the entity of A changed nothing");
    check(treesA.free(kWindswardPool) == 2 && treesA.free(kEverfallPool) == 0 && treesA.pools.at(kWindswardPool).nodes.size() == 1
              && treesB.free(kEverfallPool) == 0 && treesB.free(kWindswardPool) == 0 && treesB.pools.at(kEverfallPool).nodes.size() == 1
              && treesB.pools.at(kWindswardPool).nodes.empty() && !treesA.fault && !treesB.fault,
          "each client has its own pools: A sees its card and its 2 tokens, B its card and no token of Windsward");
    // The interest of B in A: no points state and no categorical state of A goes to B (owner only).
    bool foreign = false;
    for (std::uint32_t interest = 2; interest < 8; ++interest) {
        foreign = foreign || !slot_updates(b, kPointsSlot, 0, interest).fragments.empty() || !slot_updates(b, kCategoricalSlot, 0, interest).fragments.empty();
    }
    check(!foreign, "B gets no points state and no categorical state of the entity of A (the remote player slice has no such facet)");
    // The house card needs a free token and does not spend it (client 0x3CE7564, 0x3CE6083): B has rank 1 of the territory 4 and no free
    // token after its card: refused; with a new token it gets the house, and the token stays.
    host.datagram(b.peer, b.component_message(kTypeSpendTerritoryStandingPoint, beta->actor.bytes,
                                              card_body(beta->id, kEverfall, id_of("TB_PurchaseHouse"))));
    host.steps(3, {&a, &b});
    const bool refusedHouse = beta->skills.territoryPools.at(kEverfallPool).cards.count(id_of("TB_PurchaseHouse")) == 0;
    skills.give_standing(kCharacterB, 4, 375);
    host.datagram(b.peer, b.component_message(kTypeSpendTerritoryStandingPoint, beta->actor.bytes,
                                              card_body(beta->id, kEverfall, id_of("TB_PurchaseHouse"))));
    host.steps(3, {&a, &b});
    check(refusedHouse && beta->skills.territoryPools.at(kEverfallPool).cards.count(id_of("TB_PurchaseHouse")) == 1
              && beta->skills.free_tokens(kEverfallPool) == 1 && alpha->skills.territoryPools.at(kWindswardPool).cards.size() == 1,
          "the house card of B with no free token: refused; with one token: level 1, and the token stays; A has its one card");
}

/** QG1G: the loaders. */
void loaders(const Options& options) {
    // The progression file of the format 4 (a short one: one standing with 3 ranks, two cards).
    const std::string base = R"("levelCap":2,"levels":[[0,0,0,0],[100,1,0,0]],"attributes":{"minimum":5,"constitutionHealth":[0]},
        "vitals":{"health":800,"stamina":100,"mana":100},"progressions":[],"abilities":[],"expansions":[],"entitlements":[],"achievements":[])";
    const auto write = [&](const std::string& name, const std::string& text) {
        const std::filesystem::path path = options.workDirectory / name;
        std::filesystem::create_directories(options.workDirectory);
        std::ofstream(path) << text;
        return path.string();
    };
    const std::string rank = R"("rankTables":{"Territory_Standing":{"experience":[150,375,0],"xpReward":[0,100,150],"cards":)";
    const std::string four = "{\"format\":4," + base + "," + rank
                             + R"([["TB_GainStanding","","","TB_PurchaseHouse"],["TB_GainStanding","","",""],["","","",""]]}},
        "standings":[["6",2,"Territory_Standing","TerritoryRankBonus_6",true,1.25]],
        "territoryCards":[["TB_GainStanding","GainStanding",1,102,false,0,0.05000000074505806,0.13500000536441803,0.0024999999441206455],
                          ["TB_PurchaseHouse","HousePurchase",10,1,true,1,1.0,0.0,0.0]]})";
    gamedata::ProgressionTables tables;
    const bool loaded = tables.load(write("qg1-four.json", four));
    const gamedata::StandingRow* row = loaded ? tables.standing(6) : nullptr;
    const gamedata::TerritoryCard* card = loaded ? tables.territory_card(kGainStanding) : nullptr;
    check(loaded && row != nullptr && row->poolId == kWindswardPool && row->xpModifier == 1.25F && row->offers.size() == 3
              && row->offers[0][0] == kGainStanding && row->offers[0][3] == id_of("TB_PurchaseHouse") && row->offers[2][0] == 0
              && tables.standing_of_pool(kWindswardPool) == row && card != nullptr && card->initialBonus == bits(0x3D4CCCCDU)
              && card->kind == gamedata::TerritoryBonus::gainStanding && tables.territory_card(gamedata::TerritoryBonus::housePurchase) != nullptr
              && tables.territory_card(gamedata::TerritoryBonus::housePurchase)->doNotSpend,
          "a file of the format 4: the modifier 1.25, the pool CCDDF19B, the card offers of each rank, the cards with the f32 values "
          "of the data and their kind");
    gamedata::ProgressionTables bad;
    const std::string unknownOffer = "{\"format\":4," + base + "," + rank
                                     + R"([["TB_Nothing","","",""],["","","",""],["","","",""]]}},
        "standings":[["6",2,"Territory_Standing","TerritoryRankBonus_6",true,1.25]],"territoryCards":[]})";
    const std::string threeWithCards = "{\"format\":3," + base + "," + rank
                                       + R"([["","","",""],["","","",""],["","","",""]]}},"standings":[["6",2,"Territory_Standing","P",true]]})";
    const std::string three = "{\"format\":3," + base + R"(,"rankTables":{"Territory_Standing":{"experience":[150,375,0],"xpReward":[0,100,150]}},
        "standings":[["6",2,"Territory_Standing","TerritoryRankBonus_6",true]]})";
    gamedata::ProgressionTables old;
    const bool oldLoaded = old.load(write("qg1-three.json", three));
    check(!bad.load(write("qg1-bad.json", unknownOffer)) && !bad.loaded() && !bad.load(write("qg1-three-cards.json", threeWithCards))
              && oldLoaded && old.standing(6) != nullptr && old.standing(6)->xpModifier == 1.0F && old.standing(6)->offers.empty()
              && old.territory_cards().empty(),
          "refused: a rank that offers a card that is not a territory card, a file of the format 3 with cards; a file of the format "
          "3 loads with the modifier 1 and no cards (the card choice is refused)");

    // The bounds of the territories (POI data of the format 2).
    gamedata::PoiData pois;
    const bool boundsLoaded = pois.load_text(R"({"format":2,"pois":[],"territories":[
        {"id":6,"name":"@Windsward","shape":[[8180,2029],[10273,2029],[10273,3725],[8180,3725]]},
        {"id":4,"name":"@Everfall","shape":[[7649,3725],[10603,3725],[10603,5574],[7649,5574]]}]})");
    gamedata::PoiData oldPois;
    gamedata::PoiData badPois;
    check(boundsLoaded && pois.territory_at(9622.0F, 2306.0F) == 6 && pois.territory_at(8880.0F, 4250.0F) == 4 && pois.territory_at(100.0F, 100.0F) == 0
              && oldPois.load_text(R"({"format":1,"pois":[]})") && oldPois.territory_at(9622.0F, 2306.0F) == 0
              && !badPois.load_text(R"({"format":2,"pois":[]})") && !badPois.load_text(R"({"format":3,"pois":[],"territories":[]})"),
          "the bounds: the beach start (9622, 2306) is in Windsward 6, the Oxboro place (8880, 4250) in Everfall 4, a place outside "
          "each bounds is 0; a file of the format 1 has no territory; a format 2 file with no list and a format 3 file are refused");
    // The territory of a character in the world.
    ServerHost host;
    host.progression.set(standing_tables());
    (void)host.pois.load_text(R"({"format":2,"pois":[],"territories":[{"id":6,"name":"@Windsward","shape":[[8180,2029],[10273,2029],[10273,3725],[8180,3725]]}]})");
    const Players players(host.store);
    FakeClient a(27130);
    host.enter(a, players.alpha.registration());
    host.steps(2, {&a});
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    check(alpha != nullptr && host.world.territory_of(*alpha) == 6, "the territory of a character at the beach start is Windsward (World::territory_of)");
}

} // namespace

std::vector<Case> territory_standing_cases() {
    return {{"QG1A", "territory standing (QG1-I1): the rank-up, the clamp at 300, the points above a rank, the experience with the modifier, the GainStanding card, the card values", rank_up},
            {"QG1B", "territory standing (QG1-I2): the points state with each weapon pool and each territory pool, the tokens, the bytes of the research fixtures", points_state},
            {"QG1C", "territory standing (QG1-I3): the card choice 4040, each refusal, the house card, a restart keeps the standing, the tokens, and the cards", card_choice},
            {"QG1D", "territory standing (QG1-I4): GainGatherRate on the gather speed and GainXP on the experience in the territory of the character", applied_cards},
            {"QG1F", "territory standing (QG1-I7): two sessions with different standings, tokens, and cards (MP-10)", two_sessions},
            {"QG1G", "territory standing: the loaders (progression format 4, the bounds of the territories) and the territory of a character", loaders}};
}

} // namespace soulwarden::backend::tests

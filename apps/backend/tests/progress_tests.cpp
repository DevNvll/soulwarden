// Cases for the level, the experience, and the attributes (stage 1.5): the file of the
// progression tables, the wire form of the progression state and of the attribute state, the
// experience and the level gain with the vitals, the requests of a client with a correct and
// with a wrong body, two sessions, the persistence, and the development routes.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "core/encoding/base64.h"
#include "gamedata/progression_tables.h"
#include "middleware/compression/zlib.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/progress_requests.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "state/creation_data.h"
#include "state/world.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/progress.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using server::rep::View;
using world::ProgressError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterNone = "dddddddd-0000-4000-8000-00000000000d";
constexpr float kHeight = 50.0F;
constexpr unsigned kCon = world::attribute::kConstitution;
constexpr unsigned kStr = world::attribute::kStrength;

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(state::Store& store)
        : alpha(add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight})) {}
};

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

std::uint32_t u32_at(const Bytes& value, std::size_t offset = 0) {
    server::hub::Reader reader(std::span<const std::uint8_t>(value).subspan(offset));
    return reader.u32();
}

/** @return The fields of a fragment as text, for example "(0,0) (0,1) ". */
std::string fields_of(const Fragment& fragment) {
    std::string text;
    for (const auto& [key, value] : fragment.fields) {
        text += "(" + std::to_string(key.first) + "," + std::to_string(key.second) + ") ";
    }
    return text;
}

/** @return The row of the progress of a character as text: "level experience con foc str dex int". */
std::string row_text(state::Store& store, const std::string& characterId) {
    const state::ProgressRow row = store.progress(characterId);
    std::string text = std::to_string(row.level) + " " + std::to_string(row.experience);
    for (const std::uint32_t points : row.spent) {
        text += " " + std::to_string(points);
    }
    return text;
}

/** The last progression, attribute, and vitals values that a client got for its own character. */
struct ClientProgress {
    std::uint32_t level{0};
    std::uint32_t experience{0};
    WireAttributes attributes;
    float healthMax{0.0F};
    float health{0.0F};
    std::uint64_t coin{0};
    unsigned progressionFragments{0};
    unsigned attributeFragments{0};
    unsigned vitalsFragments{0};
    bool complete{true};
    /** The records of the last call, as text. */
    std::vector<std::string> records;
};

/** Applies what a client got from the message `from` on. */
void follow(ClientProgress& state, const FakeClient& client, std::size_t& from) {
    state.records.clear();
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        state.complete = state.complete && bundle.complete;
        for (const Record& record : bundle.records) {
            if (record.interest != 1) {
                continue;
            }
            bool counted = false;
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == kProgressionSlot) {
                    state.level = u32_at(fragment.fields.at({0, 0}));
                    state.experience = u32_at(fragment.fields.at({1, 0}));
                    ++state.progressionFragments;
                } else if (fragment.slot == kAttributeSlot) {
                    state.attributes = attributes_of(fragment);
                    ++state.attributeFragments;
                } else if (fragment.slot == kVitalsSlot && fragment.fields.count({0, 4}) != 0) {
                    state.healthMax = float_at(fragment.fields.at({0, 4}), 0);
                    state.health = float_at(fragment.fields.at({0, 0}), 0);
                    ++state.vitalsFragments;
                } else if (fragment.slot == kCurrencySlot) {
                    server::hub::Reader reader(fragment.fields.at({0, 0}));
                    state.coin = reader.u64();
                } else {
                    continue;
                }
                counted = true;
            }
            if (counted) {
                state.records.push_back(order_of(record));
            }
        }
    }
    from = client.got.size();
}

/** A1: the file of the progression tables. */
void tables_file(const Options& options) {
    using nlohmann::json;
    const std::filesystem::path path = options.workDirectory / "progression-tables.json";
    const auto write = [&](const json& document) { std::ofstream(path, std::ios::binary) << document.dump(); };
    json document = {
        {"format", 1},
        {"levelCap", 3},
        {"tradeSkillCap", 200},
        {"levels", json::array({json::array({0, 0, 0, 0}), json::array({100, 5, 50, 0}), json::array({300, 6, 100, 250}),
                                json::array({600, 7, 150, 250})})},
        {"attributes", {{"minimum", 5}, {"maximum", 8}, {"constitutionHealth", json::array({0, 40, 80, 120})}}},
        {"vitals", {{"health", 800}, {"stamina", 100}, {"mana", 100}}},
        {"rankTables",
         {{"TestWeaponRanks", {{"experience", json::array({10, 50, 70})}, {"resetCost", json::array({0, 0, 6})}}},
          {"TestMiningRanks", {{"experience", json::array({100, 105})}}}}},
        {"progressions", json::array({json::array({"SwordAbilityTable", "weapon", 2, "TestWeaponRanks"}),
                                      json::array({"Mining", "tradeskill", 1, "TestMiningRanks"})})},
        {"abilities", json::array({json::array({"SwordAbilityTable", "Test_Sword_Slash", 0, 0, 2, true, ""}),
                                   json::array({"SwordAbilityTable", "Test_Sword_SlashUp", 0, 1, 2, false, "Test_Sword_Slash"}),
                                   json::array({"SwordAbilityTable", "Test_Sword_Bash", 1, 0, 0, true, ""})})},
    };
    gamedata::ProgressionTables tables;
    check(!tables.load((options.workDirectory / "no-such-file.json").string()) && !tables.loaded(),
          "a tables file that is not there: no tables, and the backend continues");
    write(document);
    check(tables.load(path.string()) && tables.loaded() && tables.level_cap() == 3 && tables.data().levels.size() == 4
              && tables.level(1)->experience == 0 && tables.level(3)->experience == 300 && tables.level(3)->resetCost == 250
              && tables.level(0) == nullptr && tables.level(5) == nullptr,
          "the tables file of the tool loads: 4 levels, the largest level of a player is 3, and level K is entry K - 1");
    check(tables.attribute_points(1) == 0 && tables.attribute_points(2) == 5 && tables.attribute_points(3) == 11
              && tables.attribute_minimum() == 5 && tables.attribute_maximum() == 8 && tables.constitution_health(5) == 0
              && tables.constitution_health(7) == 80 && tables.constitution_health(9) == 0 && tables.data().baseHealth == 800,
          "the attribute points of a level are the sum of the levels 2 to it; an attribute is 5 to 8; the constitution "
          "table gives the health of a value");
    const gamedata::ProgressionRow* sword = tables.progression("swordabilitytable");
    const gamedata::ProgressionRow* mining = tables.progression(kMining);
    check(sword != nullptr && sword->wireId == kSword && sword->kind == gamedata::ProgressionKind::weapon && sword->maxRank == 2
              && sword->ranks.experience == std::vector<std::uint32_t>{10, 50, 70} && sword->ranks.resetCost[2] == 6
              && mining != nullptr && mining->id == "Mining" && mining->kind == gamedata::ProgressionKind::tradeSkill
              && tables.progression("NoSuchSkill") == nullptr,
          "a weapon mastery and a trade skill have the id on the wire (the CRC-32 of the lower-case id: E8EC5716 and "
          "25DD6A56), the kind, the largest rank, and the experience of each rank; the lookup ignores the letter case");
    const gamedata::AbilityNode* up = tables.node("Test_Sword_SlashUp");
    check(tables.nodes().size() == 3 && up != nullptr && up->table == kSword && up->tree == 0 && up->row == 1 && up->column == 2
              && !up->active && up->required == node_id("Test_Sword_Slash") && tables.node(node_id("Test_Sword_Bash"))->active
              && tables.last_row(kSword, 0) == 1 && tables.last_row(kSword, 1) == 0,
          "a node of a weapon tree has its weapon, its tree, its row, its column, the active mark, and the node that it "
          "needs; the tables give the last row of each tree");
    check(gamedata::progression_id("SwordAbilityTable") == 0xE8EC5716 && gamedata::progression_id("Mining") == 0x25DD6A56
              && gamedata::progression_id("Ability_Sword_WhirlingBlade") == 0x0ED6B966 && gamedata::kAbilityTableOrder.size() == 29
              && gamedata::kAbilityTableOrder[kSwordIndex] == kSword && gamedata::kAbilityTableOrder[kBowIndex] == kBow,
          "the id rule gives the ids of the research (sword E8EC5716, mining 25DD6A56, Whirling Blade 0ED6B966), and the "
          "ability table list of the client has 29 tables with the sword at the index 27");

    // A file with a different form is not the tables: the backend then has none.
    const auto refused = [&](json changed) {
        write(changed);
        gamedata::ProgressionTables other;
        return !other.load(path.string()) && !other.loaded();
    };
    json noLevels = document;
    noLevels.erase("levels");
    json shortRow = document;
    shortRow["levels"][1] = json::array({100, 5, 50});
    json highCap = document;
    highCap["levelCap"] = 5;
    json unknownTable = document;
    unknownTable["progressions"][0][3] = "NoSuchRanks";
    json unknownKind = document;
    unknownKind["progressions"][1][1] = "territory";
    json negative = document;
    negative["levels"][2][0] = -1;
    std::ofstream(path, std::ios::binary) << "not json";
    gamedata::ProgressionTables text;
    check(!text.load(path.string()) && refused(noLevels) && refused(shortRow) && refused(highCap) && refused(unknownTable)
              && refused(unknownKind) && refused(negative),
          "a file that is not JSON, that has no level list, a level row with 3 numbers, a largest level above the list, a "
          "rank table that is not there, a kind that is not known, or a negative number is refused");
}

/** A2: the wire bytes of the progression state and of the attribute state. */
void wire_bytes(const Options&) {
    using namespace server::rep;
    ManualClock clock;
    world::Entity entity;
    entity.id = 0x6060606060606060ULL;
    const auto fragment_size = [](std::uint32_t slot, std::uint32_t type, const Bytes& state) {
        RecordWriter record(1);
        record.fragment(slot, type, state);
        return record.finish().size() - 2; // The record has the interest number and the fragment count before it.
    };

    // progression.md 2.1: a level 1 character with no experience and no free point.
    const Bytes level1 = encode_progression_state(entity, View::owner, clock);
    const std::string attribute1 = "01" "0f" "05" "0000000000000005" "0000000100000005" "0000000200000005"
                                   "0000000300000005" "0000000400000005" "000000" "000000" "00000000" "00" "00" "00" "00000005";
    const Bytes attributeLevel1 = encode_attribute_state(entity, View::owner, clock);
    check(hex_text(level1) == "03" "03" "00000000" "00000000" "03" "00000000" "00000000"
              && fragment_size(kProgressionSlot, kTypeProgressionState, level1) == 22 && hex_text(attributeLevel1) == attribute1
              && fragment_size(kAttributeSlot, kTypeAttributeState, attributeLevel1) == 63,
          "level 1, owner: the progression fragment (22 bytes with the slot 70 and the type 899) and the attribute "
          "fragment (63 bytes with the slot 27 and the type 129) are the bytes of the example of the research: all five "
          "attributes are 5, the two bonus containers are empty in the full form, no free point, no spent point, and "
          "the placing behavior 5");
    const WireAttributes read1 = read_attributes({std::span(attributeLevel1).subspan(2, 41), std::span(attributeLevel1).subspan(43, 3),
                                                  std::span(attributeLevel1).subspan(46, 3), std::span(attributeLevel1).subspan(49)});
    check(read1.ok && read1.values.size() == 5 && read1.values.at(4) == 5 && read1.unspent == 0 && read1.spent.empty()
              && read1.behavior == 5 && !read1.hasRespec && !read1.ignoreBackstory && read1.bonuses == 0 && read1.placing == 0,
          "a decoder that follows the readers of the client reads the attribute state to its last byte");

    // Level 20 (index 19) with 84,100 experience and 102 free points.
    entity.progression.level = 20;
    entity.progression.experience = 84100;
    entity.attributes.unspent = 102;
    const std::string attribute20 = "01" "0f" "05" "0000000000000005" "0000000100000005" "0000000200000005"
                                    "0000000300000005" "0000000400000005" "000000" "000000" "00000066" "00" "00" "00" "00000005";
    check(hex_text(encode_progression_state(entity, View::owner, clock)) == "03" "03" "00000013" "00000000" "03" "00014884" "00000000"
              && hex_text(encode_attribute_state(entity, View::owner, clock)) == attribute20,
          "level 20 with 102 free points: the bytes of the two examples of the research (the level 19 on the wire, the "
          "experience 0x14884, the free points 0x66)");

    // The example after a spend of 50 points on the constitution and 52 on the strength.
    entity.attributes.spent = {50, 0, 52, 0, 0};
    entity.attributes.unspent = 0;
    const Bytes spent = encode_attribute_state(entity, View::owner, clock);
    check(hex_text(spent) == "01" "0f" "05" "0000000000000037" "0000000100000005" "0000000200000039" "0000000300000005"
                                 "0000000400000005" "000000" "000000" "00000000" "00" "00" "02" "0000000000000032"
                                 "0000000200000034" "00000005"
              && fragment_size(kAttributeSlot, kTypeAttributeState, spent) == 79,
          "after a spend: the values 55 and 57 (the minimum 5 is in the value), no free point, and one entry of the "
          "spent points for each attribute with points (79 bytes with the slot and the type, the example of the research)");

    // The two views.
    const Bytes observed = encode_progression_state(entity, View::observer, clock);
    check(hex_text(observed) == "01" "03" "00000013" "00000000" && encode_attribute_state(entity, View::observer, clock).empty(),
          "observer view: the progression state has only group 0 (the level and the bonus level, for the name tag of a "
          "remote player) and no experience; the attribute state has no fragment (the remote player slice has no facet)");
}

/** A3: the experience, the level gain, the attribute points, and the health. */
void level_and_experience(const Options&) {
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Progress& progress = host.world.progress();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    ClientProgress client;
    std::size_t from = 0;
    follow(client, a, from);
    check(client.complete && client.level == 0 && client.experience == 0 && client.attributes.ok && client.attributes.unspent == 0
              && client.attributes.values.at(kCon) == 5 && client.healthMax == 800.0F && client.health == 800.0F
              && alpha->vitals.healthMax == 800.0F,
          "a character with no row: level 1 (0 on the wire), no experience, each attribute 5, no free point, and the "
          "base health of the tables (800)");

    world::ProgressResult result = progress.add_experience(kCharacterA, 99);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.inWorld && result.level == 1 && result.experience == 99 && result.levelsGained == 0
              && client.level == 0 && client.experience == 99 && client.records == std::vector<std::string>{"(70,899) "}
              && row_text(host.store, kCharacterA) == "1 99 0 0 0 0 0",
          "99 experience: the level stays 1 (the next level starts at 100), the client gets one progression fragment "
          "with the experience, and the row has it");
    result = progress.add_experience(kCharacterA, 1);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.level == 2 && result.experience == 100 && result.levelsGained == 1 && result.unspent == 5
              && client.level == 1 && client.experience == 100 && client.attributes.unspent == 5 && client.healthMax == 850.0F
              && client.health == 850.0F && client.records == std::vector<std::string>{"(41,15) (70,899) (27,129) "},
          "1 more experience reaches the limit: level 2. The client gets one record with the level, the 5 attribute "
          "points of the level, and the new maximum health (850; full health stays full health)");
    result = progress.add_experience(kCharacterA, 1000);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.level == 5 && result.experience == 1100 && result.levelsGained == 3 && result.unspent == 22
              && client.level == 4 && client.attributes.unspent == 22 && client.healthMax == 1000.0F,
          "1,000 experience at once: three levels (1,100 is in the range of level 5), and the points of each level (22)");
    result = progress.add_experience(kCharacterA, 100000);
    check(result.ok() && result.level == 6 && result.experience == 1500 && result.unspent == 29,
          "experience above the last limit: the largest level of a player (6, not the 7 that the table has), and the "
          "experience stays at the start of that level");
    result = progress.add_experience(kCharacterA, 50);
    check(result.ok() && result.level == 6 && result.experience == 1500 && result.levelsGained == 0,
          "at the largest level, more experience changes nothing");

    result = progress.set_level(kCharacterA, 3);
    check(result.ok() && result.level == 3 && result.experience == 300 && result.unspent == 10 && alpha->vitals.healthMax == 900.0F,
          "set level 3: the experience is the start of the level (300), the character has 10 points, and its health is 900");
    result = progress.set_attribute(kCharacterA, kCon, 7);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.unspent == 8 && alpha->attributes.value(kCon) == 7 && alpha->vitals.healthMax == 980.0F
              && client.attributes.values.at(kCon) == 7 && client.attributes.spent.at(kCon) == 2 && client.attributes.unspent == 8
              && client.healthMax == 980.0F && row_text(host.store, kCharacterA) == "3 300 2 0 0 0 0",
          "set the constitution to 7: 2 points are spent, 8 are free, and the health is the base 800, the level 100, and "
          "the constitution 80");
    result = progress.set_level(kCharacterA, 1);
    host.steps(2, {&a});
    follow(client, a, from);
    check(result.ok() && result.unspent == 0 && alpha->attributes.value(kCon) == 5 && alpha->vitals.healthMax == 800.0F
              && client.attributes.values.at(kCon) == 5 && client.attributes.spent.empty() && row_text(host.store, kCharacterA) == "1 0 0 0 0 0 0",
          "a lower level with fewer points than the character spent: all attribute points are free again (each attribute 5)");

    const std::string before = row_text(host.store, kCharacterA);
    const world::Revision revision = host.world.revision();
    check(progress.set_level(kCharacterA, 0).error == ProgressError::badLevel
              && progress.set_level(kCharacterA, 7).error == ProgressError::badLevel
              && progress.add_experience(kCharacterA, 0).error == ProgressError::badAmount
              && progress.add_experience(kCharacterA, world::kExperienceLimit + 1).error == ProgressError::badAmount
              && progress.set_attribute(kCharacterA, 5, 6).error == ProgressError::badAttribute
              && progress.set_attribute(kCharacterA, kCon, 4).error == ProgressError::badAttribute
              && progress.set_attribute(kCharacterA, kCon, 31).error == ProgressError::badAttribute
              && progress.set_attribute(kCharacterA, kCon, 6).error == ProgressError::notEnoughPoints
              && progress.set_level(kCharacterNone, 2).error == ProgressError::unknownCharacter
              && row_text(host.store, kCharacterA) == before && host.world.revision() == revision,
          "refused, with no change of a row or of a state: the level 0, a level above the largest one, an amount of 0 "
          "or above the limit, an attribute number that is not one, a value below 5 or above the table, a value that "
          "needs more points than the character has, and a character that is not known");

    // A backend with no tables.
    ServerHost bare;
    const TwoPlayers others(bare.store);
    state::ProgressRow stored;
    stored.level = 12;
    stored.experience = 4321;
    stored.spent = {3, 0, 0, 0, 0};
    const bool written = bare.store.set_progress(kCharacterA, stored);
    FakeClient c(27000);
    bare.enter(c, others.alpha.registration());
    ClientProgress plain;
    std::size_t start = 0;
    follow(plain, c, start);
    check(written && plain.level == 11 && plain.experience == 4321 && plain.attributes.values.at(kCon) == 8
              && plain.attributes.unspent == 0 && plain.healthMax == 1000.0F
              && bare.world.progress().set_level(kCharacterA, 2).error == ProgressError::noTables
              && bare.world.progress().add_experience(kCharacterA, 10).error == ProgressError::noTables
              && row_text(bare.store, kCharacterA) == "12 4321 3 0 0 0 0",
          "with no progression tables the backend sends the stored level, experience, and attribute points (no free "
          "point, the default health), and it refuses a level and experience");
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

/** A8: the level of a character in its menu document. */
void menu_level(const Options&) {
    using nlohmann::json;
    state::AppearanceNames appearance;
    appearance.fill("none");
    appearance[state::kAppearanceGender] = "Male";
    const std::string made = state::make_creation_data("Leveltest", appearance, state::local_world().worldId, "Archetype_Test");
    const std::string level20 = state::with_current_level(made, 20);
    const auto read = state::read_creation_data(level20);
    check(document_of(made).find("\"currentLevel\":0,") != std::string::npos
              && document_of(level20).find("\"currentLevel\":19,") != std::string::npos
              && document_of(level20).size() == document_of(made).size() + 1 && read && read->archetype == "Archetype_Test"
              && document_of(state::with_current_level(level20, 1)) == document_of(made),
          "the menu document of a character gets the level of the screen minus 1 in its member currentLevel (19 for "
          "level 20), and no other byte changes: the creation data reads as before");
    check(state::with_current_level("not creation data", 5) == "not creation data" && state::with_current_level("", 5).empty(),
          "a text that is not creation data stays as it is");

    // The character list of the gateway has the level of the row of each character.
    ServerHost host;
    const TwoPlayers players(host.store);
    state::Character character;
    character.characterId = kCharacterNone;
    character.personaId = players.alpha.account.personaId;
    character.worldId = state::local_world().worldId;
    character.name = "Leveltest";
    character.createdDate = "2026-01-01T00:00:00.000Z";
    character.modifiedDate = character.createdDate;
    character.creationParams = made;
    state::ProgressRow row;
    row.level = 4;
    const bool stored = host.store.insert_character(character) == state::InsertResult::inserted
                        && host.store.set_progress(kCharacterNone, row);
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_gateway_routes(router, services);
    service::HttpRequest request;
    request.method = "GET";
    request.host = "gateway.test";
    request.path = "/prod/game/getlogininfo";
    request.target = request.path;
    request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", players.alpha.account.personaId}}.dump()));
    std::string route;
    const service::HttpResponse reply = router.dispatch(request, route);
    const json body = json::parse(reply.body, nullptr, false);
    std::string document;
    std::size_t characters = 0;
    if (reply.status == 200 && body.is_object() && body.contains("LoginInfoList")) {
        for (const json& entry : body["LoginInfoList"]["Characters"]) {
            ++characters;
            if (entry["CharacterId"] == kCharacterNone && entry.contains("PublishedData")) {
                document = document_of(entry["PublishedData"].get<std::string>());
            }
        }
    }
    check(stored && characters == 2 && document.find("\"currentLevel\":3,") != std::string::npos,
          "the character list of the gateway gives the menu document of a character with the level of its row (3 for "
          "level 4)");
}

/** A4: the requests of a client for its attributes, with a correct and with a wrong body. */
void client_requests(const Options&) {
    using namespace server::rep;
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    world::Progress& progress = host.world.progress();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    progress.set_level(kCharacterA, 4);
    progress.set_level(kCharacterB, 4);
    host.steps(2, {&a, &b});
    ClientProgress client;
    std::size_t from = 0;
    follow(client, a, from);
    const auto send = [&](FakeClient& sender, const world::Entity* route, std::uint32_t type, const Bytes& body) {
        host.datagram(sender.peer, sender.component_message(type, route->actor.bytes, body));
        host.steps(2, {&a, &b});
    };

    const Bytes live = spend_body(alpha->id, {{kCon, 2}, {kStr, 3}});
    const auto liveRequest = read_spend_request(live);
    check(live.size() == 34 && hex_text(live).substr(32) == "02" "00000000" "00000002" "02" "00000002" "00000003" && liveRequest
              && liveRequest->entries.size() == 2 && liveRequest->entries[1].type == kStr && liveRequest->entries[1].points == 3
              && plain_body(alpha->id, kAttributeSlot).size() == 16,
          "the body of the live client for 2 points of constitution and 3 points of strength: 34 bytes, the list of the "
          "types and then the list of the points; a reset request is the 16 bytes of the target and the entity");

    // Spend (type 1502).
    send(a, alpha, kTypeSpendAttributePoints, live);
    follow(client, a, from);
    check(row_text(host.store, kCharacterA) == "4 600 2 0 3 0 0" && client.attributes.values.at(kCon) == 7
              && client.attributes.values.at(kStr) == 8 && client.attributes.unspent == 11
              && client.attributes.spent == std::map<std::uint32_t, std::uint32_t>{{kCon, 2}, {kStr, 3}} && client.healthMax == 1030.0F
              && client.records == std::vector<std::string>{"(41,15) (27,129) "}
              && log_count("rep: attribute spend request (type 1502, constitution 2, strength 3): done, 11 free point(s) [s1 aaaaaaaa]") == 1,
          "spend request with a correct body: the row and the client have constitution 7 and strength 8, 11 points are "
          "free, the health has the 80 of the constitution, and the log has the line");

    // More points than the character has.
    unsigned fragments = client.attributeFragments;
    send(a, alpha, kTypeSpendAttributePoints, spend_body(alpha->id, {{kCon, 6}, {world::attribute::kFocus, 6}}));
    follow(client, a, from);
    check(row_text(host.store, kCharacterA) == "4 600 2 0 3 0 0" && alpha->attributes.unspent == 11
              && client.attributeFragments == fragments + 1 && client.attributes.unspent == 11 && client.attributes.values.at(kCon) == 7
              && log_count("the request spends more points than the character has, 11 free point(s)") == 1,
          "a request for 12 points with 11 free points: nothing changes, and the client gets the attribute state of the "
          "server again (it changed its own values when it sent the request)");

    // Bodies that do not have the form of the request.
    fragments = client.attributeFragments;
    Bytes unequal = spend_body(alpha->id, {{kCon, 1}});
    unequal.insert(unequal.end(), {0, 0, 0, 1}); // One more number of points than types.
    unequal[16 + 1 + 4] = 2;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> six(6, {kCon, 1});
    Bytes longer = spend_body(alpha->id, {{kCon, 1}});
    longer.push_back(0);
    Bytes shorter = spend_body(alpha->id, {{kCon, 1}});
    shorter.pop_back();
    const std::size_t refusedBefore = log_count("rep: attribute spend request (type 1502");
    for (const Bytes& body : {spend_body(alpha->id, {{5, 1}}), spend_body(alpha->id, {{kCon, 701}}), spend_body(alpha->id, six), unequal,
                              longer, shorter}) {
        send(a, alpha, kTypeSpendAttributePoints, body);
    }
    follow(client, a, from);
    check(row_text(host.store, kCharacterA) == "4 600 2 0 3 0 0" && client.attributeFragments == fragments
              && log_count("rep: attribute spend request (type 1502") == refusedBefore + 6
              && log_count("does not have the form of the request: no change") >= 6,
          "a body with the attribute type 5, with 701 points, with 6 entries, with lists of different lengths, with one "
          "byte more, or with one byte less is not the request: no change and no state");
    send(a, alpha, kTypeSpendAttributePoints, spend_body(alpha->id, {{kCon, 1}, {kCon, 1}}));
    follow(client, a, from);
    check(row_text(host.store, kCharacterA) == "4 600 2 0 3 0 0" && client.attributeFragments == fragments + 1
              && log_count("rep: attribute spend request (type 1502, constitution 1, constitution 1): the request is not correct") == 1,
          "a request with one attribute two times is refused, and the client gets the state of the server again");

    // A request that names the entity of a different character.
    send(a, alpha, kTypeSpendAttributePoints, spend_body(beta->id, {{kCon, 1}}));
    send(a, alpha, kTypeRefundAttributePoints, plain_body(beta->id, kAttributeSlot));
    check(row_text(host.store, kCharacterB) == "4 600 0 0 0 0 0" && beta->attributes.unspent == 16
              && row_text(host.store, kCharacterA) == "4 600 2 0 3 0 0"
              && log_count("which is not the entity of the session: no change") == 2,
          "a spend request and a reset request of A that name the entity of B change nothing for A and for B");

    // The largest value of an attribute: the points above it stay free.
    progress.set_level(kCharacterA, 6);
    send(a, alpha, kTypeSpendAttributePoints, spend_body(alpha->id, {{kCon, 22}}));
    send(a, alpha, kTypeSpendAttributePoints, spend_body(alpha->id, {{kCon, 2}}));
    follow(client, a, from);
    check(alpha->attributes.value(kCon) == 30 && alpha->attributes.spent[kCon] == 25 && alpha->attributes.unspent == 1
              && client.attributes.values.at(kCon) == 30 && client.attributes.unspent == 1,
          "a spend that goes above the largest value of the tables (30): the attribute stops at 30, and the points above "
          "it stay free");

    // Reset (type 2409). The levels 6 and 7 of the test tables have the cost 5.00 coin.
    host.world.inventory().set_coin(kCharacterA, 499);
    send(a, alpha, kTypeRefundAttributePoints, plain_body(alpha->id, kAttributeSlot));
    follow(client, a, from);
    check(alpha->attributes.spent[kCon] == 25 && alpha->wallet.coin == 499 && row_text(host.store, kCharacterA) == "6 1500 25 0 3 0 0"
              && host.store.currency(kCharacterA, state::kCurrencyCoin) == 499
              && log_count("rep: attribute reset request (type 2409): the character does not have the coin for the cost") == 1,
          "reset request with 4.99 coin and a cost of 5.00: refused, the points and the coin stay");
    host.world.inventory().set_coin(kCharacterA, 700);
    send(a, alpha, kTypeRefundAttributePoints, plain_body(alpha->id, kAttributeSlot));
    follow(client, a, from);
    check(alpha->attributes.unspent == 29 && alpha->wallet.coin == 200 && row_text(host.store, kCharacterA) == "6 1500 0 0 0 0 0"
              && host.store.currency(kCharacterA, state::kCurrencyCoin) == 200 && client.attributes.unspent == 29
              && client.attributes.spent.empty() && client.attributes.values.at(kStr) == 5 && client.coin == 200
              && client.healthMax == 1050.0F && log_count("rep: attribute reset request (type 2409): done, 29 free point(s) [s1 aaaaaaaa]") == 1,
          "reset request with 7.00 coin: all 29 points are free, each attribute is 5, the cost is gone from the coin (row "
          "and client: 2.00), and the health has no constitution part");
    send(a, alpha, kTypeRefundAttributePoints, plain_body(alpha->id, kAttributeSlot));
    Bytes longReset = plain_body(alpha->id, kAttributeSlot);
    longReset.push_back(1);
    send(a, alpha, kTypeRefundAttributePoints, longReset);
    check(alpha->wallet.coin == 200 && host.store.currency(kCharacterA, state::kCurrencyCoin) == 200
              && log_count("rep: attribute reset request (type 2409, 17 bytes) does not have the form of the request: no change") == 1,
          "a reset with no spent point takes no cost, and a reset body with one byte more is not the request");
}

/** A5: two sessions. */
void two_sessions(const Options&) {
    using namespace server::rep;
    ServerHost host(observing());
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, kHeight));
    host.steps(60, {&a, &b});
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "each of the two sessions observes the entity of the other character");
    const SlotUpdates firstLevel = slot_updates(b, kProgressionSlot, 0, 2);
    check(firstLevel.fragments.size() == 1 && fields_of(firstLevel.fragments[0]) == "(0,0) (0,1) "
              && firstLevel.records[0] == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) "
              && slot_updates(b, kAttributeSlot, 0, 2).fragments.empty(),
          "the first record of A for session B (observer view) has the level of A with no experience, and no attribute "
          "state");

    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    const std::string rowB = row_text(host.store, kCharacterB);
    host.world.progress().set_level(kCharacterA, 4);
    host.datagram(a.peer, a.component_message(kTypeSpendAttributePoints, alpha->actor.bytes, spend_body(alpha->id, {{kCon, 3}})));
    host.steps(4, {&a, &b});
    const SlotUpdates ownLevel = slot_updates(a, kProgressionSlot, fromA, 1);
    const SlotUpdates ownAttributes = slot_updates(a, kAttributeSlot, fromA, 1);
    check(alpha->progression.level == 4 && alpha->attributes.value(kCon) == 8 && !ownLevel.fragments.empty()
              && fields_of(ownLevel.fragments.back()) == "(0,0) (0,1) (1,0) (1,1) " && u32_at(ownLevel.fragments.back().fields.at({1, 0})) == 600
              && !ownAttributes.fragments.empty() && attributes_of(ownAttributes.fragments.back()).values.at(kCon) == 8,
          "A gets level 4 and spends 3 points: its own session gets the level with the experience (owner view) and the "
          "attribute state");
    const SlotUpdates seenLevel = slot_updates(b, kProgressionSlot, fromB, 2);
    const SlotUpdates seenVitals = slot_updates(b, kVitalsSlot, fromB, 2);
    check(seenLevel.fragments.size() == 1 && fields_of(seenLevel.fragments[0]) == "(0,0) (0,1) " && u32_at(seenLevel.fragments[0].fields.at({0, 0})) == 3
              && !seenVitals.fragments.empty() && float_at(seenVitals.fragments.back().fields.at({0, 4}), 0) == 1070.0F
              && slot_updates(b, kAttributeSlot, fromB, 2).fragments.empty() && seenLevel.complete,
          "session B gets only what an observer gets of A: the level 4 (3 on the wire) with no experience, and the new "
          "maximum health; it gets no attribute state");
    check(beta->progression.level == 1 && beta->attributes.unspent == 0 && beta->vitals.healthMax == 800.0F
              && row_text(host.store, kCharacterB) == rowB && slot_updates(b, kProgressionSlot, fromB, 1).fragments.empty()
              && slot_updates(b, kAttributeSlot, fromB, 1).fragments.empty(),
          "the change of A changes nothing for B: its entity, its row, and its own states stay");
}

struct Sink : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

/** A6: the persistence. */
void persistence(const Options& options) {
    {
        ServerHost host;
        set_progression(host);
        const TwoPlayers players(host.store);
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        host.world.progress().set_level(kCharacterA, 3);
        host.world.progress().add_experience(kCharacterA, 50);
        host.world.progress().set_attribute(kCharacterA, kStr, 9);
        // A command for a character that is not in the world changes its row.
        const world::ProgressResult stored = host.world.progress().set_level(kCharacterB, 2);
        host.steps(2, {&a});
        host.datagram(a.peer, a.carrier_close());
        host.steps(2, {&a});
        check(host.world.entities().empty() && row_text(host.store, kCharacterA) == "3 350 0 0 4 0 0" && stored.ok() && !stored.inWorld
                  && row_text(host.store, kCharacterB) == "2 100 0 0 0 0 0",
              "the character leaves the world: its row has the level, the experience, and the spent points; a command for "
              "a character that is not in the world changed only its row");

        FakeClient again(27002);
        host.enter(again, players.alpha.registration());
        ClientProgress client;
        std::size_t from = 0;
        follow(client, again, from);
        check(client.level == 2 && client.experience == 350 && client.attributes.values.at(kStr) == 9 && client.attributes.unspent == 6
                  && client.healthMax == 900.0F && client.progressionFragments == 1
                  && log_count("has level 3, experience 350, 4 spent and 6 free attribute point(s), maximum health 900") == 1,
              "a new session of the character gets the stored values in its first record, and the log has them");

        ManualClock clock;
        world::World second(clock, host.store, host.tables, ServerHost::with_game_data(host.catalog, host.progression));
        Sink sink;
        const world::EnterResult entered = second.enter(1, sink, kCharacterB, players.beta.account.personaId);
        check(entered.error == world::EnterError::none && entered.entity->progression.level == 2 && entered.entity->progression.experience == 100
                  && entered.entity->attributes.unspent == 5 && entered.entity->vitals.healthMax == 850.0F,
              "a new world object on the same database has the level that the command gave to the character that was "
              "not in the world");
    }

    // A write that fails changes nothing.
    const std::filesystem::path path = options.workDirectory / "progress-rollback.sqlite3";
    std::filesystem::remove(path);
    state::Store store;
    const bool opened = store.open(path.string());
    const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    ManualClock clock;
    gamedata::AppearanceTables tables;
    gamedata::ProgressionTables progression;
    progression.set(test_progression());
    world::WorldOptions worldOptions;
    worldOptions.progressionTables = &progression;
    world::World world(clock, store, tables, worldOptions);
    Sink sink;
    const bool entered = world.enter(1, sink, kCharacterA, player.account.personaId).error == world::EnterError::none;
    const bool leveled = world.progress().set_level(kCharacterA, 6).ok() && world.progress().set_attribute(kCharacterA, kCon, 8).ok()
                         && world.inventory().set_coin(kCharacterA, 900).ok();
    sqlite3* connection = nullptr;
    sqlite3_open(path.string().c_str(), &connection);
    const bool trigger = sqlite3_exec(connection,
                                      "CREATE TRIGGER refuse_coin BEFORE UPDATE ON currencies WHEN NEW.amount=400 "
                                      "BEGIN SELECT RAISE(FAIL, 'test write failure'); END;"
                                      "CREATE TRIGGER refuse_level BEFORE UPDATE ON character_progress WHEN NEW.level=2 "
                                      "BEGIN SELECT RAISE(FAIL, 'test write failure'); END",
                                      nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    check(opened && entered && leveled && trigger, "a database file with a character of level 6 that has 3 points of constitution and 9.00 coin");
    const world::Entity* entity = world.find_character(kCharacterA);
    const world::Revision before = world.revision();
    const world::ProgressResult level = world.progress().set_level(kCharacterA, 2);
    check(level.error == ProgressError::storeFailed && row_text(store, kCharacterA) == "6 1500 3 0 0 0 0" && entity->progression.level == 6
              && entity->vitals.healthMax == 1170.0F && world.revision() == before,
          "a level whose write fails: the row stays, the entity does not change, and no state has a mark");
    // The reset at level 6 has the cost 5.00: the row of the progress and the coin are one transaction.
    const world::ProgressResult reset = world.progress().reset_attributes(kCharacterA);
    check(reset.error == ProgressError::storeFailed && row_text(store, kCharacterA) == "6 1500 3 0 0 0 0"
              && store.currency(kCharacterA, state::kCurrencyCoin) == 900 && entity->attributes.spent[kCon] == 3 && entity->wallet.coin == 900,
          "a reset whose coin write fails: the row of the progress goes back with it (one transaction), and the entity "
          "keeps its points and its coin");
    check(world.progress().set_level(kCharacterA, 3).ok() && row_text(store, kCharacterA) == "3 300 3 0 0 0 0",
          "a later change that the database accepts works");
}

/** A7: the development routes. */
void progress_routes(const Options&) {
    using nlohmann::json;
    ServerHost host;
    set_progression(host);
    const TwoPlayers players(host.store);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_dev_routes(router, services);
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& serviceHost = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        request.host = serviceHost;
        std::string route;
        return router.dispatch(request, route);
    };
    const std::string forA = std::string("character=") + kCharacterA;
    const std::string forB = std::string("character=") + kCharacterB;

    service::HttpResponse reply = send("POST", "/v1/dev/progress/level", forA + "&level=4");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "world" && json::parse(reply.body)["level"] == 4
              && json::parse(reply.body)["experience"] == 600 && json::parse(reply.body)["freeAttributePoints"] == 16
              && json::parse(reply.body)["levelsGained"] == 3,
          "level route for a character that is in the world: reply 200 with the level, the experience, and the free points");
    reply = send("POST", "/v1/dev/progress/experience", forA + "&amount=450");
    check(reply.status == 200 && json::parse(reply.body)["level"] == 5 && json::parse(reply.body)["experience"] == 1050
              && json::parse(reply.body)["levelsGained"] == 1,
          "experience route: 450 experience takes the character over the limit to level 5");
    reply = send("POST", "/v1/dev/progress/attribute", forA + "&attribute=strength&value=12");
    check(reply.status == 200 && json::parse(reply.body)["freeAttributePoints"] == 15
              && send("POST", "/v1/dev/progress/attribute", forA + "&attribute=0&value=6").status == 200
              && row_text(host.store, kCharacterA) == "5 1050 1 0 7 0 0",
          "attribute route with a name and with a number: the strength is 12 and the constitution is 6");
    reply = send("POST", "/v1/dev/progress/level", forB + "&level=2");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "stored" && row_text(host.store, kCharacterB) == "2 100 0 0 0 0 0",
          "level route for a character that is not in the world: applied to the row");
    reply = send("GET", "/v1/dev/progress", forA);
    const json list = json::parse(reply.body);
    check(reply.status == 200 && list["inWorld"] == true && list["tables"] == true && list["level"] == 5 && list["experience"] == 1050
              && list["levelExperience"] == 1000 && list["nextLevelExperience"] == 1500 && list["freeAttributePoints"] == 14
              && list["attributes"]["strength"]["value"] == 12 && list["attributes"]["strength"]["spent"] == 7
              && list["attributes"]["constitution"]["value"] == 6 && list["healthMax"] == 1040.0 && list["weapons"].size() == 2
              && list["tradeSkills"].size() == 2,
          "the list route gives the level, the experience with the limits of the level, each attribute, the free points, "
          "the health, and the lists of the weapons and of the trade skills");
    const std::string before = row_text(host.store, kCharacterA);
    check(send("POST", "/v1/dev/progress/level", forA).status == 400 && send("POST", "/v1/dev/progress/level", forA + "&level=0").status == 400
              && send("POST", "/v1/dev/progress/level", forA + "&level=7").status == 400
              && send("POST", "/v1/dev/progress/level", forA + "&level=abc").status == 400
              && send("POST", "/v1/dev/progress/experience", forA + "&amount=0").status == 400
              && send("POST", "/v1/dev/progress/experience", forA).status == 400
              && send("POST", "/v1/dev/progress/attribute", forA + "&attribute=luck&value=6").status == 400
              && send("POST", "/v1/dev/progress/attribute", forA + "&attribute=focus").status == 400
              && send("POST", "/v1/dev/progress/attribute", forA + "&attribute=focus&value=31").status == 400
              && send("POST", "/v1/dev/progress/attribute", forA + "&attribute=focus&value=25").status == 400,
          "the routes refuse a request with no number, the level 0, a level above the largest one, a number that is not "
          "a whole number, an amount of 0, an attribute that is not known, a value above the table, and a value that "
          "needs more points than the character has (reply 400)");
    check(send("POST", "/v1/dev/progress/level", std::string("character=") + kCharacterNone + "&level=2").status == 404
              && send("GET", "/v1/dev/progress", std::string("character=") + kCharacterNone).status == 404
              && send("POST", "/v1/dev/progress/level", forA + "&level=2", "gateway.test").status == 403
              && send("GET", "/v1/dev/progress", forA, "gateway.test").status == 403
              && send("POST", "/v1/dev/progress/level", "level=2").status == 400 && row_text(host.store, kCharacterA) == before,
          "an unknown character gives the reply 404, a request with a service host the reply 403, a request with no "
          "character the reply 400, and no request that was refused changed a row");
}

} // namespace

std::vector<Case> progress_cases() {
    return {
        {"A1", "progression tables file: the levels, the attributes, the weapons, the trade skills, the tree nodes", tables_file},
        {"A2", "wire bytes: the progression state and the attribute state against the examples of the research, the two views", wire_bytes},
        {"A3", "level and experience: the level gain at the limit, the attribute points, the health, with no tables", level_and_experience},
        {"A4", "client requests: spend (type 1502) and reset (type 2409) with a correct and with a wrong body", client_requests},
        {"A5", "two sessions: the level and the attributes of A do not change B, and B gets only the level of A", two_sessions},
        {"A6", "persistence: the level and the attributes after a new entry, in a new world object, and a write that fails", persistence},
        {"A7", "development routes for the level, the experience, and the attributes", progress_routes},
        {"A8", "the level of a character in its menu document (the character list of the main menu)", menu_level},
    };
}

} // namespace soulwarden::backend::tests

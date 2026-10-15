// Cases of stage 1.1b: the DLC for each player (world/ownership.h). The tables have the form of the game data (the rows None and
// Expansion2023 of the expansion table, the entitlement Ent_Xpack_RiseOfTheAngryEarth_Owner with its bit 1484 and its achievement
// Expansion_2023_Unlock with its bit 11979, a DLC pack with an item reward) on the made-up levels and skills of the tests. All
// accounts, characters, names, and values are made up.
//
//   DL1   the progression tables of format 2: the owned expansion, the level and rank limits, the gear score limit, the refusals
//   DL2   two accounts and two characters of one account: the grants, the two owner states, no fragment for an observer, an
//         achievement of a different source stays (MP-10)
//   DL3   the entitlement routes: the list and the sync of the account of the token; a token of a different account changes nothing
//   DL4   the limits: the level above the base limit with experience, the level above the cap refused, the trade skill limits, and
//         the progression of a character that the grant does not change
//   DL5   the persistence: a new world entry and a new start of the store give the same grant, no second row, no second reward
//   DL6   the gear score of an equipped item: the limit of the owned expansion
//   DL7   no tables, and tables of format 1: no grant on the wire, no limit of the expansion, the backend works
//   DL8   the reward item of a DLC pack: one time for each character; a full bag gives none and no record (the next entry tries again)
//   DL9   the progression tool: the expansion rows, the entitlements of the grant, and the stops (check_progression_tool.py)
//   DL10  the bytes of the two states against the form of the client (the fixtures of the research that live check L1 proved)

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "server/http/routes.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/fake_client.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;

constexpr const char* kOwnerA1 = "0d1c0000-0000-4000-8000-0000000000a1";
constexpr const char* kOwnerA2 = "0d1c0000-0000-4000-8000-0000000000a2";
constexpr const char* kOwnerB1 = "0d1c0000-0000-4000-8000-0000000000b1";
constexpr const char* kExpansionAlias = "Ent_Xpack_RiseOfTheAngryEarth_Owner";
constexpr const char* kExpansionAchievement = "Expansion_2023_Unlock";
constexpr std::uint32_t kExpansionBit = 1484;
constexpr std::uint32_t kAchievementBit = 11979;
constexpr const char* kAmuletAlias = "STEAM_APP_ID.1200562";
constexpr const char* kAmulet = "TestDlcAmulet";
constexpr std::uint32_t kEntitlementSlot = 19;
constexpr std::uint32_t kEntitlementType = 3133;
constexpr std::uint32_t kAchievementSlot = 66;
constexpr std::uint32_t kAchievementType = 3210;

/** The tables of the tests with the expansion: the base limit 5, the expansion limit 7; Mining 5 and 6; Riding 0 and 1. */
gamedata::ProgressionData expansion_progression(bool owned = true) {
    gamedata::ProgressionData data = test_progression();
    data.levelCap = 5;
    for (gamedata::ProgressionRow& row : data.progressions) {
        if (row.id == "Mining") {
            row.expansionMaxRank = 6;
            row.ranks.experience.assign(7, 100);
        } else if (row.id == "Riding") {
            row.expansionMaxRank = 1;
            row.ranks.experience.assign(2, 450);
        }
    }
    data.expansions = {{"None", 5, 625, 625, 200, ""}, {"Expansion2023", 7, 700, 800, 250, owned ? kExpansionAlias : ""}};
    data.entitlements = {{kExpansionAlias, kExpansionBit, "Expansion2023", "", kExpansionAchievement},
                         {"STEAM_APP_ID.1200560", 224, "Entitlement", "DLC_Item_Emote_FistBump", ""},
                         {"DLC_Item_Emote_FistBump", 297, "Emote", "Emote_FistBump", ""},
                         {kAmuletAlias, 226, "InventoryItem", kAmulet, ""}};
    data.achievements = {{kExpansionAchievement, kAchievementBit}, {"Test_Other_Achievement", 5}};
    return data;
}

gamedata::ItemDefinition amulet() {
    gamedata::ItemDefinition item;
    item.id = kAmulet;
    item.nameKey = "@TestDlcAmulet_MasterName";
    item.type = "Armor";
    item.itemClass = "EquippableAmulet+Jewelry";
    item.maxStack = 1;
    item.durability = 150;
    item.minGearScore = 100;
    item.maxGearScore = 200;
    item.weight = 10;
    return item;
}

gamedata::ItemDefinition sword(const char* id, std::uint32_t gearScore) {
    gamedata::ItemDefinition item;
    item.id = id;
    item.nameKey = std::string("@") + id + "_MasterName";
    item.type = "Weapon";
    item.itemClass = "Weapon+EquippableMainHand+Sword+Melee";
    item.maxStack = 1;
    item.durability = 1000;
    item.minGearScore = 100;
    item.maxGearScore = 900;
    item.gearScoreOverride = gearScore;
    item.weight = 10;
    return item;
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** The accounts of the cases: A with the characters A1 and A2, and B with B1, near each other. */
struct Owners {
    Player a1;
    Player b1;
    explicit Owners(state::Store& store)
        : a1(add_player(store, kSteamIdA, "Tester A", kOwnerA1, "Ownerone", state::WorldPosition{100.0F, 200.0F, 30.0F})),
          b1(add_player(store, kSteamIdB, "Tester B", kOwnerB1, "Ownerbee", state::WorldPosition{102.0F, 200.0F, 30.0F})) {
        state::Character second;
        second.characterId = kOwnerA2;
        second.personaId = a1.account.personaId;
        second.worldId = kWorldId;
        second.name = "Ownertwo";
        second.createdDate = "2026-01-01T00:00:00.000Z";
        second.modifiedDate = second.createdDate;
        if (store.insert_character(second) != state::InsertResult::inserted) {
            throw std::runtime_error("ownership test: the second character of account A");
        }
    }
};

/** @return The bit indexes of the last byte vector of a slot that a client got (no value: no such fragment). */
std::optional<std::vector<std::uint32_t>> bits_of(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
    std::optional<std::vector<std::uint32_t>> bits;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot != slot || fragment.type != type || !fragment.fields.contains({0, 0})) {
                    continue;
                }
                server::hub::Reader reader(fragment.fields.at({0, 0}));
                reader.size();
                if (reader.u8() == 1) {
                    reader.size64();
                }
                const std::uint32_t count = reader.size();
                std::vector<std::uint32_t> found;
                for (std::uint32_t position = 0; position < count; ++position) {
                    const std::uint8_t byte = reader.u8();
                    for (unsigned bit = 0; bit < 8; ++bit) {
                        if ((byte & (1U << bit)) != 0) {
                            found.push_back(position * 8 + bit);
                        }
                    }
                }
                bits = found;
            }
        }
    }
    return bits;
}

/** @return The number of fragments of a slot and type that a client got for a record of a different entity (an observer view). */
std::size_t observed_fragments(const FakeClient& client, std::uint32_t slot, std::uint32_t type) {
    std::size_t count = 0;
    for (const auto& sent : client.got) {
        if (sent.type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(sent.body).records) {
            if (record.interest == 1) {
                continue; // The own character of the client.
            }
            count += static_cast<std::size_t>(std::count_if(record.fragments.begin(), record.fragments.end(),
                                                            [&](const Fragment& fragment) { return fragment.slot == slot && fragment.type == type; }));
        }
    }
    return count;
}

bool contains(const std::vector<std::string>& list, const std::string& text) {
    return std::find(list.begin(), list.end(), text) != list.end();
}

/** DL1: the progression tables of format 2 from a file. */
void tables_format2(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "ownership-progression.json";
    const auto write = [&](const json& document) {
        std::ofstream(path) << document.dump();
        gamedata::ProgressionTables tables;
        const bool loaded = tables.load(path.string());
        return std::make_pair(loaded, tables);
    };
    json levels = json::array();
    for (std::uint32_t level = 0; level < 7; ++level) {
        levels.push_back({level * 100, level == 0 ? 0 : 5, level * 50, 0});
    }
    const json base = {
        {"format", 2},
        {"levelCap", 5},
        {"tradeSkillCap", 200},
        {"levels", levels},
        {"attributes", {{"minimum", 5}, {"maximum", 6}, {"constitutionHealth", {0, 40}}}},
        {"vitals", {{"health", 800}, {"stamina", 100}, {"mana", 100}}},
        {"rankTables", {{"WeaponMastery", {{"experience", {10, 10, 10}}, {"resetCost", {0, 0, 0}}}},
                        {"Mining", {{"experience", {100, 100, 100, 100, 100, 100, 100}}}},
                        {"Riding", {{"experience", {450, 450}}}},
                        {"AzothStaff", {{"experience", {5, 5, 5}}}}}},
        {"progressions", json::array({json::array({"SwordAbilityTable", "weapon", 2, "WeaponMastery", 0}),
                                      json::array({"Mining", "tradeskill", 5, "Mining", 6}),
                                      json::array({"Riding", "tradeskill", 0, "Riding", 1}),
                                      json::array({"AzothStaff", "tradeskill", 2, "AzothStaff", 0})})},
        {"abilities", json::array()},
        {"expansions", json::array({json::array({"None", 5, 625, 625, 200, ""}),
                                    json::array({"Expansion2023", 7, 700, 800, 250, kExpansionAlias})})},
        {"entitlements", json::array({json::array({kExpansionAlias, kExpansionBit, "Expansion2023", "", kExpansionAchievement}),
                                      json::array({kAmuletAlias, 226, "InventoryItem", kAmulet, ""})})},
        {"achievements", json::array({json::array({kExpansionAchievement, kAchievementBit})})},
    };
    const auto [loaded, tables] = write(base);
    const gamedata::ExpansionRow* owned = tables.owned_expansion();
    check(loaded && owned != nullptr && owned->id == "Expansion2023" && tables.level_cap() == 7 && tables.data().levelCap == 5
              && tables.max_equip_gear_score() == 800 && owned->maxCraftGearScore == 700 && owned->maxTradeSkillLevel == 250,
          "the owned expansion: the largest level 7 (the base 5), equip gear score 800, craft gear score 700, trade skill 250");
    check(tables.rank_cap(*tables.progression("Mining")) == 6 && tables.rank_cap(*tables.progression("Riding")) == 1
              && tables.rank_cap(*tables.progression("AzothStaff")) == 2 && tables.rank_cap(*tables.progression("SwordAbilityTable")) == 2,
          "the rank limits: the expansion value of a row (Mining 6, Riding 1), the base value for a row with 0 (AzothStaff, the weapon)");
    check(tables.entitlements().size() == 2 && tables.entitlement(kExpansionAlias)->index == kExpansionBit
              && tables.achievement(kExpansionAchievement)->index == kAchievementBit,
          "the entitlements and the achievement with their bits");
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json document = base;
        change(document);
        check(!write(document).first, std::string("the loader refuses the file: ") + what);
    };
    refused("an expansion whose entitlement is not in the list", [](json& document) { document["expansions"][1][5] = "Ent_Unknown"; });
    refused("an entitlement bit outside the range of the client (4600)",
            [](json& document) { document["entitlements"][1][1] = 4600; });
    refused("an entitlement whose achievement is not in the list", [](json& document) { document["entitlements"][1][4] = "Unknown"; });
    refused("two entitlements with one bit", [](json& document) { document["entitlements"][1][1] = kExpansionBit; });
    refused("an expansion level that the level table does not have", [](json& document) { document["expansions"][1][1] = 8; });
    refused("a rank limit of the expansion above the rank table", [](json& document) { document["progressions"][2][4] = 3; });
    refused("only one of the three lists", [](json& document) { document.erase("achievements"); });
    // A file of format 1 (stage 1.6): no expansion. The limits of no expansion, and a valid file.
    json old = base;
    old["format"] = 1;
    for (const char* member : {"expansions", "entitlements", "achievements"}) {
        old.erase(member);
    }
    for (json& row : old["progressions"]) {
        row.erase(4);
    }
    const auto [oldLoaded, oldTables] = write(old);
    check(oldLoaded && oldTables.owned_expansion() == nullptr && oldTables.level_cap() == 5
              && oldTables.rank_cap(*oldTables.progression("Mining")) == 5 && oldTables.rank_cap(*oldTables.progression("Riding")) == 0
              && oldTables.max_equip_gear_score() == 0 && oldTables.entitlements().empty(),
          "a file of format 1: no expansion, the base limits (level 5, Mining 5, Riding 0), no gear score limit");
}

/** DL2: two accounts and two characters of one account: the grants and the owner states (MP-10). */
void two_accounts(const Options&) {
    ServerHost host(observing());
    host.progression.set(expansion_progression());
    host.catalog.set({amulet()});
    const Owners owners(host.store);
    // An achievement of a different source (a later stage): it stays with the expansion.
    check(host.store.grant_achievements(kOwnerA1, {"Test_Other_Achievement"}), "control: A1 has a different achievement");
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, owners.a1.registration());
    host.enter(b, owners.b1.registration());
    // The first input report with a camera starts the interest scan of a session.
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, 30.0F));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, 30.0F));
    host.steps(60, {&a, &b});
    const auto aEntitlements = bits_of(a, kEntitlementSlot, kEntitlementType);
    const auto aAchievements = bits_of(a, kAchievementSlot, kAchievementType);
    const auto bAchievements = bits_of(b, kAchievementSlot, kAchievementType);
    check(aEntitlements && *aEntitlements == std::vector<std::uint32_t>{224, 226, 297, kExpansionBit},
          "A1: the entitlement state has the bits of the four entitlements of the grant (the expansion 1484 and the DLC pack)");
    check(aAchievements && *aAchievements == std::vector<std::uint32_t>{5, kAchievementBit},
          "A1: the achievement state has the expansion (11979) and the achievement of a different source (5)");
    check(bAchievements && *bAchievements == std::vector<std::uint32_t>{kAchievementBit} && bits_of(b, kEntitlementSlot, kEntitlementType),
          "B1: its own two states with the expansion");
    check(observed_fragments(a, kEntitlementSlot, kEntitlementType) == 0 && observed_fragments(a, kAchievementSlot, kAchievementType) == 0
              && observed_fragments(b, kEntitlementSlot, kEntitlementType) == 0 && observed_fragments(b, kAchievementSlot, kAchievementType) == 0
              && host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "each client observes the other character, and gets no entitlement and no achievement state of it (owner only)");
    check(host.store.list_entitlements(owners.a1.account.personaId).size() == 4 && host.store.list_entitlements(owners.b1.account.personaId).size() == 4
              && contains(host.store.list_achievements(kOwnerA1), kExpansionAchievement)
              && contains(host.store.list_achievements(kOwnerB1), kExpansionAchievement) && host.store.list_achievements(kOwnerA2).empty(),
          "the rows: four entitlements of each account; the achievement of A1 and B1; none yet of A2 (not in the world)");
    // The second character of account A comes into the world (a different session): it gets the expansion too.
    host.disconnect(a.peer);
    host.steps(5, {&b});
    FakeClient second(27002);
    Registration fields = owners.a1.registration();
    fields.characterId = kOwnerA2;
    host.enter(second, fields);
    host.steps(40, {&second, &b});
    const auto secondAchievements = bits_of(second, kAchievementSlot, kAchievementType);
    check(secondAchievements && *secondAchievements == std::vector<std::uint32_t>{kAchievementBit}
              && contains(host.store.list_achievements(kOwnerA2), kExpansionAchievement)
              && host.store.list_entitlements(owners.a1.account.personaId).size() == 4,
          "A2, the second character of account A: the achievement of the expansion, and no second entitlement row of the account");
}

/** DL3: the routes of the entitlement service. */
void routes(const Options&) {
    ServerHost host;
    host.progression.set(expansion_progression());
    const Owners owners(host.store);
    FakeClient a(27000);
    host.enter(a, owners.a1.registration());
    host.steps(5, {&a});
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_entitlement_routes(router, services);
    const auto request = [&](const char* method, const std::string& persona, const std::string& tokenPersona, bool sync) {
        service::HttpRequest message;
        message.method = method;
        message.host = "client.entitlementservice.amazongames.com:443";
        message.path = "/players/" + persona + "/games/new-world/platforms/steam/entitlements" + (sync ? "/sync" : "");
        message.target = message.path;
        if (!tokenPersona.empty()) {
            message.headers.emplace_back("x-amzn-token", encoding::local_jwt(json{{"personaId", tokenPersona}}.dump()));
        }
        message.body = sync ? R"({"syncTypes":["FirstTimeLogin","SteamRetailSync"]})" : "";
        std::string route;
        return router.dispatch(message, route);
    };
    const std::string personaA = owners.a1.account.personaId;
    const std::string personaB = owners.b1.account.personaId;
    const service::HttpResponse sync = request("POST", personaB, personaB, true);
    check(sync.status == 200 && sync.body == "{}" && host.store.list_entitlements(personaB).size() == 4,
          "the sync of B: the reply {} and the four entitlements of the grant in the rows of B");
    check(request("POST", personaB, personaB, true).status == 200 && host.store.list_entitlements(personaB).size() == 4,
          "a second sync changes nothing");
    const service::HttpResponse list = request("GET", personaA, personaA, false);
    const json body = json::parse(list.body, nullptr, false);
    bool expansion = false;
    if (body.is_object() && body.contains("entitlements")) {
        for (const json& entry : body["entitlements"]) {
            expansion = expansion
                        || (entry["alias"] == kExpansionAlias && entry["amount"]["freeAmount"] == 1 && entry["amount"]["paidAmount"] == 0
                            && entry["amount"]["totalAmount"] == 1);
        }
    }
    check(list.status == 200 && expansion && body["entitlements"].size() == 4 && body["hasMoreResults"] == false,
          "the list of A: the four entitlements, the expansion with one owned unit, and no more results");
    const std::size_t rowsA = host.store.list_entitlements(personaA).size();
    const service::HttpResponse foreign = request("POST", personaA, personaB, true);
    const service::HttpResponse foreignList = request("GET", personaA, personaB, false);
    check(foreign.status == 403 && foreignList.status == 403 && host.store.list_entitlements(personaA).size() == rowsA,
          "the token of B for the path of A: 403 for the sync and the list, and no row of A changed");
    check(request("GET", personaA, "", false).status == 401, "no token: 401");
}

/** DL4: the limits of the owned expansion, and the progression that the grant does not change. */
void limits(const Options&) {
    ServerHost host;
    host.progression.set(expansion_progression());
    const Owners owners(host.store);
    // The progression of A1 before the grant: the base limit 5 with its experience, points on an attribute, and the skills.
    state::ProgressRow row;
    row.level = 5;
    row.experience = 1000;
    row.spent = {2, 0, 3, 0, 0};
    state::SkillRows skills;
    skills.ranks.push_back({kMining, "Mining", 5, 0});
    skills.nodes.push_back({kSword, node_id("Test_Sword_Slash"), "Test_Sword_Slash", 1});
    skills.bars.push_back({kSword, 0, node_id("Test_Sword_Slash"), "Test_Sword_Slash"});
    check(host.store.set_progress(kOwnerA1, row) && host.store.write_skills(kOwnerA1, skills), "control: the rows of A1");
    FakeClient a(27000);
    host.enter(a, owners.a1.registration());
    host.steps(10, {&a});
    const world::Entity* entity = host.world.find_character(kOwnerA1);
    const state::ProgressRow stored = host.store.progress(kOwnerA1);
    const state::SkillRows storedSkills = host.store.list_skills(kOwnerA1);
    check(entity->progression.level == 5 && entity->progression.experience == 1000 && stored.level == 5 && stored.experience == 1000
              && stored.spent == row.spent && storedSkills.ranks.size() == 1 && storedSkills.ranks[0].rank == 5 && storedSkills.nodes.size() == 1
              && storedSkills.bars.size() == 1,
          "the grant changes no level, no experience, no spent point, no rank, no node, and no bar");
    world::Progress& progress = host.world.progress();
    check(progress.add_experience(kOwnerA1, 499).level == 5 && progress.add_experience(kOwnerA1, 1).level == 6,
          "the level above the base limit with experience: 499 more stays at 5, one more gives 6 (the start of level 6: 1500)");
    check(host.store.progress(kOwnerA1).experience == 1500 && host.world.find_character(kOwnerA1)->attributes.unspent == 29 - 5,
          "the experience of the start of level 6, and the attribute points of the new level");
    check(progress.set_level(kOwnerA1, 7).ok() && progress.set_level(kOwnerA1, 8).error == world::ProgressError::badLevel,
          "level 7 (the limit of the expansion) is a level; level 8 is refused");
    check(progress.add_experience(kOwnerA1, 5000).level == 7 && host.store.progress(kOwnerA1).experience == 2100,
          "experience at the limit: the level stays 7 at the start of the level");
    world::Skills& skillSystem = host.world.skills();
    const auto rank = [&](const char* id, std::uint32_t value) {
        return skillSystem.set_rank(kOwnerA1, id, gamedata::ProgressionKind::tradeSkill, value);
    };
    check(rank("Mining", 6).ok() && rank("Mining", 7).error == world::ProgressError::badRank && rank("Riding", 1).ok()
              && rank("Riding", 2).error == world::ProgressError::badRank,
          "the trade skill limits of the expansion rows: Mining 6 (base 5), Riding 1 (base 0); one rank more is refused");
}

/** DL5: a new world entry and a new start of the store give the same grant, with no second row and no second reward. */
void persistence(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "ownership-store.sqlite3";
    std::filesystem::remove(path);
    std::string personaA;
    for (int start = 0; start < 2; ++start) {
        ServerHost host;
        state::Store store;
        check(store.open(path.string()), "control: the store of the file opens");
        if (start == 0) {
            const Player player = add_player(store, kSteamIdA, "Tester A", kOwnerA1, "Ownerone", state::WorldPosition{100.0F, 200.0F, 30.0F});
            personaA = player.account.personaId;
        }
        gamedata::ProgressionTables tables;
        tables.set(expansion_progression());
        gamedata::ItemCatalog catalog;
        catalog.set({amulet()});
        ManualClock clock;
        world::World world(clock, store, host.tables, ServerHost::with_game_data(catalog, tables));
        for (int entry = 0; entry < 2; ++entry) {
            check(world.spawn_character(kOwnerA1, state::WorldPosition{100.0F, 200.0F, 30.0F}) == world::SpawnError::none, "control: the entity");
            const world::Entity* entity = world.find_character(kOwnerA1);
            check(entity->achievements == std::vector<std::uint32_t>{kAchievementBit} && entity->entitlements.size() == 4,
                  "start " + std::to_string(start + 1) + ", entry " + std::to_string(entry + 1) + ": the same bits");
            check(world.remove_character(kOwnerA1, world::LostCause::deleted), "control: the entity leaves");
        }
        const auto items = store.list_items(kOwnerA1);
        check(store.list_entitlements(personaA).size() == 4 && store.list_achievements(kOwnerA1).size() == 1
                  && store.list_entitlement_rewards(kOwnerA1).size() == 1
                  && std::count_if(items.begin(), items.end(), [](const state::ItemRow& item) { return item.itemId == kAmulet; }) == 1,
              "start " + std::to_string(start + 1) + ": four entitlement rows, one achievement row, one reward, one amulet");
    }
}

/** DL6: the gear score of an equipped item: the limit of the owned expansion (800). */
void equip_limit(const Options&) {
    ServerHost host;
    host.progression.set(expansion_progression());
    host.catalog.set({amulet(), sword("TestSwordHigh", 801), sword("TestSwordTop", 800)});
    const Owners owners(host.store);
    FakeClient a(27000);
    host.enter(a, owners.a1.registration());
    host.steps(5, {&a});
    world::Inventory& inventory = host.world.inventory();
    world::GiveOptions high;
    high.gearScore = 801;
    const world::ItemResult over = inventory.give(kOwnerA1, "TestSwordHigh", 1, high);
    check(over.ok() && inventory.equip(kOwnerA1, over.slots.front(), std::nullopt).error == world::ItemError::gearScoreAboveLimit,
          "a sword of gear score 801: the equip is refused (MaxEquipGS 800 of the owned expansion)");
    world::GiveOptions top;
    top.gearScore = 800;
    const world::ItemResult at = inventory.give(kOwnerA1, "TestSwordTop", 1, top);
    check(at.ok() && inventory.equip(kOwnerA1, at.slots.front(), std::nullopt).ok(), "a sword of gear score 800 is equipped");
}

/** DL7: no tables, and tables of format 1: no grant on the wire, and the backend works. */
void no_data(const Options&) {
    for (const bool format1 : {false, true}) {
        ServerHost host;
        if (format1) {
            host.progression.set(test_progression());
        }
        const Owners owners(host.store);
        FakeClient a(27000);
        host.enter(a, owners.a1.registration());
        host.steps(20, {&a});
        const world::Entity* entity = host.world.find_character(kOwnerA1);
        check(entity != nullptr && !bits_of(a, kEntitlementSlot, kEntitlementType) && !bits_of(a, kAchievementSlot, kAchievementType)
                  && host.store.list_entitlements(owners.a1.account.personaId).empty() && host.store.list_achievements(kOwnerA1).empty(),
              std::string(format1 ? "tables of format 1" : "no tables") + ": the character is in the world, with no entitlement and no achievement");
        if (format1) {
            check(!host.world.progress().set_level(kOwnerA1, 7).ok() && host.world.progress().set_level(kOwnerA1, 6).ok(),
                  "tables of format 1: the limit of the file (6), no limit of an expansion");
        }
    }
}

/** DL8: the reward item of a DLC pack, one time for each character; a full bag gives none, and the next entry gives it. */
void reward(const Options&) {
    ServerHost host;
    host.progression.set(expansion_progression());
    gamedata::ItemDefinition filler = amulet();
    filler.id = "TestFiller";
    host.catalog.set({amulet(), filler});
    const Owners owners(host.store);
    // A full bag: no place for the amulet.
    std::vector<state::ItemRow> rows;
    for (std::uint32_t slot = 0; slot < world::kMaxContainerSlots; ++slot) {
        char instance[33];
        std::snprintf(instance, sizeof(instance), "%032x", slot + 1);
        rows.push_back(state::ItemRow{instance, state::kContainerBag, slot, "TestFiller", 1});
    }
    check(host.store.write_items(kOwnerA1, {}, rows), "control: the bag of A1 is full");
    check(host.world.spawn_character(kOwnerA1, state::WorldPosition{100.0F, 200.0F, 30.0F}) == world::SpawnError::none, "control: A1 enters");
    check(host.store.list_entitlement_rewards(kOwnerA1).empty() && log_count("no reward of STEAM_APP_ID.1200562") == 1,
          "a full bag: no amulet and no record of the reward");
    check(host.world.remove_character(kOwnerA1, world::LostCause::deleted), "control: A1 leaves");
    check(host.store.write_items(kOwnerA1, {std::string(32, '0').replace(31, 1, "1")}, {}), "control: one free slot");
    check(host.world.spawn_character(kOwnerA1, state::WorldPosition{100.0F, 200.0F, 30.0F}) == world::SpawnError::none, "control: A1 enters");
    const auto count = [&] {
        const auto items = host.store.list_items(kOwnerA1);
        return std::count_if(items.begin(), items.end(), [](const state::ItemRow& item) { return item.itemId == kAmulet; });
    };
    check(count() == 1 && host.store.list_entitlement_rewards(kOwnerA1).size() == 1, "the next entry gives the amulet one time");
    check(host.world.remove_character(kOwnerA1, world::LostCause::deleted)
              && host.world.spawn_character(kOwnerA1, state::WorldPosition{100.0F, 200.0F, 30.0F}) == world::SpawnError::none && count() == 1,
          "a later entry gives no second amulet");
    check(host.world.spawn_character(kOwnerB1, state::WorldPosition{102.0F, 200.0F, 30.0F}) == world::SpawnError::none
              && host.store.list_entitlement_rewards(kOwnerB1).size() == 1,
          "the character of the other account gets its own amulet (one for each character)");
}

/** DL10: the bytes of the two states against the form of the client (the fixtures of the research, live check L1). */
void state_bytes(const Options&) {
    ManualClock clock;
    world::Entity entity;
    entity.entitlements = {kExpansionBit};
    entity.achievements = {kAchievementBit};
    entity.changed[world::ComponentKey::entitlements] = 1;
    entity.changed[world::ComponentKey::achievements] = 1;
    // The research fixtures (dlc-entitlements.md, "Exact initial test fragments"), written here from the form of the readers of
    // the client (0x3050A50, 0x3050940, 0x279E550), not from the encoder: the group and field masks, the full form of a container
    // (changes 0, version 1 with the sequence 1, the count in the size code), the bit of the index.
    std::string entitlement = "0107" "000101" "ba02";
    for (std::uint32_t byte = 0; byte < 186; ++byte) {
        entitlement += byte == 185 ? "10" : "00";
    }
    entitlement += "00010100" "01";
    std::string achievement = "0101" "000101" "9a17";
    for (std::uint32_t byte = 0; byte < 1498; ++byte) {
        achievement += byte == 1497 ? "08" : "00";
    }
    const auto hex = [](const server::hub::Bytes& bytes) {
        static const char* digits = "0123456789abcdef";
        std::string text;
        for (const std::uint8_t value : bytes) {
            text += digits[value >> 4];
            text += digits[value & 15];
        }
        return text;
    };
    check(hex(server::rep::encode_entitlement_state(entity, server::rep::View::owner, clock)) == entitlement,
          "the entitlement state of the expansion: the 198 bytes of the research fixture that the client took (live check L1)");
    check(hex(server::rep::encode_achievement_state(entity, server::rep::View::owner, clock)) == achievement,
          "the achievement state of the expansion: the 1,505 bytes of the research fixture that opened the expansion (live check L1)");
    check(server::rep::encode_entitlement_state(entity, server::rep::View::observer, clock).empty()
              && server::rep::encode_achievement_state(entity, server::rep::View::observer, clock).empty(),
          "no fragment for an observer");
    world::Entity none;
    check(server::rep::encode_entitlement_state(none, server::rep::View::owner, clock).empty()
              && server::rep::encode_achievement_state(none, server::rep::View::owner, clock).empty(),
          "no fragment for a character with no grant (no expansion data)");
}

/** DL9: the progression tool with made-up tables. */
void progression_tool(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_progression_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "check-progression-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::size_t ok = 0;
    std::size_t fail = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 150));
        ok += line.rfind("ok", 0) == 0 ? 1 : 0;
        fail += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    std::filesystem::remove(output);
    check(status == 0 && fail == 0 && ok >= 8, "check_progression_tool.py: each case passes (" + std::to_string(ok) + " cases)");
}

} // namespace

std::vector<Case> ownership_cases() {
    return {{"DL1", "DLC: the progression tables of format 2: the owned expansion, the limits, the refusals", tables_format2},
            {"DL2", "DLC: two accounts and two characters of one account: the grants and the owner states (MP-10)", two_accounts},
            {"DL3", "DLC: the entitlement routes; a token of a different account changes nothing", routes},
            {"DL4", "DLC: the level and the trade skill limits of the expansion; the grant changes no progression", limits},
            {"DL5", "DLC: a new entry and a new start give the same grant, with no second row and no second reward", persistence},
            {"DL6", "DLC: the gear score limit of an equipped item (MaxEquipGS of the owned expansion)", equip_limit},
            {"DL7", "DLC: no tables and tables of format 1: no grant on the wire, the backend works", no_data},
            {"DL8", "DLC: the reward item of a DLC pack, one time for each character; a full bag tries again", reward},
            {"DL9", "DLC: the progression tool: the expansion rows, the entitlements of the grant, the stops", progression_tool},
            {"DL10", "DLC: the bytes of the entitlement and the achievement state against the form of the client", state_bytes}};
}

} // namespace soulwarden::backend::tests

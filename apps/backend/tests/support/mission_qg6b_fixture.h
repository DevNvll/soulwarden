#pragma once

// The fixture of the cases of stage QG6b (moved into this header in stage QG8 for the cases of the review of QG6b; docs/systems/mission-boards.md):
// the faction rows and templates, the faction boards (the representatives), the patrol and loot places, the host with the faction boards
// (Factions), and the expected field 2 of a faction board. Made-up ids, names, keys, and places only.

#include <array>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "support/chest_fixture.h"
#include "support/mission_fixture.h"
#include "world/factions.h"

namespace soulwarden::backend::tests::missionfix {

inline constexpr std::uint8_t kMarauderTag = 20;
inline constexpr std::uint8_t kSyndicateTag = 21;
inline constexpr std::uint8_t kSyndicateFaction = 1;
inline constexpr std::uint8_t kMarauderFaction = 2;
inline constexpr const char* kFactionKill = "FM_Test_Kill_1";
inline constexpr const char* kFactionLoot = "FM_Test_Loot_1";
inline constexpr const char* kSyndicateKill = "FM_Test_Kill_S";
inline constexpr std::uint32_t kGameEventSlot = 68;
inline constexpr std::uint32_t kGameEventType = 497;
inline const std::array<std::uint8_t, 16> kClaim6{0x4B, 0x65, 0x7E, 0x1A, 0x68, 0xE0, 0x6C, 0x9F};
inline const std::array<std::uint8_t, 16> kClaim7{0x4B, 0x66, 0x7E, 0x1A, 0x68, 0xE0, 0x6C, 0x9F};

inline json world_row(const char* id, std::uint32_t row, const char* objective, json cells) {
    cells["ObjectiveID"] = objective;
    return {{"id", id}, {"crc", crc(id)}, {"table", "world"}, {"row", row}, {"objective", objective}, {"objectiveCrc", crc(objective)},
            {"territories", {{"ids", json::array({6})}, {"exclude", false}}}, {"cells", cells}};
}

/** The quest data of the cases: the QG6a templates, a faction kill template, a faction loot template, a patrol quest, a timer and give. */
inline json qg6b_quests() {
    json document = mission_quests();
    json& tasks = document["tasks"];
    tasks["test_fm_kill_task"] = questfix::task("Test_FM_Kill_Task", "taskkillcontribution", 13, {{"TargetQty", "1"}});
    tasks["test_m_returntogiver"] = questfix::task("Test_M_ReturnToGiver", "taskinteract", 23, {{"TargetQty", "1"}, {"InteractTag", "ReturnToGiver"}});
    tasks["test_fm_container"] = questfix::task("Test_FM_Container", "consecutivetaskcontainer", 1);
    tasks["test_fm_loot_task"] = questfix::task("Test_FM_Loot_Task", "taskhaveandreturnitems", 26,
                                           {{"TargetQty", "1"}, {"StayActive", "1"}, {"EnforceLocationRequirement", "1"}});
    tasks["test_fm_loot_container"] = questfix::task("Test_FM_Loot_Container", "consecutivetaskcontainer", 1);
    tasks["test_patrol_task"] = questfix::task("Test_Patrol_Task", "patroltask", 66,
                                               {{"POITag", "test_patrol"}, {"Duration", "10"}, {"EnforceLocationRequirement", "1"}});
    tasks["test_patrol_container"] = questfix::task("Test_Patrol_Container", "consecutivetaskcontainer", 1);
    tasks["test_timer_task"] = questfix::task("Test_Timer_Task", "tasktimer", 64, {{"Duration", "30"}});
    tasks["test_give_task"] = questfix::task("Test_Give_Task", "taskgiveitem", 33, {{"TargetQty", "2"}, {"ItemName", "TestOre"}});
    tasks["test_timer_container"] = questfix::task("Test_Timer_Container", "simpletaskcontainer", 0);
    const auto objective = [&](const char* id, json tree) {
        document["objectives"].push_back(
            {{"id", id},
             {"cells", {{"Type", "Mission"}, {"SuccessGameEventId", "Test_Bounty_Template"}, {"CanBeAbandoned", "1"}, {"Repeatable", "1"}}},
             {"tree", tree}});
    };
    objective("Test_FM_Kill", json::array({json::array({0, "test_fm_kill_task", json::array()}), json::array({1, "test_m_returntogiver", json::array()}),
                                           json::array({2, "test_fm_container", json::array({0, 1})})}));
    objective("Test_FM_Loot", json::array({json::array({0, "test_fm_loot_task", json::array()}), json::array({1, "test_m_returntogiver", json::array()}),
                                           json::array({2, "test_fm_loot_container", json::array({0, 1})})}));
    objective("Test_Patrol",
              json::array({json::array({0, "test_patrol_task", json::array()}), json::array({1, "test_patrol_container", json::array({0})})}));
    objective("Test_Timer_Give", json::array({json::array({0, "test_timer_task", json::array()}), json::array({1, "test_give_task", json::array()}),
                                              json::array({2, "test_timer_container", json::array({0, 1})})}));
    return document;
}

/** The mission data of the cases: the QG6a rows (the gather row with a delivery of 3 TestOre), the faction rows, the weights, the bonus. */
inline json qg6b_missions() {
    json document = mission_document();
    json& poi = document["missions"][2]["cells"];
    poi.erase("TestColumnWithNoRule");
    poi["TaskHaveAndReturnItemsQtyOverride"] = "3";
    poi["SuccessGameEventIdOverride"] = "Test_CMission_Gather";
    poi["ReturnToGiverOverride"] = "1";
    const json kill = {{"RequiredFaction", "Marauder"}, {"MissionGoalType", "Raid"}, {"Enabled", "1"}, {"ReturnToGiverOverride", "1"},
                       {"TaskKillContributionOverride", "Wolf"}, {"TaskKillContributionQtyOverride", "2"},
                       {"SuccessGameEventIdOverride", "Test_Faction_Success"}};
    const json loot = {{"RequiredFaction", "Marauder"}, {"MissionGoalType", "Loot"}, {"Enabled", "1"}, {"TaskHaveAndReturnItemsOverride", "TestHide"},
                       {"TaskHaveAndReturnItemsQtyOverride", "2"}, {"TaskHaveAndReturnChestDropProbabilityOverride", "100"},
                       {"POITagOverride", "test_loot"}, {"SuccessGameEventIdOverride", "Test_Faction_Success"}};
    json& missions = document["missions"];
    missions.push_back(world_row(kFactionKill, 0, "Test_FM_Kill", kill));
    missions.push_back(world_row(kFactionLoot, 1, "Test_FM_Loot", loot));
    json syndicate = kill;
    syndicate["RequiredFaction"] = "Syndicate";
    missions.push_back(world_row(kSyndicateKill, 2, "Test_FM_Kill", syndicate));
    json pvp = kill;
    pvp["IsPvpMission"] = "1";
    missions.push_back(world_row("FM_Test_PvP", 3, "Test_FM_Kill", pvp));
    json high = kill;
    high["MinLevel"] = "50";
    missions.push_back(world_row("FM_Test_High", 4, "Test_FM_Kill", high));
    missions.push_back(world_row("FM_Test_Far", 5, "Test_FM_Kill", kill));
    missions.back()["territories"]["ids"] = json::array({7});
    json& events = document["events"];
    events["test_cmission_gather"] = {{"id", "Test_CMission_Gather"}, {"UniversalExpAmount", "50"}, {"CurrencyReward", "100"},
                                      {"TerritoryStanding", "100"}, {"ItemReward", "TestBrick"}};
    events["test_faction_success"] = {{"id", "Test_Faction_Success"}, {"UniversalExpAmount", "90"}, {"CurrencyReward", "480"},
                                      {"TerritoryStanding", "250"}, {"FactionReputation", "45"}, {"FactionTokens", "55"}};
    document["weights"] = json::array();
    for (const auto& [slot, goal, weight] : {std::tuple<int, const char*, int>{1, "Raid", 9000}, {1, "Loot", 1}, {2, "Loot", 9000}, {2, "Raid", 1},
                                              {3, "Raid", 1}, {3, "Loot", 1}}) {
        document["weights"].push_back({{"table", "javelindata_owg_missionweights.datasheet"},
                                       {"cells", {{"SlotNumber", std::to_string(slot)}, {"Bucket", "FactionPvE"}, {"MissionGoalType", goal},
                                                  {"Weight", std::to_string(weight)}}}});
    }
    document["weights"].push_back({{"table", "x"}, {"cells", {{"SlotNumber", "1"}, {"Bucket", "FactionPvP"}, {"MissionGoalType", "Raid"}, {"Weight", "5"}}}});
    document["rewardModifiers"] = json::array({{{"id", "FactionMissionBonus"},
                                                {"cells", {{"Experience", "2"}, {"Currency", "9"}, {"TerritoryStanding", "0"},
                                                           {"FactionTokenModifier", "4"}}}}});
    return document;
}

/** The progression: the standings of mission_progression and the three factions (the caps of the game data). */
inline gamedata::ProgressionData qg6b_progression() {
    gamedata::ProgressionData data = mission_progression();
    const auto row = [](std::uint8_t number, const char* reputation, const char* token) {
        gamedata::FactionRow faction;
        faction.faction = number;
        faction.reputation = reputation;
        faction.token = token;
        faction.reputationCaps = {3000, 11000, 26000, 49000, 85000, 85000};
        faction.requiredLevels = {10, 20, 35, 50, 60, 61};
        faction.tokenCaps = {15000, 30000, 45000, 75000, 150000, 300000};
        faction.maxRank = 4;
        faction.expansionMaxRank = 5;
        return faction;
    };
    data.factions = {row(1, "Syndicate", "SyndicateTokens"), row(2, "Marauder", "MarauderTokens"), row(3, "Covenant", "CovenantTokens")};
    data.factionChange = gamedata::FactionChange{2592000, 20, 0, 0, 50};
    return data;
}

/** A representative of a faction (a person with an ObjectiveProvider; OpenMissionBoardOption of its own Interact bound). */
inline gamedata::InteractionObject representative(std::uint8_t tag, const char* faction, const state::WorldPosition& place) {
    gamedata::InteractionObject object = object_of(tag, InteractionKind::npc, place, {kBoardOption});
    gamedata::PersonBinding person;
    person.npc = std::string("Test_Commander_") + faction;
    person.npcCrc = gamedata::interaction_option_id(person.npc);
    object.person = person;
    object.provider = gamedata::MissionProvider{false, true, {faction}, false};
    return object;
}

/** The territory bounds of the fixture (6 and 7) and the POIs of the cases: the patrol area, the loot place. */
inline std::string qg6b_pois() {
    const auto square = [](float x, float y, float half) {
        return json::array({json::array({x - half, y - half}), json::array({x + half, y - half}), json::array({x + half, y + half}),
                            json::array({x - half, y + half})});
    };
    return json{{"format", 2},
                {"pois", json::array({{{"id", 61001}, {"tags", {"test_patrol"}}, {"territory", 6}, {"center", {100.0, 212.0, 30.0}}, {"radius", 20.0},
                                       {"shape", square(100.0F, 212.0F, 3.0F)}},
                                      {{"id", 61002}, {"tags", {"test_loot"}}, {"territory", 6}, {"center", {100.0, 200.0, 30.0}}, {"radius", 20.0},
                                       {"shape", square(100.0F, 200.0F, 6.0F)}}})},
                {"territories", json::array({{{"id", 6}, {"shape", square(100.0F, 200.0F, 40.0F)}}, {{"id", 7}, {"shape", square(100.0F, 262.0F, 15.0F)}}})}}
        .dump();
}

/** The Boards host with the data of the cases: the representatives, the claims, the catalog, the factions of A (Marauder) and B (Syndicate). */
struct Factions : Boards {
    explicit Factions(const std::string& database = ":memory:", bool existing = false) : Boards(database, existing) {
        DataParts base = data_parts();
        base.options.push_back(option_row("OpenMissionBoardOption", 51, true));
        base.objects.push_back(board_object(kBoardKey, kBoardGroup, kBoardPlace, 0x65));
        base.objects.push_back(board_object(kOtherBoardKey, kOtherBoardGroup, kOtherBoardPlace, 0x66));
        base.objects.push_back(representative(kMarauderTag, "Marauder", {104.0F, 201.0F, 30.0F}));
        base.objects.push_back(representative(kSyndicateTag, "Syndicate", {98.0F, 201.0F, 30.0F}));
        if (!host.interactions.set(std::move(base.options), std::move(base.achievements), std::move(base.objects))) {
            throw std::runtime_error("QG6b test: the interaction data was not set: " + host.interactions.error());
        }
        host.interactions.set_claims({{6, kClaim6}, {7, kClaim7}});
        std::vector<gamedata::ItemDefinition> items = chests::chest_items();
        items.push_back(chests::chest_item("TestBrick", "Resource", "Resource", 1, 1));
        host.catalog.set(items);
        if (!host.quests.load_text(qg6b_quests().dump()) || !host.missions.load_text(qg6b_missions().dump())) {
            throw std::runtime_error("QG6b test: the data was not set: " + host.quests.error() + host.missions.error());
        }
        host.quests.bind_missions(host.missions);
        host.progression.set(qg6b_progression());
        if (!host.pois.load_text(qg6b_pois())) {
            throw std::runtime_error("QG6b test: the POI data was not set");
        }
    }
    /** The entry of A and B, their factions (A Marauder, B Syndicate). */
    void start_factions() {
        start();
        check(host.world.factions().set(kCharacterA, world::FactionCommand{kMarauderFaction, std::nullopt, std::nullopt}).ok()
                  && host.world.factions().set(kCharacterB, world::FactionCommand{kSyndicateFaction, std::nullopt, std::nullopt}).ok(),
              "setup: A is a Marauder, B is a Syndicate");
        host.steps(5, {&clientA, &clientB});
    }
    /** @return The representative of a tag in the data (the objects of the cases are not at the index of their tag). */
    const gamedata::InteractionObject& representative_of(std::uint8_t tag) const {
        const std::string key = object_of(tag, InteractionKind::npc, {}, {}).key;
        for (const gamedata::InteractionObject& object : host.interactions.objects()) {
            if (object.key == key) {
                return object;
            }
        }
        throw std::runtime_error("QG6b test: no representative of the tag " + std::to_string(tag));
    }
    /** The open of a representative by a client (its block, the reply, the end of the block). */
    void open_representative(FakeClient& client, std::uint8_t tag) {
        const gamedata::InteractionObject& object = representative_of(tag);
        const gamedata::InteractionOption& row = host.interactions.options()[kBoardOption];
        InteractionBlock block;
        block.actor = object.actor;
        block.group = object.group;
        block.target = object.bounds[0].target;
        block.action = row.action;
        block.baseAction = row.committed ? row.action : 0;
        block.optionEntity = object.group ^ 0x40ULL;
        block.committed = row.committed;
        block.optionId = row.id;
        block.text88 = "@Test_Representative";
        request(client, block, 4);
        end_block(client, 3);
    }
    void select(FakeClient& client, const char* character, std::uint8_t index) {
        const world::Entity* own = entity(character);
        server::hub::Writer body;
        body.u64(0);
        body.u64(own->id);
        body.u8(index);
        host.datagram(client.peer, client.component_message(server::rep::kTypeObjectiveSelection, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }
    /** The index of a mission in the offers of a character (-1: none). */
    int offer_index(const char* character, const char* mission) {
        const std::vector<world::MissionOffer>& offers = entity(character)->missionBoard.offers;
        for (std::size_t index = 0; index < offers.size(); ++index) {
            if (offers[index].mission == crc(mission)) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
};

/** The expected field 2 of the provider state of a faction board (an independent writer of the element form of FACTION.md section 3). */
inline Bytes expected_offers(const std::vector<world::MissionOffer>& offers, std::uint64_t sequence) {
    server::hub::Writer out;
    out.size(0);
    out.u8(1);
    out.size64(sequence);
    out.size(static_cast<std::uint32_t>(offers.size()));
    for (const world::MissionOffer& offer : offers) {
        out.u64(offer.instance);
        out.u32(offer.mission);
        out.raw(offer.destination);
        out.u64(0);
        out.u16(offer.origin);
        out.u8(offer.available ? 1 : 0);
        for (int flag = 0; flag < 3; ++flag) {
            out.u8(0);
        }
        out.u64(0);
        out.u8(0);
        out.size(0);
    }
    return out.data();
}

} // namespace soulwarden::backend::tests::missionfix

#pragma once

// The fixture of the mission board cases (stage QG6a; moved into this header in stage QG6b for the cases of QG6b,
// docs/systems/mission-boards.md): the made-up quest data of the town mission templates, the mission data, the boards of a
// settlement, the host with the boards (Boards), and the decoders of the provider state and of the mission elements. Made-up ids,
// names, keys, and places only.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <sqlite3.h>

#include "gamedata/mission_data.h"
#include "server/rep/quest_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/check.h"
#include "support/interaction_fixture.h"
#include "support/quest_fixture.h"
#include "support/station_fixture.h"
#include "support/test_progression.h"
#include "world/missions.h"
#include "world/quest_support.h"
#include "world/skills.h"

namespace soulwarden::backend::tests::missionfix {

using namespace interaction;
using nlohmann::json;

constexpr std::uint32_t kBoardOption = 6;
constexpr std::uint32_t kObjectiveInteractorSlot = 10;
constexpr std::uint32_t kObjectiveInteractorType = 3829;
constexpr std::uint32_t kTerritoryInterfaceSlot = 3;
constexpr std::uint32_t kTerritoryInterfaceType = 1746;
constexpr const char* kTownKey = "b0a4d000-0000-4000-8000-000000000001";
const std::string kBoardKey = std::string(kTownKey) + "/0000000000000b01";
const std::string kOtherBoardKey = std::string("b0a4d000-0000-4000-8000-000000000002") + "/0000000000000b02";
constexpr state::WorldPosition kBoardPlace{102.0F, 201.0F, 30.0F};
constexpr state::WorldPosition kOtherBoardPlace{100.0F, 261.0F, 30.0F};
constexpr std::uint64_t kBoardRoot = 0x9000;
constexpr std::uint64_t kBoardEntity = 0x9001;
constexpr std::uint64_t kBoardInteract = 0x9100;
constexpr const char* kTemplate = "Test_CM_Bounty";
constexpr const char* kWolf = "TestProject_1_Wolf_5";
constexpr const char* kBoar = "TestProject_1_Boar_8";
constexpr const char* kPoi = "TestProject_1_Poi_1";
constexpr const char* kProject = "TestProject";
constexpr std::uint64_t kMinuteNs = 60'000'000'000ULL;

inline std::uint32_t crc(const std::string& text) {
    return gamedata::quest_crc(text);
}

/** The quest data: the base document of the quest cases and the made-up template of the town kill missions (CM_bounty_kill_X_enemies). */
inline json mission_quests() {
    json document = questfix::quest_document();
    json& tasks = document["tasks"];
    tasks["test_m_kill"] = questfix::task("Test_M_Kill", "taskkillcontribution", 13, {{"TargetQty", "1"}});
    tasks["test_cm_return"] = questfix::task("Test_CM_Return", "taskinteract", 23,
                                             {{"TargetQty", "1"}, {"EnforceLocationRequirement", "1"}, {"InteractTag", "ReturnToGiver"}});
    tasks["test_cm_container"] = questfix::task("Test_CM_Container", "consecutivetaskcontainer", 1, {{"EnforceLocationRequirement", "1"}});
    tasks["test_g_have"] = questfix::task("Test_G_Have", "taskhaveandreturnitems", 26, {{"TargetQty", "1"}});
    tasks["test_g_container"] = questfix::task("Test_G_Container", "consecutivetaskcontainer", 1);
    document["objectives"].push_back({{"id", kTemplate},
                                      {"cells",
                                       {{"Type", "CommunityGoal"}, {"SuccessGameEventId", "Test_Bounty_Template"}, {"CanBeAbandoned", "1"},
                                        {"Repeatable", "1"}}},
                                      {"tree", json::array({json::array({0, "test_m_kill", json::array()}),
                                                            json::array({1, "test_cm_return", json::array()}),
                                                            json::array({2, "test_cm_container", json::array({0, 1})})})}});
    document["objectives"].push_back({{"id", "Test_CM_Gather"},
                                      {"cells", {{"Type", "CommunityGoal"}, {"CanBeAbandoned", "1"}}},
                                      {"tree", json::array({json::array({0, "test_g_have", json::array()}),
                                                            json::array({1, "test_cm_return", json::array()}),
                                                            json::array({2, "test_g_container", json::array({0, 1})})})}});
    // The event of the template: the board must not pay it (the success event of the row overrides it).
    document["events"]["test_bounty_template"] = {{"id", "Test_Bounty_Template"}, {"UniversalExpAmount", "999"}, {"CurrencyReward", "999"}};
    return document;
}

inline json mission_row(const char* id, std::uint32_t row, const char* objective, json cells, json territories = {{"ids", json::array()}, {"exclude", false}}) {
    cells["ObjectiveID"] = objective;
    return {{"id", id}, {"crc", crc(id)}, {"table", "town"}, {"row", row}, {"objective", objective}, {"objectiveCrc", crc(objective)},
            {"territories", territories}, {"cells", cells}};
}

/** The mission data: two kill rows of one template (the Kill bucket), a gather row with a POI override (the Gather bucket), the project. */
inline json mission_document() {
    const json kill = {{"TerritoryProjectId", kProject}, {"MissionTier", "1"}, {"MissionGoalType", "Kill"}, {"Enabled", "1"},
                       {"ReturnToGiverOverride", "1"}, {"IsSettlementValidDestination", "1"}, {"IsOutpostValidDestination", "0"},
                       {"TaskTimerOverride", "3600"}, {"UseGlobalStorageOverride", "1"}, {"CommunityGoalAmount", "0"},
                       {"FailureGameEventIdOverride", "Test_CMission_Failure"}};
    json wolf = kill;
    wolf["TaskKillContributionOverride"] = "Wolf";
    wolf["TaskKillContributionQtyOverride"] = "5";
    wolf["SuccessGameEventIdOverride"] = "Test_CMission_Wolf";
    json boar = kill;
    boar["TaskKillContributionOverride"] = "Boar";
    boar["TaskKillContributionQtyOverride"] = "8";
    boar["SuccessGameEventIdOverride"] = "Test_CMission_Wolf";
    json poi = {{"TerritoryProjectId", kProject}, {"MissionTier", "1"}, {"MissionGoalType", "Gather"}, {"Enabled", "1"},
                {"POITagOverride", "test_place"}, {"TaskHaveAndReturnItemsOverride", "TestOre"}, {"SuccessGameEventIdOverride", "Test_CMission_Wolf"},
                // Stage QG6b: the POI and item cells have their rule now; a column that the backend does not know keeps the row unsupported.
                {"TestColumnWithNoRule", "1"}};
    const json notSixteen = {{"ids", json::array({16})}, {"exclude", true}};
    return {{"format", 1},
            {"missions", json::array({mission_row(kWolf, 0, kTemplate, wolf, notSixteen), mission_row(kBoar, 1, kTemplate, boar, notSixteen),
                                      mission_row(kPoi, 2, "Test_CM_Gather", poi)})},
            {"projects", json::array({{{"id", kProject}, {"crc", crc(kProject)},
                                       {"cells", {{"ProjectType", "AlwaysAvailable"}, {"ProgressionLevel", "1"}, {"CurrentTier", "1"}}}}})},
            {"events",
             {{"test_cmission_wolf", {{"id", "Test_CMission_Wolf"}, {"UniversalExpAmount", "110"}, {"CurrencyReward", "135"}, {"TerritoryStanding", "350"}}},
              {"test_cmission_failure", {{"id", "Test_CMission_Failure"}, {"AzothReward", "0"}}}}},
            {"townBuckets", json::array({{{"key", 4}, {"goal", "Kill"}, {"missions", json::array({crc(kWolf), crc(kBoar)})}},
                                         {{"key", 1}, {"goal", "Gather"}, {"missions", json::array({crc(kPoi)})}}})}};
}

/** The progression tables: the standing of the territory 6 with a large first rank (no rank-up of 350 points). */
inline gamedata::ProgressionData mission_progression() {
    gamedata::ProgressionData data = test_progression();
    gamedata::StandingRow standing;
    standing.territory = 6;
    standing.id = "6";
    standing.maxRank = 2;
    standing.ranks.experience = {5000, 9000, 12000};
    standing.xpReward = {0, 0, 0};
    standing.pool = "TerritoryRankBonus_6";
    data.standings.push_back(standing);
    gamedata::StandingRow other = standing;
    other.territory = 7;
    other.id = "7";
    other.pool = "TerritoryRankBonus_7";
    data.standings.push_back(other);
    return data;
}

/** Made-up client ids of the two boards (the rule of a child that the client makes: the low 3 bits 7). */
constexpr std::uint64_t kBoardGroup = 0x8500640700010007ULL;
constexpr std::uint64_t kOtherBoardGroup = 0x8500640700020007ULL;

/**
 * A board of a settlement as the data has it (stage QG6a, format 5): a level object that the client makes, with the client id of its place
 * (`group`), the actor G and 8 zero bytes, the XOR rule for its bound (K = root XOR G), its TerritoryInterface slot, and its provider.
 */
inline gamedata::InteractionObject board_object(const std::string& key, std::uint64_t group, const state::WorldPosition& place, std::uint8_t claim) {
    gamedata::InteractionObject object;
    object.key = key;
    object.kind = InteractionKind::missionBoard;
    object.slice = "slices/test/town_project_board.dynamicslice";
    object.asset = stations::asset_of(9);
    object.sub = 2;
    for (std::size_t index = 0; index < 8; ++index) {
        object.actor[index] = static_cast<std::uint8_t>(group >> (8 * index));
    }
    object.group = group;
    object.region = "r_+00_+00";
    object.position = place;
    const std::uint64_t k = kBoardRoot ^ group;
    gamedata::InteractionBound bound;
    bound.slot = 2;
    bound.component = kBoardInteract;
    bound.target = (kBoardEntity ^ k) + kBoardInteract;
    bound.position = state::WorldPosition{place.x, place.y - 0.9F, place.z};
    bound.rangeSq = 25.0F;
    bound.capacity = -1;
    bound.arc = 180.0F;
    bound.options.push_back(gamedata::BoundOption{kBoardOption, kBoardEntity ^ k});
    object.bounds.push_back(bound);
    object.territorySlot = 3;
    object.provider = gamedata::MissionProvider{true, true, {}, false};
    // A made-up actor id of the Claim landmark of its territory (the member board.claim of the data).
    object.claim = std::array<std::uint8_t, 16>{0x4B, claim, 0x7E, 0x1A, 0x68, 0xE0, 0x6C, 0x9F};
    return object;
}

/** The interaction data: the base parts, OpenMissionBoardOption, a board of the town (territory 6) and of another (territory 7). */
inline bool set_boards(gamedata::InteractionData& data) {
    DataParts base = data_parts();
    base.options.push_back(option_row("OpenMissionBoardOption", 51, true));
    base.objects.push_back(board_object(kBoardKey, kBoardGroup, kBoardPlace, 0x65));
    base.objects.push_back(board_object(kOtherBoardKey, kOtherBoardGroup, kOtherBoardPlace, 0x66));
    return data.set(std::move(base.options), std::move(base.achievements), std::move(base.objects));
}

/** @return The block of the open of a board, as the client makes it: the client id, the target of the bound, the option entity. */
inline InteractionBlock board_block(const gamedata::InteractionData& data, const std::string& key) {
    const gamedata::InteractionObject* object = nullptr;
    for (const gamedata::InteractionObject& entry : data.objects()) {
        if (entry.key == key) {
            object = &entry;
        }
    }
    InteractionBlock block;
    if (object == nullptr) {
        return block;
    }
    const gamedata::InteractionOption& row = data.options()[kBoardOption];
    block.actor = object->actor;
    block.group = object->group;
    block.target = object->bounds[0].target;
    block.action = row.action;
    block.baseAction = row.committed ? row.action : 0;
    block.optionEntity = object->bounds[0].options[0].entity;
    block.committed = row.committed;
    block.optionId = row.id;
    block.text88 = "@Test_Board";
    return block;
}

/** The bounds of the territory 6 around the town and of the territory 7 around the other board (the POI data of the format 2). */
inline std::string territory_bounds() {
    const auto square = [](float x, float y, float half) {
        return json::array({json::array({x - half, y - half}), json::array({x + half, y - half}), json::array({x + half, y + half}),
                            json::array({x - half, y + half})});
    };
    return json{{"format", 2},
                {"pois", json::array()},
                {"territories", json::array({{{"id", 6}, {"shape", square(100.0F, 200.0F, 40.0F)}},
                                             {{"id", 7}, {"shape", square(100.0F, 262.0F, 15.0F)}}})}}
        .dump();
}

/** The reference seed of the static read (OFFERS.md, Algorithm), written apart from the backend: the minute, the phase, the FNV-1a hash. */
inline std::uint32_t reference_seed(const std::string& text, std::uint16_t territory, std::uint64_t serverNs, std::int32_t interval = 30) {
    const auto minute = static_cast<std::int32_t>(static_cast<std::int64_t>(serverNs) / 60000000000LL);
    const std::int32_t phase = minute + interval / 2;
    const std::int32_t boundary = phase - phase % interval;
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return static_cast<std::uint32_t>(hash + territory + static_cast<std::uint64_t>(static_cast<std::int64_t>(boundary)));
}

/** The reference index of the first nonempty bucket (one LCG step from projectId + seed). */
inline std::uint32_t reference_index(std::uint32_t project, std::uint32_t seed, std::uint32_t count) {
    std::uint64_t r = static_cast<std::uint32_t>(project + seed) ^ 0x5DEECE66DULL;
    r = (r * 0x5DEECE66DULL + 11) & ((1ULL << 48) - 1);
    return static_cast<std::uint32_t>((r >> 16) % count);
}

/** A host with the boards, the missions, the quests, the standing, the bounds; A and B at the town board. */
struct Boards : Shop {
    explicit Boards(const std::string& database = ":memory:", bool existing = false, bool data = true) : Shop(false, database, true, existing) {
        if (!set_boards(host.interactions)) {
            throw std::runtime_error("mission test: the interaction data was not set: " + host.interactions.error());
        }
        if (!host.quests.load_text(mission_quests().dump())) {
            throw std::runtime_error("mission test: the quest data was not set: " + host.quests.error());
        }
        if (data && !host.missions.load_text(mission_document().dump())) {
            throw std::runtime_error("mission test: the mission data was not set: " + host.missions.error());
        }
        host.quests.bind_missions(host.missions);
        host.progression.set(mission_progression());
        if (!host.pois.load_text(territory_bounds())) {
            throw std::runtime_error("mission test: the territory bounds were not set");
        }
    }
    world::Missions& missions() {
        return host.world.missions();
    }
    world::Entity* board(const std::string& key = kBoardKey) {
        return host.world.find(host.world.interactions().board_entity(key));
    }
    /** The server clock of the world now (nanoseconds). */
    std::uint64_t server_ns() const {
        return host.clock.server_time_ns();
    }
    /** Moves the clock to the start of the first seed window from now on in which `wanted(offers)` holds for the characters (60 windows). */
    template <class Want>
    bool seek(Want&& wanted) {
        for (int window = 0; window < 60; ++window) {
            if (wanted()) {
                return true;
            }
            const std::uint64_t next = world::next_seed_ns(server_ns(), world::town_rule::kIntervalMinutes);
            host.clock.advance(std::chrono::nanoseconds(next - server_ns() + 1'000'000'000ULL));
        }
        return wanted();
    }
    /** @return The kill offer of a character at the town board now (0: none). */
    std::uint32_t kill_offer(const char* character) {
        const world::Entity* entity = this->entity(character);
        for (const world::TownOffer& offer : missions().offers_of(*entity, 6).offers) {
            if (offer.bucket == 4) {
                return offer.mission;
            }
        }
        return 0;
    }
    /** The open of a board by a character (its block, the reply, the end of the block). */
    void open(FakeClient& client, const std::string& key = kBoardKey) {
        const world::Entity* entity = board(key);
        if (entity == nullptr) {
            return;
        }
        request(client, board_block(host.interactions, key), 4);
        end_block(client, 3);
    }
    void accept(FakeClient& client, const char* character, std::uint32_t mission, std::uint32_t destination = 0) {
        const world::Entity* own = entity(character);
        server::hub::Writer body;
        body.u64(0);
        body.u64(own->id);
        body.u32(mission);
        body.u32(destination);
        host.datagram(client.peer, client.component_message(server::rep::kTypeCommunityGoalSelection, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }
    void hand_in(FakeClient& client, const char* character, std::uint64_t instance) {
        const world::Entity* own = entity(character);
        server::hub::Writer body;
        body.u64(0);
        body.u64(own->id);
        body.u64(instance);
        host.datagram(client.peer, client.component_message(server::rep::kTypeObjectiveCompletion, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }
    void abandon(FakeClient& client, const char* character, std::uint64_t instance) {
        const world::Entity* own = entity(character);
        server::hub::Writer body;
        body.u64(0);
        body.u64(own->id);
        body.u64(instance);
        host.datagram(client.peer, client.component_message(server::rep::kTypeAbandonObjective, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }
    /** Credits `count` kills of a vitals category to a character (the kill event of Rewards::killed). */
    void kill(const char* character, const char* category, int count) {
        for (int index = 0; index < count; ++index) {
            world::QuestEvent event;
            event.kind = world::QuestEventKind::kill;
            event.keys = {category};
            event.result = ++m_result;
            host.world.quests().event(*entity(character), event);
        }
        host.steps(2, {&clientA, &clientB});
    }
    /** The mission instance of a character (null: none). */
    const world::QuestInstance* mission_of(const char* character, std::uint32_t mission) {
        for (const world::QuestInstance& quest : entity(character)->quests.active) {
            if (quest.mission == mission) {
                return &quest;
            }
        }
        return nullptr;
    }
    /** A travels to a place (World::travel: the use and the service end) and reports it. */
    void travel_a(const state::WorldPosition& place) {
        placeA = place;
        (void)host.world.travel(kCharacterA, place);
        for (int step = 0; step < 4; ++step) {
            report(clientA, std::nullopt);
            host.steps(30, {&clientA, &clientB});
        }
    }
    /** A walks in steps of 0.5 m (each an accepted position) along y. */
    void walk_a(float toY) {
        while (std::fabs(placeA.y - toY) > 0.01F) {
            placeA.y += placeA.y < toY ? std::min(0.5F, toY - placeA.y) : -std::min(0.5F, placeA.y - toY);
            report(clientA, std::nullopt);
            host.steps(10, {&clientA, &clientB});
        }
    }
    /** The entry of the world of A and B, their 2709 (the quest states), and the spawn of the boards. */
    void start() {
        enter(true);
        questfix::send_2709(host, clientA, kCharacterA);
        questfix::send_2709(host, clientB, kCharacterB);
        host.steps(20, {&clientA, &clientB});
    }

private:
    std::uint64_t m_result{0x5000};
};

/** @return The last ObjectiveInteractor fragment of the own character of a client (from a message index on). */
inline std::optional<Fragment> provider_state(const FakeClient& client, std::size_t from = 0) {
    const std::vector<Fragment> found = questfix::own_fragments(client, kObjectiveInteractorSlot, kObjectiveInteractorType, from);
    return found.empty() ? std::nullopt : std::optional<Fragment>(found.back());
}

/** The expected provider state bytes (an independent writer of the form of OFFERS.md 1.3 and of research mission-boards.md 1.1). */
struct TownEntry {
    std::uint16_t territory{0};
    std::uint32_t seed{0};
    std::vector<std::uint32_t> unavailable;
};

inline Bytes expected_provider(const std::array<std::uint8_t, 16>& provider, std::uint64_t expiration, std::uint64_t sequence,
                        const std::vector<TownEntry>& towns) {
    server::hub::Writer out;
    out.u8(0x01); // group 0
    out.u8(0x0F); // fields 0 to 3
    out.raw(provider);
    out.u64(expiration);
    out.size(0);
    out.u8(1);
    out.size64(sequence);
    out.size(0);
    out.size(static_cast<std::uint32_t>(towns.size()));
    for (const TownEntry& town : towns) {
        out.u16(town.territory);
        out.u32(town.seed);
        out.size(static_cast<std::uint32_t>(town.unavailable.size()));
        for (const std::uint32_t mission : town.unavailable) {
            out.u32(mission);
        }
        out.size(1);
        out.u32(crc(kProject));
    }
    return out.data();
}

/** @return The decoded destination, the mission, the community flag, and the origin of each active element of the last Objectives state. */
struct MissionElement {
    std::uint32_t objective{0};
    std::uint64_t instance{0};
    std::uint32_t mission{0};
    std::array<std::uint8_t, 16> destination{};
    std::uint16_t origin{0};
    bool available{false};
    bool community{false};
};

inline std::vector<MissionElement> mission_elements(const Fragment& fragment) {
    std::vector<MissionElement> found;
    if (!fragment.fields.contains({1, 3})) {
        return found;
    }
    server::hub::Reader reader(fragment.fields.at({1, 3}));
    reader.size();
    if (reader.u8() == 1) {
        reader.size64();
    }
    for (std::uint32_t count = reader.size(), index = 0; index < count; ++index) {
        MissionElement element;
        element.objective = reader.u32();
        element.instance = reader.u64();
        element.mission = reader.u32();
        const auto destination = reader.raw(16);
        std::copy(destination.begin(), destination.end(), element.destination.begin());
        reader.u64();
        element.origin = reader.u16();
        element.available = reader.u8() == 1;
        element.community = reader.u8() == 1;
        reader.raw(2);
        reader.u64();
        reader.u8();
        reader.size();
        found.push_back(element);
    }
    return found;
}

inline std::optional<questfix::ObjectivesState> last_objectives(const FakeClient& client) {
    const std::vector<Fragment> found = questfix::own_fragments(client, questfix::kObjectivesSlot, questfix::kObjectivesType);
    return found.empty() ? std::nullopt : std::optional<questfix::ObjectivesState>(questfix::decode_objectives(found.back()));
}

inline std::optional<Fragment> last_objectives_fragment(const FakeClient& client) {
    const std::vector<Fragment> found = questfix::own_fragments(client, questfix::kObjectivesSlot, questfix::kObjectivesType);
    return found.empty() ? std::nullopt : std::optional<Fragment>(found.back());
}

inline const questfix::ObjectivesState::Task* task_of(const questfix::ObjectivesState& state, std::uint64_t instance, std::uint8_t index) {
    for (const auto& task : state.tasks) {
        if (task.instance == instance && task.index == index) {
            return &task;
        }
    }
    return nullptr;
}

/** The common start of QG6AE, QG6AF, QG6AH: A accepted the wolf at the town board. */
inline std::uint64_t accepted(Boards& shop) {
    shop.start();
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf); }), "setup: a seed window in which the Kill offer of A is the wolf");
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kWolf));
    check(quest != nullptr, "setup: A accepted the wolf");
    return quest != nullptr ? quest->instance : 0;
}

/** Runs an SQL text on a database file of a case (a trigger that refuses a row: a store failure). */
inline bool run_sql(const std::filesystem::path& path, const char* text) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.string().c_str(), &database) == SQLITE_OK && sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

} // namespace soulwarden::backend::tests::missionfix

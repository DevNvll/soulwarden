// The cases of stage QG6a: the town mission board (docs/systems/mission-boards.md). Made-up ids, names, keys, and places with the shape of
// the game data (the town project board of a settlement, a town mission row with a kill override, its template with a kill task, a return
// task, and a consecutive container, its success event, the always available project).
//
//   QG6A-TOOL the check of the data tool (runs check_missions_tool.py)
//   QG6AA  the mission data (I1): the loader (the rows, the territory rule, the town buckets, the events), the refusals of a file, the binding
//        of the quest data (the effective objective of a kill override, a custom row, an override with no rule, a missing template); no
//        file: no mission
//   QG6AB  the seed and the selection of the client (I2): the vectors of the static read (the empty text and a made-up character id), the
//        rounding of the minute, the seconds to the next seed, the LCG index, the unavailable rule
//   QG6AC  the open of the board (I2): the board is a server child with its TerritoryInterface state (38 bytes); the open (action 51) gives
//        3743 result 1, the provider state (the actor of the board, the expiration, the empty mission vector, the town entry), and the
//        service of the board
//   QG6AD  the accept 3819 (I3): each refusal changes nothing (no open, a forged id, another offer, the destination argument, an unsupported
//        mission, the range, an expired selection, a second accept); the instance in the Objectives state (the template, the mission, the
//        destination, the origin 6, the community flag) and in the store
//   QG6AE  the progress (I4): the kill task of the override (Wolf, 5), a kill of another category, the return task at the open of the board
//   QG6AF  the hand-in 2461 (I5): the rewards of the success event of the mission (110 experience, 135 coin units, 350 standing of the
//        territory 6; not the event of the template), one payment, the refusals (a retry, another board, a forged instance, a quest that is
//        not ready), the town entry after it (the mission is unavailable for the seed), and the offer again at the next seed
//   QG6AG  a restart keeps an accepted mission with its progress and its provider (I6); schema 19 from a database of schema 18
//   QG6AH  the abandon 3836 (I7): the instance goes, and the mission is an offer again
//   QG6AI  two sessions (I9, MP-10): two characters at the board, each with its own seed and offers, instance, progress, and town entry; the
//        kills of one do not count for the other
//   QG6AK no mission data: the open of a board is refused (3743 result 2), and no provider state goes out
//   QG6AL the support manifest (I8): a mission template that a board of the data gives with its effective objective (the kill of the row,
//         the return to the provider, the success event of the row) is supported; a template whose board missions have an override with no
//         rule is not

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
#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"
#include "support/quest_fixture.h"
#include "support/station_fixture.h"
#include "support/test_progression.h"
#include "world/missions.h"
#include "world/quest_support.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

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

std::uint32_t crc(const std::string& text) {
    return gamedata::quest_crc(text);
}

/** The quest data: the base document of the quest cases and the made-up template of the town kill missions (CM_bounty_kill_X_enemies). */
json mission_quests() {
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

json mission_row(const char* id, std::uint32_t row, const char* objective, json cells, json territories = {{"ids", json::array()}, {"exclude", false}}) {
    cells["ObjectiveID"] = objective;
    return {{"id", id}, {"crc", crc(id)}, {"table", "town"}, {"row", row}, {"objective", objective}, {"objectiveCrc", crc(objective)},
            {"territories", territories}, {"cells", cells}};
}

/** The mission data: two kill rows of one template (the Kill bucket), a gather row with a POI override (the Gather bucket), the project. */
json mission_document() {
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
                {"POITagOverride", "test_place"}, {"TaskHaveAndReturnItemsOverride", "TestOre"}, {"SuccessGameEventIdOverride", "Test_CMission_Wolf"}};
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
gamedata::ProgressionData mission_progression() {
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
gamedata::InteractionObject board_object(const std::string& key, std::uint64_t group, const state::WorldPosition& place, std::uint8_t claim) {
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
bool set_boards(gamedata::InteractionData& data) {
    DataParts base = data_parts();
    base.options.push_back(option_row("OpenMissionBoardOption", 51, true));
    base.objects.push_back(board_object(kBoardKey, kBoardGroup, kBoardPlace, 0x65));
    base.objects.push_back(board_object(kOtherBoardKey, kOtherBoardGroup, kOtherBoardPlace, 0x66));
    return data.set(std::move(base.options), std::move(base.achievements), std::move(base.objects));
}

/** @return The block of the open of a board, as the client makes it: the client id, the target of the bound, the option entity. */
InteractionBlock board_block(const gamedata::InteractionData& data, const std::string& key) {
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
std::string territory_bounds() {
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
std::uint32_t reference_seed(const std::string& text, std::uint16_t territory, std::uint64_t serverNs, std::int32_t interval = 30) {
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
std::uint32_t reference_index(std::uint32_t project, std::uint32_t seed, std::uint32_t count) {
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
std::optional<Fragment> provider_state(const FakeClient& client, std::size_t from = 0) {
    const std::vector<Fragment> found = questfix::own_fragments(client, kObjectiveInteractorSlot, kObjectiveInteractorType, from);
    return found.empty() ? std::nullopt : std::optional<Fragment>(found.back());
}

/** The expected provider state bytes (an independent writer of the form of OFFERS.md 1.3 and of research mission-boards.md 1.1). */
struct TownEntry {
    std::uint16_t territory{0};
    std::uint32_t seed{0};
    std::vector<std::uint32_t> unavailable;
};

Bytes expected_provider(const std::array<std::uint8_t, 16>& provider, std::uint64_t expiration, std::uint64_t sequence,
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

std::vector<MissionElement> mission_elements(const Fragment& fragment) {
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

std::optional<questfix::ObjectivesState> last_objectives(const FakeClient& client) {
    const std::vector<Fragment> found = questfix::own_fragments(client, questfix::kObjectivesSlot, questfix::kObjectivesType);
    return found.empty() ? std::nullopt : std::optional<questfix::ObjectivesState>(questfix::decode_objectives(found.back()));
}

std::optional<Fragment> last_objectives_fragment(const FakeClient& client) {
    const std::vector<Fragment> found = questfix::own_fragments(client, questfix::kObjectivesSlot, questfix::kObjectivesType);
    return found.empty() ? std::nullopt : std::optional<Fragment>(found.back());
}

const questfix::ObjectivesState::Task* task_of(const questfix::ObjectivesState& state, std::uint64_t instance, std::uint8_t index) {
    for (const auto& task : state.tasks) {
        if (task.instance == instance && task.index == index) {
            return &task;
        }
    }
    return nullptr;
}

/** QG6A-TOOL: the check of the data tool. */
void tool_check(const Options& options) {
    const std::filesystem::path work = options.workDirectory / "missions-tool";
    std::filesystem::create_directories(work);
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_missions_tool.py";
    const std::string command = "python3 -B \"" + script.string() + "\" \"" + work.string() + "\" > \"" + (work / "missions-tool.log").string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    check(status == 0, "the check of the mission data tool (check_missions_tool.py): status 0 (log: work folder missions-tool/missions-tool.log)");
}

/** QG6AA: the mission data and the binding of the quest data. */
void loader_and_binding(const Options&) {
    gamedata::MissionData data;
    check(data.load_text(mission_document().dump()), "the mission data loads: " + data.error());
    const gamedata::MissionRow* wolf = data.mission(crc(kWolf));
    check(wolf != nullptr && wolf->table == gamedata::MissionTable::town && wolf->objectiveCrc == crc(kTemplate) && wolf->killCategory == "Wolf"
              && wolf->killQuantity == 5u && wolf->successEvent == "Test_CMission_Wolf" && wolf->excludeTerritories
              && wolf->territories == std::vector<std::uint16_t>{16} && wolf->projectCrc == crc(kProject) && wolf->overrides.empty()
              && wolf->timerSeconds == 3600.0,
          "the wolf row: the template, the kill override (Wolf, 5), the success event, the rule !16, the project, no override with no rule");
    const gamedata::MissionRow* poi = data.mission(kPoi);
    check(poi != nullptr && poi->overrides.size() == 2, "the gather row has two override cells with no rule (POITagOverride, the item)");
    check(data.town_buckets().size() == 2 && data.town_buckets()[0].key == 4 && data.town_buckets()[0].missions.size() == 2
              && data.town_buckets()[1].key == 1,
          "the town buckets in the order of the file: Kill (the wolf, the boar), Gather");
    check(data.game_event("test_cmission_wolf") != nullptr && data.game_event("Test_CMission_Wolf")->experience == 110
              && data.game_event("Test_CMission_Wolf")->coin == 135 && data.game_event("Test_CMission_Wolf")->standing == 350,
          "the success event of the mission: 110 experience, 135 coin units, 350 standing");
    const gamedata::MissionProject* project = data.project(crc(kProject));
    check(project != nullptr && project->type == "AlwaysAvailable" && project->level == 1, "the project: AlwaysAvailable, level 1");
    // The refusals of a file.
    json wrongCrc = mission_document();
    wrongCrc["missions"][0]["crc"] = 1;
    json badBucket = mission_document();
    badBucket["townBuckets"][0]["missions"].push_back(12345);
    json twoKeys = mission_document();
    twoKeys["townBuckets"][1]["key"] = 4;
    json badTerritory = mission_document();
    badTerritory["missions"][0]["territories"]["ids"] = json::array({0});
    json badFormat = mission_document();
    badFormat["format"] = 2;
    gamedata::MissionData refused;
    check(!refused.load_text(wrongCrc.dump()) && !refused.load_text(badBucket.dump()) && !refused.load_text(twoKeys.dump())
              && !refused.load_text(badTerritory.dump()) && !refused.load_text(badFormat.dump()) && !refused.loaded() && refused.missions().empty(),
          "a CRC that is not the CRC of the id, a bucket with an unknown mission, two buckets of one key, a territory 0, and another format "
          "refuse the file as a whole");
    check(!refused.load("/nonexistent/mission-data.json") && !refused.loaded(), "no file: no mission data");
    // The binding of the quest data.
    gamedata::QuestData quests;
    check(quests.load_text(mission_quests().dump()), "the quest data loads");
    const std::size_t tasks = quests.tasks().size();
    quests.bind_missions(data);
    const gamedata::MissionBinding* bound = quests.mission(crc(kWolf));
    const gamedata::QuestObjective* base = quests.objective(kTemplate);
    check(bound != nullptr && bound->refusals.empty() && bound->objective != nullptr && bound->objective != base && bound->templateObjective == base
              && bound->objective->crc == base->crc && bound->objective->nodes[0].target == 5 && base->nodes[0].target == 1
              && quests.task(bound->objective->nodes[0].task).targets == std::vector<std::string>{"wolf"}
              && quests.task(base->nodes[0].task).targets.empty() && bound->objective->nodes[1].task == base->nodes[1].task
              && bound->objective->nodes[2].target == 2,
          "the wolf: an effective objective of the template (the kill leaf: category wolf, target 5; the return task and the container of the "
          "template); the template keeps its values");
    check(quests.objective_by_crc(base->crc, crc(kWolf)) == bound->objective && quests.objective_by_crc(base->crc, 0) == base
              && quests.objective_by_crc(base->crc, crc(kBoar)) != bound->objective
              && quests.objective_by_crc(base->crc, crc(kBoar))->nodes[0].target == 8,
          "an instance finds the effective objective of its mission; a quest (mission 0) the template; two missions of one template differ");
    const gamedata::MissionBinding* gather = quests.mission(crc(kPoi));
    check(gather != nullptr && gather->refusals.size() == 2 && gather->refusals[0].find("POITagOverride") != std::string::npos,
          "the gather row is refused: POITagOverride (a common task value) and an item override with an item task have no rule yet");
    check(quests.game_event("test_cmission_wolf") != nullptr && quests.game_event("Test_Bounty_Template")->experience == 999,
          "the quest data gets the events of the missions it does not have, and keeps its own");
    // A second binding replaces the first one (no task copies of the first stay); a custom row keeps the template; a missing template.
    json custom = mission_document();
    custom["missions"][0]["cells"]["IsCustomObjective"] = "1";
    custom["missions"][1]["objective"] = "Test_No_Template";
    custom["missions"][1]["objectiveCrc"] = crc("Test_No_Template");
    custom["missions"][1]["cells"]["ObjectiveID"] = "Test_No_Template";
    gamedata::MissionData second;
    check(second.load_text(custom.dump()), "the custom file loads");
    quests.bind_missions(second);
    check(quests.mission(crc(kWolf))->objective == base && quests.mission(crc(kWolf))->refusals.empty() && quests.tasks().size() == tasks
              && quests.mission(crc(kBoar))->objective == nullptr && !quests.mission(crc(kBoar))->refusals.empty(),
          "a custom row runs the template (IsCustomObjective 1, row +0x144); a missing template is refused; a new binding drops the copies");
    gamedata::MissionData none;
    quests.bind_missions(none);
    check(quests.mission(crc(kWolf)) == nullptr && quests.mission_count() == 0 && quests.tasks().size() == tasks,
          "no mission data: no binding");
}

/** QG6AB: the seed and the selection of the client. */
void seed_rule(const Options&) {
    // The vectors of the static read (OFFERS.md section 11): the empty text, territory 6, the minutes 465 to 494; a made-up character id,
    // the minutes 1605 to 1634.
    const std::uint64_t first = 465ULL * kMinuteNs;
    const std::uint64_t last = 494ULL * kMinuteNs + 59'000'000'000ULL;
    check(world::town_seed("", 6, first, 30.0F, 0) == 2216830219U && world::town_seed("", 6, last, 30.0F, 0) == 2216830219U
              && world::town_seed("", 6, 495ULL * kMinuteNs, 30.0F, 0) != 2216830219U
              && world::town_seed("", 6, 464ULL * kMinuteNs, 30.0F, 0) != 2216830219U,
          "the seed of the empty text: 2216830219 in the minutes 465 to 494 (the half-interval phase of 0x39D2A70), another value before and after");
    check(world::town_seed("qg6a-character-001", 6, 1605ULL * kMinuteNs, 30.0F, 0) == 601472566U
              && world::town_seed("qg6a-character-001", 6, 1634ULL * kMinuteNs, 30.0F, 0) == 601472566U,
          "the seed of a made-up character id: 601472566 in the minutes 1605 to 1634");
    bool same = true;
    for (std::uint64_t minute = 0; minute < 4000; minute += 7) {
        same = same && world::town_seed(kCharacterA, 6, minute * kMinuteNs + 5'000'000'000ULL, 30.0F, 0)
                           == reference_seed(kCharacterA, 6, minute * kMinuteNs + 5'000'000'000ULL);
    }
    check(same, "the seed equals the reference of the algorithm of the static read (a character id, 572 minutes)");
    check(world::town_seed("", 6, first, 30.0F, 7) == 2216830226U, "the seed addition of the provider adds with the u32 wrap");
    check(world::town_seed("", 6, first, 0.5F, 0) == world::town_seed("", 6, first, 1.0F, 0)
              && world::town_seed("", 6, first, 30.9F, 0) == world::town_seed("", 6, first, 30.0F, 0),
          "the interval: below 1 is 1, a fraction is truncated (MAXSS, CVTTSS2SI)");
    // GetSecondsToNextSeed (0x4957F20): 60 * (P - ((m + P/2) % P)) - s; a full interval at a boundary.
    check(world::seconds_to_next_seed(15ULL * kMinuteNs, 30.0F) == 1800 && world::seconds_to_next_seed(15ULL * kMinuteNs + 10'000'000'000ULL, 30.0F) == 1790
              && world::seconds_to_next_seed(44ULL * kMinuteNs + 59'000'000'000ULL, 30.0F) == 1
              && world::next_seed_ns(16ULL * kMinuteNs, 30.0F) == 45ULL * kMinuteNs && world::next_seed_ns(45ULL * kMinuteNs, 30.0F) == 75ULL * kMinuteNs,
          "the seconds to the next seed: 1800 at the boundary of the minute 15, 1790 ten seconds later, 1 at 44:59; the next seed of the "
          "minute 16 at the minute 45");
    // The LCG of the selection: the index 51 of 53 Kill candidates for CommunityProjectId with the seed of the empty text (the static read).
    check(reference_index(0x2591C412U, 2216830219U, 53) == 51, "control: the reference LCG gives the index 51 of the static read");
    Boards shop;
    shop.start();
    world::Missions& missions = shop.missions();
    world::TownInputs inputs;
    inputs.seedText = "";
    inputs.territory = 6;
    inputs.serverNs = first;
    inputs.projects = {crc(kProject)};
    const world::TownOffers offers = missions.town_offers(inputs, nullptr, nullptr);
    check(offers.seed == 2216830219U && offers.offers.size() == 2 && offers.offers[0].bucket == 4 && offers.offers[0].candidates == 2
              && offers.offers[0].mission == (reference_index(crc(kProject), 2216830219U, 2) == 0 ? crc(kWolf) : crc(kBoar))
              && offers.offers[1].bucket == 1 && offers.offers[1].mission == crc(kPoi) && offers.offers[1].candidates == 1,
          "the offers of the made-up data: one candidate of the Kill bucket (two candidates, the index of the reference LCG), then the one "
          "of the Gather bucket (the LCG advances only for a bucket with candidates)");
    world::MissionTownEntry entry{offers.seed, {offers.offers[0].mission}, {}};
    world::MissionTownEntry old{offers.seed + 1, {offers.offers[0].mission}, {}};
    check(!missions.town_offers(inputs, &entry, nullptr).offers[0].available && missions.town_offers(inputs, &old, nullptr).offers[0].available,
          "an unavailable mission of the stored seed is not available; of another seed it is (0x3A3684C)");
    inputs.territory = 16;
    check(missions.town_offers(inputs, nullptr, nullptr).offers.empty() || missions.town_offers(inputs, nullptr, nullptr).offers[0].bucket != 4,
          "the territory 16 has no Kill candidate (the rule !16 of both kill rows)");
}

/** QG6AC: the open of the board. */
void board_open(const Options&) {
    Boards shop;
    shop.start();
    world::Entity* board = shop.board();
    check(board != nullptr && board->object.territorySlot == 3 && board->object.territory == 6 && board->object.projects == std::vector<std::uint32_t>{crc(kProject)},
          "the board is a level object that the client makes: its entity has the actor of the client id, its TerritoryInterface slot, the "
          "territory 6, its project");
    const std::uint32_t interest = stations::interest_of(shop.clientA, board->actor.bytes);
    std::optional<Fragment> territory;
    for (const Fragment& fragment : stations::fragments_for(shop.clientA, interest)) {
        note("board fragment slot " + std::to_string(fragment.slot) + " type " + std::to_string(fragment.type) + " fields "
             + std::to_string(fragment.fields.size()));
        if (fragment.slot == kTerritoryInterfaceSlot && fragment.type == kTerritoryInterfaceType) {
            territory = fragment;
        }
    }
    server::hub::Writer progression;
    progression.size(1);
    progression.u8(6);
    progression.u32(crc(kProject));
    progression.u64(0);
    progression.u32(0);
    progression.size(0);
    progression.u32(0);
    progression.u64(0);
    progression.u32(0);
    progression.size(0);
    check(interest != 0 && territory && territory->fields.size() == 1 && territory->fields.contains({1, 0})
              && Bytes(territory->fields.at({1, 0}).begin(), territory->fields.at({1, 0}).end()) == progression.data(),
          "the record of the board has the TerritoryInterface state: group 1 field 0 with the always available project (key 6), the 38 "
          "bytes of the static read");
    check(!provider_state(shop.clientA), "no provider state before the first open");
    const std::size_t before = shop.clientA.got.size();
    shop.open(shop.clientA);
    const auto answers = replies(shop.clientA, before);
    world::Entity* a = shop.entity(kCharacterA);
    const std::uint64_t now = shop.server_ns();
    const std::uint32_t seed = world::town_seed(kCharacterA, 6, now, 30.0F, 0);
    check(!answers.empty() && answers.back().first == 51 && answers.back().second == 1, "the open (action 51) gets 3743 result 1");
    check(board->object.providerIdentity == *shop.host.interactions.objects()[shop.host.interactions.boards()[0]].claim
              && board->object.providerIdentity != board->actor.bytes,
          "the provider identity of the board is the claim of the data (not the actor of the board)");
    check(a->missionBoard.opened && a->missionBoard.provider == board->object.providerIdentity && a->missionBoard.providerKey == kBoardKey
              && a->missionBoard.territory == 6 && a->missionBoard.expirationNs == world::next_seed_ns(now, 30.0F)
              && a->missionBoard.towns.size() == 1 && a->missionBoard.towns.at(6).seed == seed,
          "the board state of A: the actor of the board, its key, the territory 6, the expiration at the next seed, the town entry of the seed");
    const std::optional<Fragment> state = provider_state(shop.clientA, before);
    server::hub::Writer value;
    check(state && state->fields.size() == 4, "A gets the provider state (slot 10, type 3829) with its four fields");
    if (state) {
        const auto sequence = a->changed.at(world::ComponentKey::objectiveInteractor);
        const Bytes encoded = server::rep::encode_objective_interactor_state(*a, server::rep::View::owner, shop.host.clock);
        check(encoded == expected_provider(board->object.providerIdentity, world::next_seed_ns(now, 30.0F), sequence, {{6, seed, {}}}),
              "the bytes of the provider state: the 16 bytes of the actor of the board, the expiration (server clock), the empty mission "
              "vector, the town entry (territory 6, the seed of the character id, no unavailable mission, the project of the seed)");
    }
    check(provider_state(shop.clientB, before) == std::nullopt, "B does not get the provider state of A (owner only)");
    check(a->serviceGrant && a->serviceGrant->kind == "missionBoard" && a->serviceGrant->revision == seed, "A has the service of the board");
}

/** QG6AD: the accept and its refusals. */
void accept_and_refusals(const Options&) {
    Boards shop;
    shop.start();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf); }), "setup: a seed window in which the Kill offer of A is the wolf");
    const auto none = [&] { return a->quests.active.empty(); };
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(none() && shop.missions().counts().acceptRefusals == 1, "an accept with no open of a board: refused (no provider)");
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kBoar));
    shop.accept(shop.clientA, kCharacterA, 0x12345678U);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf), 7);
    check(none() && shop.missions().counts().acceptRefusals == 4,
          "the boar (not an offer of this seed), a forged id, and the wolf with a project argument 7 (not the project of the offer): refused, "
          "no instance");
    // The range of the service: A walks 10 m away (an accepted position), then the accept is refused; the service ends.
    shop.walk_a(193.0F);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(none() && !a->serviceGrant, "an accept 7 m from the board: refused, and the service ended (the range of its bound)");
    shop.walk_a(200.0F);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(none(), "back at the board with no new open: refused (a walk back does not give the service back)");
    // An expired selection: the next window in which the offer of A is not the wolf.
    shop.open(shop.clientA);
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) != crc(kWolf); }), "setup: a later window in which the wolf is not the offer of A");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(none(), "an accept of an offer of an earlier seed (an expired selection): refused");
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf); }), "setup: the wolf is the offer again");
    shop.open(shop.clientA);
    const std::size_t before = shop.clientA.got.size();
    // The card of the town screen sends the project of the card as the second argument (townproject_activeproject.luac OnTaskStart).
    shop.accept(shop.clientA, kCharacterA, crc(kWolf), crc(kProject));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kWolf));
    world::Entity* board = shop.board();
    check(quest != nullptr && quest->objectiveCrc == crc(kTemplate) && quest->communityGoal && quest->provider == kBoardKey
              && quest->destination == board->object.providerIdentity && quest->originTerritory == 6 && quest->source == "board"
              && quest->tasks.size() == 3 && quest->tasks[0].state == world::quest_state::kActive && quest->tasks[1].state == world::quest_state::kNew,
          "the accept of the offer: an instance of the template with the mission, the community flag, the provider, the destination (the "
          "claim of the board), the origin 6, the kill task Active");
    const std::optional<Fragment> objectives = last_objectives_fragment(shop.clientA);
    const std::vector<MissionElement> elements = objectives ? mission_elements(*objectives) : std::vector<MissionElement>{};
    const auto element = std::find_if(elements.begin(), elements.end(), [&](const MissionElement& value) { return value.mission == crc(kWolf); });
    check(element != elements.end() && element->objective == crc(kTemplate) && element->instance == quest->instance
              && element->destination == board->object.providerIdentity && element->origin == 6 && element->available && element->community,
          "the Objectives state of A (slot 22, group 1 field 3): the template CRC, the new instance, the missionId, the destination, the "
          "origin territory 6, available, isCommunityGoal 1");
    const std::optional<questfix::ObjectivesState> decoded = last_objectives(shop.clientA);
    const auto* kill = decoded ? task_of(*decoded, quest->instance, 0) : nullptr;
    check(kill != nullptr && kill->crc == crc("Test_M_Kill") && kill->progress == 0 && kill->state == world::quest_state::kActive
              && shop.clientA.got.size() > before,
          "the task elements: the kill task with its CRC, progress 0, Active");
    const std::vector<state::QuestRow> rows = shop.host.store.list_quests(kCharacterA);
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const state::QuestRow& value) { return value.instance == quest->instance; });
    check(row != rows.end() && row->mission == crc(kWolf) && row->communityGoal && row->provider == kBoardKey && row->source == "board",
          "the row of the store has the mission, the community flag, and the provider (schema 19)");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(a->quests.active.size() == 1, "a second accept of the same mission: refused (alreadyActive)");
    // An unsupported mission: the gather row is an offer of the Gather bucket, but its overrides have no rule.
    shop.accept(shop.clientA, kCharacterA, crc(kPoi));
    check(a->quests.active.size() == 1 && shop.mission_of(kCharacterA, crc(kPoi)) == nullptr,
          "the accept of the gather offer (POITagOverride and an item override with no rule): refused (unsupported)");
}

/** The common start of QG6AE, QG6AF, QG6AH: A accepted the wolf at the town board. */
std::uint64_t accepted(Boards& shop) {
    shop.start();
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf); }), "setup: a seed window in which the Kill offer of A is the wolf");
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kWolf));
    check(quest != nullptr, "setup: A accepted the wolf");
    return quest != nullptr ? quest->instance : 0;
}

/** QG6AE: the progress. */
void progress(const Options&) {
    Boards shop;
    const std::uint64_t instance = accepted(shop);
    shop.kill(kCharacterA, "wolf", 4);
    shop.kill(kCharacterA, "boar", 2);
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kWolf));
    std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    const auto* kill = state ? task_of(*state, instance, 0) : nullptr;
    check(quest != nullptr && quest->tasks[0].progress == 4 && quest->tasks[0].state == world::quest_state::kActive && kill != nullptr
              && kill->progress == 4,
          "four wolf kills: the kill task has 4 of 5 (the override), and the client gets 4; two boar kills count nothing");
    shop.kill(kCharacterA, "wolf", 1);
    quest = shop.mission_of(kCharacterA, crc(kWolf));
    check(quest->tasks[0].state == world::quest_state::kComplete && quest->tasks[0].progress == 5 && quest->tasks[1].state == world::quest_state::kActive
              && !shop.host.world.quests().ready(*quest),
          "the fifth kill: the kill task is complete, the return task is Active, the mission is not ready");
    // The return task: it stays Active at the open of a board (the client offers the completion only while the container has its child
    // count minus one: DESTINATION.md section 3); a hand-in at another board does not complete it; the hand-in at its provider does.
    shop.travel_a(state::WorldPosition{100.0F, 259.0F, 30.0F});
    shop.open(shop.clientA, kOtherBoardKey);
    shop.hand_in(shop.clientA, kCharacterA, instance);
    quest = shop.mission_of(kCharacterA, crc(kWolf));
    check(shop.entity(kCharacterA)->missionBoard.providerKey == kOtherBoardKey && quest != nullptr
              && quest->tasks[1].state == world::quest_state::kActive,
          "the open of another board (territory 7) and a hand-in there: the return task stays Active, the mission stays");
    shop.travel_a(kHere);
    shop.open(shop.clientA);
    quest = shop.mission_of(kCharacterA, crc(kWolf));
    state = last_objectives(shop.clientA);
    const auto* back = state ? task_of(*state, instance, 1) : nullptr;
    const auto* container = state ? task_of(*state, instance, 2) : nullptr;
    check(quest != nullptr && quest->tasks[1].state == world::quest_state::kActive && back != nullptr
              && back->state == world::quest_state::kActive && container != nullptr && container->progress == 1,
          "the open of the provider board: the return task stays Active, and the container has 1 of its 2 children (the ready check of the "
          "client: the child count minus one)");
    const std::uint32_t experience = shop.entity(kCharacterA)->progression.experience;
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && shop.missions().counts().returns == 1
              && shop.entity(kCharacterA)->progression.experience - experience == 110,
          "the hand-in 2461 at the provider board: the return task completes (TaskInteract ReturnToGiver), then the hand-in pays");
}

bool run_sql(const std::filesystem::path& path, const char* text) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.string().c_str(), &database) == SQLITE_OK && sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

/** QG6AF: the hand-in. */
void hand_in(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "mission-hand-in.db";
    std::filesystem::remove(path);
    Boards shop(path.string());
    const std::uint64_t instance = accepted(shop);
    world::Entity* a = shop.entity(kCharacterA);
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) != nullptr && shop.missions().counts().handInRefusals == 1,
          "a hand-in of a mission that is not ready: refused, the instance stays");
    shop.kill(kCharacterA, "wolf", 5);
    // A forged instance and a hand-in with the service of another board change nothing.
    shop.hand_in(shop.clientA, kCharacterA, 0x77777777ULL);
    shop.travel_a(state::WorldPosition{100.0F, 259.0F, 30.0F});
    shop.open(shop.clientA, kOtherBoardKey);
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) != nullptr && shop.missions().counts().handInRefusals == 3,
          "a forged instance and a hand-in at another board: refused, the instance stays");
    shop.travel_a(kHere);
    shop.open(shop.clientA);
    const std::uint32_t experience = a->progression.experience;
    const std::uint64_t coin = a->wallet.coin;
    const std::uint32_t standing = world::Skills::standing(a->skills, 6).points;
    const std::uint32_t seed = world::town_seed(kCharacterA, 6, shop.server_ns(), 30.0F, 0);
    const std::size_t before = shop.clientA.got.size();
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && a->progression.experience - experience == 110 && a->wallet.coin - coin == 135
              && world::Skills::standing(a->skills, 6).points - standing == 350,
          "the hand-in at the provider: the instance goes; 110 experience, 135 coin units, 350 standing of the territory 6 (the success "
          "event of the mission, not the 999 of the template)");
    const std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    check(state && state->hasCompleted && std::find(state->completed.begin(), state->completed.end(), instance) != state->completed.end()
              && std::none_of(state->active.begin(), state->active.end(), [&](const auto& value) { return value.second == instance; }),
          "the Objectives state: the instance is in the completed notice and not active");
    check(a->missionBoard.towns.at(6).seed == seed && a->missionBoard.towns.at(6).unavailable == std::vector<std::uint32_t>{crc(kWolf)},
          "the town entry: the wolf is unavailable for the current seed");
    const std::optional<Fragment> provider = provider_state(shop.clientA, before);
    check(provider && server::rep::encode_objective_interactor_state(*a, server::rep::View::owner, shop.host.clock)
                          == expected_provider(shop.board()->object.providerIdentity, a->missionBoard.expirationNs,
                                               a->changed.at(world::ComponentKey::objectiveInteractor),
                                               {{6, seed, {crc(kWolf)}}, {7, a->missionBoard.towns.at(7).seed, {}}}),
          "the provider state after it: the town entry of the territory 6 with the seed, the unavailable wolf, and the project of the "
          "seed; the entry of the territory 7 of the other board stays");
    const std::vector<state::MissionOfferRow> rows = shop.host.store.list_mission_offers(kCharacterA);
    check(rows.size() == 1 && rows[0].territory == 6 && rows[0].seed == seed && rows[0].unavailable == std::vector<std::uint32_t>{crc(kWolf)},
          "the row of the town entry is in the store");
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(a->progression.experience - experience == 110 && a->wallet.coin - coin == 135, "a second 2461 of the instance pays nothing");
    const world::TownOffers offers = shop.missions().offers_of(*a, 6);
    check(!offers.offers.empty() && offers.offers[0].mission == crc(kWolf) && !offers.offers[0].available, "the wolf is an unavailable offer now");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr, "an accept of the unavailable wolf in this seed: refused");
    // A refused store write of the town entry changes nothing (one transaction with the rewards).
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf) && world::town_seed(kCharacterA, 6, shop.server_ns(), 30.0F, 0) != seed; }),
          "setup: a later seed in which the wolf is the offer again");
    shop.open(shop.clientA);
    const world::TownOffers again = shop.missions().offers_of(*a, 6);
    check(!again.offers.empty() && again.offers[0].available, "at a new seed the wolf is available again (the client clears the exclusions)");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    const world::QuestInstance* second = shop.mission_of(kCharacterA, crc(kWolf));
    check(second != nullptr && second->instance != instance, "a new accept of the wolf at the new seed: a new instance");
    shop.kill(kCharacterA, "wolf", 5);
    shop.open(shop.clientA);
    const std::uint32_t experience2 = a->progression.experience;
    check(run_sql(path, "CREATE TRIGGER no_offer BEFORE INSERT ON character_mission_offers BEGIN SELECT RAISE(ABORT, 'test'); END; "
                        "CREATE TRIGGER no_offer_update BEFORE UPDATE ON character_mission_offers BEGIN SELECT RAISE(ABORT, 'test'); END;"),
          "setup: the store refuses the row of the town entry");
    shop.hand_in(shop.clientA, kCharacterA, second->instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) != nullptr && a->progression.experience == experience2,
          "a refused town entry row: the hand-in changes nothing (no reward, the instance stays: one transaction)");
}

/** QG6AG: a restart keeps the mission; schema 19. */
void restart_keeps(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "mission-restart.db";
    std::filesystem::remove(path);
    std::uint64_t instance = 0;
    std::array<std::uint8_t, 16> firstActor{};
    {
        Boards shop(path.string());
        instance = accepted(shop);
        shop.kill(kCharacterA, "wolf", 3);
        firstActor = shop.board()->object.providerIdentity;
    }
    Boards again(path.string(), true);
    again.start();
    const world::QuestInstance* quest = again.mission_of(kCharacterA, crc(kWolf));
    world::Entity* board = again.board();
    note("restart: quest " + std::to_string(quest != nullptr) + " board " + std::to_string(board != nullptr)
         + (quest != nullptr && board != nullptr ? " destination equal " + std::to_string(quest->destination == board->object.providerIdentity)
                                                       + " same actor " + std::to_string(board->object.providerIdentity == firstActor)
                                                 : std::string()));
    check(quest != nullptr && quest->instance == instance && quest->tasks[0].progress == 3 && quest->provider == kBoardKey && quest->communityGoal
              && board != nullptr && quest->destination == board->object.providerIdentity,
          "after a restart: the mission with its instance, its progress 3, its provider; its destination is the claim of the board (the "
          "board entity sets it)");
    const std::optional<Fragment> objectives = last_objectives_fragment(again.clientA);
    const std::vector<MissionElement> elements = objectives ? mission_elements(*objectives) : std::vector<MissionElement>{};
    check(!elements.empty() && elements.back().mission == crc(kWolf) && elements.back().destination == board->object.providerIdentity,
          "the client gets the mission with the destination of the new board");
    again.kill(kCharacterA, "wolf", 2);
    again.open(again.clientA);
    again.hand_in(again.clientA, kCharacterA, instance);
    check(again.mission_of(kCharacterA, crc(kWolf)) == nullptr && again.missions().counts().handIns == 1,
          "the progress goes on: two kills, then the hand-in at the provider (the return and the payment)");
    // Schema 19 from a database of schema 18: the quest rows keep their values, the new columns have their defaults.
    const std::filesystem::path old = options.workDirectory / "mission-schema18.db";
    std::filesystem::remove(old);
    {
        state::Store store;
        check(store.open(old.string()), "setup: a database");
    }
    sqlite3* database = nullptr;
    bool ok = sqlite3_open(old.string().c_str(), &database) == SQLITE_OK
              && sqlite3_exec(database, "DROP TABLE character_mission_offers; CREATE TABLE q(x); DROP TABLE q; PRAGMA user_version=18;", nullptr,
                              nullptr, nullptr) == SQLITE_OK;
    // A database of schema 18 has no mission columns: a copy of the table of schema 15 with one row.
    ok = ok && sqlite3_exec(database,
                            "ALTER TABLE character_quests RENAME TO q19; CREATE TABLE character_quests(character_id TEXT NOT NULL, instance_id BLOB "
                            "NOT NULL, objective TEXT NOT NULL, objective_crc INTEGER NOT NULL, accept_ns INTEGER NOT NULL, origin_territory "
                            "INTEGER NOT NULL DEFAULT 0, parent_instance BLOB, source TEXT NOT NULL DEFAULT '', PRIMARY KEY(character_id, "
                            "instance_id)); DROP TABLE q19; INSERT INTO character_quests VALUES('c1', x'0000000000000101', 'Test_Old', 7, 1, 6, NULL, "
                            "'offer');",
                            nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    state::Store migrated;
    const bool opened = ok && migrated.open(old.string());
    const std::vector<state::QuestRow> rows = opened ? migrated.list_quests("c1") : std::vector<state::QuestRow>{};
    check(opened && rows.size() == 1 && rows[0].objective == "Test_Old" && rows[0].originTerritory == 6 && rows[0].mission == 0
              && !rows[0].communityGoal && rows[0].provider.empty() && migrated.list_mission_offers("c1").empty(),
          "schema 18 to 19: the quest row keeps its values; mission 0, no community flag, no provider; the table of the town entries");
}

/** QG6AH: the abandon. */
void abandon(const Options&) {
    Boards shop;
    const std::uint64_t instance = accepted(shop);
    shop.kill(kCharacterA, "wolf", 2);
    shop.abandon(shop.clientA, kCharacterA, instance);
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && shop.host.store.list_quests(kCharacterA).empty(),
          "3836 with the instance: the mission goes (CanBeAbandoned 1 of the template), and its row");
    const std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    check(state && std::none_of(state->active.begin(), state->active.end(), [&](const auto& value) { return value.second == instance; }),
          "the Objectives state has no instance of it");
    const world::TownOffers offers = shop.missions().offers_of(*a, 6);
    check(!offers.offers.empty() && offers.offers[0].mission == crc(kWolf) && offers.offers[0].available,
          "the wolf is an available offer again (an abandon makes no mission unavailable)");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    const world::QuestInstance* again = shop.mission_of(kCharacterA, crc(kWolf));
    check(again != nullptr && again->instance != instance && again->tasks[0].progress == 0, "a new accept: a new instance with progress 0");
}

/**
 * The bytes of the LandClaimManager state of the world entity (the static read OWNER.md, "Smallest states", with versioned full vectors): the
 * field masks fc (fields 2 to 6), fd (7, 9 to 13), 0e (15 to 17), and for each claim the quiet values of a territory with no owner.
 */
Bytes expected_land_claims(const std::vector<std::pair<std::uint16_t, std::vector<std::uint32_t>>>& claims, std::uint64_t version) {
    server::hub::Writer out;
    const auto header = [&] {
        out.size(0);
        out.u8(1);
        out.size64(version);
        out.size(static_cast<std::uint32_t>(claims.size()));
    };
    const auto zeros = [&](std::size_t bytes) {
        header();
        out.zeros(claims.size() * bytes);
    };
    out.u8(0x01);
    out.u8(0xFC);
    header();
    for (const auto& [territory, projects] : claims) {
        out.u16(territory);
    }
    zeros(1);  // 3 the conflict faction
    zeros(8);  // 4 the conflict lottery end
    zeros(8);  // 5 the conflict start
    zeros(4);  // 6 the darkness threshold
    out.u8(0xFD);
    zeros(8);  // 7 the darkness cycle end
    zeros(1);  // 9, 10, 11 the influence of the three factions
    zeros(1);
    zeros(1);
    zeros(18); // 12 the governance
    zeros(8);  // 13 the influence race start
    out.u8(0x0E);
    header();  // 15 the progression summaries
    for (const auto& [territory, projects] : claims) {
        out.u8(0);
        out.size(static_cast<std::uint32_t>(projects.size()));
        for (const std::uint32_t project : projects) {
            out.u32(project);
            out.u8(6);
            out.u32(0);
        }
        out.u8(0);
    }
    zeros(63); // 16 the owners: no company (63 zero bytes on the wire)
    zeros(1);  // 17 the war declaration threshold faction
    return out.data();
}

/** QG6AM: the world entity and its LandClaimManager state. */
void world_entity(const Options&) {
    Boards shop;
    shop.start();
    const world::EntityId id = shop.host.world.world_entity_id();
    world::Entity* entity = shop.host.world.find(id);
    check(entity != nullptr && entity->object.world && entity->object.landClaims.size() == 2 && entity->object.landClaims[0].territory == 6
              && entity->object.landClaims[1].territory == 7
              && entity->object.landClaims[0].projects == std::vector<std::uint32_t>{crc(kProject)},
          "the world entity has one claim for the territory of each board (6 and 7, in order), with the always available project");
    if (entity == nullptr) {
        return;
    }
    const std::uint32_t interestA = stations::interest_of(shop.clientA, entity->actor.bytes);
    const std::uint32_t interestB = stations::interest_of(shop.clientB, entity->actor.bytes);
    check(interestA != 0 && interestB != 0, "each session observes the world entity (its record: the metadata state with its actor)");
    std::optional<Fragment> claims;
    for (const Fragment& fragment : stations::fragments_for(shop.clientA, interestA)) {
        if (fragment.slot == 2 && fragment.type == 3086) {
            claims = fragment;
        }
    }
    const Bytes encoded = server::rep::encode_land_claim_state(*entity, server::rep::View::observer, shop.host.clock);
    const std::uint64_t version = entity->changed.at(world::ComponentKey::landClaims);
    check(claims && claims->fields.size() == 14 && encoded == expected_land_claims({{6, {crc(kProject)}}, {7, {crc(kProject)}}}, version),
          "the LandClaimManager state (slot 2, type 3086): 14 fields, one element for each claim in each (the keys, the quiet values, the "
          "project summary, an owner with no company), no position");
    // A character far away keeps the world entity (it is in each interest table at each distance).
    shop.travel_a(state::WorldPosition{2000.0F, 2000.0F, 30.0F});
    const std::size_t after = shop.clientA.got.size();
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    bool stopped = false;
    for (std::size_t index = after; index < shop.clientA.got.size(); ++index) {
        if (shop.clientA.got[index].type == 8) {
            for (const std::uint32_t stop : read_bundle(shop.clientA.got[index].body).stops) {
                stopped = stopped || stop == interestA;
            }
        }
    }
    check(!stopped && shop.host.world.find(id) != nullptr, "2 km away: no stop for the world entity");
}

/** QG6AI: two sessions. */
void two_sessions(const Options&) {
    Boards shop;
    shop.start();
    check(shop.seek([&] { return shop.kill_offer(kCharacterA) == crc(kWolf) && shop.kill_offer(kCharacterB) == crc(kWolf); }),
          "setup: a seed window in which the Kill offer of A and of B is the wolf");
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(world::town_seed(kCharacterA, 6, shop.server_ns(), 30.0F, 0) != world::town_seed(kCharacterB, 6, shop.server_ns(), 30.0F, 0),
          "the seeds of A and B differ (the character id is the text of the seed): each has its own offers");
    shop.open(shop.clientA);
    shop.open(shop.clientB);
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    shop.accept(shop.clientB, kCharacterB, crc(kWolf));
    const world::QuestInstance* questA = shop.mission_of(kCharacterA, crc(kWolf));
    const world::QuestInstance* questB = shop.mission_of(kCharacterB, crc(kWolf));
    check(questA != nullptr && questB != nullptr && questA->instance != questB->instance, "A and B: two instances of the wolf");
    shop.kill(kCharacterA, "wolf", 5);
    questA = shop.mission_of(kCharacterA, crc(kWolf));
    questB = shop.mission_of(kCharacterB, crc(kWolf));
    check(questA->tasks[0].progress == 5 && questB->tasks[0].progress == 0, "the kills of A count for A only");
    shop.kill(kCharacterB, "wolf", 2);
    check(shop.mission_of(kCharacterB, crc(kWolf))->tasks[0].progress == 2 && shop.mission_of(kCharacterA, crc(kWolf))->tasks[0].progress == 5,
          "the kills of B count for B only");
    shop.open(shop.clientA);
    const std::uint32_t experienceB = b->progression.experience;
    shop.hand_in(shop.clientA, kCharacterA, questA->instance);
    check(shop.mission_of(kCharacterA, crc(kWolf)) == nullptr && shop.mission_of(kCharacterB, crc(kWolf)) != nullptr
              && b->progression.experience == experienceB && a->missionBoard.towns.at(6).unavailable.size() == 1
              && (b->missionBoard.towns.count(6) == 0 || b->missionBoard.towns.at(6).unavailable.empty()),
          "the hand-in of A: the instance of A goes, the town entry of A has the wolf; B keeps its mission, its experience, its entry");
    shop.hand_in(shop.clientB, kCharacterB, questA->instance);
    check(shop.mission_of(kCharacterB, crc(kWolf)) != nullptr && b->progression.experience == experienceB,
          "B cannot hand in the instance of A");
    const std::optional<questfix::ObjectivesState> stateB = last_objectives(shop.clientB);
    check(stateB && std::none_of(stateB->active.begin(), stateB->active.end(), [&](const auto& value) { return value.second == questA->instance; }),
          "the Objectives state of B has only its own instance");
    check(!provider_state(shop.clientA) || provider_state(shop.clientA)->fields.size() == 4, "control: A has its provider state");
}

/** QG6AK: no mission data. */
void no_data(const Options&) {
    Boards shop(":memory:", false, false);
    shop.start();
    const std::size_t before = shop.clientA.got.size();
    shop.open(shop.clientA);
    const auto answers = replies(shop.clientA, before);
    check(!answers.empty() && answers.back().second == 2 && !shop.entity(kCharacterA)->missionBoard.opened && !provider_state(shop.clientA),
          "no mission data: the open of the board is refused (3743 result 2); no provider state");
    shop.accept(shop.clientA, kCharacterA, crc(kWolf));
    check(shop.entity(kCharacterA)->quests.active.empty(), "an accept with no mission data: no instance");
}

/** QG6AL: the support manifest. */
void manifest_rows(const Options&) {
    Boards shop;
    // A wolf of the category Wolf at a spawn point 4 km away (the kill target of the effective objective of the wolf mission).
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/mission_wolf.dynamicslice";
    slice.asset.fill(0x31);
    slice.subId = 2;
    slice.root = 0x1000000000000031ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = 5;
    slice.vitalsSlot = 9;
    slice.slots = {{5, "ActionList", 11}, {9, "Vitals", 15}};
    gamedata::CreatureKind kind;
    kind.id = "Test_Mission_Wolf";
    kind.vitals = kind.id;
    kind.category = "Wolf";
    kind.vitalsId = 0x31112222;
    kind.categoryId = 0x31112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 1;
    kind.bodies = {{0, 100}};
    gamedata::SpawnPoint point;
    point.id = "0e1f0000-0000-4000-8000-000000000001";
    point.position = state::WorldPosition{4000.0F, 4000.0F, 30.0F};
    check(shop.host.creatures.set_full({slice}, {kind}, {point}, {{1, 100.0F, 100.0F, 50.0F}}), "setup: a wolf of the category Wolf");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const auto row_of = [&](const char* id) -> json {
        for (const char* list : {"objectives", "missionTemplates"}) {
            for (const json& row : manifest[list]) {
                if (row["id"] == id) {
                    return row;
                }
            }
        }
        return json();
    };
    const json bounty = row_of(kTemplate);
    note("QG6AL bounty: " + bounty.dump().substr(0, 900));
    check(!bounty.is_null() && bounty.contains("mission") && bounty["mission"]["board"] == kBoardKey && bounty["mission"]["territory"] == 6
              && std::any_of(bounty["grants"].begin(), bounty["grants"].end(),
                             [](const json& grant) { return grant["source"] == "board" && grant["supported"] == true; }),
          "the bounty template has the grant of the town board (a mission of the board, its territory 6)");
    const bool returns = std::any_of(bounty["tasks"].begin(), bounty["tasks"].end(), [](const json& task) {
        return task["id"] == "Test_CM_Return" && task["supported"] == true;
    });
    check(returns && bounty["rewards"]["experience"] == 110 && bounty["rewards"]["coin"] == 135 && bounty["rewards"]["standing"] == 350,
          "its return task goes to the provider board, and it pays the success event of the mission (110, 135, 350), not the template");
    check(bounty["supported"] == true && manifest["counts"]["supportedByKind"].value("communityGoal", 0) == 1,
          "the bounty template is supported with the kill of the row (wolf, 5): the counts have one supported community goal");
    const json gather = row_of("Test_CM_Gather");
    check(!gather.is_null() && gather["supported"] == false && gather.contains("mission")
              && std::any_of(gather["grants"].begin(), gather["grants"].end(),
                             [](const json& grant) { return grant["source"] == "board" && grant["supported"] == false; }),
          "the gather template: its board mission has an override with no rule: not supported");
    check(manifest["counts"]["boards"] == 2 && manifest["counts"]["boardMissions"] == 3, "the counts: 2 boards, 3 board missions of the data");
}

} // namespace

std::vector<Case> mission_board_cases() {
    return {{"QG6A-TOOL", "mission boards QG6a (I1): the check of the data tool (runs check_missions_tool.py)", tool_check},
            {"QG6AA", "mission boards QG6a (I1): the mission data, its refusals, and the binding of the quest data", loader_and_binding},
            {"QG6AB", "mission boards QG6a (I2): the seed and the selection of the client", seed_rule},
            {"QG6AC", "mission boards QG6a (I2): the board, its TerritoryInterface state, the open, and the provider state", board_open},
            {"QG6AD", "mission boards QG6a (I3): the accept 3819 and each refusal", accept_and_refusals},
            {"QG6AE", "mission boards QG6a (I4): the kill task of the override and the return task", progress},
            {"QG6AF", "mission boards QG6a (I5): the hand-in 2461, its rewards, one payment, the refusals, the unavailable mission", hand_in},
            {"QG6AG", "mission boards QG6a (I6): a restart keeps the mission, its progress, and its provider; schema 19", restart_keeps},
            {"QG6AH", "mission boards QG6a (I7): the abandon 3836", abandon},
            {"QG6AI", "mission boards QG6a (I9, MP-10): two characters at the board", two_sessions},
            {"QG6AK", "mission boards QG6a: no mission data", no_data},
            {"QG6AM", "mission boards QG6a (I2): the world entity and its LandClaimManager state (the town screen)", world_entity},
            {"QG6AL", "mission boards QG6a (I8): the support manifest of the mission templates of the boards", manifest_rows}};
}

} // namespace soulwarden::backend::tests

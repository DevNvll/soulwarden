// Cases for the creatures of the world (stage 3.4a): the creature data file, the bytes of a
// creature record, the entity table and the position index, the spawn point rule, the interest
// of a session, health and death as state changes, the commands, and the case with more than
// one session. All ids and names are made up: no value of a game file is here.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using nlohmann::json;
using world::CreatureError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-00000000000c";
constexpr const char* kSteamIdC = "76561202255233010";
constexpr const char* kTokenA = "token-of-the-start-of-A-0123456789abcdef";
constexpr float kHeight = 50.0F;
/** The place of the character of A in each case. */
constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
/** Slots of the two test slices: movement, vitals, marker. */
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kRabbitSlots[] = {2, 5, 17};
/** The death action of the wolf slice of the test: its index, and the index of its fragment. */
constexpr std::uint32_t kWolfDeathAction = 27;
constexpr std::int32_t kWolfDeathFragment = 4;
constexpr float kWolfHealth = 1690.875977F;

gamedata::CreatureSlice test_slice(const char* path, std::uint8_t mark, const std::uint32_t slots[3], float radius,
                                   std::uint32_t deathAction, std::int32_t deathFragment) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x1000000000000000ULL + mark;
    slice.interestRadius = radius;
    slice.movementSlot = slots[0];
    slice.vitalsSlot = slots[1];
    slice.markerSlot = slots[2];
    slice.slots = {{slots[0], "ActionList", 11}, {slots[1], "Vitals", 15}, {slots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}, {slice.root + 7, 0x0000000000000B02ULL, "Marker/Marker"}};
    for (std::uint32_t index = 0; index <= deathAction; ++index) {
        gamedata::CreatureSlice::Action action;
        action.name = index == deathAction ? "Death" : "Action_" + std::to_string(index);
        action.fragment = index == deathAction ? "r_Death" : "Fragment";
        action.fragmentIndex = index == deathAction ? deathFragment : static_cast<std::int32_t>(index);
        action.serverStarted = index == deathAction;
        slice.actions.push_back(std::move(action));
    }
    return slice;
}

gamedata::CreatureKind test_kind(const char* id, const char* name, std::uint32_t vitalsId, std::uint32_t categoryId, float factor,
                                 std::uint32_t level, std::vector<gamedata::CreatureBody> bodies) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = categoryId;
    kind.name = name;
    kind.vitalsName = name;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = factor;
    kind.level = level;
    kind.bodies = std::move(bodies);
    return kind;
}

gamedata::SpawnPoint test_point(const char* id, std::uint32_t kind, std::uint32_t level, float x, float y) {
    gamedata::SpawnPoint point;
    point.id = id;
    point.kind = kind;
    point.level = level;
    point.position = state::WorldPosition{x, y, kHeight};
    point.yaw = 90.0F;
    point.respawnMin = 360.0F;
    return point;
}

/** The creature data of the cases: a wolf, a rabbit, a kind with two slices, and a slice with no marker and no death action. */
bool set_creatures(ServerHost& host, std::vector<gamedata::SpawnPoint> points = {}) {
    std::vector<gamedata::CreatureSlice> slices{
        test_slice("slices/test/wolf.dynamicslice", 0x11, kWolfSlots, 100.0F, kWolfDeathAction, kWolfDeathFragment),
        test_slice("slices/test/rabbit.dynamicslice", 0x22, kRabbitSlots, 35.0F, 0, 2),
        test_slice("slices/test/plain.dynamicslice", 0x33, kWolfSlots, 60.0F, 3, 1),
    };
    slices[2].markerSlot = 0;
    slices[2].actions.clear();
    std::vector<gamedata::CreatureKind> kinds{
        test_kind("Test_Wolf", "Test Wolf", 0x11112222, 0x33334444, 1.0F, 18, {{0, 100}}),
        test_kind("Test_Rabbit", "Test Rabbit", 0x55556666, 0x77778888, 0.05F, 1, {{1, 100}}),
        test_kind("Test_Twin", "Test Twin", 0x9999AAAA, 0xBBBBCCCC, 2.0F, 18, {{0, 100}, {1, 100}}),
        test_kind("Test_Plain", "Test Plain", 0xDDDDEEEE, 0xFFFF0001, 1.0F, 18, {{2, 100}}),
    };
    return host.creatures.set(std::move(slices), std::move(kinds), std::move(points),
                              {{1, 337.5F}, {18, kWolfHealth}, {20, 2000.0F}});
}

std::vector<Bundle> bundles_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<Bundle> bundles;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == 8) {
            bundles.push_back(read_bundle(client.got[index].body));
        }
    }
    return bundles;
}

/** @return The records of the bundles that are not records of the own character (interest 1). */
std::vector<Record> other_records(const std::vector<Bundle>& bundles) {
    std::vector<Record> records;
    for (const Bundle& bundle : bundles) {
        for (const Record& record : bundle.records) {
            if (record.interest != 1) {
                records.push_back(record);
            }
        }
    }
    return records;
}

std::vector<std::uint32_t> stops_of(const std::vector<Bundle>& bundles) {
    std::vector<std::uint32_t> stops;
    for (const Bundle& bundle : bundles) {
        stops.insert(stops.end(), bundle.stops.begin(), bundle.stops.end());
    }
    return stops;
}

bool all_complete(const std::vector<Bundle>& bundles) {
    for (std::size_t index = 0; index < bundles.size(); ++index) {
        if (!bundles[index].complete || bundles[index].sequence != index + 1) {
            return false;
        }
    }
    return !bundles.empty();
}

std::string fields_text(const Fragment& fragment) {
    std::string text;
    for (const auto& [key, value] : fragment.fields) {
        text += "(" + std::to_string(key.first) + "," + std::to_string(key.second) + ") ";
    }
    return text;
}

/** @return The payload of a state bundle message: the records, with no bundle header. */
Bytes payload_of(const Sent& sent) {
    server::hub::Reader reader(sent.body);
    reader.u8();
    reader.size64();
    reader.u8();
    reader.u8();
    reader.u8();
    reader.u8();
    const std::uint32_t size = reader.size();
    const auto rest = std::span<const std::uint8_t>(sent.body).subspan(reader.position());
    return rest.size() == size ? Bytes(rest.begin(), rest.end()) : Bytes{};
}

/** @return The payloads of the bundles that a client got from the message `from` on and that have no record of its own character. */
std::vector<Bytes> creature_payloads(const FakeClient& client, std::size_t from) {
    std::vector<Bytes> payloads;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        if (!bundle.records.empty() && bundle.records.front().interest != 1) {
            payloads.push_back(payload_of(client.got[index]));
        }
    }
    return payloads;
}

std::string u64_hex(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

/** A client enters the world with the character of a player and sends its first camera position. */
void enter_with_camera(ServerHost& host, FakeClient& client, const Player& player, const state::WorldPosition& position) {
    host.enter(client, player.registration());
    host.datagram(client.peer, client.input_at(1, position.x, position.y, position.z));
    host.steps(60, {&client});
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

json file_document() {
    return json{
        {"format", 1},
        {"counts", {{"spawnPoints", 2}, {"kinds", 2}, {"slices", 2}}},
        {"levels", {{1, 337.5}, {18, 1690.875977}}},
        {"slices",
         {{{"path", "slices/test/wolf.dynamicslice"},
           {"asset", "11111111-1111-1111-1111-111111111111"},
           {"sub", 2},
           {"root", "1000000000000011"},
           {"interestRadius", 100.0},
           {"movement", 5},
           {"vitals", 9},
           {"marker", 15},
           {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
           {"components", json::array({json::array({"1000000000000011", "0000000000000A01", "Root/Vitals"})})},
           {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Sound", "", -1, false}, {"Death", "r_Death", 4, true}}}},
          {{"path", "slices/test/rabbit.dynamicslice"},
           {"asset", "22222222-2222-2222-2222-222222222222"},
           {"sub", 2},
           {"root", "1000000000000022"},
           {"interestRadius", 35.012},
           {"movement", 2},
           {"vitals", 5},
           {"marker", 17},
           {"slots", {{2, "ActionList", 11}, {5, "Vitals", 15}, {17, "Marker", 5399}}},
           {"components", json::array()},
           {"actions", json::array({json::array({"Death_FB", "r_Death_F", 7, true})})}}}},
        {"kinds",
         {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
           {"categoryId", 0x33334444}, {"name", "Test Wolf"}, {"vitalsName", "Test Wolf"}, {"family", "Wildlife"},
           {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})}, {"points", 1}},
          {{"id", "Test_Rabbit:2"}, {"vitals", "Test_Rabbit"}, {"category", "Test_Rabbit_Category"}, {"vitalsId", 0x55556666},
           {"categoryId", 0x77778888}, {"name", "Test Rabbit"}, {"vitalsName", "Test Hare"}, {"family", "Wildlife"},
           {"creatureType", "Critter"}, {"health", 0.05}, {"level", 1}, {"bodies", json::array({json::array({1, 100})})}, {"points", 1}}}},
        {"spawnPoints",
         {{"00000000-0000-4000-8000-0000000000p1", 0, 18, 1100.0, 1000.0, 50.0, 90.0, 360.0, 0.0},
          {"00000000-0000-4000-8000-0000000000p2", 1, 1, 1030.0, 1000.0, 50.0, 0.0, 360.0, 360.0}}},
    };
}

/** CR1: the loader of the creature data file: a correct file, no file, and files that are not complete. */
void creature_file(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "creatures";
    std::filesystem::create_directories(folder);
    const auto write = [&](const char* name, const json& document) {
        const std::filesystem::path path = folder / name;
        std::ofstream(path, std::ios::binary) << document.dump();
        return path.string();
    };
    gamedata::CreatureData data;
    check(data.load(write("correct.json", file_document())) && data.loaded() && data.error().empty()
              && data.slices().size() == 2 && data.kinds().size() == 2 && data.spawn_points().size() == 2,
          "a correct file loads: 2 slices, 2 kinds, 2 spawn points");
    const gamedata::CreatureKind* wolf = data.find_kind("test_wolf");
    const gamedata::CreatureKind* rabbit = data.find_kind("Test_Rabbit:2");
    check(wolf != nullptr && rabbit != nullptr && data.find_kind("Test_Rabbit") == nullptr && wolf->vitalsId == 0x11112222
              && wolf->categoryId == 0x33334444 && wolf->level == 18 && rabbit->vitalsName == "Test Hare",
          "a kind is found by its id (the compare ignores the letter case), with its ids on the wire and its names");
    check(wolf != nullptr && data.health(*wolf, 18) == kWolfHealth && rabbit != nullptr
              && std::abs(data.health(*rabbit, 1) - 16.875F) < 0.001F && data.health(*wolf, 19) == 0.0F
              && !data.has_level(19),
          "the health of a kind is the health of the level times its factor (1690.876 and 16.875); a level with no row has none");
    const gamedata::CreatureSlice& wolfSlice = data.slices()[0];
    const gamedata::CreatureSlice& rabbitSlice = data.slices()[1];
    check(wolfSlice.asset[0] == 0x11 && wolfSlice.asset[15] == 0x11 && wolfSlice.subId == 2 && wolfSlice.movementSlot == 5
              && wolfSlice.vitalsSlot == 9 && wolfSlice.markerSlot == 15 && wolfSlice.root == 0x1000000000000011ULL
              && rabbitSlice.interestRadius > 35.0F && rabbitSlice.interestRadius < 35.1F,
          "a slice has its asset id, its root entity, its three slots, and its interest radius");
    check(wolfSlice.actions.size() == 3 && wolfSlice.deathAction == 2U && wolfSlice.find_action("Idle_No_Task") == 0U
              && !wolfSlice.find_action("Sound") && rabbitSlice.deathAction == 0U,
          "the action table: the death action is \"Death\" (index 2), or \"Death_FB\" for a slice with no such action; "
          "an action with no fragment is not found");
    const gamedata::CreatureSlice::Component* component = wolfSlice.find(0x544F5A4140000000ULL, wolfSlice.target(0x544F5A4140000000ULL, wolfSlice.components[0]));
    check(component != nullptr && component->name == "Root/Vitals" && wolfSlice.find(0x544F5A4140000000ULL, 1) == nullptr,
          "the facet target of a component of a slice is found for an entity group id (the id rule)");
    std::size_t total = 0;
    check(data.search("rabbit", 0, 10, &total).size() == 1 && total == 1 && data.search("", 0, 10).size() == 2
              && data.search("wildlife test", 1, 10, &total).size() == 1 && total == 2 && data.search("bear", 0, 10).empty(),
          "the search finds the kinds whose id, name, category, or family has each word");
    const gamedata::SpawnPoint& point = data.spawn_points()[0];
    check(point.id == "00000000-0000-4000-8000-0000000000p1" && point.kind == 0 && point.level == 18 && point.position.x == 1100.0F
              && point.yaw == 90.0F && point.respawnMin == 360.0F && data.spawn_points()[1].respawnMax == 360.0F,
          "a spawn point has its placement id, its kind, its level, its position, its direction, and its two respawn values");

    gamedata::CreatureData absent;
    check(!absent.load((folder / "not-there.json").string()) && !absent.loaded() && absent.error() == "the file is not there",
          "with no file: no data, and the cause is \"the file is not there\"");

    // A file that is not complete gives no data: a part of the creatures with no sign of the fault is not acceptable.
    const auto refused = [&](const char* name, const json& document) {
        gamedata::CreatureData broken;
        const bool loaded = broken.load(write(name, document));
        return !loaded && !broken.loaded() && broken.kinds().empty() && broken.spawn_points().empty() && !broken.error().empty();
    };
    json noKinds = file_document();
    noKinds.erase("kinds");
    json otherFormat = file_document();
    otherFormat["format"] = 2;
    json lostPoint = file_document();
    lostPoint["spawnPoints"].erase(1);
    json badBody = file_document();
    badBody["kinds"][0]["bodies"][0][0] = 7;
    json badKind = file_document();
    badKind["spawnPoints"][0][1] = 9;
    json noMovement = file_document();
    noMovement["slices"][0]["movement"] = 0;
    json farPoint = file_document();
    farPoint["spawnPoints"][0][3] = 5000000.0;
    json shortPoint = file_document();
    shortPoint["spawnPoints"][0].erase(8);
    json badAction = file_document();
    badAction["slices"][0]["actions"][0] = json::array({"Idle", "Idle", "zero", false});
    json noLevels = file_document();
    noLevels["levels"] = json::array();
    json badAsset = file_document();
    badAsset["slices"][1]["asset"] = "not-an-asset-id";
    check(refused("no-kinds.json", noKinds) && refused("format.json", otherFormat) && refused("levels.json", noLevels),
          "not complete: a file with no kind list, a file with a different format number, and a file with no level are not used");
    check(refused("lost-point.json", lostPoint), "not complete: a file whose spawn point list is shorter than its count is not used");
    check(refused("bad-body.json", badBody) && refused("bad-kind.json", badKind),
          "not complete: a kind with a slice index outside the list, and a spawn point with a kind index outside the list");
    check(refused("no-movement.json", noMovement) && refused("far-point.json", farPoint) && refused("short-point.json", shortPoint)
              && refused("bad-action.json", badAction) && refused("bad-asset.json", badAsset),
          "not complete: a slice with no movement slot, a position outside the world, a spawn point with a value less, an "
          "action row with a wrong form, and an asset id with a wrong form");
    std::ofstream(folder / "text.json", std::ios::binary) << "this is not JSON";
    gamedata::CreatureData text;
    check(!text.load((folder / "text.json").string()) && !text.loaded(), "a file that is not JSON is not used");

    // A backend with no creature data: the world has no creature, and a command says so.
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    enter_with_camera(host, a, alpha, kPlace);
    host.advance(std::chrono::seconds(2));
    host.steps(10, {&a});
    check(!host.world.creatures().has_data() && host.world.creatures().count() == 0 && host.world.entities().size() == 1
              && host.world.creatures().spawn("Test_Wolf", kPlace, 0.0F).error == CreatureError::noData
              && other_records(bundles_of(a)).empty(),
          "with no creature data the world has no creature, a spawn command gives the error \"no data\", and the session gets "
          "no record of a creature");
}

/** CR2: the bytes of a creature record, of a health change, and of a death, in the layout that the live client accepted. */
void creature_record_bytes(const Options&) {
    ServerHost host;
    check(set_creatures(host), "the creature data of the test is complete");
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    enter_with_camera(host, a, alpha, kPlace);
    std::size_t from = a.got.size();
    const world::CreatureResult made =
        host.world.creatures().spawn("Test_Wolf", state::WorldPosition{1004.0F, 1000.0F, kHeight}, 90.0F);
    check(made.error == CreatureError::none && made.entity != nullptr
              && world::actor_text(made.entity->actor) == "00000040415a4f5420000000415a4f54",
          "the first creature of a world has the actor id of the first serial of the range of other entities (kind 20)");
    if (made.entity == nullptr) {
        return;
    }
    const world::EntityId id = made.entity->id;
    host.steps(60, {&a});
    std::vector<Bytes> payloads = creature_payloads(a, from);
    check(payloads.size() == 1, "the session sends one bundle for the new creature");
    if (payloads.size() != 1) {
        return;
    }
    // The time anchor of the record is the time of the world clock at the bundle: its 8 bytes
    // are not a part of the stored layout.
    const std::string record = hex_text(payloads[0]);
    const std::string anchor = record.size() == 264 ? record.substr(132, 16) : "";
    // The record of a Grey Wolf of the live run of stage 3.4a, with the made-up values of the
    // test for the asset, the actor, the position, and the vitals ids:
    //   interest 2, 4 fragments
    //   0, 10    metadata: AssetId with the sub id 2, GdeRef
    //   5, 11    movement: rotation (yaw 90 degrees), lookDir, worldPosAbs, teleport id, time
    //            anchor, dataBits, moreDataBits
    //   9, 15    vitals: HealthAmount, vitalsData (18 bytes, not dead), healthChangeFlags 1,
    //            HealthMax, HealthTickRate, vitalsId, vitalsCategoryId, vitalsLevel 18,
    //            invulnerability, displayImmuneWhenInvulnerable, maxHealth 1690
    //   15, 5399 marker: enabled
    const std::string expected = std::string("0204")
                                 + "000a0103" + "11111111111111111111111111111111" + "00000002" + "00000040415a4f5420000000415a4f54"
                                 + "050b01fc000c00448003" + "66ff" + "3a" + "447b0000447a000022e9" + "01" + anchor + "0000"
                                 + "090f01bd" + "44d35c08" + "000000000000000000000000000000000000" + "01" + "44d35c08" + "0000"
                                 + "7e" + "11112222" + "33334444" + "00000012" + "00" + "00" + "069a"
                                 + "0f975401" + "0101";
    note("record of the creature (132 bytes): " + record);
    check(payloads[0].size() == 132 && record == expected && !anchor.empty(),
          "the record of a creature has the bytes of the layout that the live client accepted (132 bytes; the time anchor is "
          "the world clock)");
    const std::vector<Record> records = other_records(bundles_of(a, from));
    check(records.size() == 1 && records[0].interest == 2 && order_of(records[0]) == "(0,10) (5,11) (9,15) (15,5399) "
              && fields_text(*fragment_of(records[0], 5)) == "(0,11) (0,12) (0,27) (0,31) (0,40) (0,41) (0,42) "
              && fields_text(*fragment_of(records[0], 9))
                     == "(0,0) (0,2) (0,3) (0,4) (0,5) (0,8) (0,9) (0,10) (0,11) (0,12) (0,13) ",
          "the record decodes to its last byte: the metadata state and the states of the slots 5, 9, and 15 of the slice, with "
          "no action field (the client plays the idle action itself)");

    // A health change: one fragment with the health, the vitals data, and the change mark.
    from = a.got.size();
    check(host.world.creatures().set_health(id, 500.0F).error == CreatureError::none, "the health of the creature is set to 500");
    host.steps(10, {&a});
    payloads = creature_payloads(a, from);
    check(payloads.size() == 1
              && hex_text(payloads[0]) == std::string("0201") + "090f010d" + "43fa0000" + "000000000000000000000000000000000000" + "01",
          "a health change is one fragment of 25 bytes for the vitals slot: HealthAmount, vitalsData, and healthChangeFlags 1");

    // The death: the movement state with the death action in scope 0, and the vitals state with the dead mark.
    from = a.got.size();
    check(host.world.creatures().kill(id).error == CreatureError::none, "the creature is killed");
    host.steps(10, {&a});
    payloads = creature_payloads(a, from);
    const std::string death = payloads.size() == 1 ? hex_text(payloads[0]) : "";
    const std::string deathAnchor = death.size() == 180 ? death.substr(102, 16) : "";
    const std::string expectedDeath = std::string("0202")
                                      + "050b01fe001800b8470706" + "66ff" + "3a" + "447b0000447a000022e9" + "00" + "00" + "01" + "01"
                                      + "07" + "0001" + "05" + "1c" + "3c00" + "00" + "000000000000000000000000" + "00" + deathAnchor
                                      + "0000" + "00" + "00"
                                      + "090f010d" + "00000000" + "010000000000000000000000000000000000" + "01";
    note("death of the creature (90 bytes): " + death);
    check(payloads.size() == 1 && payloads[0].size() == 90 && death == expectedDeath && !deathAnchor.empty(),
          "the death has the bytes that the live client accepted (90 bytes): the movement state with the scope fields (start "
          "count 1, fragment 4 + 1, action 27 + 1, speed 1.0, option 0, 12 tag bytes, time 0), and the vitals state with "
          "the health 0 and the dead mark");
    check(all_complete(bundles_of(a)), "each bundle of the session decodes to its last byte, and the sequences have no gap");
}

/** CR3: a creature in the entity table and in the position index. */
void creature_entity(const Options&) {
    ServerHost host;
    set_creatures(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    enter_with_camera(host, a, alpha, kPlace);
    world::Creatures& creatures = host.world.creatures();
    const world::CreatureResult wolf = creatures.spawn("Test_Wolf", state::WorldPosition{1010.0F, 1000.0F, kHeight}, 45.0F);
    const world::CreatureResult rabbit = creatures.spawn("test_rabbit", state::WorldPosition{1300.0F, 1000.0F, kHeight}, 0.0F, 1);
    check(wolf.entity != nullptr && rabbit.entity != nullptr && wolf.entity->id != rabbit.entity->id
              && wolf.entity->kind == world::EntityKind::creature && wolf.entity->owner == 0 && wolf.entity->characterId.empty()
              && wolf.entity->personaId.empty() && wolf.entity->name == "Test Wolf",
          "a creature is an entity of the kind \"creature\" with no character, no account, and no session");
    if (wolf.entity == nullptr || rabbit.entity == nullptr) {
        return;
    }
    const world::EntityId wolfId = wolf.entity->id;
    const world::Entity* player = host.world.find_character(kCharacterA);
    check(wolf.entity->creature.kind != nullptr && wolf.entity->creature.kind->id == "Test_Wolf" && wolf.entity->creature.level == 18
              && wolf.entity->creature.yaw == 45.0F && !wolf.entity->creature.dead && wolf.entity->vitals.health == kWolfHealth
              && wolf.entity->vitals.healthMax == kWolfHealth && wolf.entity->position.x == 1010.0F
              && std::abs(rabbit.entity->vitals.healthMax - 16.875F) < 0.001F,
          "the entity has the kind, the level, the direction, the life state, the health of its kind and level, and the position");
    check((wolf.entity->actor.bytes[0] & 1) == 0 && wolf.entity->actor.bytes[8] == 0x20 && wolf.entity->actor != rabbit.entity->actor
              && player != nullptr && player->actor.bytes[8] == 0x10 && host.world.entities().size() == 3,
          "the world gives each creature its own actor id of the kind 20 (an even first byte); the table has the player and the two creatures");
    check(host.world.find_character("") == nullptr && host.world.find_character(kCharacterA) == player
              && host.world.entity_of(1) == player && creatures.find(wolfId) == wolf.entity && creatures.find(player->id) == nullptr
              && !host.world.progress().view("").has_value(),
          "a function for a character does not find a creature (an empty character id finds nothing), and the creature system "
          "does not find the entity of a player");
    std::set<world::EntityId> near;
    host.world.for_each_near(kPlace, 50.0F, [&](world::Entity& entity) { near.insert(entity.id); });
    std::set<world::EntityId> far;
    host.world.for_each_near(state::WorldPosition{1300.0F, 1000.0F, kHeight}, 50.0F, [&](world::Entity& entity) { far.insert(entity.id); });
    check(near == std::set<world::EntityId>{player->id, wolfId} && far == std::set<world::EntityId>{rabbit.entity->id},
          "the position index has the creatures: a search near the player finds the wolf, and a search 300 m away finds the rabbit");
    check(creatures.count() == 2 && creatures.counts().spawnedByCommand == 2 && creatures.counts().largest == 2,
          "the creature system counts the creatures of the world");
    check(creatures.remove(wolfId).error == CreatureError::none && host.world.find(wolfId) == nullptr && creatures.count() == 1
              && creatures.remove(wolfId).error == CreatureError::unknownCreature
              && creatures.remove(player->id).error == CreatureError::unknownCreature && host.world.find_character(kCharacterA) != nullptr,
          "a removed creature is not in the table; a second removal and the removal of a player entity give \"no such creature\"");
    near.clear();
    host.world.for_each_near(kPlace, 50.0F, [&](world::Entity& entity) { near.insert(entity.id); });
    check(near == std::set<world::EntityId>{player->id}, "the position index has no entry of the removed creature");
    check(creatures.spawn("No_Such_Kind", kPlace, 0.0F).error == CreatureError::unknownKind
              && creatures.spawn("Test_Wolf", state::WorldPosition{5000000.0F, 0.0F, 0.0F}, 0.0F).error == CreatureError::badPosition
              && creatures.spawn("Test_Wolf", kPlace, 0.0F, 19).error == CreatureError::badLevel && creatures.count() == 1,
          "a spawn with a kind that is not known, with a position outside the world, or with a level that has no row changes nothing");
    const world::CreatureResult levelled = creatures.spawn("Test_Wolf", kPlace, 0.0F, 20);
    check(levelled.entity != nullptr && levelled.entity->creature.level == 20 && levelled.entity->vitals.healthMax == 2000.0F,
          "a spawn with a level gives the health of that level");
    // A kind with two slices: each slice gets its part of the creatures.
    std::set<std::string> bodies;
    for (int index = 0; index < 8; ++index) {
        const world::CreatureResult twin = creatures.spawn("Test_Twin", kPlace, 0.0F);
        if (twin.entity != nullptr) {
            bodies.insert(twin.entity->creature.body->path);
        }
    }
    check(bodies.size() == 2, "a kind with two slices gives creatures of each slice");
    // The session of the client ends: the creatures stay in the world.
    host.datagram(a.peer, a.carrier_close());
    host.steps(3, {&a});
    check(host.world.find_character(kCharacterA) == nullptr && creatures.count() == 10,
          "the end of a session removes the entity of its character, and the creatures of commands stay in the world");
}

/** CR4: the spawn point rule: the two distances, one creature for a point, the world clock, and a point that waits. */
void spawn_points(const Options&) {
    const world::WorldOptions defaults;
    check(defaults.creatureSpawnEnter == 150.0F && defaults.creatureSpawnLeave == 200.0F,
          "the spawn point distances of the world are 150 m (make) and 200 m (remove)");
    ServerHost host;
    set_creatures(host, {test_point("point-near", 0, 18, 1100.0F, 1000.0F), test_point("point-edge", 1, 1, 1160.0F, 1000.0F),
                         test_point("point-far", 0, 18, 1500.0F, 1000.0F), test_point("point-level", 0, 19, 1000.0F, 1050.0F)});
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    world::Creatures& creatures = host.world.creatures();
    host.tick();
    host.advance(std::chrono::seconds(2));
    host.tick();
    check(creatures.count() == 0, "with no player in the world, the spawn points make no creature");
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    host.advance(std::chrono::milliseconds(600));
    host.steps(2, {&a});
    const world::EntityId player = host.world.find_character(kCharacterA)->id;
    const auto at_point = [&](std::uint32_t point) { return creatures.spawn_points()[point].entity; };
    check(creatures.count() == 1 && at_point(0) != 0 && at_point(1) == 0 && at_point(2) == 0,
          "a player 100 m from a point: the world makes its creature; the points at 160 m and at 500 m have none");
    const world::Entity* wolf = creatures.find(at_point(0));
    check(wolf != nullptr && wolf->creature.spawnPoint == 0U && wolf->creature.level == 18 && wolf->creature.yaw == 90.0F
              && wolf->position.x == 1100.0F && wolf->creature.kind->id == "Test_Wolf",
          "the creature has the kind, the level, the position, and the direction of its spawn point");
    check(at_point(3) == 0 && creatures.spawn_points()[3].waiting && log_count("makes no creature: the level table has no level 19") == 1,
          "a point whose level has no row in the level table makes no creature, with one log line");
    const world::EntityId first = at_point(0);
    host.advance(std::chrono::seconds(3));
    host.steps(5, {&a});
    check(creatures.count() == 1 && at_point(0) == first && creatures.counts().spawnedByPoint == 1,
          "more scans make no second creature for the same point");

    const auto move = [&](float x) { host.world.set_position(player, state::WorldPosition{x, 1000.0F, kHeight}, false); };
    // The rule runs one time for each 500 ms of the world clock, not in each tick.
    move(1011.0F);
    host.steps(1, {&a});
    check(at_point(1) == 0, "the player is 149 m from the second point, and the scan interval did not pass: no creature");
    host.advance(std::chrono::milliseconds(500));
    host.steps(1, {&a});
    check(at_point(1) != 0 && creatures.count() == 2, "after 500 ms of the world clock the world makes the creature of the second point");
    // Between the two distances a creature stays; above the second one it leaves the world.
    move(910.0F);
    host.advance(std::chrono::milliseconds(600));
    host.steps(1, {&a});
    check(at_point(0) == first && at_point(1) == 0 && creatures.count() == 1 && creatures.counts().removedByDistance == 1,
          "the player at 190 m from the first point and at 250 m from the second: the first creature stays, the second leaves the world");
    move(890.0F);
    host.advance(std::chrono::milliseconds(600));
    host.steps(1, {&a});
    check(at_point(0) == 0 && creatures.count() == 0 && host.world.find(first) == nullptr,
          "the player at 210 m: the first creature leaves the world");
    move(960.0F);
    host.advance(std::chrono::milliseconds(600));
    host.steps(1, {&a});
    const world::EntityId second = at_point(0);
    check(second != 0 && second != first && creatures.find(second)->vitals.health == kWolfHealth,
          "the player back at 140 m: the point makes a new creature with a new id and full health");

    // A creature that died: its body stays, and its point waits. The distance rule does not make it again.
    check(creatures.kill(second).error == CreatureError::none && creatures.spawn_points()[0].waiting && at_point(0) == second
              && creatures.find(second)->creature.dead,
          "a kill: the creature is dead and stays in the world, and its spawn point waits");
    move(700.0F);
    host.advance(std::chrono::milliseconds(600));
    host.steps(1, {&a});
    move(1000.0F);
    host.advance(std::chrono::milliseconds(600));
    host.steps(1, {&a});
    check(at_point(0) == 0 && creatures.spawn_points()[0].waiting,
          "the player goes away and comes back: the body left the world, and the point that waits makes no new creature");
    // The place of the respawn time: a time of the world clock makes the point free again.
    check(creatures.respawn_at(0, host.clock.now() + std::chrono::seconds(2)) && !creatures.respawn_at(99, host.clock.now()),
          "a point that waits gets a respawn time on the world clock");
    host.advance(std::chrono::milliseconds(1200));
    host.steps(1, {&a});
    check(at_point(0) == 0, "1.2 s later the point still waits");
    host.advance(std::chrono::milliseconds(1200));
    host.steps(1, {&a});
    check(at_point(0) != 0 && !creatures.spawn_points()[0].waiting, "after the respawn time the point makes its creature again");

    // A command removes a creature of a point: the point waits until a reset.
    const world::EntityId third = at_point(0);
    check(creatures.remove(third).error == CreatureError::none && at_point(0) == 0 && creatures.spawn_points()[0].waiting,
          "a command removes the creature of a point: the point waits");
    host.advance(std::chrono::seconds(2));
    host.steps(1, {&a});
    check(at_point(0) == 0, "the distance rule does not make a creature that a command removed");
    check(creatures.reset_spawn_points() == 2, "the reset command makes the two points that wait free (the removed one and the one with no level)");
    host.steps(1, {&a});
    check(at_point(0) != 0 && at_point(0) != third, "after the reset the point makes its creature again");

    // The player leaves the world: no player is near, and each creature of a point leaves the world.
    host.datagram(a.peer, a.carrier_close());
    host.advance(std::chrono::milliseconds(600));
    host.steps(3, {&a});
    check(creatures.count() == 0, "when the last player leaves the world, the creatures of the spawn points leave it too");
}

/** CR5: the interest of a session for creatures: the distances of each slice, and many creatures in more than one bundle. */
void creature_interest(const Options&) {
    ServerHost host;
    // The sessions of this case do not observe other players: the option of a start of the user.
    set_creatures(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1005.0F, 1000.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    world::Creatures& creatures = host.world.creatures();
    const world::EntityId wolf = creatures.spawn("Test_Wolf", state::WorldPosition{1090.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    const world::EntityId rabbit = creatures.spawn("Test_Rabbit", state::WorldPosition{1040.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    const world::EntityId farWolf = creatures.spawn("Test_Wolf", state::WorldPosition{1110.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    host.steps(60, {&a, &b});
    check(other_records(bundles_of(a)).empty(), "before the first camera position of its client, a session gets no record of a creature");
    host.datagram(a.peer, a.input_at(1, kPlace.x, kPlace.y, kPlace.z));
    host.steps(60, {&a, &b});
    const world::EntityId player = host.world.find_character(kCharacterA)->id;
    const world::EntityId other = host.world.find_character(kCharacterB)->id;
    check(interest_of(host, 1, wolf) == 2 && interest_of(host, 1, rabbit) == 0 && interest_of(host, 1, farWolf) == 0
              && interest_of(host, 1, other) == 0,
          "the wolf at 90 m (radius 100 m) is in the interest table with the number 2; the rabbit at 40 m (radius 35 m), the "
          "wolf at 110 m, and the character of a different player (5 m, option off) are not");
    std::vector<Record> records = other_records(bundles_of(a));
    check(records.size() == 1 && records[0].interest == 2 && records[0].fragments.size() == 4,
          "the session gets one record with four fragments for the wolf");

    const auto move = [&](float x) {
        host.world.set_position(player, state::WorldPosition{x, 1000.0F, kHeight}, false);
        host.steps(60, {&a, &b});
    };
    std::size_t from = a.got.size();
    move(1006.0F);
    records = other_records(bundles_of(a, from));
    check(interest_of(host, 1, rabbit) == 3 && records.size() == 1 && records[0].interest == 3
              && order_of(records[0]) == "(0,10) (2,11) (5,15) (17,5399) ",
          "the player at 34 m from the rabbit: the rabbit comes in with the number 3 and the slots of its own slice (2, 5, 17)");
    from = a.got.size();
    move(990.0F);
    check(interest_of(host, 1, rabbit) == 3 && interest_of(host, 1, wolf) == 2 && stops_of(bundles_of(a, from)).empty()
              && other_records(bundles_of(a, from)).empty(),
          "the player at 50 m from the rabbit and at 100 m from the wolf (between the two distances of each): both stay, with no "
          "stop and no new record");
    move(980.0F);
    check(interest_of(host, 1, rabbit) == 0 && interest_of(host, 1, wolf) == 2
              && stops_of(bundles_of(a, from)) == std::vector<std::uint32_t>{3} && creatures.find(rabbit) != nullptr,
          "the player at 60 m from the rabbit (radius 35 m plus 20 m): the session sends the stop of the number 3, and the "
          "rabbit stays in the world");
    move(965.0F);
    check(interest_of(host, 1, wolf) == 0 && stops_of(bundles_of(a, from)) == std::vector<std::uint32_t>({3, 2}),
          "the player at 125 m from the wolf (100 m plus 20 m): the stop of the number 2");
    check(log_count("goes out of the interest table (interest 3, 60.0 m)") == 1, "the log line of a stop has the distance");

    // A creature that leaves the world: each observer gets its stop at once.
    move(1000.0F);
    from = a.got.size();
    creatures.remove(wolf);
    host.steps(2, {&a, &b});
    check(stops_of(bundles_of(a, from)).size() == 1 && interest_of(host, 1, wolf) == 0,
          "a creature that a command removes: the session sends its stop in the next tick");

    // Many creatures near the player at one time: 16 new ones for each scan at most, the
    // nearest ones first, and each bundle is below the size of one carrier message.
    from = a.got.size();
    std::vector<world::EntityId> pack;
    for (int index = 0; index < 40; ++index) {
        pack.push_back(creatures.spawn("Test_Wolf", state::WorldPosition{1001.0F + static_cast<float>(index), 1000.0F, kHeight}, 0.0F).entity->id);
    }
    host.advance(std::chrono::milliseconds(500));
    host.steps(1, {&a, &b});
    std::size_t known = 0;
    for (const world::EntityId id : pack) {
        known += interest_of(host, 1, id) != 0 ? 1 : 0;
    }
    bool nearest = true;
    for (int index = 0; index < 40; ++index) {
        nearest = nearest && (interest_of(host, 1, pack[static_cast<std::size_t>(index)]) != 0) == (index < 16);
    }
    check(known == 16 && nearest, "40 new creatures near the player: the first scan takes the 16 nearest ones");
    host.steps(120, {&a, &b});
    known = 0;
    for (const world::EntityId id : pack) {
        known += interest_of(host, 1, id) != 0 ? 1 : 0;
    }
    std::size_t largest = 0;
    std::size_t bundleCount = 0;
    std::set<std::uint32_t> interests;
    bool full = true;
    for (std::size_t index = from; index < a.got.size(); ++index) {
        if (a.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(a.got[index].body);
        if (bundle.records.empty() || bundle.records.front().interest == 1) {
            continue;
        }
        ++bundleCount;
        largest = std::max(largest, a.got[index].frame.size());
        for (const Record& record : bundle.records) {
            interests.insert(record.interest);
            full = full && record.fragments.size() == 4;
        }
    }
    note("40 creatures: " + std::to_string(bundleCount) + " bundles, the largest frame has " + std::to_string(largest) + " bytes");
    check(known == 40 && interests.size() == 40 && full && bundleCount >= 6 && largest <= 1024,
          "after three scans the session has the 40 creatures: 40 full records in 6 or more bundles, and no frame is larger "
          "than one carrier message (1,024 bytes)");
    check(host.session(1).creature_records() >= 42 && host.session(1).creatures_observed() >= 40,
          "the session counts its creature records and the largest number of creatures at one time");
    check(all_complete(bundles_of(a)), "each bundle of the session decodes to its last byte, and the sequences have no gap");
    check(other_records(bundles_of(b)).empty() && stops_of(bundles_of(b)).empty(),
          "the session of B sent no camera position: it gets no record and no stop of a creature");
}

/** CR6: the health and the death of a creature as state changes of its observers. */
void creature_health(const Options&) {
    ServerHost host;
    set_creatures(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    FakeClient a(27000);
    enter_with_camera(host, a, alpha, kPlace);
    world::Creatures& creatures = host.world.creatures();
    const world::EntityId wolf = creatures.spawn("Test_Wolf", state::WorldPosition{1010.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    const world::EntityId plain = creatures.spawn("Test_Plain", state::WorldPosition{1012.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    host.steps(60, {&a});
    const std::uint32_t wolfNumber = interest_of(host, 1, wolf);
    const std::uint32_t plainNumber = interest_of(host, 1, plain);
    std::vector<Record> records = other_records(bundles_of(a));
    check(wolfNumber != 0 && plainNumber != 0 && records.size() == 2, "the session observes the two creatures");
    const Record* plainRecord = nullptr;
    for (const Record& record : records) {
        if (record.interest == plainNumber) {
            plainRecord = &record;
        }
    }
    check(plainRecord != nullptr && order_of(*plainRecord) == "(0,10) (5,11) (9,15) ",
          "a slice with no marker component: the record has no marker fragment");
    const float playerHealth = host.world.find_character(kCharacterA)->vitals.health;

    std::size_t from = a.got.size();
    check(creatures.set_health(wolf, kWolfHealth / 2.0F).error == CreatureError::none, "the health of the wolf is set to one half");
    host.steps(10, {&a});
    records = other_records(bundles_of(a, from));
    check(records.size() == 1 && records[0].interest == wolfNumber && order_of(records[0]) == "(9,15) "
              && fields_text(records[0].fragments[0]) == "(0,0) (0,2) (0,3) "
              && float_at(records[0].fragments[0].fields.at({0, 0}), 0) == kWolfHealth / 2.0F
              && records[0].fragments[0].fields.at({0, 2})[0] == 0 && records[0].fragments[0].fields.at({0, 3})[0] == 1,
          "the observer gets one vitals fragment: the new health, the vitals data with no dead mark, and the change mark 1 (the "
          "name plate of the client changes its bar only with this mark)");
    check(creatures.set_health(wolf, kWolfHealth + 1.0F).error == CreatureError::badHealth
              && creatures.set_health(wolf, -1.0F).error == CreatureError::badHealth
              && creatures.set_health(0x1234, 1.0F).error == CreatureError::unknownCreature
              && creatures.find(wolf)->vitals.health == kWolfHealth / 2.0F,
          "a health above the maximum or below 0, and a creature that is not there, change nothing");

    from = a.got.size();
    const auto before = host.clock.now();
    check(creatures.kill(wolf).error == CreatureError::none && creatures.find(wolf)->creature.dead
              && creatures.find(wolf)->vitals.health == 0.0F && creatures.find(wolf)->creature.diedAt == before
              && creatures.counts().deaths == 1,
          "a kill: the health is 0, the creature is dead, and the time of the death is the time of the world clock");
    host.steps(10, {&a});
    records = other_records(bundles_of(a, from));
    check(records.size() == 1 && order_of(records[0]) == "(5,11) (9,15) ", "the observer gets the movement state and the vitals state");
    if (records.size() == 1 && records[0].fragments.size() == 2) {
        const Fragment& movement = records[0].fragments[0];
        const Fragment& vitals = records[0].fragments[1];
        const Bytes scope = movement.fields.count({0, 33}) != 0 ? movement.fields.at({0, 33}) : Bytes{};
        check(fields_text(movement) == "(0,11) (0,12) (0,27) (0,28) (0,29) (0,31) (0,32) (0,33) (0,34) (0,38) (0,40) (0,41) (0,42) (0,49) (0,50) "
                  && hex_text(scope) == "070001051c3c0000" && hex_text(movement.fields.at({0, 32})) == "01"
                  && hex_text(movement.fields.at({0, 29})) == "00" && hex_text(movement.fields.at({0, 28})) == "00",
              "the movement state has the death action of the slice in scope 0: start count 1, fragment index 4 + 1, action "
              "index 27 + 1, the speed 1.0, the option 0, and the time 0");
        check(float_at(vitals.fields.at({0, 0}), 0) == 0.0F && vitals.fields.at({0, 2})[0] == 1 && vitals.fields.at({0, 3})[0] == 1,
              "the vitals state has the health 0 and the dead mark (bit 0 of the vitals data)");
    }
    check(creatures.set_health(wolf, 100.0F).error == CreatureError::dead && creatures.kill(wolf).error == CreatureError::dead
              && creatures.find(wolf)->vitals.health == 0.0F,
          "a dead creature does not get health again, and a second kill changes nothing");

    // A slice with no death action: the dead mark only.
    from = a.got.size();
    creatures.kill(plain);
    host.steps(10, {&a});
    records = other_records(bundles_of(a, from));
    check(records.size() == 1 && order_of(records[0]) == "(9,15) " && !creatures.find(plain)->creature.action,
          "a slice with no death action: the observer gets only the vitals state");
    check(host.world.find_character(kCharacterA)->vitals.health == playerHealth, "the health of the player did not change");

    // A session that observes the dead creature later gets the time of the death action: its
    // client shows the pose of that time, not the start of the action.
    host.advance(std::chrono::seconds(10));
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1005.0F, 1000.0F, kHeight});
    FakeClient b(27001);
    enter_with_camera(host, b, beta, state::WorldPosition{1005.0F, 1000.0F, kHeight});
    const std::uint32_t number = interest_of(host, 2, wolf);
    const Record* late = nullptr;
    const std::vector<Record> ofB = other_records(bundles_of(b));
    for (const Record& record : ofB) {
        if (record.interest == number) {
            late = &record;
        }
    }
    check(late != nullptr && order_of(*late) == "(0,10) (5,11) (9,15) (15,5399) ", "a session that comes later gets the full record of the dead creature");
    if (late != nullptr && late->fragments.size() == 4) {
        const Fragment& movement = late->fragments[1];
        const unsigned ticks = movement.fields.at({0, 28})[0] | (movement.fields.at({0, 38})[0] << 8);
        note("time of the death action in the record of the later session: " + std::to_string(ticks) + " ticks");
        check(ticks >= 300 && ticks <= 400 && late->fragments[2].fields.at({0, 2})[0] == 1
                  && float_at(late->fragments[2].fields.at({0, 0}), 0) == 0.0F
                  && late->fragments[2].fields.count({0, 4}) == 1,
              "its movement state has the time of the death action (10 s and more: 300 ticks of 1/30 s and more), and its "
              "vitals state has the dead mark, the health 0, and the maximum");
    }
}

/** A backend with the development routes and the admin routes, for the commands. */
struct CommandHost {
    ServerHost host;
    Player alpha;
    Player beta;
    server::http::AdminAccess access;
    server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;

    CommandHost()
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace)),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1300.0F, 1000.0F, kHeight})) {
        set_creatures(host, {test_point("point-near", 0, 18, 1020.0F, 1000.0F)});
        if (!access.add(kTokenA, kSteamIdA)) {
            throw std::runtime_error("creature test: the setup failed");
        }
        server::http::register_dev_routes(router, services);
        server::http::register_admin_routes(router, services, access);
        host.tick();
    }

    service::HttpResponse send(const char* method, const std::string& path, const std::string& query, const std::string& body = {},
                               const char* token = nullptr, const std::string& serviceHost = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = query.empty() ? path : path + "?" + query;
        request.host = serviceHost;
        request.body = body;
        if (token != nullptr) {
            request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        }
        return server::http::handle_request(router, request);
    }
    service::HttpResponse dev(const std::string& path, const std::string& query) {
        return send("POST", "/v1/dev/creatures/" + path, query);
    }
    service::HttpResponse admin(const char* name, const char* character, const json& arguments, const char* token = kTokenA) {
        return send("POST", "/admin/v1/commands", {}, json{{"command", name}, {"character", character}, {"args", arguments}}.dump(), token);
    }
};

/** CR7: the commands for creatures of the development routes and of the admin interface. */
void creature_commands(const Options&) {
    CommandHost c;
    ServerHost& host = c.host;
    world::Creatures& creatures = host.world.creatures();
    FakeClient a(27000);
    enter_with_camera(host, a, c.alpha, kPlace);
    host.advance(std::chrono::milliseconds(600));
    host.steps(2, {&a});
    const std::string character = std::string("character=") + kCharacterA;

    // The list.
    json list = json::parse(c.send("GET", "/v1/dev/creatures", character).body, nullptr, false);
    check(list.is_object() && list["data"] == true && list["inWorld"] == true && list["creatures"].size() == 1
              && list["creatures"][0]["kind"] == "Test_Wolf" && list["creatures"][0]["name"] == "Test Wolf"
              && list["creatures"][0]["level"] == 18 && list["creatures"][0]["spawnPoint"] == true
              && list["creatures"][0]["distance"] == 20.0 && list["creatures"][0]["dead"] == false
              && list["spawnPoints"] == 1 && list["total"] == 1,
          "the list has the creature of the spawn point: its id, kind, name, level, health, position, and its distance from the character");
    // The spawn command.
    json made = json::parse(c.dev("spawn", character + "&kind=Test_Rabbit&dx=5&dy=0&dz=0&yaw=180").body, nullptr, false);
    check(made.is_object() && made["applied"] == "world" && made["creature"]["kind"] == "Test_Rabbit"
              && made["creature"]["position"][0] == 1005.0 && made["creature"]["yaw"] == 180.0
              && made["creature"]["spawnPoint"] == false && creatures.count() == 2,
          "spawn: a creature of the kind is in the world 5 m to the east of the character");
    const std::string rabbit = made.is_object() ? made["creature"]["id"].get<std::string>() : "";
    json north = json::parse(c.dev("spawn", character + "&kind=Test_Wolf&level=20").body, nullptr, false);
    check(north.is_object() && north["creature"]["position"][1] == 1003.0 && north["creature"]["level"] == 20
              && north["creature"]["healthMax"] == 2000.0,
          "spawn with no place: 3 m to the north of the character; with a level: the health of that level");
    const std::string wolf = north.is_object() ? north["creature"]["id"].get<std::string>() : "";
    check(c.dev("spawn", character + "&kind=No_Kind").status == 404 && c.dev("spawn", character).status == 400
              && c.dev("spawn", character + "&kind=Test_Wolf&dx=abc").status == 400
              && c.dev("spawn", character + "&kind=Test_Wolf&level=0").status == 400
              && c.dev("spawn", character + "&kind=Test_Wolf&level=19").status == 400
              && c.dev("spawn", character + "&kind=Test_Wolf&yaw=1000").status == 400
              && c.dev("spawn", std::string("character=") + kCharacterB + "&kind=Test_Wolf").status == 404
              && c.dev("spawn", "kind=Test_Wolf").status == 404 && creatures.count() == 3,
          "spawn refuses a kind that is not known (404), no kind, a distance that is not a number, a level of 0, a level with no "
          "row, a wrong direction (400), and a character that is not in the world (404): the world does not change");
    list = json::parse(c.send("GET", "/v1/dev/creatures", character + "&range=10").body, nullptr, false);
    check(list["creatures"].size() == 2 && list["creatures"][0]["distance"] == 3.0 && list["creatures"][1]["distance"] == 5.0
              && list["total"] == 3,
          "the list with a range has only the creatures that are this near, the nearest one first");
    // Health.
    json half = json::parse(c.dev("health", "creature=" + wolf + "&percent=50").body, nullptr, false);
    check(half.is_object() && half["creature"]["health"] == 1000.0 && half["creature"]["healthMax"] == 2000.0,
          "health with a percent value: one half of the maximum");
    check(json::parse(c.dev("health", "creature=" + wolf + "&health=250.5").body, nullptr, false)["creature"]["health"] == 250.5
              && c.dev("health", "creature=" + wolf + "&health=3000").status == 400
              && c.dev("health", "creature=" + wolf + "&percent=101").status == 400
              && c.dev("health", "creature=" + wolf + "&health=10&percent=10").status == 400
              && c.dev("health", "creature=" + wolf).status == 400 && c.dev("health", "creature=zz&health=1").status == 400
              && c.dev("health", "creature=0000000000001234&health=1").status == 404
              && creatures.find(std::strtoull(wolf.c_str(), nullptr, 16))->vitals.health == 250.5F,
          "health with a number sets the health; a value above the maximum, a percent above 100, two values, no value, and an id "
          "with a wrong form give 400, and a creature that is not there gives 404");
    // Kill and remove.
    json dead = json::parse(c.dev("kill", "creature=" + wolf).body, nullptr, false);
    check(dead.is_object() && dead["creature"]["dead"] == true && dead["creature"]["health"] == 0.0
              && c.dev("kill", "creature=" + wolf).status == 400 && c.dev("health", "creature=" + wolf + "&percent=50").status == 400,
          "kill: the creature is dead; a second kill and a health command for a dead creature give 400");
    check(c.dev("remove", "creature=" + rabbit).status == 200 && c.dev("remove", "creature=" + rabbit).status == 404
              && c.dev("remove", "").status == 400 && creatures.count() == 2,
          "remove: the creature leaves the world; a second removal gives 404, and no id gives 400");
    // The reset of the spawn points.
    const std::string pointCreature = u64_hex(creatures.spawn_points()[0].entity);
    check(c.dev("kill", "creature=" + pointCreature).status == 200 && creatures.spawn_points()[0].waiting, "the creature of the spawn point is killed: its point waits");
    json reset = json::parse(c.dev("reset", "").body, nullptr, false);
    host.steps(2, {&a});
    check(reset.is_object() && reset["spawnPoints"] == 1 && !creatures.spawn_points()[0].waiting && creatures.spawn_points()[0].entity != 0
              && u64_hex(creatures.spawn_points()[0].entity) != pointCreature,
          "reset: the point is free again, the body of the dead creature left the world, and the point has a new creature");
    // The kinds.
    json kinds = json::parse(c.send("GET", "/v1/dev/creatures/kinds", "q=rabbit").body, nullptr, false);
    check(kinds.is_object() && kinds["loaded"] == true && kinds["kinds"].size() == 1 && kinds["kinds"][0]["id"] == "Test_Rabbit"
              && json::parse(c.send("GET", "/v1/dev/creatures/kinds", "").body, nullptr, false)["kinds"].size() == 4,
          "the kind list has each kind of the creature data, and a text selects the kinds that have it");
    check(c.send("POST", "/v1/dev/creatures/spawn", character + "&kind=Test_Wolf", {}, nullptr, "gateway.example").status == 403
              && c.send("GET", "/v1/dev/creatures", character, {}, nullptr, "gateway.example").status == 403,
          "a development route is a management route: a request with a service host gets 403");
    // The fragment command for a creature.
    const world::EntityId target = creatures.spawn_points()[0].entity;
    check(c.send("POST", "/v1/dev/world/fragment", "creature=" + u64_hex(target) + "&slot=9&type=15&state=010143fa0000").status == 200
              && creatures.find(target)->rawFragments.size() == 1
              && c.send("POST", "/v1/dev/world/fragment", "creature=0000000000001234&slot=9&type=15&state=0101").status == 404,
          "the fragment command gives a creature a state fragment with bytes of the caller");

    // The admin interface: the same commands, for the account of the token.
    const std::size_t before = creatures.count();
    json adminMade = json::parse(c.admin("spawn_creature", kCharacterA, {{"kind", "Test_Wolf"}, {"dx", 2}, {"dy", 2}}).body, nullptr, false);
    check(adminMade.is_object() && adminMade["creature"]["kind"] == "Test_Wolf" && adminMade["creature"]["position"][0] == 1002.0
              && creatures.count() == before + 1,
          "admin: spawn_creature makes a creature near the character of the account of the token");
    const std::string adminWolf = adminMade.is_object() ? adminMade["creature"]["id"].get<std::string>() : "";
    check(c.admin("spawn_creature", kCharacterB, {{"kind", "Test_Wolf"}}).status == 404
              && c.admin("kill_creature", kCharacterB, {{"creature", adminWolf}}).status == 404
              && c.admin("spawn_creature", kCharacterA, {{"kind", "Test_Wolf"}}, nullptr).status == 401
              && c.admin("spawn_creature", kCharacterA, {{"kind", "Test_Wolf"}, {"level", json::array()}}).status == 400
              && creatures.count() == before + 1 && !creatures.find(std::strtoull(adminWolf.c_str(), nullptr, 16))->creature.dead,
          "admin: a command for the character of a different account gives 404, a request with no token gives 401, and an "
          "argument with a wrong form gives 400: the world does not change");
    const json quarter = json::parse(c.admin("set_creature_health", kCharacterA, {{"creature", adminWolf}, {"percent", 25}}).body, nullptr, false);
    check(quarter.is_object() && quarter.contains("creature")
              && std::abs(quarter["creature"]["health"].get<double>() - static_cast<double>(kWolfHealth) / 4.0) < 0.01
              && c.admin("kill_creature", kCharacterA, {{"creature", adminWolf}}).status == 200
              && c.admin("remove_creature", kCharacterA, {{"creature", adminWolf}}).status == 200
              && c.admin("reset_creatures", kCharacterA, json::object()).status == 200 && creatures.count() == before,
          "admin: set_creature_health, kill_creature, remove_creature, and reset_creatures do the same as the development commands");
    json state = json::parse(c.send("GET", "/admin/v1/state", "", {}, kTokenA).body, nullptr, false);
    check(state.is_object() && state["selected"] == kCharacterA && state["creatures"]["data"] == true
              && state["creatures"]["creatures"].size() == before && state["creatures"]["creatures"][0].contains("distance"),
          "admin: the state of the selected character has the creatures near it, with their distance");
    json search = json::parse(c.send("GET", "/admin/v1/catalog/creatures", "q=test+wolf", {}, kTokenA).body, nullptr, false);
    check(search.is_object() && search["loaded"] == true && search["total"] == 1 && search["kinds"][0]["id"] == "Test_Wolf"
              && search["kinds"][0]["health"] == static_cast<double>(kWolfHealth)
              && c.send("GET", "/admin/v1/catalog/creatures", "q=wolf", {}, nullptr).status == 401
              && c.send("GET", "/admin/v1/catalog/creatures", "limit=0", {}, kTokenA).status == 400,
          "admin: the kind search finds the kinds of a text; it needs the token, and a wrong limit gives 400");
}

/** CR8: three sessions (rule MP-10): two observe the same creature, one is far away. */
void creature_sessions(const Options&) {
    ServerHost host;
    set_creatures(host);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1004.0F, 1000.0F, kHeight});
    const Player gamma = add_player(host.store, kSteamIdC, "Tester C", kCharacterC, "Gamma", state::WorldPosition{1600.0F, 1000.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    FakeClient c(27002);
    world::Creatures& creatures = host.world.creatures();
    // A observes a rabbit before the wolf is in the world, and the rabbit is too far for B:
    // thus the wolf has a different interest number in each of the two sessions.
    const world::EntityId rabbit = creatures.spawn("Test_Rabbit", state::WorldPosition{968.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    enter_with_camera(host, a, alpha, kPlace);
    const world::EntityId wolf = creatures.spawn("Test_Wolf", state::WorldPosition{1020.0F, 1000.0F, kHeight}, 0.0F).entity->id;
    enter_with_camera(host, b, beta, state::WorldPosition{1004.0F, 1000.0F, kHeight});
    enter_with_camera(host, c, gamma, state::WorldPosition{1600.0F, 1000.0F, kHeight});
    host.steps(60, {&a, &b, &c});
    const std::uint32_t ofA = interest_of(host, 1, wolf);
    const std::uint32_t ofB = interest_of(host, 2, wolf);
    check(ofA == 3 && ofB == 2 && interest_of(host, 1, rabbit) == 2 && interest_of(host, 2, rabbit) == 0 && interest_of(host, 3, wolf) == 0
              && creatures.count() == 2,
          "A and B observe the same wolf, each one with its own interest number (3 and 2); C is 580 m away and does not");
    const auto record_for = [&](const FakeClient& client, std::uint32_t interest, std::size_t from) {
        std::vector<Record> found;
        for (const Record& record : other_records(bundles_of(client, from))) {
            if (record.interest == interest) {
                found.push_back(record);
            }
        }
        return found;
    };
    const std::vector<Record> firstA = record_for(a, ofA, 0);
    const std::vector<Record> firstB = record_for(b, ofB, 0);
    check(firstA.size() == 1 && firstB.size() == 1 && firstA[0].fragments.size() == 4
              && firstA[0].fragments[0].fields.at({0, 1}) == firstB[0].fragments[0].fields.at({0, 1})
              && firstA[0].fragments[2].fields == firstB[0].fragments[2].fields,
          "the two records have the same actor id and the same vitals state: one creature of the world, not one for each session");

    std::size_t fromA = a.got.size();
    std::size_t fromB = b.got.size();
    std::size_t fromC = c.got.size();
    creatures.set_health(wolf, 400.0F);
    host.steps(10, {&a, &b, &c});
    std::vector<Record> changeA = record_for(a, ofA, fromA);
    std::vector<Record> changeB = record_for(b, ofB, fromB);
    check(changeA.size() == 1 && changeB.size() == 1 && changeA[0].fragments.size() == 1
              && changeA[0].fragments[0].fields == changeB[0].fragments[0].fields
              && float_at(changeA[0].fragments[0].fields.at({0, 0}), 0) == 400.0F,
          "a health change of the creature goes to A and to B with the same fields (health 400)");
    check(other_records(bundles_of(c, fromC)).empty() && other_records(bundles_of(c)).empty() && stops_of(bundles_of(c)).empty(),
          "C gets no record and no stop of the creature");

    // A goes away: A gets the stop, the creature stays in the world, and B still observes it.
    fromA = a.got.size();
    fromB = b.got.size();
    host.world.set_position(host.world.find_character(kCharacterA)->id, state::WorldPosition{880.0F, 1000.0F, kHeight}, false);
    host.steps(60, {&a, &b, &c});
    const std::vector<std::uint32_t> stopsA = stops_of(bundles_of(a, fromA));
    check(interest_of(host, 1, wolf) == 0 && std::find(stopsA.begin(), stopsA.end(), ofA) != stopsA.end()
              && creatures.find(wolf) != nullptr && interest_of(host, 2, wolf) == ofB && stops_of(bundles_of(b, fromB)).empty(),
          "A goes 140 m away: A gets the stop of its number, the creature stays in the world, and B gets no stop");
    fromA = a.got.size();
    fromB = b.got.size();
    creatures.kill(wolf);
    host.steps(10, {&a, &b, &c});
    changeB = record_for(b, ofB, fromB);
    check(changeB.size() == 1 && order_of(changeB[0]) == "(5,11) (9,15) " && record_for(a, ofA, fromA).empty()
              && other_records(bundles_of(c, fromC)).empty(),
          "the death of the creature goes to B only: A does not observe it any more, and C never did");

    // The session of B ends: the creature of a command stays, and A can observe it again.
    host.datagram(b.peer, b.carrier_close());
    host.steps(3, {&a, &c});
    fromA = a.got.size();
    host.world.set_position(host.world.find_character(kCharacterA)->id, kPlace, false);
    host.steps(60, {&a, &c});
    const std::vector<Record> again = other_records(bundles_of(a, fromA));
    bool dead = false;
    for (const Record& record : again) {
        const Fragment* vitals = fragment_of(record, 9);
        dead = dead || (record.fragments.size() == 4 && vitals != nullptr && vitals->fields.at({0, 2})[0] == 1);
    }
    check(creatures.find(wolf) != nullptr && interest_of(host, 1, wolf) != 0 && dead,
          "the end of the session of B does not remove the creature: A comes back and gets its full record, with the dead mark");
    check(all_complete(bundles_of(a)) && all_complete(bundles_of(b)) && all_complete(bundles_of(c)),
          "each bundle of the three sessions decodes to its last byte, and the sequences of each session have no gap");
    check(log_count("rep: creatures of the session") >= 1, "the end of a session writes its creature counts to the log");
}

} // namespace

std::vector<Case> creature_cases() {
    return {
        {"CR1", "creatures: the creature data file (correct, not there, not complete), and a world with no data", creature_file},
        {"CR2", "creatures: the bytes of a record, of a health change, and of a death", creature_record_bytes},
        {"CR3", "creatures: the entity table and the position index", creature_entity},
        {"CR4", "creatures: the spawn point rule (two distances, the world clock, a point that waits)", spawn_points},
        {"CR5", "creatures: the interest of a session, and many creatures in more than one bundle", creature_interest},
        {"CR6", "creatures: health and death as state changes", creature_health},
        {"CR7", "creatures: the commands of the development routes and of the admin interface", creature_commands},
        {"CR8", "creatures: three sessions (two observers of one creature, one session far away)", creature_sessions},
    };
}

} // namespace soulwarden::backend::tests

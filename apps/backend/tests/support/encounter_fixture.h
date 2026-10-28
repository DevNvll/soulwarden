#pragma once

// The world of the encounter cases (stage QG5c2, tests/encounter_tests.cpp; stage QG5c3, tests/encounter_pattern_tests.cpp and
// tests/review_qg5c2_tests.cpp; docs/systems/encounters.md): made-up recipes of the form of the Obelisk recipe of the game data, a member
// kind, a start object template and its row, the quests of the events, and the readers of the root record, the stage states, the links,
// and the VariationData state. All slices, kinds, places, recipes, quests, and accounts are made up.

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/encounters.h"

namespace soulwarden::backend::tests::encfix {

using namespace gather;
using questfix::json;

inline constexpr const char* kStartSlice = "slices/test/encstart.dynamicslice";
inline constexpr const char* kMemberSlice = "slices/test/member.dynamicslice";
inline constexpr const char* kSuccessEvent = "Test_Encounter_Success";
inline constexpr const char* kStartEvent = "Test_EncStart_Interact";
inline constexpr std::uint32_t kManagerSlot = 2;
inline constexpr std::uint32_t kPlaceSlot = 3;
inline constexpr std::uint32_t kObjectivesSlot = 4;
inline constexpr std::uint32_t kCleanupSlot = 5;
inline constexpr std::uint32_t kSetupSlot = 6;
inline constexpr std::uint32_t kKillSlot = 7;
inline constexpr std::uint32_t kInteractSlot = 8;
inline constexpr std::uint32_t kTypeMetadata = 10;
inline constexpr std::uint32_t kTypePlace = 13;
inline constexpr std::uint32_t kTypeManager = 6786;
inline constexpr std::uint32_t kTypeStage = 2133;
inline constexpr std::uint32_t kTypeObjectives = 3857;
inline constexpr std::uint32_t kTypeVariation = 14;
/** The VariationData slot of the start object slice and the row index of its variant (733: the one of the live Obelisk start object). */
inline constexpr std::uint32_t kVariationSlot = 1;
inline constexpr std::uint16_t kVariant = 733;
inline constexpr std::array<std::uint8_t, 16> kRootAsset{0xE4, 0xC0, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
inline constexpr std::array<std::uint8_t, 16> kStartAsset{0xE4, 0xC0, 0x00, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x03};
inline constexpr std::uint64_t kStartRoot = 0x0000E4C000000001ULL;
inline constexpr std::uint64_t kStartBoundEntity = 0x0000E4C000000011ULL;
inline constexpr std::uint64_t kStartComponent = 0x00E4C0FFEE000001ULL;
inline constexpr std::uint64_t kStartOption = 0x0000E4C000000021ULL;
inline constexpr state::WorldPosition kRootAt{100.0F, 205.0F, 30.0F};
inline constexpr state::WorldPosition kStartAt{100.0F, 201.0F, 30.0F};
inline constexpr state::WorldPosition kMemberAt{103.0F, 206.0F, 30.0F};
inline constexpr const char* kKey = "e4c00000-0000-4000-8000-000000000001/17";
inline constexpr const char* kAdminToken = "token-of-the-encounter-test-0123456789ab";

inline std::string hex16(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llX", static_cast<unsigned long long>(value));
    return text;
}

inline json place_json(const state::WorldPosition& place, float yaw = 0.0F) {
    return {{"position", {place.x, place.y, place.z}}, {"rotation", {0.0, 0.0, 0.0, 1.0}}, {"yaw", yaw}};
}

inline json stage_json(const char* name, std::uint64_t entity, std::uint32_t slot, bool userWave, json objective, json spawns) {
    return {{"name", name},           {"entity", hex16(entity)},       {"component", hex16(entity + 0x1000)}, {"slot", slot},
            {"userWave", userWave},   {"children", json::array()},     {"childrenOptional", false},          {"disableSpawners", true},
            {"objective", objective}, {"startActions", json::array()}, {"endActions", json::array()},        {"spawns", spawns}};
}

inline json timed(double seconds) {
    return {{"type", "InvasionWaveObjective"}, {"timeLimit", seconds}, {"succeedOnTimeout", true}, {"failureCondition", false}};
}

inline json destroyed(std::uint32_t count, const char* tracked = "") {
    return {{"type", "SliceDestroyedObjective"}, {"timeLimit", 0},          {"succeedOnTimeout", false},  {"failureCondition", false},
            {"requiredCount", count},            {"trackedSlice", tracked}, {"trackedAlias", ""},         {"title", "@Test_Title"},
            {"description", "@Test_Kill"}};
}

/** One recipe of the form of the Obelisk recipe at a place, with a key. */
inline json recipe_json(const char* key, const state::WorldPosition& root, const state::WorldPosition& start, const state::WorldPosition& member,
                        std::vector<std::string> refused = {}) {
    const json spawnStart = json::array({{{"member", kStartSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(start)})}}});
    const json spawnKill = json::array({{{"member", kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(member)})}},
                                        {{"member", kMemberSlice}, {"delay", 5}, {"count", 1}, {"locations", json::array({place_json(member)})}}});
    return {{"key", key},
            {"slice", "slices/test/enc_test.dynamicslice"},
            {"asset", gather::hex_bytes(kRootAsset)},
            {"sub", 2},
            {"root", hex16(0x00007C0000000001ULL)},
            {"rootName", "Enc_Test_Defenders"},
            {"layer", "test"},
            {"position", {root.x, root.y, root.z}},
            {"rotation", {0.0, 0.0, 0.0, 1.0}},
            {"yaw", 0.0},
            {"slots", {{"manager", kManagerSlot}, {"place", kPlaceSlot}, {"objectives", kObjectivesSlot}}},
            {"manager",
             {{"autoActivate", true}, {"autoActivateDelayMs", 0}, {"gracePeriod", 10}, {"destroyOnCompletion", true}, {"resetEvent", ""},
              {"autoPerceiveParticipants", false}}},
            {"tracker", {{"component", hex16(11980792288100801750ULL)}, {"grace", 10}, {"addedEvent", ""}}},
            {"detector", {{"radius", 30.0}, {"filter", "OnlyPlayerTarget"}, {"sphere", true}}},
            {"successEvent", kSuccessEvent},
            {"questEvents", {gamedata::lower_text(kSuccessEvent)}},
            {"parent", {{"kind", "area"}, {"maintainPopulation", true}, {"spawnOnEnable", true}, {"liveCount", 1}, {"respawn", {10.0, 15.0}}}},
            {"stageOrder", {0, 1, 2, 3}},
            {"stages", json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                                    stage_json("Wave_01: Interact with IO", 0x200, kInteractSlot, true, destroyed(1, kStartSlice), spawnStart),
                                    stage_json("Wave_02 - Objective: Kill AI", 0x300, kKillSlot, true, destroyed(2), spawnKill),
                                    stage_json("Wave_03 - Cleanup", 0x400, kCleanupSlot, false, timed(3), json::array())})},
            {"unreachable", json::array()},
            {"pattern", "a kill wave"},
            {"refused", refused}};
}

inline json members_json() {
    return {{kStartSlice,
             {{"kind", "object"}, {"variant", "Test_EncStart"}, {"table", "Gatherable_Quest"}, {"row", "Test_EncStart"}, {"event", kStartEvent},
              {"tag", "IO_Test_EncStart"}, {"onlyOnQuest", false}, {"proximity", false}}},
            {kMemberSlice,
             {{"kind", "creature"}, {"vitals", "Test_Member"}, {"category", "Test_Member_Category"}, {"level", 20},
              {"body", "slices/test/bag-wolf.dynamicslice"}, {"bodySlices", {"slices/test/bag-wolf.dynamicslice"}}}}};
}

inline json encounter_document(const std::vector<json>& recipes) {
    return {{"format", 1}, {"recipes", recipes}, {"members", members_json()}};
}

inline json encounter_quests() {
    json document{{"format", 1},           {"objectives", json::array()}, {"tasks", json::object()}, {"achievements", json::array()},
                  {"npa", json::array()}, {"territories", json::array()}, {"backstories", json::array()}};
    const auto add = [&](const char* objective, const char* task, const char* type, std::uint32_t code, json cells) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, type, code, cells);
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add("Test_Defend", "Test_Defend_Encounter", "taskgameevent", gamedata::task_type::kGameEvent,
        {{"KillEnemyType", kSuccessEvent}, {"TargetQty", "1"}});
    add("Test_Examine", "Test_Examine_Device", "taskgameevent", gamedata::task_type::kGameEvent, {{"KillEnemyType", kStartEvent}, {"TargetQty", "1"}});
    add("Test_Member_Hunt", "Test_Member_Kill", "taskkillcontribution", 13, {{"KillEnemyType", "Test_Member_Category"}, {"TargetQty", "5"}});
    add("Test_Refused_Defend", "Test_Refused_Encounter", "taskgameevent", gamedata::task_type::kGameEvent,
        {{"KillEnemyType", "Test_Refused_Success"}, {"TargetQty", "1"}});
    return document;
}

/** The world of the gathering cases with the data of an encounter: the member kind, the start object template and its row, the recipes. */
struct EncounterShop : GatherShop {
    explicit EncounterShop(const std::vector<json>& recipes, const std::string& database = ":memory:") : GatherShop(database) {
        gamedata::CreatureKind member = bags::bag_wolf_kind();
        member.id = "worldevent_test_member";
        member.vitals = "Test_Member";
        member.category = "Test_Member_Category";
        member.vitalsId = 0x7E570001;
        member.categoryId = 0x7E570002;
        member.member = kMemberSlice;
        member.points = 0;
        if (!host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind(), member}, {}, {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("encounter test: the creature data was not set: " + host.creatures.error());
        }
        host.creatures.set_members({{kMemberSlice, {1, ""}}});
        // The start object: a server child with a GatherOption bound (the template of interactions.py, member encounterObjects).
        gamedata::InteractionTemplate start;
        start.slice = kStartSlice;
        start.kind = gamedata::InteractionKind::gathering;
        start.asset = kStartAsset;
        start.sub = 2;
        start.root = kStartRoot;
        start.positionSlot = 2;
        gamedata::InteractionBound bound;
        bound.slot = 4;
        bound.component = kStartComponent;
        bound.position = state::WorldPosition{0.0F, 0.0F, 0.0F};
        bound.rangeSq = 9.0F;
        bound.capacity = 1;
        bound.arc = 180.0F;
        bound.entity = kStartBoundEntity;
        bound.options = {{kGatherOptionIndex, kStartOption}};
        bound.tag = "IO_Test_EncStart";
        start.bounds.push_back(bound);
        if (!host.interactions.set({option_row("GatherOption", 14, true)}, {},
                                   {object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex})}, {start})
            || !host.interactions.set_encounter_objects({{kStartSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}}})) {
            throw std::runtime_error("encounter test: the interaction data was not set: " + host.interactions.error());
        }
        // The gathering row of the start object (a quest row of Gatherable_Quest, 1 s, its game event) and its template.
        json gathering = gathering_document();
        json row = row_json("Test_EncStart", "Gatherables", "None", "NoToolNecessary", "", 0, 1.0, 200.0, 300.0, "Empty", kStartEvent, true);
        row["quest"] = true;
        gathering["rows"].push_back(row);
        json slice = template_json(kOreSlice, "encstart", "Gatherable_Quest");
        slice["slice"] = kStartSlice;
        gathering["templates"].push_back(slice);
        gathering["encounterObjects"] = {{kStartSlice, {gathering["templates"].size() - 1, gathering["rows"].size() - 1, "IO_Test_EncStart", false}}};
        if (!host.gathering.set(gathering.dump(), {{kRegion, region_records()}})) {
            throw std::runtime_error("encounter test: the gathering data was not set: " + host.gathering.error());
        }
        if (!host.quests.load_text(encounter_quests().dump())) {
            throw std::runtime_error("encounter test: the quest data did not load: " + host.quests.error());
        }
        if (!host.encounters.load_text(encounter_document(recipes).dump())) {
            throw std::runtime_error("encounter test: the encounter data did not load: " + host.encounters.error());
        }
        host.world.set_creature_behavior(false);
    }

    [[nodiscard]] world::Encounters& encounters() {
        return host.world.encounters();
    }

    /** The world goes on by `seconds` in steps of 10 ms; the clients report their place each 100 ms. */
    void wait(float seconds) {
        for (float passed = 0.0F; passed < seconds - 0.001F; passed += 0.1F) {
            report(clientA, std::nullopt);
            report(clientB, std::nullopt);
            host.steps(10, {&clientA, &clientB});
        }
    }

    [[nodiscard]] const world::EncounterInstance* instance(const char* key = kKey) {
        const auto index = encounters().placement_of(key);
        for (const auto& [root, instance] : encounters().instances()) {
            if (index && instance.placement == *index) {
                return &instance;
            }
        }
        return nullptr;
    }

    /** @return The live entities of the members of the instance (creatures, or start objects). */
    [[nodiscard]] std::vector<world::Entity*> members(bool objects, const char* key = kKey) {
        std::vector<world::Entity*> found;
        if (const world::EncounterInstance* run = instance(key)) {
            for (const auto& [id, member] : run->members) {
                world::Entity* entity = host.world.find(id);
                if (entity != nullptr && member.object == objects && !member.done) {
                    found.push_back(entity);
                }
            }
        }
        return found;
    }

    /** @return The block of GatherOption of a start object (the XOR rule with the entity id of the object as G). */
    [[nodiscard]] static InteractionBlock start_block(const world::Entity& object) {
        const std::uint64_t k = kStartRoot ^ object.id;
        InteractionBlock block;
        block.actor = object.actor.bytes;
        block.group = object.id;
        block.target = (kStartBoundEntity ^ k) + kStartComponent;
        block.action = 14;
        block.baseAction = 14;
        block.optionEntity = kStartOption ^ k;
        block.committed = true;
        block.optionId = gamedata::interaction_option_id("GatherOption");
        block.text88 = "@Test_EncStart";
        return block;
    }

    /** A kill of a member by a character (the hurt path: the credit and the death). */
    void kill(world::Entity& member, const char* character) {
        (void)host.world.creatures().hurt(member.id, bags::kWolfHealth * 4.0F, entity(character)->id);
        host.steps(3, {&clientA, &clientB});
    }

    /** Moves a character to a place (the travel of the world) and keeps its reports there. */
    void move(const char* character, const state::WorldPosition& place) {
        (void)host.world.travel(character, place, world::TravelScreen::off);
        (std::string(character) == kCharacterA ? placeA : placeB) = place;
        wait(0.5F);
    }

    [[nodiscard]] std::uint32_t progress(const char* character, const char* objective) {
        const world::QuestInstance* quest = questfix::quest_of(*entity(character), objective);
        return quest != nullptr && !quest->tasks.empty() ? quest->tasks[0].progress : 9999;
    }
};

/** What a client got for the root of an encounter (by the asset of its metadata). */
struct RootSeen {
    std::uint32_t interest{0};
    std::size_t records{0};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> first;
    std::vector<std::uint32_t> managers;
    std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>> stages;
    std::vector<questfix::ObjectivesState> objectives;
    std::size_t stops{0};
};

inline std::vector<std::pair<std::uint32_t, std::uint32_t>> stage_entries(const tests::Fragment& fragment) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
    const auto value = fragment.fields.find({0, 0});
    if (value == fragment.fields.end()) {
        return entries;
    }
    server::hub::Reader reader(value->second);
    for (std::uint32_t count = reader.size(), index = 0; index < count && reader.ok(); ++index) {
        const std::uint32_t sequence = reader.u32();
        entries.emplace_back(sequence, reader.u32());
    }
    return entries;
}

inline std::uint32_t u32_of(const Bytes& bytes) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4 && index < bytes.size(); ++index) {
        value = (value << 8U) | bytes[index];
    }
    return value;
}

/** @return What a client got for the newest root (the last record with the metadata of the asset) from a message index on. */
inline RootSeen root_seen(const FakeClient& client, std::size_t from = 0) {
    RootSeen out;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const tests::Bundle bundle = read_bundle(client.got[index].body);
        for (const tests::Record& record : bundle.records) {
            for (const tests::Fragment& fragment : record.fragments) {
                const auto field = fragment.fields.find({0, 0});
                if (fragment.type == kTypeMetadata && field != fragment.fields.end() && field->second.size() >= 16
                    && std::equal(kRootAsset.begin(), kRootAsset.end(), field->second.begin())) {
                    out = RootSeen{};
                    out.interest = record.interest;
                    for (const tests::Fragment& part : record.fragments) {
                        out.first.emplace_back(part.slot, part.type);
                    }
                }
            }
            if (out.interest == 0 || record.interest != out.interest) {
                continue;
            }
            ++out.records;
            for (const tests::Fragment& fragment : record.fragments) {
                if (fragment.type == kTypeManager && fragment.fields.contains({0, 0})) {
                    out.managers.push_back(u32_of(fragment.fields.at({0, 0})));
                } else if (fragment.type == kTypeStage) {
                    out.stages[fragment.slot] = stage_entries(fragment);
                } else if (fragment.type == kTypeObjectives && fragment.slot == kObjectivesSlot) {
                    out.objectives.push_back(questfix::decode_objectives(fragment));
                }
            }
        }
        for (const std::uint32_t stop : bundle.stops) {
            out.stops += out.interest != 0 && stop == out.interest ? 1 : 0;
        }
    }
    return out;
}

/** @return The fields of the last VariationData fragment (type 14) of the record of the start object that a client got (its asset). */
inline std::optional<std::map<std::pair<unsigned, unsigned>, tests::Bytes>> variation_seen(const FakeClient& client) {
    std::optional<std::map<std::pair<unsigned, unsigned>, tests::Bytes>> out;
    for (const auto& message : client.got) {
        if (message.type != 8) {
            continue;
        }
        for (const tests::Record& record : read_bundle(message.body).records) {
            bool start = false;
            for (const tests::Fragment& fragment : record.fragments) {
                const auto field = fragment.fields.find({0, 0});
                start = start
                        || (fragment.type == kTypeMetadata && field != fragment.fields.end() && field->second.size() >= 16
                            && std::equal(kStartAsset.begin(), kStartAsset.end(), field->second.begin()));
            }
            for (const tests::Fragment& fragment : record.fragments) {
                if (start && fragment.type == kTypeVariation && fragment.slot == kVariationSlot) {
                    out = fragment.fields;
                }
            }
        }
    }
    return out;
}

inline bool has(const RootSeen& view, std::uint32_t slot, std::uint32_t type) {
    return std::find(view.first.begin(), view.first.end(), std::make_pair(slot, type)) != view.first.end();
}

/** @return The field 5 lists (the encounter roots) of the player Objectives states that a client got from a message index on. */
inline std::vector<std::vector<std::uint64_t>> links_seen(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<std::uint64_t>> lists;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const tests::Record& record : read_bundle(client.got[index].body).records) {
            for (const tests::Fragment& fragment : record.fragments) {
                if (fragment.type == kTypeObjectives && fragment.slot == server::rep::kObjectivesSlot) {
                    const questfix::ObjectivesState state = questfix::decode_objectives(fragment);
                    if (state.hasPoi) {
                        lists.push_back(state.poi);
                    }
                }
            }
        }
    }
    return lists;
}

/** @return The progress and state of a task element of the last root Objectives state of a view (its index), or no value. */
inline std::optional<std::pair<std::uint32_t, std::uint8_t>> root_task(const RootSeen& view, std::uint8_t index) {
    if (view.objectives.empty()) {
        return std::nullopt;
    }
    for (const auto& task : view.objectives.back().tasks) {
        if (task.index == index) {
            return std::pair{task.progress, task.state};
        }
    }
    return std::nullopt;
}

inline std::vector<std::uint32_t> statuses(const RootSeen& view, std::uint32_t slot) {
    std::vector<std::uint32_t> out;
    const auto found = view.stages.find(slot);
    if (found != view.stages.end()) {
        for (const auto& [sequence, status] : found->second) {
            out.push_back(status);
        }
    }
    return out;
}

/** The start of a run: both enter, 2709 of both, the quests of each one; the instance with its root. */
inline void begin(EncounterShop& shop, bool questA = true, bool questB = true) {
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    shop.wait(0.5F);
    for (const auto& [character, wanted] : {std::pair{kCharacterA, questA}, std::pair{kCharacterB, questB}}) {
        if (wanted) {
            (void)shop.host.world.quests().grant(*shop.entity(character), "Test_Defend", "command", 0);
            (void)shop.host.world.quests().grant(*shop.entity(character), "Test_Examine", "command", 0);
        }
    }
    shop.wait(0.5F);
}

} // namespace soulwarden::backend::tests::encfix

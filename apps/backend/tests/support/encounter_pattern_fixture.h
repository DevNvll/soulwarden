#pragma once

// The pattern fixture of the encounter cases (stage QG5c3, tests/encounter_pattern_tests.cpp; stage QG5c4, tests/encounter_sequence_tests.cpp
// and tests/review_qg5c3_tests.cpp; docs/systems/encounters.md): the members of the patterns (a direct boss, a proximity object, an area
// trigger, a member that ends itself, a visual, a creature member that the client does not show), the summoned opponent and the ambush
// recipes, their quests, and the world with them. All slices, kinds, places, recipes, quests, and accounts are made up.

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/encounter_fixture.h"

namespace soulwarden::backend::tests::encfix {

inline constexpr const char* kBossSlice = "slices/test/worldevent_ai_test_boss.dynamicslice";
inline constexpr const char* kAmbushSlice = "slices/test/io_test_ambush.dynamicslice";
inline constexpr const char* kTriggerSlice = "slices/test/ai_encounter_area_trigger_test.dynamicslice";
inline constexpr const char* kSmokeSlice = "slices/test/test_smoke_burst.dynamicslice";
inline constexpr const char* kVisualSlice = "slices/test/test_cannon_storm.dynamicslice";
inline constexpr const char* kHiddenSlice = "slices/test/worldevent_ai_hidden.dynamicslice";
inline constexpr const char* kAmbushEvent = "Test_Ambush_Interact";
inline constexpr const char* kAddedEvent = "Test_Ambush_Added";
inline constexpr const char* kAmbushSuccess = "Test_Ambush_Complete";
inline constexpr const char* kTriggerEvent = "Test_Trigger_Interact";
inline constexpr const char* kAmbushTag = "IO_Test_Ambush";
inline constexpr std::uint32_t kChildSlotA = 9;
inline constexpr std::uint32_t kChildSlotB = 10;

inline json pattern_members() {
    json members = members_json();
    members[kBossSlice] = {{"kind", "creature"}, {"vitals", "Test_Boss"},          {"category", ""}, {"level", 20},
                           {"body", kBossSlice}, {"bodySlices", {kBossSlice}}, {"direct", true}};
    members[kAmbushSlice] = {{"kind", "object"},     {"variant", "Test_Ambush"}, {"table", "Gatherable_Quest_Proximity"},
                             {"row", "Test_Ambush"}, {"event", kAmbushEvent},    {"tag", kAmbushTag},
                             {"onlyOnQuest", true},  {"proximity", true},        {"interact", true},
                             {"radius", 5.0}};
    members[kTriggerSlice] = {{"kind", "object"},   {"variant", ""},     {"table", ""},          {"row", "PressurePlate_Test"},
                              {"event", kTriggerEvent}, {"tag", ""},     {"onlyOnQuest", false}, {"proximity", true},
                              {"interact", false},  {"radius", 20.0}};
    members[kSmokeSlice] = {{"kind", "unknown"}, {"cause", "no vitals row and no start object; it ends itself 4.2 s after its spawn"},
                            {"lifetime", 4.2}};
    members[kVisualSlice] = {{"kind", "unknown"}, {"cause", "no vitals row and no start object"}};
    members[kHiddenSlice] = {{"kind", "creature"}, {"vitals", "Test_Hidden"}, {"body", "slices/test/hidden_body.dynamicslice"}};
    return members;
}

inline json spawn_json(const char* member, double delay, int count, const state::WorldPosition& at) {
    return {{"member", member}, {"delay", delay}, {"count", count}, {"locations", json::array({place_json(at)})}};
}

/** A recipe of the fixture at a place with other stages and a stage order. */
inline json pattern_recipe(const char* key, const state::WorldPosition& root, json stages, json order) {
    json recipe = recipe_json(key, root, root, root);
    recipe["stages"] = std::move(stages);
    recipe["stageOrder"] = std::move(order);
    return recipe;
}

/** The summoned opponent: a timed setup (3 s), the start object, the boss (a direct creature member, 1 s after the start), a cleanup. */
inline json summon_recipe(const char* key = kKey) {
    return pattern_recipe(
        key, kRootAt,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                     stage_json("Wave_01 - Objective: Interact", 0x200, kInteractSlot, false, destroyed(1, kStartSlice),
                                json::array({spawn_json(kStartSlice, 0, 1, kStartAt)})),
                     stage_json("Wave_02 - Objective: Kill the Boss", 0x300, kKillSlot, false, destroyed(1, kBossSlice),
                                json::array({spawn_json(kBossSlice, 1, 1, kMemberAt)})),
                     stage_json("Wave_03 - Cleanup", 0x400, kCleanupSlot, false, timed(3), json::array())}),
        {0, 1, 2, 3});
}

/**
 * The ambush (the form of 99E_MSQ_04 BlightAmbush): a timed setup (3 s), the proximity object, the kill stage (it tracks the member, needs 3,
 * and spawns nothing) with two timed child stages (10 s each: 2 members at once; 1 member at 2 s and a visual), a cleanup; the participant
 * event and the success event of the ambush.
 */
inline json ambush_recipe(const char* key = kKey, const state::WorldPosition& root = kRootAt, const state::WorldPosition& start = kStartAt) {
    const state::WorldPosition member{root.x + 3.0F, root.y + 1.0F, root.z};
    json recipe = pattern_recipe(
        key, root,
        json::array({stage_json("Wave_00 - Setup", 0x100, kSetupSlot, false, timed(3), json::array()),
                     stage_json("Wave_01: Interact with IO", 0x200, kInteractSlot, false, destroyed(1, kAmbushSlice),
                                json::array({spawn_json(kAmbushSlice, 0, 1, start)})),
                     stage_json("Wave_02 - Objective: Kill AI", 0x300, kKillSlot, false, destroyed(3, kMemberSlice), json::array()),
                     stage_json("Wave_03 - Cleanup", 0x400, kCleanupSlot, false, timed(3), json::array()),
                     stage_json("Subwave01_Basepop", 0x500, kChildSlotA, false, timed(10), json::array({spawn_json(kMemberSlice, 0, 2, member)})),
                     stage_json("Subwave02_Basepop", 0x600, kChildSlotB, false, timed(10),
                                json::array({spawn_json(kMemberSlice, 2, 1, member), spawn_json(kVisualSlice, 0, 1, member)}))}),
        {0, 1, 2, 3});
    recipe["stages"][2]["children"] = {4, 5};
    recipe["tracker"]["addedEvent"] = kAddedEvent;
    recipe["successEvent"] = kAmbushSuccess;
    recipe["questEvents"] = {gamedata::lower_text(kAmbushSuccess), gamedata::lower_text(kAmbushEvent)};
    return recipe;
}

inline json pattern_quests() {
    json document = encounter_quests();
    const auto add = [&](const char* objective, const char* task, json cells) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, "taskgameevent", gamedata::task_type::kGameEvent, cells);
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add("Test_Ambush", "Test_Ambush_Start", {{"KillEnemyType", kAmbushEvent}, {"InteractTag", kAmbushTag}, {"TargetQty", "1"}});
    add("Test_Ambush_Join", "Test_Ambush_Join_Task", {{"KillEnemyType", kAddedEvent}, {"TargetQty", "1"}});
    add("Test_Ambush_Win", "Test_Ambush_Win_Task", {{"KillEnemyType", kAmbushSuccess}, {"TargetQty", "1"}});
    add("Test_Trigger", "Test_Trigger_Task", {{"KillEnemyType", kTriggerEvent}, {"TargetQty", "1"}});
    return document;
}

/** The world of the encounter cases with the members of the patterns (the proximity object takes the template of the start object). */
struct PatternShop : EncounterShop {
    explicit PatternShop(const std::vector<json>& recipes, json members = pattern_members(), const std::string& database = ":memory:")
        : EncounterShop({}, database) {
        host.creatures.set_members({{kMemberSlice, {1, ""}}, {kBossSlice, {1, ""}}, {kHiddenSlice, {-1, "the client does not show its slice"}}});
        if (!host.interactions.set_encounter_objects({{kStartSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}},
                                                      {kAmbushSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}}})) {
            throw std::runtime_error("pattern test: the start object templates were not set: " + host.interactions.error());
        }
        if (!host.quests.load_text(pattern_quests().dump())) {
            throw std::runtime_error("pattern test: the quest data did not load: " + host.quests.error());
        }
        const json document{{"format", 1}, {"recipes", recipes}, {"members", std::move(members)}};
        if (!host.encounters.load_text(document.dump())) {
            throw std::runtime_error("pattern test: the encounter data did not load: " + host.encounters.error());
        }
    }

    void grant(const char* character, const char* objective) {
        (void)host.world.quests().grant(*entity(character), objective, "command", 0);
    }

    /** @return The live members of the instance with no entity (an area trigger, a member that ends itself), and the ones that are done. */
    [[nodiscard]] std::pair<std::size_t, std::size_t> no_entity(const char* key = kKey) {
        std::pair<std::size_t, std::size_t> out{0, 0};
        if (const world::EncounterInstance* run = instance(key)) {
            for (const auto& [id, member] : run->members) {
                if (member.noEntity) {
                    ++(member.done ? out.second : out.first);
                }
            }
        }
        return out;
    }
};


} // namespace soulwarden::backend::tests::encfix

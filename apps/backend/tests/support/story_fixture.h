#pragma once

// The world of the story instance cases (stage QG9a, tests/story_instance_tests.cpp; code review of stage QG9a, tests/review_qg9a_tests.cpp;
// stage QG9b, tests/story_mode_tests.cpp; docs/systems/instances.md): the made-up game mode Test_Story of the main world level (the form of
// QuestMedea of the game data: a setup stage of the manager, an intro cutscene, a boss fight with an event child, a cleanup), its entrance, its
// recipe, its exit portal, and its quests. All accounts, characters, ids, and places are made up.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "server/rep/instance_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "world/instances.h"

namespace soulwarden::backend::tests::storyfix {

using namespace encfix;

inline constexpr const char* kMode = "Test_Story";
inline constexpr const char* kStoryKey = "e4c00000-0000-4000-8000-0000000000a1/17";
inline constexpr const char* kStorySlice = "slices/test/story_boss_encounter.dynamicslice";
inline constexpr const char* kIntro = "Test_Story_Intro_IGC";
inline constexpr const char* kPortalSlice = "slices/test/story_portal.dynamicslice";
inline constexpr const char* kStoryObjective = "Test_Story_Enter";
inline constexpr const char* kMutatedObjective = "Test_Story_Mutated";
inline constexpr const char* kBossObjective = "Test_Story_Boss";
inline constexpr state::WorldPosition kEntranceAt{100.0F, 202.0F, 30.0F};
inline constexpr state::WorldPosition kStoryStart{300.0F, 300.0F, 30.0F};
inline constexpr state::WorldPosition kStoryRoot{300.0F, 306.0F, 30.0F};
inline constexpr state::WorldPosition kBossAt{300.0F, 308.0F, 30.0F};
inline constexpr state::WorldPosition kPortalAt{302.0F, 300.0F, 30.0F};
inline constexpr std::uint8_t kEntranceTag = 0x31;
inline constexpr std::uint64_t kEntranceTarget = 0x53B85D6500000031ULL;
inline constexpr std::uint32_t kOpenMenuIndex = 1;
inline constexpr std::uint32_t kEnterIndex = 2;
inline constexpr std::uint32_t kExitIndex = 3;
inline constexpr std::uint64_t kPortalRoot = 0x0000E4C0000000A1ULL;
inline constexpr std::uint64_t kPortalBound = 0x0000E4C0000000B1ULL;
inline constexpr std::uint64_t kPortalComponent = 0x00E4C0FFEE0000A1ULL;
inline constexpr std::uint64_t kPortalOption = 0x0000E4C0000000C1ULL;
inline constexpr std::array<std::uint8_t, 16> kStoryAsset{0xE4, 0xC0, 0x00, 0x0A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04};
inline constexpr std::array<std::uint8_t, 16> kPortalAsset{0xE4, 0xC0, 0x00, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x05};

inline json objective_of(const char* type, double limit, bool succeed) {
    return {{"type", type}, {"timeLimit", limit}, {"succeedOnTimeout", succeed}, {"failureCondition", false}};
}

/** The recipe of the mode (the form of the QuestMedea recipe of the tool): stages 0 to 3 of the manager, stage 4 the child of the fight. */
inline json story_recipe() {
    json setup = stage_json(
        "Phase 00 - Setup", 0x500, 8, false, objective_of("WaitForParentObjective", 0, false),
        json::array(
            {{{"member", kStartSlice}, {"delay", 0}, {"count", 1}, {"skip", true}, {"locations", json::array({place_json(kStoryRoot)})}},
             {{"member", kMemberSlice}, {"delay", 0}, {"count", 1}, {"locations", json::array({place_json(kBossAt)})}}}));
    json intro = objective_of("CutsceneFinishedObjective", 20, true);
    intro["cutscene"] = kIntro;
    json fight = stage_json("Phase 01 - Boss Fight", 0x600, 7, true, destroyed(1, kMemberSlice), json::array());
    fight["children"] = {4};
    fight["childrenOptional"] = true;
    json event = objective_of("EncounterEventObjective", 0, false);
    event["eventId"] = "Intro_Complete";
    json recipe = recipe_json(kStoryKey, kStoryRoot, kStoryRoot, kBossAt);
    recipe["slice"] = kStorySlice;
    recipe["asset"] = gather::hex_bytes(kStoryAsset);
    recipe["root"] = hex16(0x00007C00000000A1ULL);
    recipe["rootName"] = "Test_Story_Encounter";
    recipe["mode"] = kMode;
    recipe["map"] = kMode;
    recipe["daily"] = false;
    recipe["successEvent"] = "";
    recipe["questEvents"] = json::array();
    recipe["detector"] = {{"radius", 20.0}, {"filter", "OnlyPlayerTarget"}, {"sphere", true}};
    recipe["parent"] = {
        {"kind", "prefab"}, {"maintainPopulation", false}, {"spawnOnEnable", false}, {"liveCount", 0}, {"respawn", {15.0, 15.0}}};
    recipe["stageOrder"] = {0, 1, 2, 3};
    recipe["stages"] = json::array({setup, stage_json("Phase 00b - Play Intro IGC", 0x700, 5, true, intro, json::array()), fight,
                                    stage_json("Phase 02 - Cleanup", 0x800, 9, true, timed(1), json::array()),
                                    stage_json("01 Intro - Play Intro", 0x900, 10, true, event, json::array())});
    return recipe;
}

inline json story_document() {
    json document = encounter_document({story_recipe()});
    document["format"] = 2;
    document["instanceObjects"] = json::array({{{"mode", kMode},
                                                {"map", kMode},
                                                {"key", "e4c00000-0000-4000-8000-0000000000b1/18"},
                                                {"slice", kPortalSlice},
                                                {"spawner", "Portal"},
                                                {"position", {kPortalAt.x, kPortalAt.y, kPortalAt.z}},
                                                {"yaw", 180.0},
                                                {"onInit", false}}});
    return document;
}

inline gamedata::GameMap story_map(std::uint32_t requiredLevel = 0, std::uint32_t timeout = 600, const char* mode = kMode) {
    gamedata::GameMap map;
    map.id = mode;
    map.mode = mode;
    map.level = world::kMainLevel;
    map.terrain = world::kMainTerrain;
    map.starts = {{kStoryStart, 90.0F}};
    map.nameKey = "@Test_Story";
    map.name = "Test Story";
    map.requiredLevel = requiredLevel;
    map.teamCapacity = 0;
    map.dungeon = true;
    map.soloTrial = true;
    map.timeoutSeconds = timeout;
    return map;
}

/** The quests of the mode: TaskGameMode (one needs a mutated mode) and a kill of the boss category in the mode. */
inline json story_quests() {
    json document = encounter_quests();
    const auto add = [&](const char* objective, const char* task, const char* type, std::uint32_t code, json cells) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, type, code, cells);
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add(kStoryObjective, "Test_Story_Enter_Mode", "taskgamemode", gamedata::task_type::kGameMode, {{"GameModeID", kMode}});
    add(kMutatedObjective, "Test_Story_Mutated_Mode", "taskgamemode", gamedata::task_type::kGameMode,
        {{"GameModeID", kMode}, {"GameModeMustBeMutated", "1"}});
    add(kBossObjective, "Test_Story_Boss_Kill", "taskkillcontribution", gamedata::task_type::kKillContribution,
        {{"KillEnemyType", "Test_Member_Category"}, {"TargetQty", "1"}, {"GameModeID", kMode}});
    return document;
}

/** The world of the encounter cases with the game mode Test_Story: its map, its entrance near A and B, its recipe, its portal, its quests. */
struct StoryShop : EncounterShop {
    /** `database`: a file of the store (code review of stage QG9a, finding QG9AR-R6: a trigger of the store refuses a row). */
    /** Stage QG9b: `document` and `quests`: the encounter data and the quest data of a case (default: the QuestMedea form of stage QG9a). */
    /** Stage QG9b: `mode`: the game mode of the map and of the entrance (the setting of QuestMedusa, case SY5). */
    explicit StoryShop(const std::string& database = ":memory:", const json& document = story_document(),
                       const json& quests = story_quests(), const char* mode = kMode)
        : EncounterShop({}, database) {
        check(host.gameModes.set({story_map(0, 600, mode)}), "setup: the map of the mode (the level of the main world)");
        std::vector<gamedata::InteractionOption> options{option_row("GatherOption", 14, true), option_row("OpenDungeonMenu", 71, true),
                                                         option_row("EnterQuestTest", 72, false), option_row("ExitDungeon", 74, false)};
        options[kEnterIndex].requiredItem = "KeyDynasty";
        options[kEnterIndex].requiredItemCount = 0;
        gamedata::InteractionObject entrance =
            object_of(kEntranceTag, InteractionKind::dungeonEntrance, kEntranceAt, {kOpenMenuIndex, kEnterIndex});
        gamedata::DungeonEntrance door;
        door.mode = mode;
        door.solo = true;
        door.slot = 3;
        door.target = kEntranceTarget;
        door.exit = state::WorldPosition{100.0F, 204.0F, 30.0F};
        entrance.entrance = door;
        // The templates: the start object of the encounter fixture (index 0) and the exit portal (index 1).
        gamedata::InteractionTemplate start;
        start.slice = kStartSlice;
        start.kind = InteractionKind::gathering;
        start.asset = kStartAsset;
        start.sub = 2;
        start.root = kStartRoot;
        start.positionSlot = 2;
        gamedata::InteractionBound startBound;
        startBound.slot = 4;
        startBound.component = kStartComponent;
        startBound.rangeSq = 9.0F;
        startBound.capacity = 1;
        startBound.arc = 180.0F;
        startBound.entity = kStartBoundEntity;
        startBound.options = {{kGatherOptionIndex, kStartOption}};
        startBound.tag = "IO_Test_EncStart";
        start.bounds.push_back(startBound);
        gamedata::InteractionTemplate portal;
        portal.slice = kPortalSlice;
        portal.kind = InteractionKind::dungeonExit;
        portal.asset = kPortalAsset;
        portal.sub = 2;
        portal.root = kPortalRoot;
        portal.positionSlot = 2;
        gamedata::InteractionBound portalBound;
        portalBound.slot = 4;
        portalBound.component = kPortalComponent;
        portalBound.rangeSq = 9.0F;
        portalBound.arc = 180.0F;
        portalBound.entity = kPortalBound;
        portalBound.options = {{kExitIndex, kPortalOption}};
        portal.bounds.push_back(portalBound);
        if (!host.interactions.set(std::move(options), {},
                                   {object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex}), entrance},
                                   {start, portal})
            || !host.interactions.set_encounter_objects({{kStartSlice, gamedata::EncounterObject{0, kVariationSlot, kVariant}}})) {
            throw std::runtime_error("story test: the interaction data was not set: " + host.interactions.error());
        }
        host.interactions.set_instance_objects({{kPortalSlice, 1}});
        if (!host.quests.load_text(quests.dump())) {
            throw std::runtime_error("story test: the quest data did not load: " + host.quests.error());
        }
        if (!host.encounters.load_text(document.dump())) {
            throw std::runtime_error("story test: the encounter data did not load: " + host.encounters.error());
        }
    }

    [[nodiscard]] world::Instances& instances() {
        return host.world.instances();
    }

    /** The block of an option of the entrance (the rule of block_for; the Enter option comes in the window of the open menu). */
    [[nodiscard]] InteractionBlock entrance_block(std::uint32_t option) const {
        const gamedata::InteractionObject& object = host.interactions.objects()[1];
        const gamedata::InteractionOption& row = host.interactions.options()[option];
        InteractionBlock block;
        block.actor = object.actor;
        block.group = object.group;
        block.target = object.bounds[0].target;
        block.action = row.action;
        block.baseAction = option == kOpenMenuIndex ? row.action : 71;
        block.optionEntity = object.group ^ 0x40ULL;
        block.committed = row.committed;
        block.optionId = row.id;
        block.text88 = "@Test_Story_Entrance";
        return block;
    }

    /** The block of ExitDungeon of an exit portal (the XOR rule with the entity id of the portal as G). */
    [[nodiscard]] static InteractionBlock portal_block(const world::Entity& portal) {
        const std::uint64_t k = kPortalRoot ^ portal.id;
        InteractionBlock block;
        block.actor = portal.actor.bytes;
        block.group = portal.id;
        block.target = (kPortalBound ^ k) + kPortalComponent;
        block.action = 74;
        block.optionEntity = kPortalOption ^ k;
        block.optionId = gamedata::interaction_option_id("ExitDungeon");
        block.text88 = "@Test_Story_Portal";
        return block;
    }

    /** The entry of a character at the entrance: the open of the menu, the Enter option, and the reports at the start of the map. */
    void enter_story(const char* character) {
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        request(client, entrance_block(kOpenMenuIndex));
        request(client, entrance_block(kEnterIndex));
        end_block(client);
        if (const world::Entity* own = entity(character); own != nullptr && own->space != world::kMainSpace) {
            (std::string(character) == kCharacterA ? placeA : placeB) = own->position;
        }
        wait(0.5F);
    }

    /** The place of a character in the main world again after a leave (its reports follow it). */
    void follow(const char* character) {
        if (const world::Entity* own = entity(character)) {
            (std::string(character) == kCharacterA ? placeA : placeB) = own->position;
        }
        wait(0.3F);
    }

    /** @return The run of the story recipe in a space, or null. */
    [[nodiscard]] const world::EncounterInstance* story_run(world::SpaceId space) {
        for (const auto& [root, run] : encounters().instances()) {
            if (run.space == space) {
                return &run;
            }
        }
        return nullptr;
    }

    [[nodiscard]] world::Entity* boss(world::SpaceId space) {
        const world::EncounterInstance* run = story_run(space);
        if (run == nullptr) {
            return nullptr;
        }
        for (const auto& [id, member] : run->members) {
            world::Entity* found = host.world.find(id);
            if (found != nullptr && !member.object && !member.done) {
                return found;
            }
        }
        return nullptr;
    }

    /** A message of the GameModeParticipant facet of a character (3309, 1980). */
    void participant_message(const char* character, std::uint32_t type, const Bytes& tail) {
        const world::Entity* own = entity(character);
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        server::hub::Writer body;
        body.u64(own->id + 0x8DE633590B8FBEE0ULL);
        body.u64(own->id);
        body.raw(tail);
        host.datagram(client.peer, client.component_message(type, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }

    void cutscene(const char* character, std::uint32_t type, std::uint32_t crc) {
        const world::Entity* own = entity(character);
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        server::hub::Writer body;
        body.u64(own->id + server::rep::kPlayerCutsceneComponent);
        body.u64(own->id);
        body.u32(crc);
        host.datagram(client.peer, client.component_message(type, own->actor.bytes, body.data()));
        host.steps(2, {&clientA, &clientB});
    }

    void cutscene_end(const char* character) {
        const world::Entity* own = entity(character);
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        host.datagram(client.peer, client.component_call(server::rep::kTypeCutsceneEnded, own->actor.bytes,
                                                         own->id + server::rep::kPlayerCutsceneComponent, own->id));
        host.steps(2, {&clientA, &clientB});
    }
};

/** One 3094 of a client: target, entity, result. */
struct DungeonResult {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::uint32_t result{0};
};

inline std::vector<DungeonResult> dungeon_results(const FakeClient& client, std::size_t from = 0) {
    std::vector<DungeonResult> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != server::rep::kTypeDungeonActivateResult) {
            continue;
        }
        server::hub::Reader reader(client.got[index].body);
        DungeonResult result;
        result.target = reader.u64();
        result.entity = reader.u64();
        result.result = reader.u32();
        if (reader.ok() && reader.left() == 0) {
            found.push_back(result);
        }
    }
    return found;
}

inline std::uint32_t task_progress(world::Entity& character, const char* objective) {
    const world::QuestInstance* quest = questfix::quest_of(character, objective);
    return quest != nullptr && !quest->tasks.empty() ? quest->tasks[0].progress : 9999;
}

} // namespace soulwarden::backend::tests::storyfix

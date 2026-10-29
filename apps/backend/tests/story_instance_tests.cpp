// The cases of stage QG9a: a solo story instance of the level of the main world (docs/systems/instances.md, docs/systems/encounters.md). The
// game mode Test_Story, its entrance, its encounter, its exit portal, its quests, and all accounts, characters, ids, and places are made up
// (the form of QuestMedea of the game data: a setup stage of the manager, an intro cutscene, a boss fight with an event child, a cleanup).
//
//   SI1  the entrance: OpenDungeonMenu (71) is accepted; the Enter option (72; KeyDynasty with the count 0 is no requirement) gives 3094 with
//        the result (Success 0 and the entry in the next tick; EntranceActivationFailure 10 for the level and for a mode whose content
//        the backend does not run; GroupAlreadyInDungeon 11 in an instance); the same level, a new space, the start of the map
//   SI2  the GameModeParticipant state (slot 24, type 3312): nothing before the first entry; the owner gets the entry (the mode reference,
//        the CRCs, the entry time) and the flag PlayerCanQuit; an observer gets no fragment; after the leave the empty map and no flag; a
//        restored session in the instance gets the membership
//   SI3  TaskGameMode and a kill task with GameModeID: done only in the mode; a task that needs a mutated mode stays open; the second
//        character in the main world gets nothing (MP-10); a kill in the main world does not count for the task of the mode; a death in
//        the mode keeps the membership and the done task
//   SI4  the content in the space: the recipe of the mode starts only in the space of an instance (a character of the main world at the
//        same place starts nothing and sees no record), the skipped start object; two instances of the mode have two runs (MP-10)
//   SI5  the stages: the setup stage of the manager in the background, the cutscene grant (6707) and its end (6705), the event child at its
//        start, the boss kill, the cleanup, the success of the instance; the time limit of a cutscene with no end of the client
//   SI6  the leaves: 3309 with a key that is not the key of the membership changes nothing, with the key leaves; 1980 with the leave event;
//        the exit portal (action 74); a death and a respawn at the start of the instance
//   SI7  the empty instance: its timer, the join again of its former member, the end at the timeout with its content; no join after a
//        success (a new instance)

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "server/rep/instance_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_fixture.h"
#include "world/instances.h"

namespace soulwarden::backend::tests {
namespace {

using namespace encfix;
using std::chrono::milliseconds;
using world::horizontal_distance;

constexpr const char* kMode = "Test_Story";
constexpr const char* kStoryKey = "e4c00000-0000-4000-8000-0000000000a1/17";
constexpr const char* kStorySlice = "slices/test/story_boss_encounter.dynamicslice";
constexpr const char* kIntro = "Test_Story_Intro_IGC";
constexpr const char* kPortalSlice = "slices/test/story_portal.dynamicslice";
constexpr const char* kStoryObjective = "Test_Story_Enter";
constexpr const char* kMutatedObjective = "Test_Story_Mutated";
constexpr const char* kBossObjective = "Test_Story_Boss";
constexpr state::WorldPosition kEntranceAt{100.0F, 202.0F, 30.0F};
constexpr state::WorldPosition kStoryStart{300.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kStoryRoot{300.0F, 306.0F, 30.0F};
constexpr state::WorldPosition kBossAt{300.0F, 308.0F, 30.0F};
constexpr state::WorldPosition kPortalAt{302.0F, 300.0F, 30.0F};
constexpr std::uint8_t kEntranceTag = 0x31;
constexpr std::uint64_t kEntranceTarget = 0x53B85D6500000031ULL;
constexpr std::uint32_t kOpenMenuIndex = 1;
constexpr std::uint32_t kEnterIndex = 2;
constexpr std::uint32_t kExitIndex = 3;
constexpr std::uint64_t kPortalRoot = 0x0000E4C0000000A1ULL;
constexpr std::uint64_t kPortalBound = 0x0000E4C0000000B1ULL;
constexpr std::uint64_t kPortalComponent = 0x00E4C0FFEE0000A1ULL;
constexpr std::uint64_t kPortalOption = 0x0000E4C0000000C1ULL;
constexpr std::array<std::uint8_t, 16> kStoryAsset{0xE4, 0xC0, 0x00, 0x0A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04};
constexpr std::array<std::uint8_t, 16> kPortalAsset{0xE4, 0xC0, 0x00, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x05};

json objective_of(const char* type, double limit, bool succeed) {
    return {{"type", type}, {"timeLimit", limit}, {"succeedOnTimeout", succeed}, {"failureCondition", false}};
}

/** The recipe of the mode (the form of the QuestMedea recipe of the tool): stages 0 to 3 of the manager, stage 4 the child of the fight. */
json story_recipe() {
    json setup = stage_json("Phase 00 - Setup", 0x500, 8, false, objective_of("WaitForParentObjective", 0, false),
                            json::array({{{"member", kStartSlice}, {"delay", 0}, {"count", 1}, {"skip", true},
                                          {"locations", json::array({place_json(kStoryRoot)})}},
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
    recipe["parent"] = {{"kind", "prefab"}, {"maintainPopulation", false}, {"spawnOnEnable", false}, {"liveCount", 0}, {"respawn", {15.0, 15.0}}};
    recipe["stageOrder"] = {0, 1, 2, 3};
    recipe["stages"] = json::array({setup, stage_json("Phase 00b - Play Intro IGC", 0x700, 5, true, intro, json::array()), fight,
                                    stage_json("Phase 02 - Cleanup", 0x800, 9, true, timed(1), json::array()),
                                    stage_json("01 Intro - Play Intro", 0x900, 10, true, event, json::array())});
    return recipe;
}

json story_document() {
    json document = encounter_document({story_recipe()});
    document["format"] = 2;
    document["instanceObjects"] = json::array({{{"mode", kMode}, {"map", kMode}, {"key", "e4c00000-0000-4000-8000-0000000000b1/18"},
                                                {"slice", kPortalSlice}, {"spawner", "Portal"}, {"position", {kPortalAt.x, kPortalAt.y, kPortalAt.z}},
                                                {"yaw", 180.0}, {"onInit", false}}});
    return document;
}

gamedata::GameMap story_map(std::uint32_t requiredLevel = 0, std::uint32_t timeout = 600) {
    gamedata::GameMap map;
    map.id = kMode;
    map.mode = kMode;
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
json story_quests() {
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
    StoryShop() : EncounterShop({}) {
        check(host.gameModes.set({story_map()}), "setup: the map of the mode (the level of the main world)");
        std::vector<gamedata::InteractionOption> options{option_row("GatherOption", 14, true), option_row("OpenDungeonMenu", 71, true),
                                                         option_row("EnterQuestTest", 72, false), option_row("ExitDungeon", 74, false)};
        options[kEnterIndex].requiredItem = "KeyDynasty";
        options[kEnterIndex].requiredItemCount = 0;
        gamedata::InteractionObject entrance = object_of(kEntranceTag, InteractionKind::dungeonEntrance, kEntranceAt, {kOpenMenuIndex, kEnterIndex});
        gamedata::DungeonEntrance door;
        door.mode = kMode;
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
        if (!host.quests.load_text(story_quests().dump())) {
            throw std::runtime_error("story test: the quest data did not load: " + host.quests.error());
        }
        if (!host.encounters.load_text(story_document().dump())) {
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

std::vector<DungeonResult> dungeon_results(const FakeClient& client, std::size_t from = 0) {
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

/** The field 0 (activeGameModes) and the field 1 (flags) of one GameModeParticipant fragment. */
struct ModeState {
    bool ok{false};
    std::uint64_t revision{0};
    std::uint32_t count{0};
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t group{0};
    std::uint64_t facet{0};
    std::uint32_t modeCrc{0};
    std::uint32_t mapCrc{0};
    std::uint64_t entered{0};
    Bytes flags;
};

ModeState mode_state(const Fragment& fragment) {
    ModeState state;
    const auto modes = fragment.fields.find({0, 0});
    const auto flags = fragment.fields.find({0, 1});
    if (modes == fragment.fields.end() || flags == fragment.fields.end()) {
        return state;
    }
    server::hub::Reader reader(modes->second);
    const std::uint32_t changes = reader.size();
    const std::uint8_t stamped = reader.u8();
    state.revision = reader.size64();
    state.count = reader.size();
    if (state.count == 1) {
        const auto actor = reader.raw(16);
        std::copy(actor.begin(), actor.end(), state.actor.begin());
        state.group = reader.u64();
        state.facet = reader.u64();
        state.modeCrc = reader.u32();
        state.mapCrc = reader.u32();
        state.entered = reader.u64();
        (void)reader.u64();
    }
    state.flags = flags->second;
    state.ok = reader.ok() && reader.left() == 0 && changes == 0 && stamped == 1;
    return state;
}

std::vector<Seen> mode_fragments(const FakeClient& client, std::size_t from = 0) {
    return fragments_of(client, server::rep::kGameModeParticipantSlot, server::rep::kTypeGameModeParticipantState, from);
}

std::uint32_t task_progress(world::Entity& character, const char* objective) {
    const world::QuestInstance* quest = questfix::quest_of(character, objective);
    return quest != nullptr && !quest->tasks.empty() ? quest->tasks[0].progress : 9999;
}

/** SI1 */
void entrance(const Options&) {
    StoryShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    // The level of the character is below RequiredLevel of the mode: 10, and no move.
    check(shop.host.gameModes.set({story_map(99)}), "setup: the mode needs level 99");
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.entrance_block(kOpenMenuIndex));
    const auto menu = replies(shop.clientA, from);
    shop.request(shop.clientA, shop.entrance_block(kEnterIndex));
    shop.end_block(shop.clientA);
    auto results = dungeon_results(shop.clientA, from);
    check(menu == std::vector<std::pair<unsigned, unsigned>>{{71U, 1U}}, "OpenDungeonMenu (71) is accepted (3743 result 1): the client opens "
                                                                       "its window itself");
    check(results.size() == 1 && results[0].result == world::dungeon_result::kEntranceActivationFailure && results[0].target == kEntranceTarget
              && results[0].entity == shop.host.interactions.objects()[1].group && a.space == world::kMainSpace,
          "a level below RequiredLevel: 3094 with EntranceActivationFailure (10) to the DungeonEntrance component; no entry");
    // The success: 3094 with 0, and in the next tick the entry into a new space of the main world level, at the start of the map.
    check(shop.host.gameModes.set({story_map()}), "setup: the mode needs no level");
    from = shop.clientA.got.size();
    shop.enter_story(kCharacterA);
    results = dungeon_results(shop.clientA, from);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    check(results.size() == 1 && results[0].result == world::dungeon_result::kSuccess, "the Enter option (72): 3094 with Success (0)");
    check(instance != nullptr && a.space == instance->space && a.space != world::kMainSpace && instance->mode == kMode
              && shop.host.world.main_level(a.space) && horizontal_distance(a.position, kStoryStart) < 1.0F,
          "the entry: a new space of the level of the main world, at the start of the map");
    check(shop.instances().counts().entranceRequests == 2 && shop.instances().counts().entranceRefused == 1,
          "the counts: two requests, one refused");
    // In an instance: GroupAlreadyInDungeon (11); the entrance is an object of the main world, so the check of the world is direct.
    check(shop.instances().entrance_result(a, *shop.host.interactions.objects()[1].entrance) == world::dungeon_result::kGroupAlreadyInDungeon,
          "a character in an instance: GroupAlreadyInDungeon (11)");
    // The entrance of a mode whose content the backend does not run (a recipe that is refused): 10 for B.
    {
        StoryShop other;
        json document = story_document();
        document["recipes"][0]["refused"] = {"a made-up refusal"};
        check(other.host.encounters.load_text(document.dump()), "setup: the recipe of the mode is refused");
        other.enter();
        const std::size_t start = other.clientB.got.size();
        other.enter_story(kCharacterB);
        const auto refused = dungeon_results(other.clientB, start);
        check(refused.size() == 1 && refused[0].result == world::dungeon_result::kEntranceActivationFailure
                  && other.entity(kCharacterB)->space == world::kMainSpace,
              "a mode whose content the backend does not run: 10, no entry");
    }
}

/** SI2 */
void membership_state(const Options&) {
    StoryShop shop;
    shop.enter();
    shop.wait(0.5F);
    check(mode_fragments(shop.clientA).empty() && mode_fragments(shop.clientB).empty(),
          "no GameModeParticipant fragment before the first entry of the world entry");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.enter_story(kCharacterA);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    const auto seen = mode_fragments(shop.clientA, fromA);
    check(!seen.empty() && instance != nullptr, "the owner gets the GameModeParticipant state at the entry");
    if (seen.empty() || instance == nullptr) {
        return;
    }
    const ModeState state = mode_state(seen.back().fragment);
    note("SI2 count=" + std::to_string(state.count) + " mode=" + std::to_string(state.modeCrc) + " flags=" + std::to_string(state.flags.size()));
    check(state.ok && state.count == 1 && state.actor == instance->modeActor && state.group == instance->modeEntity
              && state.facet == instance->modeEntity && state.modeCrc == gamedata::quest_crc(kMode) && state.mapCrc == gamedata::quest_crc(kMode)
              && state.entered != 0,
          "the entry of activeGameModes: the mode reference (actor, G, facet), the CRC of the mode and of the map, the entry time");
    check(state.flags == Bytes{0x00, 0x00, 0x40, 0x00, 0x00, 0x00}, "the flags: only PlayerCanQuit (bit 22)");
    check(mode_fragments(shop.clientB, fromB).empty(), "the observer B gets no GameModeParticipant fragment of A");
    const std::size_t afterEntry = shop.clientA.got.size();
    check(shop.instances().leave(kCharacterA, world::InstanceLeave::command).error == world::InstanceError::none, "setup: A leaves");
    shop.follow(kCharacterA);
    const auto left = mode_fragments(shop.clientA, afterEntry);
    const ModeState empty = left.empty() ? ModeState{} : mode_state(left.back().fragment);
    check(!left.empty() && empty.ok && empty.count == 0 && empty.revision > state.revision && empty.flags == Bytes(6, 0)
              && a.space == world::kMainSpace,
          "after the leave: the empty map with a newer stamp, and no flag");
    // A restored session: a newer session of the account takes the entity in the instance, and its owner record has the membership.
    shop.enter_story(kCharacterA);
    FakeClient again(27050);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(10, {&again, &shop.clientB});
    const auto restored = mode_fragments(again);
    const ModeState back = restored.empty() ? ModeState{} : mode_state(restored.back().fragment);
    check(!restored.empty() && back.ok && back.count == 1 && back.modeCrc == gamedata::quest_crc(kMode) && a.space != world::kMainSpace
              && shop.instances().in_mode(a, kMode),
          "a restored session (a newer session of the account in the instance): its owner record has the membership");
}

/** SI3 */
void mode_tasks(const Options&) {
    StoryShop shop;
    begin(shop);
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    for (world::Entity* character : {&a, &b}) {
        for (const char* objective : {kStoryObjective, kMutatedObjective, kBossObjective}) {
            check(shop.host.world.quests().grant(*character, objective, "command", 0).ok(), std::string("setup: the quest ") + objective);
        }
    }
    shop.wait(0.3F);
    check(task_progress(a, kStoryObjective) == 0 && task_progress(b, kStoryObjective) == 0, "TaskGameMode in the main world: not done");
    // A kill of the boss category in the main world does not count for the task of the mode.
    world::Entity* wolf = shop.host.world.creatures().spawn("worldevent_test_member", state::WorldPosition{104.0F, 200.0F, 30.0F}, 0.0F).entity;
    check(wolf != nullptr, "setup: a creature of the boss category in the main world");
    if (wolf != nullptr) {
        shop.kill(*wolf, kCharacterA);
    }
    check(task_progress(a, kBossObjective) == 0, "a kill in the main world does not count for a kill task with GameModeID");
    shop.enter_story(kCharacterA);
    shop.wait(0.3F);
    note("SI3 enter=" + std::to_string(task_progress(a, kStoryObjective)) + " mutated=" + std::to_string(task_progress(a, kMutatedObjective))
         + " b=" + std::to_string(task_progress(b, kStoryObjective)));
    check(task_progress(a, kStoryObjective) == 1, "TaskGameMode is done in the mode");
    check(task_progress(a, kMutatedObjective) == 0, "a TaskGameMode that needs a mutated mode stays open (the backend has no mutation)");
    check(task_progress(b, kStoryObjective) == 0, "B in the main world: its TaskGameMode stays open (MP-10)");
    // A failure: A dies in the mode (the fight can fail); the membership and the done TaskGameMode stay. The respawn is in the mode.
    shop.host.world.health().hurt(a.characterId, a.vitals.healthMax * 2.0F);
    shop.wait(0.5F);
    check(a.life.dead && a.gameModeMembership.has_value() && task_progress(a, kStoryObjective) == 1,
          "a death in the mode: the membership and the done TaskGameMode stay");
    (void)shop.host.world.health().respawn(a.owner);
    shop.placeA = a.position;
    // The boss of the instance: its kill counts for the task of the mode.
    shop.wait(1.0F);
    world::Entity* boss = shop.boss(a.space);
    check(boss != nullptr && boss->space == a.space, "setup: the boss of the instance");
    if (boss != nullptr) {
        shop.kill(*boss, kCharacterA);
    }
    check(task_progress(a, kBossObjective) == 1, "the kill of the boss in the mode counts for the kill task with GameModeID");
}

/** SI4 */
void content_in_space(const Options&) {
    StoryShop shop;
    shop.enter();
    // B goes to the place of the recipe in the main world: no run starts, and B gets no record of the root.
    shop.move(kCharacterB, kStoryStart);
    shop.wait(1.0F);
    check(shop.encounters().instances().empty() && root_seen(shop.clientB).interest == 0,
          "a character of the main world at the place of the recipe of a mode: no run, no record");
    shop.move(kCharacterB, state::WorldPosition{101.0F, 200.0F, 30.0F});
    const std::size_t fromB = shop.clientB.got.size();
    shop.enter_story(kCharacterA);
    shop.wait(1.0F);
    world::Entity& a = *shop.entity(kCharacterA);
    const world::EncounterInstance* runA = shop.story_run(a.space);
    world::Entity* bossA = shop.boss(a.space);
    check(runA != nullptr && bossA != nullptr && bossA->space == a.space && horizontal_distance(bossA->position, kBossAt) < 1.0F,
          "the run of the recipe in the space of the instance, with its boss in that space");
    std::size_t objects = 0;
    if (runA != nullptr) {
        for (const auto& [id, member] : runA->members) {
            objects += member.object ? 1 : 0;
        }
    }
    check(objects == 0, "the start object that no objective tracks (skip of the tool) is not made");
    // B enters too: a second instance of the mode (a solo map), with its own run and boss.
    shop.enter_story(kCharacterB);
    shop.wait(1.0F);
    world::Entity& b = *shop.entity(kCharacterB);
    const world::Instance* instanceA = shop.instances().instance_of(kCharacterA);
    const world::Instance* instanceB = shop.instances().instance_of(kCharacterB);
    world::Entity* bossB = shop.boss(b.space);
    check(instanceA != nullptr && instanceB != nullptr && instanceA->id != instanceB->id && a.space != b.space && bossB != nullptr
              && bossB != bossA && bossB->space == b.space && shop.encounters().instances().size() == 2,
          "two instances of the mode: two spaces, two runs, two bosses (MP-10)");
    std::size_t seenByB = 0;
    shop.host.world.for_each_near(b.space, kBossAt, 30.0F, [&](world::Entity& entity) { seenByB += entity.id == bossA->id ? 1 : 0; });
    check(seenByB == 0, "the boss of A is not in the space of B");
    note("SI4 rootRecordsOfB=" + std::to_string(root_seen(shop.clientB, fromB).records));
}

/** SI5 */
void story_stages(const Options&) {
    for (const bool clientEnd : {true, false}) {
        StoryShop shop;
        begin(shop);
        world::Entity& a = *shop.entity(kCharacterA);
        shop.enter_story(kCharacterA);
        shop.wait(1.0F);
        const world::EncounterInstance* run = shop.story_run(a.space);
        check(run != nullptr && run->stages.size() == 5 && run->stages[0].started && !run->stages[0].ended && run->stages[1].started,
              "the setup stage of the manager runs in the background, and the manager starts the intro");
        if (run == nullptr) {
            return;
        }
        const std::size_t from = shop.clientA.got.size();
        shop.cutscene(kCharacterA, server::rep::kTypeRequestStartCutscene, gamedata::quest_crc(kIntro));
        check(count_type(shop.clientA, server::rep::kTypeCutsceneGranted, from) == 1 && a.encounterCutscene == gamedata::quest_crc(kIntro),
              "6703 of the cutscene of the running stage: the grant 6707");
        const std::size_t other = shop.clientA.got.size();
        shop.cutscene(kCharacterA, server::rep::kTypeRequestStartCutscene, gamedata::quest_crc("Test_Other_IGC"));
        check(count_type(shop.clientA, server::rep::kTypeCutsceneGranted, other) == 0, "a cutscene of no stage: no grant");
        if (clientEnd) {
            shop.cutscene_end(kCharacterA);
            shop.wait(0.3F);
            check(run->stages[1].ended && run->stages[2].started, "6705: the intro stage ends, and the fight starts");
        } else {
            shop.wait(16.0F);
            check(!run->stages[1].ended, "no end of the client: the intro waits for its time limit");
            shop.wait(4.0F);
            check(run->stages[1].ended && run->stages[2].started, "the time limit of the cutscene (succeedOnTimeout): the fight starts");
        }
        shop.wait(0.3F);
        check(run->stages[4].started && run->stages[4].ended, "the event child (Intro_Complete) is met at its start");
        world::Entity* boss = shop.boss(a.space);
        check(boss != nullptr, "setup: the boss");
        if (boss == nullptr) {
            return;
        }
        shop.kill(*boss, kCharacterA);
        shop.wait(2.0F);
        const world::Instance* instance = shop.instances().instance_of(kCharacterA);
        note("SI5 clientEnd=" + std::to_string(clientEnd) + " runs=" + std::to_string(shop.encounters().instances().size())
             + " succeeded=" + std::to_string(instance != nullptr && instance->succeeded));
        check(shop.story_run(a.space) == nullptr && instance != nullptr && instance->succeeded,
              "the kill of the boss, the cleanup, the success: the run ends, and the instance has the success of its content");
    }
}

/** SI6 */
void leaves(const Options&) {
    StoryShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const state::WorldPosition home = a.position;
    shop.enter_story(kCharacterA);
    // 3309 with a key that is not the key of the membership: no change.
    const world::GameModeMembership membership = *a.gameModeMembership;
    server::hub::Writer wrong;
    wrong.raw(membership.actor);
    wrong.u64(membership.group + 1);
    wrong.u64(membership.facet);
    shop.participant_message(kCharacterA, server::rep::kTypeRequestLeaveGameMode, wrong.data());
    check(a.space != world::kMainSpace, "3309 with a key that is not the key of the membership: no leave");
    server::hub::Writer right;
    right.raw(membership.actor);
    right.u64(membership.group);
    right.u64(membership.facet);
    shop.participant_message(kCharacterA, server::rep::kTypeRequestLeaveGameMode, right.data());
    check(a.space == world::kMainSpace && horizontal_distance(a.position, home) < 0.5F && shop.instances().counts().requestLeaves == 1,
          "3309 with the key: the leave to the return place");
    shop.follow(kCharacterA);
    // 1980 with an event that is not the leave event: no change; with 9BB080D0: the leave.
    shop.enter_story(kCharacterA);
    server::hub::Writer otherEvent;
    otherEvent.u32(0x12345678U);
    shop.participant_message(kCharacterA, server::rep::kTypeGameModeClientEvent, otherEvent.data());
    check(a.space != world::kMainSpace, "1980 with an event that has no rule: no leave");
    server::hub::Writer leaveEvent;
    leaveEvent.u32(server::rep::kGameModeLeaveEvent);
    shop.participant_message(kCharacterA, server::rep::kTypeGameModeClientEvent, leaveEvent.data());
    check(a.space == world::kMainSpace && shop.instances().counts().requestLeaves == 2, "1980 with the leave event: the leave");
    shop.follow(kCharacterA);
    // A death in the instance: the respawn at the start of the map, in the space.
    shop.enter_story(kCharacterA);
    const world::SpaceId space = a.space;
    shop.host.world.health().hurt(a.characterId, a.vitals.healthMax * 2.0F);
    shop.wait(0.3F);
    check(a.life.dead, "setup: A dies in the instance");
    (void)shop.host.world.health().respawn(a.owner);
    shop.wait(0.3F);
    check(!a.life.dead && a.space == space && horizontal_distance(a.position, kStoryStart) < 1.0F,
          "the respawn in the instance: at the start of the map, in its space");
    shop.placeA = a.position;
    shop.wait(0.3F);
    // The exit portal (ExitDungeon, action 74) of the instance.
    const world::Instance* instance = shop.instances().instance_of(kCharacterA);
    world::Entity* portal = instance != nullptr && !instance->objects.empty() ? shop.host.world.find(instance->objects.front()) : nullptr;
    check(portal != nullptr && portal->space == space && horizontal_distance(portal->position, kPortalAt) < 0.5F,
          "the exit portal of the mode is a server child of the space of the instance");
    if (portal == nullptr) {
        return;
    }
    shop.placeA = state::WorldPosition{kPortalAt.x - 1.0F, kPortalAt.y, kPortalAt.z};
    shop.wait(0.5F);
    shop.request(shop.clientA, StoryShop::portal_block(*portal));
    shop.end_block(shop.clientA);
    note("SI6 portalLeaves=" + std::to_string(shop.instances().counts().portalLeaves) + " space=" + std::to_string(a.space));
    check(a.space == world::kMainSpace && shop.instances().counts().portalLeaves == 1 && horizontal_distance(a.position, home) < 0.5F,
          "the exit portal: the leave to the return place");
}

/** SI7 */
void empty_instance(const Options&) {
    StoryShop shop;
    // A timeout of 5 s (the mode of the data has 600 s): the sessions report each 100 ms in the wait.
    check(shop.host.gameModes.set({story_map(0, 5)}), "setup: the mode has a timeout of 5 s");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    shop.enter_story(kCharacterA);
    shop.wait(1.0F);
    const std::uint64_t first = shop.instances().instance_of(kCharacterA)->id;
    const world::SpaceId space = a.space;
    check(shop.instances().leave(kCharacterA, world::InstanceLeave::command).error == world::InstanceError::none, "setup: A leaves");
    shop.follow(kCharacterA);
    const world::Instance* empty = shop.instances().find(first);
    check(empty != nullptr && empty->members.empty() && empty->emptyUntil && shop.story_run(space) != nullptr,
          "the empty instance with content stays: its timer runs, and its run stays");
    shop.enter_story(kCharacterA);
    check(shop.instances().instance_of(kCharacterA) != nullptr && shop.instances().instance_of(kCharacterA)->id == first && a.space == space
              && shop.instances().counts().rejoined == 1 && !shop.instances().find(first)->emptyUntil,
          "the former member enters again: the same instance, its timer stops");
    check(shop.instances().leave(kCharacterA, world::InstanceLeave::command).error == world::InstanceError::none, "setup: A leaves again");
    shop.follow(kCharacterA);
    shop.wait(4.0F);
    check(shop.instances().find(first) != nullptr, "before the timeout of the mode the instance stays");
    shop.wait(1.5F);
    check(shop.instances().find(first) == nullptr && shop.story_run(space) == nullptr && shop.instances().counts().emptyEnded == 1,
          "at the timeout of the mode (TimerThresholdDefault) the instance ends with its run and its portal");
    // A success: no join again (a new instance at the next entry).
    shop.enter_story(kCharacterA);
    shop.wait(1.0F);
    const std::uint64_t second = shop.instances().instance_of(kCharacterA)->id;
    shop.cutscene(kCharacterA, server::rep::kTypeRequestStartCutscene, gamedata::quest_crc(kIntro));
    shop.cutscene_end(kCharacterA);
    shop.wait(0.5F);
    if (world::Entity* boss = shop.boss(a.space)) {
        shop.kill(*boss, kCharacterA);
    }
    shop.wait(2.0F);
    check(shop.instances().find(second) != nullptr && shop.instances().find(second)->succeeded, "setup: the content of the second instance succeeded");
    check(shop.instances().leave(kCharacterA, world::InstanceLeave::command).error == world::InstanceError::none, "setup: A leaves");
    shop.follow(kCharacterA);
    shop.enter_story(kCharacterA);
    const world::Instance* third = shop.instances().instance_of(kCharacterA);
    check(third != nullptr && third->id != second && shop.instances().counts().rejoined == 1,
          "after a success the next entry makes a new instance (no join of the old one)");
}

} // namespace

std::vector<Case> story_instance_cases() {
    return {{"SI1", "story instance (stage QG9a): the entrance, its checks, the result 3094, and the entry", entrance},
            {"SI2", "story instance (stage QG9a): the GameModeParticipant state of the owner", membership_state},
            {"SI3", "story instance (stage QG9a): TaskGameMode and a kill task with GameModeID, two characters", mode_tasks},
            {"SI4", "story instance (stage QG9a): the content of the mode only in the space of an instance, two instances", content_in_space},
            {"SI5", "story instance (stage QG9a): the cutscene, the event child, the boss, the success", story_stages},
            {"SI6", "story instance (stage QG9a): the leaves (3309, 1980, the exit portal), the respawn at the start", leaves},
            {"SI7", "story instance (stage QG9a): the empty instance, its timer, the join again, no join after a success", empty_instance}};
}

} // namespace soulwarden::backend::tests

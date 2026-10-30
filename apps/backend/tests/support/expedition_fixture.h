#pragma once

// The made-up expedition of the expedition cases (stage DG3, tests/expedition_tests.cpp; code review of stage DG3, tests/review_dg3_tests.cpp,
// stage QG9e): its map (an own level), its level set in the memory (a lore page, the exit stone, two checkpoints), and the story fixture with
// it. The ids, the levels, and the places are made up.

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "gamedata/level_data.h"
#include "server/rep/health_messages.h"
#include "server/rep/travel_messages.h"
#include "support/story_fixture.h"

namespace soulwarden::backend::tests::expedfix {

using namespace storyfix;

constexpr const char* kExpedition = "Test_Expedition";
constexpr const char* kExpeditionLevel = "nw_test_expedition_00";
constexpr const char* kExpeditionTerrain = "coatlicue/nw_test_expedition_00";
constexpr state::WorldPosition kPagePlace{304.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kStonePlace{296.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kEntrancePoint{300.0F, 301.0F, 30.0F};
constexpr state::WorldPosition kVestibule{340.0F, 300.0F, 30.0F};
constexpr state::WorldPosition kVestibuleRespawn{340.0F, 304.0F, 30.0F};
constexpr std::uint8_t kPageTag = 0x51;
constexpr std::uint8_t kStoneTag = 0x52;
constexpr std::uint32_t kPageAchievement = 3001;
constexpr std::uint64_t kCheckpointRoot = 0x0000E4C1000000A1ULL;
constexpr std::uint64_t kHomeEntity = 0x0000015411BD66FAULL;
constexpr std::uint64_t kHomeComponent = 0x52D0DB72600E2508ULL;

inline bool near(const state::WorldPosition& a, const state::WorldPosition& b, float within = 0.3F) {
    return std::fabs(a.x - b.x) <= within && std::fabs(a.y - b.y) <= within && std::fabs(a.z - b.z) <= within;
}

inline gamedata::GameMap expedition_map() {
    gamedata::GameMap map;
    map.id = kExpedition;
    map.mode = kExpedition;
    map.level = kExpeditionLevel;
    map.terrain = kExpeditionTerrain;
    map.starts = {{kStoryStart, 90.0F}};
    map.nameKey = "@Test_Expedition";
    map.name = "Test Expedition";
    map.recommendedLevel = 25;
    map.teamCapacity = 5;
    map.minGroupSize = 3;
    map.dungeon = true;
    map.timeoutSeconds = 600;
    return map;
}

/** The objects of the level set: options ReadLore (0), AddLoreToJournal (1), ExitDungeon (2); a lore page; the exit stone. */
inline gamedata::InteractionObject page_object() {
    return object_of(kPageTag, InteractionKind::lorePage, kPagePlace, {0, 1}, 0);
}
inline gamedata::InteractionObject stone_object() {
    return object_of(kStoneTag, InteractionKind::dungeonExit, kStonePlace, {2});
}

inline gamedata::ModeCheckpoint checkpoint(const char* spawner, const state::WorldPosition& place, const state::WorldPosition& respawn,
                                           float radius) {
    gamedata::ModeCheckpoint point;
    point.mode = kExpedition;
    point.map = kExpedition;
    point.spawner = spawner;
    point.slice = "slices/test/checkpoint.dynamicslice";
    point.templateIndex = 0;
    point.position = place;
    point.respawn = respawn;
    point.type = 10;
    point.cooldown = 30.0F;
    point.name = "@Respawn_ShrineName";
    point.homeEntity = kHomeEntity;
    point.homeComponent = kHomeComponent;
    point.triggerCenter = place;
    point.triggerRadius = radius;
    return point;
}

/** The level set of the made-up expedition (in the memory): its interaction data and its checkpoints (no creature, no navigation). */
inline std::unique_ptr<gamedata::LevelData> expedition_set() {
    auto data = std::make_unique<gamedata::LevelData>(kExpeditionLevel);
    data->accept_for_test(kExpeditionTerrain);
    gamedata::InteractionTemplate crystal;
    crystal.slice = "slices/test/checkpoint.dynamicslice";
    crystal.kind = InteractionKind::other;
    crystal.asset = {0xE4, 0xC1, 0x00, 0x0C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x06};
    crystal.sub = 2;
    crystal.root = kCheckpointRoot;
    crystal.positionSlot = 1;
    const bool set = data->interactions().set({option_row("ReadLore", 47, true), option_row("AddLoreToJournal", 50, false),
                                               option_row("ExitDungeon", 74, false)},
                                              {{"Lore_unlock_Test_Expedition_01", kPageAchievement}}, {page_object(), stone_object()},
                                              {crystal});
    if (!set) {
        throw std::runtime_error("expedition test: the interaction data of the level was not set: " + data->interactions().error());
    }
    data->interactions().set_checkpoints({checkpoint("Checkpoint_Entrance", kEntrancePoint, kEntrancePoint, 30.0F),
                                          checkpoint("Checkpoint_Vestibule", kVestibule, kVestibuleRespawn, 5.0F)});
    data->mark_parts_for_test();
    return data;
}

/** The story fixture with the made-up expedition: its map (an own level), its entrance near A and B, its level set in the memory. */
struct ExpeditionShop : StoryShop {
    ExpeditionShop() : StoryShop(":memory:", story_document(), story_quests(), kExpedition) {
        host.levels.made[kExpeditionLevel] = [] { return expedition_set(); };
        set_map(expedition_map());
    }

    void set_map(const gamedata::GameMap& map) {
        check(host.gameModes.set({map}), "setup: the map of the expedition");
    }

    /** The entry of a character at the entrance with a difficulty (the Enter option of action 72, record +0x70). */
    void enter_with(const char* character, std::uint32_t difficulty) {
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        request(client, entrance_block(kOpenMenuIndex));
        InteractionBlock enter = entrance_block(kEnterIndex);
        enter.difficulty = difficulty;
        request(client, enter);
        end_block(client);
        if (const world::Entity* own = entity(character); own != nullptr && own->space != world::kMainSpace) {
            (std::string(character) == kCharacterA ? placeA : placeB) = own->position;
        }
        wait(0.5F);
    }

    /** The block of an option of an object of the level set (the rule of block_for). */
    static InteractionBlock level_block(const gamedata::InteractionObject& object, std::uint32_t option, const gamedata::InteractionOption& row) {
        InteractionBlock block;
        block.actor = object.actor;
        block.group = object.group;
        block.target = object.bounds[0].target;
        block.action = row.action;
        block.baseAction = row.committed ? row.action : 0;
        block.optionEntity = object.bounds[0].options.at(option).entity;
        block.committed = row.committed;
        block.optionId = row.id;
        block.text88 = "@Test_Level_Object";
        return block;
    }

    /** The respawn request 1938 of a character (random 0 and an id, or random 1). */
    void respawn(const char* character, const std::string& id) {
        const world::Entity* own = entity(character);
        FakeClient& client = std::string(character) == kCharacterA ? clientA : clientB;
        server::hub::Writer body;
        body.u64(own->id + server::rep::kPlayerComponent);
        body.u64(own->id);
        body.u8(id.empty() ? 1 : 0);
        body.string(id);
        host.datagram(client.peer, client.component_message(server::rep::kTypeRequestRespawn, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
        if (const world::Entity* after = entity(character)) {
            (std::string(character) == kCharacterA ? placeA : placeB) = after->position;
        }
    }
};

} // namespace soulwarden::backend::tests::expedfix

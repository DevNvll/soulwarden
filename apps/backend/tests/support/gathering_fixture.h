#pragma once

// The world of the gathering cases (stage 3.6a, tests/gathering_tests.cpp; docs/systems/gathering.md): made-up gathering data of the form
// of the file of tools/gamedata/gathering.py (the rows, the events, the ranks, the tools, the templates, one capital node, one region
// with its records, and the skinning of the wolf of the bag cases), made-up loot tables with gathering roots, the four trade skills, two
// characters near the nodes, and the readers of the node state (type 12) and of the Gathering state (81). All keys, ids, positions,
// items, and tables are made up.

#include <array>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/gathering_data.h"
#include "support/chest_fixture.h"
#include "support/loot_bag_fixture.h"
#include "support/test_progression.h"
#include "world/gathering.h"

namespace soulwarden::backend::tests::gather {

using namespace interaction;
using nlohmann::json;

/** The slot of the GatherableController of the made-up slices (3: the slot of the master slices of the game) and its state type. */
inline constexpr std::uint32_t kNodeSlot = 3;
inline constexpr std::uint32_t kNodeType = 12;
inline constexpr std::uint8_t kHealthy = 4;
inline constexpr std::uint8_t kDepleted = 8;
/** The paperdoll slots of the tools. */
inline constexpr std::uint32_t kAxeSlot = 33;
inline constexpr std::uint32_t kSickleSlot = 34;
inline constexpr std::uint32_t kKnifeSlot = 35;
inline constexpr std::uint32_t kPickSlot = 36;

/** The gatherable rows, by their index in the file. */
enum Row : std::uint32_t { kOreRow, kStoneRow, kTreeRow, kHempRow, kSkinRow, kQuestRow, kNoLootRow, kMysteryRow, kHeapRow, kRowCount };
/** The templates (slices), by their index in the file. */
enum Slice : std::uint32_t { kOreSlice, kStoneSlice, kTreeSlice, kHempSlice, kWolfSlice };
/** The records of the region, by their number (the record of a G with the counter N + 1). */
enum Record : std::uint32_t { kOre, kStone, kTree, kHemp, kRock, kQuest, kNoLoot, kFarOre, kMystery, kOre2, kHeap, kRecordCount };

inline constexpr const char* kRegion = "r_+00_+00";
inline constexpr const char* kVersion = "test-distribution-1";
/** The tag of the capital node in the interaction data (its key is the UUID of object_of). */
inline constexpr std::uint8_t kCapitalTag = 0x21;
inline constexpr state::WorldPosition kCapitalPlace{99.0F, 198.0F, 30.0F};
inline constexpr std::uint32_t kGatherOptionIndex = 0;

/** The place of each record: near the characters (A at 100, 200; B at 101, 200), the far ore 200 m to the north. */
inline state::WorldPosition record_place(std::uint32_t record) {
    switch (record) {
    case kOre:
        return {100.0F, 202.0F, 29.0F};
    case kStone:
        return {102.0F, 200.0F, 29.0F};
    case kTree:
        return {98.0F, 200.0F, 29.0F};
    case kHemp:
        return {100.0F, 198.0F, 29.0F};
    case kRock:
        return {101.0F, 201.0F, 29.0F};
    case kQuest:
        return {99.0F, 202.0F, 29.0F};
    case kNoLoot:
        return {101.0F, 198.0F, 29.0F};
    case kFarOre:
        return {100.0F, 400.0F, 29.0F};
    case kMystery:
        return {99.0F, 201.0F, 29.0F};
    case kHeap:
        return {100.5F, 201.5F, 29.0F};
    default:
        return {101.0F, 202.0F, 29.0F};
    }
}

/** The slice index of the region of each record (its type in the region file). */
inline std::uint16_t record_type(std::uint32_t record) {
    switch (record) {
    case kOre:
    case kFarOre:
    case kOre2:
        return 0;
    case kStone:
        return 1;
    case kTree:
        return 2;
    case kHemp:
        return 3;
    case kRock:
        return 4;
    case kQuest:
        return 5;
    case kNoLoot:
        return 6;
    case kMystery:
        return 7;
    case kHeap:
        return 8;
    default:
        return 0; // The more ore records of the load case.
    }
}

inline std::string hex64(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

inline std::string hex_bytes(const std::array<std::uint8_t, 16>& bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        char pair[3];
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        text += pair;
    }
    return text;
}

/** The ids of a made-up slice: its asset, its root, and the entity, component, and option of its Interact bound. */
struct SliceIds {
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{2};
    std::uint64_t root{0};
    std::uint64_t entity{0};
    std::uint64_t component{0};
    std::uint64_t option{0};
    float rangeSq{9.0F};
    std::array<float, 3> translation{0.0F, 0.0F, 1.0F};
};

inline SliceIds slice_ids(std::uint32_t slice) {
    SliceIds ids;
    if (slice == kWolfSlice) {
        const gamedata::CreatureSlice wolf = bags::bag_wolf_slice();
        ids.asset = wolf.asset;
        ids.sub = wolf.subId;
        ids.root = wolf.root;
        ids.translation = {0.5F, 0.0F, 0.0F};
        ids.rangeSq = 16.0F;
    } else {
        ids.asset = {0x6A, 0x7E, 0x00, static_cast<std::uint8_t>(slice), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
        ids.root = 0x6A7E000000000A01ULL + (static_cast<std::uint64_t>(slice) << 16);
        ids.rangeSq = slice == kTreeSlice ? 16.0F : 9.0F;
    }
    ids.entity = ids.root + 0x10;
    ids.component = 0x00C0FFEE00000000ULL + slice;
    ids.option = ids.root + 0x20;
    return ids;
}

inline json template_json(std::uint32_t slice, const char* name, const char* table) {
    const SliceIds ids = slice_ids(slice);
    return json{{"slice", std::string("slices/test/") + name + ".dynamicslice"},
                {"asset", hex_bytes(ids.asset)},
                {"sub", ids.sub},
                {"root", hex64(ids.root)},
                {"controllerSlot", kNodeSlot},
                {"table", table},
                {"bound",
                 {{"entity", hex64(ids.entity)},
                  {"component", hex64(ids.component)},
                  {"option", hex64(ids.option)},
                  {"slot", 20},
                  {"rangeSq", ids.rangeSq},
                  {"capacity", 1},
                  {"translation", {ids.translation[0], ids.translation[1], ids.translation[2]}},
                  {"lineOfSight", false},
                  {"arcRequired", false},
                  {"requires", json::array()}}}};
}

inline json row_json(const char* id, const char* table, const char* type, const char* restriction, const char* skill, std::uint32_t level,
                     double time, double low, double high, const char* loot, const char* event, bool reset,
                     std::vector<std::string> unsupported = {}) {
    return json{{"id", id},
                {"table", table},
                {"type", type},
                {"restriction", restriction},
                {"tradeskill", skill},
                {"level", level},
                {"time", time},
                {"respawn", {low, high}},
                {"loot", loot},
                {"material", ""},
                {"event", event},
                {"reset", reset},
                {"destroy", false},
                {"depleted", ""},
                {"required", 1},
                {"waitForImpact", false},
                {"unsupported", unsupported}};
}

/**
 * The gathering data of the cases. Rows: the ore (Mining, a pick, 4 s, respawn 60 to 120 s, the rest of a work stays), the stone (no
 * tool, 1.5 s, 30 s), the tree (Logging rank 50, an axe, 10 s, 300 to 600 s), the hemp (a sickle, 3 s, the work resets), the wolf skin
 * (a knife, 3 s), a quest node (a rule that the backend cannot check), a node with no loot table, a node with an item that the catalog
 * does not have, a heap (2,500 stones in one roll). Ranks: Mining 0 to 5 (GatheringEfficiency 0.05 for each rank; rank 5 has the RollBonus 100,000), Logging 0 to 60.
 * Tools: the pick (Mining, 1 to 2 over the gear score range 100 to 200), the axe (Chopping, 1.5 to 3), the sickle, the knife (2.2).
 * @param regions More regions (name, x, y, records) with only ore records (the load case).
 * @param nearExtra More ore records of the region of the cases after its records (the load case).
 */
inline json gathering_document(const std::vector<std::tuple<std::string, int, int, std::uint32_t>>& regions = {},
                               std::uint32_t nearExtra = 0) {
    json rows = json::array({row_json("Test_OreSmall", "Gatherable_Minerals", "Mining", "ToolRequired", "Mining", 0, 4.0, 60.0, 120.0,
                                      "Test_GatherOre", "Test_GatherMining", false),
                             row_json("Test_Stone", "Gatherable_Stones", "None", "NoToolNecessary", "", 0, 1.5, 30.0, 30.0, "Test_GatherStone", "",
                                      true),
                             row_json("Test_TreeLarge", "Gatherable_Trees", "Chopping", "ToolRequired", "Logging", 50, 10.0, 300.0, 600.0,
                                      "Test_GatherWood", "Test_GatherLogging", false),
                             row_json("Test_Hemp", "Gatherable_Plants", "Cutting", "ToolRequired", "Harvesting", 0, 3.0, 100.0, 100.0,
                                      "Test_GatherFiber", "Test_GatherHarvesting", true),
                             row_json("Test_WolfSkin", "Gatherable_Skinning", "Dressing", "ToolRequired", "Skinning", 0, 3.0, 0.0, 0.0,
                                      "Test_GatherHide", "Test_GatherSkinning", true),
                             row_json("Test_QuestNode", "Gatherable_Alchemy", "None", "NoToolNecessary", "", 0, 1.0, 30.0, 30.0, "Test_GatherStone",
                                      "", true, {"the column SongId (a song of the node)"}),
                             row_json("Test_NoLoot", "Gatherable_Stones", "None", "NoToolNecessary", "", 0, 1.0, 30.0, 30.0, "Test_NoSuchRoot", "",
                                      true),
                             row_json("Test_Mystery", "Gatherable_Stones", "None", "NoToolNecessary", "", 0, 1.0, 30.0, 30.0, "Test_GatherMystery",
                                      "", true),
                             row_json("Test_Heap", "Gatherable_Stones", "None", "NoToolNecessary", "", 0, 1.0, 30.0, 30.0, "Test_GatherHeap", "",
                                      true)});
    json mining = json::array();
    for (int rank = 0; rank <= 5; ++rank) {
        mining.push_back({rank == 5 ? 100000.0 : 0.0, 0.05 * rank, 0.0});
    }
    json logging = json::array();
    for (int rank = 0; rank <= 60; ++rank) {
        logging.push_back({0.0, 0.0, 0.0});
    }
    json types = json::array({{kOreSlice, kOreRow, "Test_Ore_A"},
                              {kStoneSlice, kStoneRow, "Test_Stone_A"},
                              {kTreeSlice, kTreeRow, "Test_Tree_A"},
                              {kHempSlice, kHempRow, "Test_Hemp_A"},
                              {-1, -1, "Test_Rock_Decoration"},
                              {kStoneSlice, kQuestRow, "Test_Quest_A"},
                              {kStoneSlice, kNoLootRow, "Test_NoLoot_A"},
                              {kStoneSlice, kMysteryRow, "Test_Mystery_A"},
                              {kStoneSlice, kHeapRow, "Test_Heap_A"}});
    json regionList = json::array({json{{"name", kRegion},
                                        {"x", 0},
                                        {"y", 0},
                                        {"file", std::string(kRegion) + ".swgn"},
                                        {"records", static_cast<std::uint32_t>(kRecordCount) + nearExtra},
                                        {"tails", 2},
                                        {"version", kVersion},
                                        {"types", types}}});
    for (const auto& [name, x, y, records] : regions) {
        regionList.push_back(json{{"name", name},
                                  {"x", x},
                                  {"y", y},
                                  {"file", name + ".swgn"},
                                  {"records", records},
                                  {"tails", 0},
                                  {"version", kVersion},
                                  {"types", json::array({{kOreSlice, kOreRow, "Test_Ore_A"}})}});
    }
    const SliceIds wolf = slice_ids(kWolfSlice);
    return json{
        {"format", 1},
        {"option", {{"id", gamedata::interaction_option_id("GatherOption")}, {"action", 14}}},
        {"rows", rows},
        {"events",
         {{"Test_GatherMining", {"Mining", 40}},
          {"Test_GatherLogging", {"Logging", 44}},
          {"Test_GatherHarvesting", {"Harvesting", 20}},
          {"Test_GatherSkinning", {"Skinning", 32}}}},
        {"ranks", {{"Mining", mining}, {"Logging", logging}, {"Harvesting", json::array({{0.0, 0.0, 0.0}})}, {"Skinning", json::array({{0.0, 0.0, 0.0}})}}},
        {"tools",
         {{"TestPick", {{"types", {"Mining"}}, {"min", 1.0}, {"max", 2.0}, {"efficiency", 0.0}, {"multiplier", 0.0}}},
          {"TestAxe", {{"types", {"Chopping"}}, {"min", 1.5}, {"max", 3.0}, {"efficiency", 0.0}, {"multiplier", 0.0}}},
          {"TestSickle", {{"types", {"Cutting"}}, {"min", 1.0}, {"max", 1.0}, {"efficiency", 0.0}, {"multiplier", 0.0}}},
          {"TestKnife", {{"types", {"Dressing"}}, {"min", 2.2}, {"max", 2.2}, {"efficiency", 0.0}, {"multiplier", 0.0}}}}},
        {"templates",
         json::array({template_json(kOreSlice, "test_ore", "Gatherable_Minerals"), template_json(kStoneSlice, "test_stone", "Gatherable_Stones"),
                      template_json(kTreeSlice, "test_tree", "Gatherable_Trees"), template_json(kHempSlice, "test_hemp", "Gatherable_Plants"),
                      template_json(kWolfSlice, "test_wolf", "Gatherable_Skinning")})},
        {"capitals", {{object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex}).key, {kOreSlice, kOreRow}}}},
        {"regions", regionList},
        {"creatures", {{hex_bytes(wolf.asset) + ":" + std::to_string(wolf.sub), {{"template", kWolfSlice}, {"row", kSkinRow}}}}}};
}

/** @return The records of the region of the cases. */
inline std::vector<gamedata::NaturalRecord> region_records() {
    std::vector<gamedata::NaturalRecord> records;
    for (std::uint32_t record = 0; record < kRecordCount; ++record) {
        const state::WorldPosition place = record_place(record);
        records.push_back({record_type(record), place.x, place.y, place.z});
    }
    return records;
}

inline bool set_gathering(gamedata::GatheringData& data) {
    return data.set(gathering_document().dump(), {{kRegion, region_records()}});
}

/** @return The G of a record of the region of the cases (counter = record + 1). */
inline std::uint64_t node_group(std::uint32_t record) {
    return gamedata::GatheringData::natural_group(0, 0, record + 1);
}

/** @return The key of a natural node of the region of the cases. */
inline std::string node_key(std::uint32_t record) {
    return std::string(kRegion) + "#" + std::to_string(record);
}

inline std::vector<gamedata::ItemDefinition> gathering_items() {
    using chests::chest_item;
    return {chest_item("TestPick", "Weapon", "Pickaxe", 1, 1, 100, 200),        chest_item("TestAxe", "Weapon", "LoggingAxe", 1, 1, 100, 200),
            chest_item("TestSickle", "Weapon", "Sickle", 1, 1, 100, 200),       chest_item("TestKnife", "Weapon", "SkinningKnife", 1, 1, 100, 200),
            chest_item("TestOreT1", "Resource", "Resource", 1, 1000),           chest_item("TestStoneT1", "Resource", "Resource", 1, 1000),
            chest_item("TestWoodT1", "Resource", "Resource", 1, 1000),          chest_item("TestFiberT1", "Resource", "Resource", 1, 1000),
            chest_item("TestHideT1", "Resource", "Resource", 1, 1000),          chest_item("TestMeatT1", "Resource", "Resource", 1, 1000),
            chest_item("TestGemA", "Resource", "Resource", 3, 100),             chest_item("TestGemB", "Resource", "Resource", 3, 100),
            chest_item("TestFiller", "Armor", "EquippableChest+Armor", 2, 1, 200, 300)};
}

/**
 * The loot tables of the gathering roots. Test_GatherOre (AND): 2 TestOreT1 and the table Test_GatherRare (OR, rolls 1 to 100,000, AddToRoll:
 * TestGemA and TestGemB at the same threshold 99,999: a tie). Test_GatherStone: 1 TestStoneT1. Test_GatherWood: 7 TestWoodT1. Test_GatherFiber:
 * 3 TestFiberT1. Test_GatherHide: 2 TestHideT1 and 1 TestMeatT1. Test_GatherMystery: 1 TestMissingItem (the catalog does not have it).
 * Test_GatherHeap: 2,500 TestStoneT1 (three stacks of 1,000 at most). The
 * bonus tables of case GAT5: Test_BonusAdd, Test_BonusClamp, Test_BonusIgnore (rolls 1 to 100, TestGemA at 100).
 */
inline gamedata::LootData gathering_loot() {
    using chests::loot_entry;
    using chests::loot_table;
    using gamedata::LootEntryKind;
    gamedata::LootData data;
    for (std::uint32_t level = 1; level <= 65; ++level) {
        data.rewards.push_back({9 + level, 300 + 10 * level, 1});
        std::array<std::pair<std::uint32_t, std::uint32_t>, gamedata::kGearScoreTiers> ranges{};
        for (std::uint32_t tier = 0; tier < gamedata::kGearScoreTiers; ++tier) {
            ranges[tier] = {100 + 10 * tier + level, 120 + 10 * tier + level};
        }
        data.gearScore.push_back(ranges);
        data.lootCeiling[level] = 150 + level;
        data.containerFloor[level] = 250;
    }
    data.modifiers = {{"Solo", {1.0F, 1.0F, 0.0F, 1.0F, std::nullopt}}};
    data.events = {{"killevent", 0.05F}};
    data.credit.timeLimit = 120.0F;
    data.credit.healthShare = 0.01F;
    data.credit.range = 200.0F;
    data.credit.receivers = 1;
    data.credit.modifiers = {{1.0F, 1.0F, 1.0F}};
    gamedata::LootTable clamp = loot_table("Test_BonusClamp", false, 100, {}, {loot_entry(LootEntryKind::item, "TestGemA", 100)});
    clamp.bonus = gamedata::RollBonus::clampMax;
    gamedata::LootTable ignore = loot_table("Test_BonusIgnore", false, 100, {}, {loot_entry(LootEntryKind::item, "TestGemA", 100)});
    ignore.bonus = gamedata::RollBonus::ignoreBonus;
    data.tables = {
        loot_table("Test_GatherOre", true, 0, {},
                   {loot_entry(LootEntryKind::item, "TestOreT1", 0, 2, 2), loot_entry(LootEntryKind::table, "Test_GatherRare", 0)}),
        loot_table("Test_GatherRare", false, 100000, {},
                   {loot_entry(LootEntryKind::item, "TestGemA", 99999), loot_entry(LootEntryKind::item, "TestGemB", 99999)}),
        loot_table("Test_GatherStone", true, 0, {}, {loot_entry(LootEntryKind::item, "TestStoneT1", 0)}),
        loot_table("Test_GatherWood", true, 0, {}, {loot_entry(LootEntryKind::item, "TestWoodT1", 0, 7, 7)}),
        loot_table("Test_GatherFiber", true, 0, {}, {loot_entry(LootEntryKind::item, "TestFiberT1", 0, 3, 3)}),
        loot_table("Test_GatherHide", true, 0, {},
                   {loot_entry(LootEntryKind::item, "TestHideT1", 0, 2, 2), loot_entry(LootEntryKind::item, "TestMeatT1", 0)}),
        loot_table("Test_GatherMystery", true, 0, {}, {loot_entry(LootEntryKind::item, "TestMissingItem", 0)}),
        loot_table("Test_GatherHeap", true, 0, {}, {loot_entry(LootEntryKind::item, "TestStoneT1", 0, 2500, 2500)}),
        loot_table("Test_BonusAdd", false, 100, {}, {loot_entry(LootEntryKind::item, "TestGemA", 100)}),
        clamp,
        ignore,
    };
    return data;
}

/** The progression tables of the tests with the four trade skills of gathering (Mining 0 to 5 is in the tables of the tests). */
inline gamedata::ProgressionData gathering_progression() {
    gamedata::ProgressionData data = test_progression();
    gamedata::RankTable logging;
    logging.experience.assign(61, 1000);
    gamedata::RankTable small;
    small.experience.assign(6, 100);
    data.progressions.push_back({"Logging", 0, gamedata::ProgressionKind::tradeSkill, 60, logging});
    data.progressions.push_back({"Harvesting", 0, gamedata::ProgressionKind::tradeSkill, 5, small});
    data.progressions.push_back({"Skinning", 0, gamedata::ProgressionKind::tradeSkill, 5, small});
    return data;
}

/** The two characters (A and B, level 30) near the nodes, with their clients. */
struct GatherShop : Shop {
    /** @param data False: no gathering data (case GAT1). */
    explicit GatherShop(const std::string& database = ":memory:", bool existing = false, bool data = true)
        : Shop(false, database, true, existing) {
        if (!host.interactions.set({option_row("GatherOption", 14, true)}, {},
                                   {object_of(kCapitalTag, InteractionKind::gathering, kCapitalPlace, {kGatherOptionIndex})})) {
            throw std::runtime_error("gathering test: the interaction data was not set: " + host.interactions.error());
        }
        host.catalog.set(gathering_items());
        if (!host.loot.set(gathering_loot())) {
            throw std::runtime_error("gathering test: the loot tables were not set: " + host.loot.error());
        }
        if (!host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {}, {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("gathering test: the creature data was not set: " + host.creatures.error());
        }
        host.progression.set(gathering_progression());
        if (data && !set_gathering(host.gathering)) {
            throw std::runtime_error("gathering test: the gathering data was not set: " + host.gathering.error());
        }
        host.world.set_random_seed(0x36A6A7E5ULL);
    }

    [[nodiscard]] world::Gathering& gathering() {
        return host.world.gathering();
    }

    /** @return The block of GatherOption of a natural node, as the client makes it (live check L2: the offline id and the XOR rule). */
    [[nodiscard]] static InteractionBlock natural_block(std::uint32_t record) {
        const std::uint64_t group = node_group(record);
        const SliceIds ids = slice_ids(record == kTree ? kTreeSlice : record == kHemp ? kHempSlice : record_type(record) == 0 ? kOreSlice : kStoneSlice);
        const std::uint64_t k = ids.root ^ group;
        InteractionBlock block;
        for (std::size_t index = 0; index < 8; ++index) {
            block.actor[index] = static_cast<std::uint8_t>(group >> (8 * index));
        }
        block.group = group;
        block.target = (ids.entity ^ k) + ids.component;
        block.action = 14;
        block.baseAction = 14;
        block.optionEntity = ids.option ^ k;
        block.committed = true;
        block.optionId = gamedata::interaction_option_id("GatherOption");
        block.text88 = "@Test_Node";
        return block;
    }

    /** @return The block of the capital node (a placement of the interaction data). */
    [[nodiscard]] InteractionBlock capital_block() const {
        InteractionBlock block = block_for(host.interactions, 1, kGatherOptionIndex);
        block.text88 = "@Test_Node";
        return block;
    }

    /** @return The block of the skinning of the body of a creature (the XOR rule with the entity id of the creature as G). */
    [[nodiscard]] static InteractionBlock body_block(const world::Entity& creature) {
        const SliceIds ids = slice_ids(kWolfSlice);
        const std::uint64_t k = ids.root ^ creature.id;
        InteractionBlock block;
        block.actor = creature.actor.bytes;
        block.group = creature.id;
        block.target = (ids.entity ^ k) + ids.component;
        block.action = 14;
        block.baseAction = 14;
        block.optionEntity = ids.option ^ k;
        block.committed = true;
        block.optionId = gamedata::interaction_option_id("GatherOption");
        block.text88 = "@Test_Wolf";
        return block;
    }

    /** The world goes on by `milliseconds` in steps of 10 ms; the clients report their block each 100 ms (the input of a client). */
    void hold(FakeClient& client, const std::optional<InteractionBlock>& block, int milliseconds) {
        for (int passed = 0; passed < milliseconds; passed += 100) {
            report(client, block);
            host.steps(10, {&clientA, &clientB});
        }
    }

    /** A gather of a block: the request, the block held for `milliseconds`, the end of the block. */
    void gather(FakeClient& client, const InteractionBlock& block, int milliseconds) {
        request(client, block);
        hold(client, block, milliseconds);
        end_block(client);
    }

    /** Gives a character a tool with a gear score and puts it into its paperdoll slot. @return False when a step failed. */
    bool equip(const char* character, const char* itemId, std::uint32_t slot, std::uint32_t gearScore = 150) {
        world::GiveOptions options;
        options.gearScore = gearScore;
        const world::ItemResult given = host.world.inventory().give(character, itemId, 1, options);
        return given.ok() && !given.slots.empty() && host.world.inventory().equip(character, given.slots.front(), slot).ok();
    }

    bool set_rank(const char* character, const char* skill, std::uint32_t rank) {
        return host.world.skills().set_rank(character, skill, gamedata::ProgressionKind::tradeSkill, rank).ok();
    }

    /** @return The rank and the experience of a trade skill of a character. */
    [[nodiscard]] std::pair<std::uint32_t, std::uint64_t> skill(const char* character, const char* skill) {
        const world::Entity* entity = this->entity(character);
        if (entity == nullptr) {
            return {0, 0};
        }
        const auto found = entity->skills.progressions.find(gamedata::progression_id(skill));
        return found == entity->skills.progressions.end() ? std::pair<std::uint32_t, std::uint64_t>{0, 0}
                                                          : std::pair<std::uint32_t, std::uint64_t>{found->second.rank, found->second.points};
    }

    /** A wolf at a place, killed by character A (the body lies there). */
    world::Entity* dead_wolf(const state::WorldPosition& place = bags::kKillPlace) {
        world::Entity* wolf = host.world.creatures().spawn("Test_BagWolf", place, 0.0F).entity;
        if (wolf == nullptr) {
            throw std::runtime_error("gathering test: no wolf");
        }
        const world::EntityId id = wolf->id;
        (void)host.world.creatures().hurt(id, bags::kWolfHealth * 2.0F, entity(kCharacterA)->id);
        host.steps(3, {&clientA, &clientB});
        return host.world.find(id);
    }
};

/** @return The node states (type 12 of slot 3) that a client got, from a message index on: (interest, state). */
inline std::vector<std::pair<std::uint32_t, std::uint8_t>> node_states(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::pair<std::uint32_t, std::uint8_t>> found;
    for (const Seen& seen : fragments_of(client, kNodeSlot, kNodeType, from)) {
        const auto value = field_of(seen.fragment, 0, 0);
        found.emplace_back(seen.interest, value.empty() ? 0 : value[0]);
    }
    return found;
}

/** @return The last node state of a client from a message index on (0: none). */
inline std::uint8_t last_node_state(const FakeClient& client, std::size_t from = 0) {
    const auto states = node_states(client, from);
    return states.empty() ? 0 : states.back().second;
}

} // namespace soulwarden::backend::tests::gather

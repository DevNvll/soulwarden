// Tests of stage 3.5a: the rewards of a kill and the respawn of a creature (protocol notes 4.21).
//
//   LT1  the loot tables file: a file of the form of the tool, each part that does not fit
//        together refused as a whole, no file; the weapon mastery of the combat tables
//   LT2  the credit: the damage part, the time, a character that is dead, ignored, far, or not in
//        the world, a death by a command, two characters, the reset
//   LT3  the experience and the mastery: the worked example, the level difference, a level up,
//        the weapon in the hand, NoExpNoCoin, the states of the owner
//   LT4  the loot: the outer chance, the roll rules (AND, OR, ties, the bonus, the conditions,
//        the bucket tags, the odds, the quantities, the gear score, the guard), the delivery with
//        the loot notice cause and the coin in one transaction, a full bag
//   LT5  the respawn: the time of the spawner, a player near and no player near, the reset, a
//        removal by a command
//   LT6  two sessions: one kill, the rewards for one character, the death and the new creature
//        for the two observers
//   LT7  the load: 10,000 rolls of a master table of 19 entries; the kills of the creatures near 50
//        players in a world of 33,856 spawn points; the scans with the points that wait; with a
//        time limit
//   LT8  the loot tables tool with made-up tables (tools/gamedata/check_loot_tool.py; stage 3.5b.1: format 2)
//
// The accounts, the characters, the creature kinds, the items, the tables, and the places are
// made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "gamedata/loot_tables.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/item_decoder.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/rewards.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000a501";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000b502";
constexpr float kHeight = 50.0F;
constexpr std::uint32_t kSlots[] = {5, 9, 15};
/** The health of a creature of the levels 1, 8, 20, and 30 of the test data. */
constexpr float kHealth = 1000.0F;
constexpr std::uint32_t kBagSlot = 61;

state::WorldPosition at(float x, float y, float z = kHeight) {
    return state::WorldPosition{x, y, z};
}

gamedata::CreatureSlice loot_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/loot-wolf.dynamicslice";
    slice.asset.fill(0x61);
    slice.subId = 2;
    slice.root = 0x1000000000000061ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kSlots[0];
    slice.vitalsSlot = kSlots[1];
    slice.markerSlot = kSlots[2];
    slice.slots = {{kSlots[0], "ActionList", 11}, {kSlots[1], "Vitals", 15}, {kSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Death", "r_Death", 4, true}};
    return slice;
}

gamedata::CreatureKind loot_kind(const char* id, const char* vitals, std::uint32_t vitalsId, std::uint32_t level, const char* type) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = vitals;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = vitalsId + 1;
    kind.name = id;
    kind.vitalsName = id;
    kind.family = "Wildlife";
    kind.creatureType = type;
    kind.healthFactor = 1.0F;
    kind.level = level;
    kind.bodies = {{0, 100}};
    return kind;
}

gamedata::SpawnPoint loot_point(const char* id, float x, float y, float low, float high) {
    gamedata::SpawnPoint point;
    point.id = id;
    point.kind = 0;
    point.level = 8;
    point.position = at(x, y);
    point.respawnMin = low;
    point.respawnMax = high;
    point.ground = kHeight;
    return point;
}

/** The spawn points: a wolf with the respawn values (360, 0), (15, 360), (0, 0), and (360, 15). */
std::vector<gamedata::SpawnPoint> loot_points() {
    return {loot_point("p-wolf", 1010.0F, 1000.0F, 360.0F, 0.0F), loot_point("p-range", 1100.0F, 1000.0F, 15.0F, 360.0F),
            loot_point("p-zero", 1200.0F, 1000.0F, 0.0F, 0.0F), loot_point("p-inverse", 1300.0F, 1000.0F, 360.0F, 15.0F)};
}

bool set_creatures(ServerHost& host, std::vector<gamedata::SpawnPoint> points = loot_points()) {
    return host.creatures.set_full({loot_slice()},
                                   {loot_kind("Test_Wolf", "Test_Wolf", 0x61616161, 8, "Solo"),
                                    loot_kind("Test_Grunt", "Test_Grunt_Variant", 0x62626262, 20, "Solo+"),
                                    loot_kind("Test_Quiet", "Test_Quiet", 0x63636363, 8, "Solo"),
                                    loot_kind("Test_Plain", "Test_Plain", 0x64646464, 8, "Solo")},
                                   std::move(points),
                                   {{1, kHealth, 0.0F, 0.0F}, {8, kHealth, 0.0F, 0.0F}, {20, kHealth, 0.0F, 0.0F}, {30, kHealth, 0.0F, 0.0F}});
}

gamedata::ItemDefinition item(const char* id, const char* type, const char* itemClass, std::uint32_t tier, std::uint32_t stack,
                              std::uint32_t low = 0, std::uint32_t high = 0, std::uint32_t over = 0) {
    gamedata::ItemDefinition definition;
    definition.id = id;
    definition.nameKey = std::string("@") + id;
    definition.type = type;
    definition.itemClass = itemClass;
    definition.tier = tier;
    definition.maxStack = stack;
    definition.durability = high != 0 ? 1000 : 0;
    definition.minGearScore = low;
    definition.maxGearScore = high;
    definition.gearScoreOverride = over;
    return definition;
}

/** The items: index K + 1 for item K. */
std::vector<gamedata::ItemDefinition> loot_items() {
    return {item("TestSword", "Weapon", "EquippableMainHand+Sword", 2, 1, 200, 300),
            item("TestSwordT3", "Weapon", "EquippableMainHand+Sword", 3, 1, 300, 400),
            item("TestArmorT2", "Armor", "EquippableChest+Armor", 2, 1, 200, 300),
            item("TestHide", "Resource", "Resource", 1, 1000),
            item("TestOre", "Resource", "Resource", 1, 1000),
            item("TestTrophy", "Resource", "Resource", 0, 1),
            item("TestNamed", "Weapon", "EquippableMainHand+Sword", 5, 1, 590, 625, 600)};
}

/** The combat tables: a sword whose weapon row has the mastery SwordAbilityTable. */
gamedata::CombatData loot_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct}, {"Slash", 4, gamedata::DamageCategory::physical}};
    data.attributeMinimum = 5;
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity, world::attribute::kIntelligence,
                                     world::attribute::kFocus}) {
        data.attributeDamage[attribute].assign(96, 1.0F);
    }
    data.damageRows = {{"Test_Sword_Light", "Light", 1, 1.0F, 0.0F}};
    gamedata::WeaponRow sword;
    sword.id = "TestSwordRow";
    sword.melee = true;
    sword.tags = "1H_Melee,Sword";
    sword.baseDamage = 64.0F;
    sword.lightRow = 0;
    sword.mastery = "SwordAbilityTable";
    data.weapons = {sword};
    data.items = {{"testsword", 0}};
    gamedata::DefenseRow none;
    none.weakness.assign(data.damageTypes.size(), 0.0F);
    none.absorption.assign(data.damageTypes.size(), 0.0F);
    data.defenses = {none};
    return data;
}

gamedata::LootEntry entry(gamedata::LootEntryKind kind, const char* id, std::uint32_t threshold, std::uint32_t low = 1,
                          std::uint32_t high = 1, std::uint32_t gearLow = 0, std::uint32_t gearHigh = 0) {
    gamedata::LootEntry value;
    value.kind = kind;
    value.id = id;
    value.threshold = threshold;
    value.quantityMin = low;
    value.quantityMax = high;
    value.gearScoreMin = gearLow;
    value.gearScoreMax = gearHigh;
    return value;
}

gamedata::LootTable table(const char* id, bool all, gamedata::RollBonus bonus, std::uint32_t maxRoll,
                          std::vector<std::string> conditions, std::vector<gamedata::LootEntry> entries) {
    gamedata::LootTable value;
    value.id = id;
    value.all = all;
    value.bonus = bonus;
    value.maxRoll = maxRoll;
    value.conditions = std::move(conditions);
    value.entries = std::move(entries);
    return value;
}

gamedata::LootTag tag(const char* name) {
    return gamedata::LootTag{name, false, 0, 0};
}
gamedata::LootTag tag(const char* name, std::uint32_t low, std::uint32_t high) {
    return gamedata::LootTag{name, true, low, high};
}

gamedata::BucketCandidate candidate(const char* itemId, std::uint32_t low, std::uint32_t high, std::vector<gamedata::LootTag> tags,
                                    bool matchOne = false, float odds = 1.0F) {
    gamedata::BucketCandidate value;
    value.item = itemId;
    value.quantityMin = low;
    value.quantityMax = high;
    value.tags = std::move(tags);
    value.matchOne = matchOne;
    value.odds = odds;
    return value;
}

/** The indexes of the tables of the test data. */
enum TableIndex : std::uint32_t {
    kMaster,
    kCommon,
    kGear,
    kFixed,
    kAll,
    kTags,
    kCycle,
    kLimit,
    kBuckets,
    kGearScore,
};

/**
 * The loot data of the cases. The rewards: KillExperience 9 + level, KillCurrency 300 + 10 * level.
 * The master table is the form of CreatureLootMaster of the game: an AND table of the GlobalMod with
 * the common table at 0 and a rare bucket at 999900; the common table is an OR table of the tag
 * Common: the gear at 35000, the resources at 35000 (a tie), the trophy at 99500.
 */
gamedata::LootData loot_data() {
    using gamedata::LootEntryKind;
    using gamedata::RollBonus;
    gamedata::LootData data;
    for (std::uint32_t level = 1; level <= 30; ++level) {
        data.rewards.push_back({9 + level, 300 + 10 * level, 1});
    }
    data.modifiers = {{"Solo", {1.0F, 1.0F, 0.0F, 1.0F, std::nullopt}},
                      {"Solo+", {2.0F, 1.15F, 0.0F, 1.0F, std::nullopt}},
                      {"Critter", {0.0F, 0.0F, 0.0F, 1.0F, std::nullopt}}};
    data.disparity.push_back({-20, -0.8F, 0});
    for (std::int32_t difference = -6; difference <= -3; ++difference) {
        data.disparity.push_back({difference, -0.4F, 0});
    }
    for (std::int32_t difference = 10; difference <= 20; ++difference) {
        data.disparity.push_back({difference, 0.0F, 10});
    }
    data.events = {{"killevent", 0.05F}};
    data.credit.timeLimit = 120.0F;
    data.credit.healthShare = 0.01F;
    data.credit.range = 200.0F;
    data.credit.receivers = 10;
    // A modifier for each number of receivers (the loader needs one, finding F2 of the review of
    // stage 3.5a): the last one for 3 to 10 characters.
    data.credit.modifiers = {{1.0F, 1.0F, 1.0F}, {0.7F, 1.0F, 0.8F}};
    data.credit.modifiers.resize(10, {0.55F, 1.0F, 0.6F});
    for (std::uint32_t level = 1; level <= 30; ++level) {
        std::array<std::pair<std::uint32_t, std::uint32_t>, gamedata::kGearScoreTiers> ranges{};
        for (std::uint32_t tier = 0; tier < gamedata::kGearScoreTiers; ++tier) {
            ranges[tier] = {100 + 10 * tier + level, 120 + 10 * tier + level};
        }
        data.gearScore.push_back(ranges);
        data.lootCeiling[level] = 150 + level;
    }
    data.tables = {
        table("Test_Master", true, RollBonus::clampMax, 1000000, {"GlobalMod"},
              {entry(LootEntryKind::table, "Test_Common", 0), entry(LootEntryKind::bucket, "Test_Rare", 999900)}),
        table("Test_Common", false, RollBonus::clampMax, 100000, {"Common"},
              {entry(LootEntryKind::table, "Test_Gear", 35000), entry(LootEntryKind::bucket, "Test_Resources", 35000),
               entry(LootEntryKind::item, "TestTrophy", 99500)}),
        table("Test_Gear", false, RollBonus::addToRoll, 100000, {},
              {entry(LootEntryKind::bucket, "Test_Weapons", 40000), entry(LootEntryKind::item, "TestArmorT2", 65000)}),
        // The roll is the level of the character (a numeric condition that overrides the roll).
        table("Test_Fixed", false, RollBonus::addToRoll, 0, {"Level"},
              {entry(LootEntryKind::item, "TestHide", 0, 2, 2), entry(LootEntryKind::item, "TestOre", 5),
               entry(LootEntryKind::item, "TestTrophy", 5), entry(LootEntryKind::item, "TestSword", 50, 1, 1, 210, 220)}),
        table("Test_All", true, RollBonus::ignoreBonus, 100, {},
              {entry(LootEntryKind::item, "TestHide", 0), entry(LootEntryKind::item, "TestOre", 0, 3, 3),
               entry(LootEntryKind::item, "TestTrophy", 101)}),
        table("Test_Tags", true, RollBonus::ignoreBonus, 0, {"Elite", "Named"}, {entry(LootEntryKind::item, "TestHide", 0)}),
        table("Test_Cycle", true, RollBonus::ignoreBonus, 0, {},
              {entry(LootEntryKind::table, "Test_Cycle", 0), entry(LootEntryKind::item, "TestHide", 0)}),
        table("Test_Limit", true, RollBonus::ignoreBonus, 0, {"[LIM]Test_Limit"}, {entry(LootEntryKind::item, "TestHide", 0)}),
        table("Test_Buckets", true, RollBonus::ignoreBonus, 0, {},
              {entry(LootEntryKind::bucket, "Test_Weapons", 0, 3, 3), entry(LootEntryKind::bucket, "Test_Resources", 0, 50, 50)}),
        table("Test_GearScore", true, RollBonus::ignoreBonus, 0, {},
              {entry(LootEntryKind::item, "TestSwordT3", 0), entry(LootEntryKind::item, "TestNamed", 0),
               entry(LootEntryKind::item, "TestSword", 0, 1, 1, 900, 950)}),
    };
    data.tables[kMaster].useLevelGearScore = true;
    data.tables[kFixed].conditionOverridesRoll = true;
    data.tables[kGearScore].useLevelGearScore = true;
    data.tables[kGearScore].gearScoreBonus = 5;
    data.buckets = {
        {"Test_Weapons",
         {candidate("TestSword", 1, 1, {tag("MinContLevel", 0, 18)}), candidate("TestSwordT3", 1, 1, {tag("MinContLevel", 19, 38)}),
          candidate("TestArmorT2", 1, 1, {}, false, 0.0F)}},
        {"Test_Resources",
         {candidate("TestHide", 1, 3, {tag("Beast")}), candidate("TestOre", 2, 2, {tag("Lost"), tag("Corrupted")}, true),
          candidate("TestTrophy", 1, 1, {tag("Beast"), tag("Elite")})}},
        {"Test_Rare", {candidate("TestNamed", 1, 1, {tag("Named")})}},
    };
    const auto base = [](const char* id, const char* type, bool drops, float chance, bool ignore, std::optional<std::uint32_t> lootTable,
                         std::vector<std::string> tags, bool quiet) {
        gamedata::LootVitals value;
        value.id = id;
        value.creatureType = type;
        value.canDropLoot = drops;
        value.dropChance = chance;
        value.ignoreDropModifier = ignore;
        value.table = lootTable;
        value.tags = std::move(tags);
        value.noExperienceNoCoin = quiet;
        return value;
    };
    data.bases = {base("Test_Wolf", "Solo", true, 0.1F, false, kMaster, {"Beast", "Common"}, false),
                  base("Test_Grunt", "Solo+", true, 0.35F, false, kMaster, {"Lost", "Common"}, false),
                  base("Test_Quiet", "Solo", true, 1.0F, true, kMaster, {"Beast", "Common"}, true),
                  base("Test_Plain", "Solo", false, 0.0F, false, std::nullopt, {}, false)};
    data.vitals = {{"test_wolf", 0}, {"test_grunt", 1}, {"test_grunt_variant", 1}, {"test_quiet", 2}, {"test_plain", 3}};
    return data;
}

/** A host with the creature data, the items, the combat tables, the progression tables, and the loot tables. */
struct LootHost {
    ServerHost host;
    explicit LootHost(bool loot = true, std::vector<gamedata::SpawnPoint> points = loot_points()) {
        if (!set_creatures(host, std::move(points))) {
            throw std::runtime_error("loot test: the creature data is not valid: " + host.creatures.error());
        }
        host.catalog.set(loot_items());
        if (!host.combat.set(loot_combat())) {
            throw std::runtime_error("loot test: the combat tables are not valid");
        }
        set_progression(host);
        if (loot && !host.loot.set(loot_data())) {
            throw std::runtime_error("loot test: the loot tables are not valid");
        }
        host.world.set_random_seed(0x5EED3501ULL);
    }
    world::World& world() {
        return host.world;
    }
    world::Rewards& rewards() {
        return host.world.rewards();
    }
    world::Creatures& creatures() {
        return host.world.creatures();
    }
    /** A character of the store in the world as an entity with no session. */
    world::Entity* character(const char* id, const char* name, const state::WorldPosition& place, unsigned number) {
        add_player(host.store, "7656120225533" + std::to_string(4100 + number), std::string("Tester ") + name, id, name, std::nullopt);
        if (host.world.spawn_character(id, place) != world::SpawnError::none) {
            return nullptr;
        }
        return host.world.find_character(id);
    }
    world::Entity* wolf(const state::WorldPosition& place, const char* kind = "Test_Wolf", std::optional<std::uint32_t> level = {}) {
        return creatures().spawn(kind, place, 0.0F, level).entity;
    }
    /** The world clock goes on by `seconds`, in ticks of the spawn point rule. */
    void pass(float seconds) {
        const int steps = static_cast<int>(std::lround(seconds / 0.5F));
        for (int index = 0; index < steps; ++index) {
            host.clock.advance(std::chrono::milliseconds(500));
            host.world.tick();
        }
    }
};

void arm_sword(world::Entity& character) {
    world::ItemInstance sword;
    sword.instance.fill(0x5B);
    sword.itemId = "TestSword";
    sword.index = 1;
    sword.quantity = 1;
    sword.gearScore = 200;
    character.equipment.slots[world::paperdoll::kMainHand1].item = sword;
}

std::uint32_t count_of(const world::Bag& bag, const std::string& itemId) {
    std::uint32_t total = 0;
    for (const world::BagSlot& slot : bag.slots) {
        total += slot.item && slot.item->itemId == itemId ? slot.item->quantity : 0;
    }
    return total;
}

std::uint32_t count_of(const std::vector<world::LootDrop>& drops, const std::string& itemId) {
    std::uint32_t total = 0;
    for (const world::LootDrop& drop : drops) {
        total += drop.itemId == itemId ? drop.quantity : 0;
    }
    return total;
}

std::uint32_t u32_of(const Bytes& value) {
    server::hub::Reader reader(value);
    return reader.u32();
}

/** LT1: the loot tables file. */
void loot_file(const Options& options) {
    json document{
        {"format", 1},
        {"rewards", {{1, 10, 310, 1}, {2, 11, 320, 1}, {3, 12, 330, 1}}},
        {"modifiers", {{"Solo", {1, 1, 0, 1, nullptr}}, {"Solo+", {2, 1.15, 0, 1, nullptr}}, {"Rich", {1, 1, 0.5, 1, 3.0}},
                       {"Critter", {0, 0, 0, 1, nullptr}}}},
        {"disparity", {{10, 0, 10}, {-10, -0.4, 0}}},
        {"events", {{"killevent", 0.05}, {"KillStructure", 0}}},
        {"credit", {{"timeLimit", 120}, {"healthShare", 0.01}, {"range", 200}, {"receivers", 2}, {"modifiers", {{1, 1, 1}, {0.7, 1, 0.8}}}}},
        {"gearScore", {{100, 100, 100, 100, 100, 100, 100, 100, 100, 100}, {100, 120, 100, 130, 100, 130, 100, 130, 100, 130}}},
        {"lootCeiling", {{1, 100}, {2, 120}}},
        {"bases", {{"Wolf", "Solo", true, 0.1, false, "Master", {"Beast", "Common"}, false, ""},
                   {"Rock", "Critter", false, 0, false, "", json::array(), true, "KillStructure"}}},
        {"vitals", {{"wolf", 0}, {"wolf_grey_7", 0}, {"rock", 1}}},
        {"tables",
         {{"Master", {1, 1, 1000000, {"GlobalMod"}, false, true, 0, {{"t", "Common", 0, 1, 1, 0, 0}, {"b", "Rare", 999900, 1, 1, 0, 0}}}},
          {"Common", {0, 0, 100000, {"Common"}, false, false, 5, {{"i", "TestOre", 90000, 1, 3, 0, 0}, {"b", "Weapons", 35000, 1, 1, 200, 240}}}}}},
        {"buckets",
         {{"Weapons", {{"TestSword", 1, 1, 1.0, false, {{"MinContLevel", 0, 18}, {"Beast"}}}}}, {"Rare", {{"TestNamed", 1, 1, 0.2, true, {{"Named"}}}}}}},
        {"counts", {{"tables", 2}}}};
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "loot-tables.json";
    const auto load = [&path](const json& content, gamedata::LootTables& tables) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return tables.load(path.string());
    };
    gamedata::LootTables tables;
    check(load(document, tables) && tables.loaded(), "a loot tables file of the form of the tool loads");
    const gamedata::LootVitals* wolf = tables.vitals_of("Wolf_Grey_7");
    const gamedata::LootTable* master = tables.table("master");
    const gamedata::LootTable* common = tables.table("Common");
    check(wolf != nullptr && wolf->id == "Wolf" && wolf->canDropLoot && wolf->dropChance == 0.1F && wolf->table && master != nullptr
              && &tables.table(*wolf->table) == master && wolf->tags == std::vector<std::string>{"Beast", "Common"}
              && tables.vitals_of("ROCK") != nullptr && tables.vitals_of("rock")->noExperienceNoCoin
              && tables.vitals_of("rock")->event == "killstructure" && tables.vitals_of("nothing") == nullptr,
          "a vitals row (a variant row and a base row, with no regard to the case) has the loot data of its base row");
    check(master->all && master->bonus == gamedata::RollBonus::clampMax && master->maxRoll == 1000000 && master->useLevelGearScore
              && master->entries.size() == 2 && master->entries[1].kind == gamedata::LootEntryKind::bucket
              && tables.bucket(master->entries[1].index).id == "Rare" && common != nullptr && common->gearScoreBonus == 5
              && common->entries[0].threshold == 35000 && common->entries[0].gearScoreMin == 200
              && common->entries[1].quantityMax == 3,
          "a table has its mode, its bonus setting, MaxRoll, its gear score values, and its entries in the sequence of their "
          "thresholds, with each reference resolved");
    const gamedata::LootBucket& weapons = tables.bucket(common->entries[0].index);
    check(weapons.candidates.size() == 1 && weapons.candidates[0].tags.size() == 2 && weapons.candidates[0].tags[0].numeric
              && weapons.candidates[0].tags[0].high == 18 && !weapons.candidates[0].tags[1].numeric
              && tables.bucket(master->entries[1].index).candidates[0].matchOne
              && tables.bucket(master->entries[1].index).candidates[0].odds == 0.2F,
          "a bucket candidate has its item, its quantity, its odds, MatchOne, and its tags (a name, or a name with a range)");
    check(tables.reward(2) != nullptr && tables.reward(2)->experience == 11 && tables.reward(2)->coin == 320 && tables.reward(4) == nullptr
              && tables.modifier("Solo+") != nullptr && tables.modifier("Solo+")->experience == 2.0F
              && !tables.modifier("Solo")->progression && tables.modifier("Rich")->progression == 3.0F
              && tables.disparity(-10) != nullptr && tables.disparity(-10)->experience == -0.4F && tables.disparity(0) == nullptr
              && tables.disparity_range() == std::make_pair(-10, 10) && tables.coin_chance("") == 0.05F
              && tables.coin_chance("KillStructure") == 0.0F && tables.coin_chance("other") == 0.05F
              && tables.data().credit.modifiers.size() == 2 && tables.data().credit.modifiers[1].coin == 0.8F
              && tables.gear_score(2, 3) == std::make_pair(100U, 130U) && !tables.gear_score(3, 1) && !tables.gear_score(1, 6)
              && tables.loot_ceiling(2) == 120U,
          "the rewards of each level, the creature types (an empty progression amount is no value), the level differences, "
          "the kill events, the credit data, the gear score ranges, and the loot ceilings");

    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json wrong = document;
        change(wrong);
        gamedata::LootTables other;
        const bool loaded = load(wrong, other);
        if (loaded || other.loaded() || other.error().empty()) {
            note(std::string("accepted: ") + what);
        }
        return !loaded && !other.loaded() && !other.error().empty();
    };
    bool all = true;
    all = refused("format 2", [](json& d) { d["format"] = 2; }) && all;
    all = refused("no member tables", [](json& d) { d.erase("tables"); }) && all;
    all = refused("reward levels that do not start at 1", [](json& d) { d["rewards"][0][0] = 2; }) && all;
    all = refused("a reward level two times", [](json& d) { d["rewards"][1][0] = 1; }) && all;
    all = refused("a negative experience factor", [](json& d) { d["modifiers"]["Solo"][0] = -1; }) && all;
    all = refused("a coin chance above 1", [](json& d) { d["modifiers"]["Solo"][2] = 2; }) && all;
    all = refused("a text in the place of a number", [](json& d) { d["modifiers"]["Solo"][1] = "1"; }) && all;
    all = refused("a level difference two times", [](json& d) { d["disparity"][1][0] = 10; }) && all;
    all = refused("no kill event", [](json& d) { d["events"].erase("killevent"); }) && all;
    all = refused("credit data with no modifier", [](json& d) { d["credit"]["modifiers"] = json::array(); }) && all;
    all = refused("credit data with a part of the health above 1", [](json& d) { d["credit"]["healthShare"] = 2; }) && all;
    all = refused("a gear score range whose end is below its start", [](json& d) { d["gearScore"][1][1] = 90; }) && all;
    all = refused("a vitals row that names a table that the file does not have", [](json& d) { d["bases"][0][5] = "Nothing"; }) && all;
    all = refused("a vitals id with a base row outside the list", [](json& d) { d["vitals"]["wolf"] = 7; }) && all;
    all = refused("a table entry that names a table that the file does not have", [](json& d) { d["tables"]["Master"][7][0][1] = "Nothing"; }) && all;
    all = refused("a table entry that names a bucket that the file does not have", [](json& d) { d["tables"]["Master"][7][1][1] = "Nothing"; }) && all;
    all = refused("an entry kind that is not t, b, or i", [](json& d) { d["tables"]["Master"][7][0][0] = "x"; }) && all;
    all = refused("a quantity range whose end is below its start", [](json& d) { d["tables"]["Common"][7][0][3] = 5; }) && all;
    all = refused("a mode 2", [](json& d) { d["tables"]["Master"][0] = 2; }) && all;
    all = refused("a bucket tag range whose end is below its start", [](json& d) { d["buckets"]["Weapons"][0][5][0][1] = 40; }) && all;
    all = refused("a bucket candidate with a negative odds", [](json& d) { d["buckets"]["Weapons"][0][3] = -1; }) && all;
    all = refused("a bucket candidate with 5 values", [](json& d) { d["buckets"]["Weapons"][0].erase(5); }) && all;
    check(all, "the loader refuses a file whose parts do not fit together, as a whole: a format, a member, a level, a factor, a "
               "difference, the kill event, the credit data, a range, a reference of a vitals row, of a table entry, and of a "
               "bucket, an entry kind, a mode, a tag, an odds");
    gamedata::LootTables none;
    check(!none.load((options.workDirectory / "no-loot-tables.json").string()) && !none.loaded() && none.error() == "the file is not there",
          "with no file the loader has no tables");

    // A world with no loot tables: a kill gives nothing.
    LootHost empty(false);
    world::Entity* alpha = empty.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    world::Entity* wolfEntity = empty.wolf(at(1003, 1000));
    empty.rewards().set_forced(kCharacterA, true);
    const std::uint32_t before = alpha->progression.experience;
    (void)empty.creatures().hurt(wolfEntity->id, kHealth, alpha->id);
    check(wolfEntity->creature.dead && alpha->progression.experience == before && alpha->bag.slots.empty()
              && alpha->wallet.coin == 0 && empty.rewards().counts().kills == 0,
          "with no loot tables a kill gives nothing (as before stage 3.5a)");

    // The weapon mastery of the combat tables: a weapon row of 12 values has it; a file from before has 11.
    const auto combatPath = options.workDirectory / "loot-combat-tables.json";
    json combat{{"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreSmallerRate", 0.6667},
                               {"gearScoreInterval", 5}, {"gearScoreMinimum", 100}, {"gearScoreLimit", 500}}},
                {"damageTypes", {{"Slash", 4, "Physical"}}},
                {"attributes", {{"minimum", 5}, {"strength", {0.0}}, {"dexterity", {0.0}}, {"intelligence", {0.0}}, {"focus", {0.0}}}},
                {"damageRows", {{"Row_Light", "Sword", "Light", "Slash", 1.0, 0.0}}},
                {"weapons", {{"SwordT1", "Combat Melee", "1H_Melee,Sword", 64.0, 0.0, 0.0, 0.0, 0.0, "Row_Light", "", 0.0, "SwordAbilityTable"},
                             {"Unarmed", "Combat Melee", "2H_Melee", 10.0, 0.0, 0.0, 0.0, 0.0, "Row_Light", "", 0.0}}},
                {"items", {{"1hswordt1", 0}, {"unarmed", 1}}},
                {"defenses", json::array()},
                {"vitals", json::object()}};
    const auto loadCombat = [&](const json& content, gamedata::CombatTables& value) {
        std::ofstream(combatPath, std::ios::binary | std::ios::trunc) << content.dump();
        return value.load(combatPath.string());
    };
    gamedata::CombatTables withMastery;
    gamedata::CombatTables badMastery;
    json wrongMastery = combat;
    wrongMastery["weapons"][0][11] = 7;
    check(loadCombat(combat, withMastery) && withMastery.weapon_of("1HSwordT1")->mastery == "SwordAbilityTable"
              && withMastery.weapon_of("unarmed")->mastery.empty() && !loadCombat(wrongMastery, badMastery),
          "the combat tables: a weapon of 12 values has its mastery, a weapon of 11 values (a file from before) has none, and "
          "a mastery that is not a text is refused");
}

/** LT2: the credit of a kill. */
void credit(const Options&) {
    LootHost world;
    world::Entity* alpha = world.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    world::Entity* beta = world.character(kCharacterB, "Beta", at(1001, 1000), 2);
    world.rewards().set_forced(kCharacterA, true);
    world.rewards().set_forced(kCharacterB, true);
    (void)world.world().progress().set_level(kCharacterA, 5);
    (void)world.world().progress().set_level(kCharacterB, 5);
    const auto experience = [](const world::Entity* entity) { return entity->progression.experience; };
    const std::uint32_t start = experience(alpha);

    // One character: its hits did 1% of the health or more.
    world::Entity* first = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(first->id, 10.0F, alpha->id);
    (void)world.creatures().hurt(first->id, kHealth, beta->id);
    check(experience(alpha) == start + 11 && experience(beta) == start + 11 && alpha->wallet.coin == 304 && beta->wallet.coin == 304,
          "two characters with the credit (10 of 1000 health is 1 percent, the share of the game): each one gets the "
          "experience and the coin with the factors of two characters (17 * 0.7 = 11, 380 * 0.8 = 304)");
    const std::uint32_t afterFirst = experience(alpha);

    world::Entity* second = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(second->id, 9.0F, alpha->id);
    (void)world.creatures().hurt(second->id, kHealth, beta->id);
    check(experience(alpha) == afterFirst && experience(beta) == start + 11 + 17,
          "a character whose hits did less than 1 percent of the health gets nothing; the other one gets the full reward");

    world::Entity* third = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(third->id, 100.0F, alpha->id);
    world.host.clock.advance(std::chrono::seconds(121));
    (void)world.creatures().hurt(third->id, kHealth, beta->id);
    check(experience(alpha) == afterFirst && log_count("its last hit is older than the time of the credit") == 1,
          "a character whose last hit is more than 120 s before the death gets nothing");

    const auto refusedFor = [&](const char* cause, const std::function<void()>& before, const std::function<void()>& after) {
        world::Entity* creature = world.wolf(at(1003, 1000));
        const std::uint32_t was = experience(alpha);
        (void)world.creatures().hurt(creature->id, 100.0F, alpha->id);
        before();
        (void)world.creatures().hurt(creature->id, kHealth, beta->id);
        world::Entity* still = world.world().find_character(kCharacterA);
        const bool nothing = still == nullptr || experience(still) == was;
        after();
        return nothing && log_count(cause) >= 1;
    };
    check(refusedFor("it is dead", [&] { (void)world.world().health().hurt(kCharacterA, 100000.0F); },
                     [&] { (void)world.world().health().heal(kCharacterA); }),
          "a character that is dead at the death of the creature gets nothing");
    check(refusedFor("the creatures ignore it", [&] { world.creatures().behavior().set_ignored(kCharacterA, true); },
                     [&] { world.creatures().behavior().set_ignored(kCharacterA, false); }),
          "a character that the creatures ignore gets nothing");
    check(refusedFor("it is too far from the creature", [&] { (void)world.world().set_position(alpha->id, at(1300, 1000), false); },
                     [&] { (void)world.world().set_position(alpha->id, at(1000, 1000), false); }),
          "a character more than 200 m from the creature gets nothing");
    check(refusedFor("it is not in the world", [&] { (void)world.world().remove_character(kCharacterA, world::LostCause::deleted); },
                     [] {}),
          "a character that left the world gets nothing");
    // The character comes back into the world (a new entity).
    (void)world.world().spawn_character(kCharacterA, at(1000, 1000));
    alpha = world.world().find_character(kCharacterA);

    // A death by a command gives nothing; a death by a hit of a character that has no credit gives nothing to the others.
    world::Entity* commanded = world.wolf(at(1003, 1000));
    const std::uint64_t kills = world.rewards().counts().kills;
    const std::uint32_t was = experience(beta);
    (void)world.creatures().hurt(commanded->id, 500.0F, beta->id);
    (void)world.creatures().kill(commanded->id);
    check(commanded->creature.dead && experience(beta) == was && world.rewards().counts().kills == kills,
          "a death by the kill command gives nothing, also to a character that hurt the creature");

    // The reset: at its anchor with its full health the creature has no credit.
    world::Entity* reset = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(reset->id, 500.0F, alpha->id);
    check(reset->creature.credit.size() == 1 && reset->creature.credit[0].damage == 500.0F, "setup: the creature has the credit of one hit");
    world.creatures().restore(*reset);
    const std::uint32_t alphaWas = experience(alpha);
    (void)world.creatures().hurt(reset->id, kHealth, beta->id);
    check(reset->creature.credit.size() == 1 && experience(alpha) == alphaWas && reset->creature.dead,
          "the reset (the full health at the anchor) takes the credit away: only the character of the later hits gets the "
          "reward");
}

/** LT3: the experience and the weapon mastery of a kill. */
void experience(const Options&) {
    LootHost world;
    world::Entity* alpha = world.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    (void)world.world().progress().set_level(kCharacterA, 5);
    const std::uint32_t start = alpha->progression.experience;
    world::Entity* wolf = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(wolf->id, kHealth, alpha->id);
    check(alpha->progression.experience == start + 17 && alpha->progression.level == 5,
          "the worked example: a level 8 creature of the type Solo and a character of level 5 (a difference of +3, no row) give "
          "KillExperience 17 * 1 * 1 * (1 + 0) = 17 experience");
    check(alpha->skills.rank(kSword) == 0 && alpha->skills.progressions.count(kSword) == 0,
          "a character with no weapon in its hand gets no mastery experience");

    arm_sword(*alpha);
    const std::uint32_t armed = alpha->progression.experience;
    world::Entity* second = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(second->id, kHealth, alpha->id);
    check(alpha->progression.experience == armed + 17 && alpha->skills.rank(kSword) == 1
              && alpha->skills.progressions.at(kSword).points == 7,
          "the mastery of the weapon in the hand (WeaponMasteryCategoryId of the sword: SwordAbilityTable) gets the experience "
          "of the kill: 17 = rank 1 (10) and 7 of rank 1");

    // The level difference: a creature 4 levels below (-0.4), and a creature far above (its reward level is the level of
    // the character plus 10).
    const std::uint32_t low = alpha->progression.experience;
    world::Entity* small = world.wolf(at(1003, 1000), "Test_Wolf", 1);
    (void)world.creatures().hurt(small->id, kHealth, alpha->id);
    check(alpha->progression.experience == low + 6, "a creature 4 levels below the character: 10 * (1 - 0.4) = 6 experience");
    (void)world.world().progress().set_level(kCharacterA, 1);
    const std::uint32_t one = alpha->progression.experience;
    world::Entity* big = world.wolf(at(1003, 1000), "Test_Wolf", 30);
    (void)world.creatures().hurt(big->id, kHealth, alpha->id);
    check(alpha->progression.experience == one + 20 && alpha->progression.level == 1,
          "a creature 29 levels above a character of level 1: the difference row +20 (the last row) limits the reward level "
          "to 1 + 10 = 11: 20 experience");
    world::Entity* grunt = world.wolf(at(1003, 1000), "Test_Grunt");
    (void)world.world().progress().set_level(kCharacterA, 5);
    const std::uint32_t five = alpha->progression.experience;
    (void)world.creatures().hurt(grunt->id, kHealth, alpha->id);
    check(alpha->progression.experience == five + 48,
          "a creature of the type Solo+ (experience factor 2) of level 20 for a character of level 5: the difference +15 limits "
          "the reward level to 15: 24 * 2 = 48 experience");

    // A level up: the experience goes over the start of the next level.
    (void)world.world().progress().set_level(kCharacterA, 1);
    (void)world.world().progress().add_experience(kCharacterA, 90);
    world::Entity* third = world.wolf(at(1003, 1000));
    (void)world.creatures().hurt(third->id, kHealth, alpha->id);
    check(alpha->progression.level == 2 && alpha->progression.experience == 107,
          "a kill gives the level 2 at its limit (90 + 17 = 107; the level 2 starts at 100)");

    // NoExpNoCoin: no experience and no coin; the loot is rolled.
    world.rewards().set_forced(kCharacterA, true);
    const std::uint32_t quietBefore = alpha->progression.experience;
    const std::uint64_t coinBefore = alpha->wallet.coin;
    world::Entity* quiet = world.wolf(at(1003, 1000), "Test_Quiet");
    (void)world.creatures().hurt(quiet->id, kHealth, alpha->id);
    check(alpha->progression.experience == quietBefore && alpha->wallet.coin == coinBefore && world.rewards().counts().lootGates >= 1,
          "a creature with NoExpNoCoin gives no experience and no coin, and its loot is rolled");

    // The states of the owner: a client sees the experience, the mastery, the level, and the coin.
    LootHost wire;
    Player player = add_player(wire.host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", at(1000, 1000));
    FakeClient client(27100);
    wire.host.enter(client, player.registration());
    wire.host.datagram(client.peer, client.report_at(1, 1000, 1000, kHeight));
    wire.host.steps(60, {&client});
    world::Entity* own = wire.world().find_character(kCharacterA);
    arm_sword(*own);
    (void)wire.world().progress().add_experience(kCharacterA, 90);
    wire.rewards().set_forced(kCharacterA, true);
    wire.host.steps(10, {&client});
    const std::size_t from = client.got.size();
    world::Entity* target = wire.creatures().spawn("Test_Wolf", at(1003, 1000), 0.0F).entity;
    (void)wire.creatures().hurt(target->id, kHealth, own->id);
    wire.host.steps(20, {&client});
    const SlotUpdates progression = slot_updates(client, kProgressionSlot, from);
    const SlotUpdates ranks = slot_updates(client, kCategoricalSlot, from);
    const SlotUpdates coin = slot_updates(client, kCurrencySlot, from);
    bool level2 = false;
    for (const Fragment& fragment : progression.fragments) {
        level2 = level2 || (fragment.fields.count({0, 0}) != 0 && u32_of(fragment.fields.at({0, 0})) == 1
                            && fragment.fields.count({1, 0}) != 0 && u32_of(fragment.fields.at({1, 0})) == 107);
    }
    bool sword = false;
    for (const Fragment& fragment : ranks.fragments) {
        const WireRanks value = ranks_of(fragment);
        for (const WireRank& rank : value.entries) {
            sword = sword || (rank.id == kSword && rank.rank == 1 && rank.points == 7);
        }
    }
    check(level2 && sword && !coin.fragments.empty(),
          "the client of the owner gets the progression state with the level 2 (1 on the wire) and the experience 107, the "
          "rank of the sword mastery with its experience, and the coin");
}

/** LT4: the loot of a kill. */
void loot(const Options& options) {
    LootHost world;
    world::Rewards& rewards = world.rewards();
    const gamedata::LootTables& tables = world.host.loot;
    const gamedata::LootVitals& wolfVitals = *tables.vitals_of("Test_Wolf");
    world::LootContext context;
    context.characterLevel = 5;
    context.creatureLevel = 8;
    context.tags = {"Beast", "Common"};
    // The outer chance: 0.1 for the wolf, with the generator of the world.
    unsigned gates = 0;
    for (int roll = 0; roll < 4000; ++roll) {
        const std::uint64_t before = rewards.counts().lootGates;
        (void)rewards.roll(wolfVitals, context, false);
        gates += rewards.counts().lootGates != before ? 1 : 0;
    }
    note("the outer chance passed " + std::to_string(gates) + " times in 4000 rolls");
    check(gates > 330 && gates < 470, "the outer chance of the wolf (LootDropChance 0.1) passes about one roll of ten");
    const std::uint64_t forcedBefore = rewards.counts().lootGates;
    for (int roll = 0; roll < 100; ++roll) {
        (void)rewards.roll(wolfVitals, context, true);
    }
    check(rewards.counts().lootGates == forcedBefore + 100, "with the loot switch the outer chance passes each time");
    gamedata::LootVitals plain = *tables.vitals_of("Test_Plain");
    check(rewards.roll(plain, context, true).empty(), "a vitals row with CanDropLoot false gives no loot");

    // The master table: the probabilities of the OR table Test_Common (each one 4000 rolls).
    std::map<std::string, unsigned> found;
    unsigned none = 0;
    for (int roll = 0; roll < 4000; ++roll) {
        const std::vector<world::LootDrop> drops = rewards.roll(wolfVitals, context, true);
        none += drops.empty() ? 1 : 0;
        for (const world::LootDrop& drop : drops) {
            ++found[drop.itemId];
        }
    }
    note("4000 rolls: no item " + std::to_string(none) + ", TestSword " + std::to_string(found["TestSword"]) + ", TestArmorT2 "
         + std::to_string(found["TestArmorT2"]) + ", TestHide " + std::to_string(found["TestHide"]) + ", TestTrophy "
         + std::to_string(found["TestTrophy"]) + ", TestSwordT3 " + std::to_string(found["TestSwordT3"]) + ", TestOre "
         + std::to_string(found["TestOre"]) + ", TestNamed " + std::to_string(found["TestNamed"]));
    // OR Test_Common: 1..34999 nothing (35 %); 35000..99499 the gear or the resources (a tie, 32.25 % each); 99500.. the trophy
    // (0.5 %). The gear: 1..39999 nothing (40 % of 32.25 %), 40000..64999 the weapons bucket (the sword at the level 5:
    // 8.1 %), 65000.. the armor (11.3 %). No item: 35 % + 12.9 % = 47.9 %.
    check(none > 1750 && none < 2080 && found["TestHide"] > 1150 && found["TestHide"] < 1430 && found["TestSword"] > 250
              && found["TestSword"] < 400 && found["TestArmorT2"] > 360 && found["TestArmorT2"] < 540 && found["TestTrophy"] < 45
              && found["TestSwordT3"] == 0 && found["TestOre"] == 0 && found["TestNamed"] == 0,
          "the rolls of the master table: an AND table visits each entry that its roll reaches; an OR table takes the entry of "
          "the highest reached threshold, one of two equal thresholds at random; a bucket takes only the candidates whose "
          "tags fit (the sword of MinContLevel 0 to 18, the hide of Beast, not the ore of Lost or Corrupted, not the trophy of "
          "Beast and Elite); the rare bucket of 999900 needs the tag Named");
    // The roll of a table whose condition overrides the roll: the roll is the level of the character.
    const auto rolled = [&](std::uint32_t index, std::uint32_t level) {
        gamedata::LootVitals vitals = wolfVitals;
        vitals.table = index;
        world::LootContext own = context;
        own.characterLevel = level;
        return rewards.roll(vitals, own, true);
    };
    const std::vector<world::LootDrop> fixed1 = rolled(kFixed, 1);
    check(fixed1.size() == 1 && count_of(fixed1, "TestHide") == 2, "a numeric condition (Level 1) is the roll: the threshold 0, a quantity 2-2");
    unsigned ore = 0;
    unsigned trophy = 0;
    for (int roll = 0; roll < 400; ++roll) {
        const std::vector<world::LootDrop> drops = rolled(kFixed, 6);
        ore += count_of(drops, "TestOre");
        trophy += count_of(drops, "TestTrophy");
    }
    check(ore + trophy == 400 && ore > 140 && trophy > 140, "the roll 6 reaches two entries of the threshold 5: one of them at random");
    const std::vector<world::LootDrop> fixed60 = rolled(kFixed, 60);
    check(fixed60.size() == 1 && fixed60[0].itemId == "TestSword" && fixed60[0].gearScore && *fixed60[0].gearScore >= 210
              && *fixed60[0].gearScore <= 220,
          "the roll 60 reaches the threshold 50: the sword with the gear score range of its entry (210 to 220)");
    const std::vector<world::LootDrop> everything = rolled(kAll, 5);
    check(everything.size() == 2 && count_of(everything, "TestHide") == 1 && count_of(everything, "TestOre") == 3,
          "an AND table gives each entry that its roll reaches (the threshold 0) and not an entry above MaxRoll");
    check(rolled(kTags, 5).empty(), "a table whose tag conditions (Elite, Named) are not tags of the roll gives nothing");
    check(rolled(kLimit, 5).empty(), "a loot limit condition ([LIM]) is not met: the backend has no loot limits");
    const std::uint64_t guards = rewards.counts().guards;
    const std::vector<world::LootDrop> cycle = rolled(kCycle, 5);
    check(count_of(cycle, "TestHide") == 1 && rewards.counts().guards == guards + 1,
          "a table that names itself is not visited a second time in its path (the guard of the client): one item, no loop");
    const std::vector<world::LootDrop> selections = rolled(kBuckets, 5);
    check(count_of(selections, "TestSword") == 3 && count_of(selections, "TestArmorT2") == 0 && count_of(selections, "TestHide") >= 50
              && count_of(selections, "TestHide") <= 150,
          "the quantity of a bucket entry is the number of selections (3 swords; the armor has the odds 0), and a candidate "
          "has its quantity range (50 hides of 1 to 3)");
    world::LootContext high = context;
    high.characterLevel = 25;
    high.creatureLevel = 30;
    gamedata::LootVitals bucketVitals = wolfVitals;
    bucketVitals.table = kBuckets;
    const std::vector<world::LootDrop> level25 = rewards.roll(bucketVitals, high, true);
    check(count_of(level25, "TestSwordT3") == 3 && count_of(level25, "TestSword") == 0,
          "MinContLevel is the lower one of the two levels (25): the candidate of 19 to 38");
    gamedata::LootVitals gearVitals = wolfVitals;
    gearVitals.table = kGearScore;
    const std::vector<world::LootDrop> gear = rewards.roll(gearVitals, context, true);
    const auto score = [&](const char* id) {
        for (const world::LootDrop& drop : gear) {
            if (drop.itemId == id && drop.gearScore) {
                return *drop.gearScore;
            }
        }
        return 0U;
    };
    // Tier 3 at the level 5: 100 + 20 + 5 = 125 to 145, not above the ceiling of the creature level 8 (158); GSBonus 5.
    check(score("TestSwordT3") >= 130 && score("TestSwordT3") <= 150 && score("TestNamed") == 605 && score("TestSword") == 300,
          "the gear score: UseLevelGS gives the range of the tier of the item at the lower level (125 to 145) plus GSBonus 5, "
          "not limited by the smallest gear score of the item table (300); an override of the item (600) plus the bonus; the "
          "range of an entry (900 to 950) is not above the largest gear score of the item (300)");

    // The delivery: a kill with the loot switch gives the items and the coin in one transaction, with the loot notice cause.
    LootHost wire;
    Player player = add_player(wire.host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", at(1000, 1000));
    FakeClient client(27101);
    wire.host.enter(client, player.registration());
    wire.host.datagram(client.peer, client.report_at(1, 1000, 1000, kHeight));
    wire.host.steps(60, {&client});
    world::Entity* own = wire.world().find_character(kCharacterA);
    (void)wire.world().inventory().give(kCharacterA, "TestHide", 1);
    wire.rewards().set_forced(kCharacterA, true);
    wire.host.steps(10, {&client});
    // Kills until one gives items (the OR table of the common loot gives no item for a roll below 35000).
    std::size_t from = 0;
    for (int attempt = 0; attempt < 40 && wire.rewards().counts().items == 0; ++attempt) {
        from = client.got.size();
        world::Entity* target = wire.creatures().spawn("Test_Wolf", at(1003, 1000), 0.0F).entity;
        (void)wire.creatures().hurt(target->id, kHealth, own->id);
        wire.host.steps(10, {&client});
    }
    const bool given = wire.rewards().counts().items != 0;
    const SlotUpdates bag = slot_updates(client, kBagSlot, from);
    bool notice = !bag.fragments.empty();
    unsigned elements = 0;
    for (const Fragment& fragment : bag.fragments) {
        const WireContainer value = read_container(fragment.fields.at({0, 0}));
        notice = notice && value.ok && !value.full;
        for (const auto& [index, wireItem] : value.elements) {
            ++elements;
            notice = notice && wireItem.action == world::sync::kItemGathered && !wireItem.empty();
        }
    }
    const SlotUpdates coins = slot_updates(client, kCurrencySlot, from);
    std::uint64_t rows = 0;
    for (const state::ItemRow& row : wire.host.store.list_items(kCharacterA)) {
        rows += row.container == state::kContainerBag ? 1 : 0;
    }
    std::uint32_t stacks = 0;
    for (const world::BagSlot& slot : own->bag.slots) {
        stacks += slot.item ? 1 : 0;
    }
    check(given && notice && elements >= 1 && !coins.fragments.empty() && own->wallet.coin >= 380
              && wire.host.store.currency(kCharacterA, state::kCurrencyCoin) == own->wallet.coin && rows == stacks,
          "a kill with loot: the bag of the owner gets each new or changed slot in a journal with the cause ItemGathered (1, the "
          "loot notice of the HUD), the coin state has the coin, and the rows of the items and the coin row are those of the "
          "entity");

    // A full bag: an item that the bag has no slot for is not given; the coin and the other values are given.
    LootHost full;
    world::Entity* filled = full.character(kCharacterA, "Alpha", at(1000, 1000), 1);
    (void)full.world().inventory().give(kCharacterA, "TestTrophy", world::kMaxContainerSlots);
    full.rewards().set_forced(kCharacterA, true);
    std::uint32_t lost = 0;
    std::uint64_t coinGiven = 0;
    for (int attempt = 0; attempt < 40 && lost == 0; ++attempt) {
        world::Entity* target = full.creatures().spawn("Test_Wolf", at(1003, 1000), 0.0F).entity;
        const std::uint64_t coinBefore = filled->wallet.coin;
        (void)full.creatures().hurt(target->id, kHealth, filled->id);
        coinGiven = filled->wallet.coin - coinBefore;
        lost = static_cast<std::uint32_t>(full.rewards().counts().itemsLost);
    }
    check(lost >= 1 && coinGiven == 380 && filled->bag.slots.size() == world::kMaxContainerSlots
              && count_of(filled->bag, "TestTrophy") == world::kMaxContainerSlots,
          "a full bag (500 slots): the items of the kill that have no slot are not given (the world has no item on the "
          "ground), and the coin is given");

    // One transaction: a database that refuses the coin row changes no item row and no entity.
    const std::filesystem::path path = options.workDirectory / "loot-transaction.sqlite3";
    std::filesystem::remove(path);
    state::Store store;
    const bool opened = store.open(path.string());
    (void)add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    ManualClock clock;
    gamedata::AppearanceTables appearance;
    gamedata::ItemCatalog catalog;
    catalog.set(loot_items());
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &catalog;
    world::World alone(clock, store, appearance, worldOptions);
    (void)alone.spawn_character(kCharacterA, at(1000, 1000));
    sqlite3* connection = nullptr;
    bool trigger = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK
                   && sqlite3_exec(connection,
                                   "CREATE TRIGGER refuse_coin BEFORE INSERT ON currencies BEGIN SELECT RAISE(FAIL, 'test write "
                                   "failure'); END",
                                   nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    const world::ItemResult refused =
        alone.inventory().grant(kCharacterA, {world::GrantItem{"TestSword", 1, 210}, world::GrantItem{"TestHide", 3, std::nullopt}}, 380);
    const world::Entity* entity = alone.find_character(kCharacterA);
    check(opened && trigger && refused.error == world::ItemError::storeFailed && store.list_items(kCharacterA).empty()
              && entity->bag.slots.empty() && entity->wallet.coin == 0,
          "the items and the coin of a kill are one transaction: when the database refuses the coin row, no item row is "
          "written, and the entity has no item and no coin");
}

/** LT5: the respawn of a creature of a spawn point. */
void respawn(const Options&) {
    LootHost world;
    world::Entity* alpha = world.character(kCharacterA, "Alpha", at(1005, 1000), 1);
    world.pass(1.0F);
    world::Creatures& creatures = world.creatures();
    world::Entity* wolf = creatures.find(creatures.spawn_points()[0].entity);
    check(wolf != nullptr && !wolf->creature.dead, "setup: the spawn point p-wolf (respawn values 360 and 0) has its creature");
    if (wolf == nullptr) {
        return;
    }
    const world::EntityId first = wolf->id;
    (void)creatures.hurt(first, kHealth, alpha->id);
    const world::SpawnPointState& point = creatures.spawn_points()[0];
    check(point.waiting && point.respawnAt && *point.respawnAt == wolf->creature.diedAt + std::chrono::seconds(360),
          "the death gives the spawn point its respawn time: the smallest value 360 s (a largest value of 0 is no value)");
    world.pass(359.0F);
    check(creatures.find(first) != nullptr && creatures.find(first)->creature.dead && point.entity == first,
          "before the respawn time the body stays and the point makes no creature");
    world.pass(1.5F);
    world::Entity* next = creatures.find(point.entity);
    check(creatures.find(first) == nullptr && next != nullptr && next->id != first && !next->creature.dead
              && next->vitals.health == next->vitals.healthMax && !point.waiting && creatures.counts().removedByRespawn == 1,
          "at the respawn time, with a player near the point, the body leaves the world and the point makes a new creature "
          "with a new id and its full health");

    // No player near at the respawn time: the body leaves by the distance rule, and the point waits for a player.
    (void)creatures.hurt(next->id, kHealth, alpha->id);
    const world::EntityId second = next->id;
    (void)world.world().set_position(alpha->id, at(1600, 1000), false);
    world.pass(400.0F);
    check(creatures.find(second) == nullptr && point.entity == 0 && point.waiting,
          "with no player near, the body leaves the world (the distance rule) and the point waits after its respawn time");
    (void)world.world().set_position(alpha->id, at(1005, 1000), false);
    world.pass(1.0F);
    check(point.entity != 0 && creatures.find(point.entity) != nullptr && !creatures.find(point.entity)->creature.dead,
          "when a player comes near after the respawn time, the point makes its creature");

    // The respawn values of the spawners: a range, two values of 0, and a largest value below the smallest.
    const auto time = [](const char* id, float low, float high, float unit) {
        const std::chrono::milliseconds value = world::respawn_time(loot_point(id, 0, 0, low, high), unit);
        note(std::string("respawn time of ") + id + ": " + std::to_string(value.count()) + " ms");
        return value;
    };
    check(time("a", 15, 360, 0.0F) == std::chrono::seconds(15) && time("a", 15, 360, 1.0F) == std::chrono::seconds(360)
              && time("a", 15, 360, 0.5F) == std::chrono::milliseconds(187500) && time("b", 0, 0, 0.7F) == world::kShortestRespawn
              && time("c", 360, 15, 0.9F) == std::chrono::seconds(360),
          "the respawn time: a time of the range with the generator (15 to 360), 30 s for two values of 0, and the smallest "
          "value when the largest value is below it");

    // The command reset still makes a point free at once; a removal by a command waits for the reset.
    world::Entity* third = creatures.find(point.entity);
    (void)creatures.hurt(third->id, kHealth, alpha->id);
    const std::size_t reset = creatures.reset_spawn_points();
    world.pass(1.0F);
    check(reset >= 1 && point.entity != 0 && creatures.find(point.entity) != nullptr && !creatures.find(point.entity)->creature.dead
              && !point.waiting,
          "the command reset makes a point that waits free at once: a new creature");
    (void)creatures.remove(point.entity);
    world.pass(1000.0F);
    check(point.waiting && !point.respawnAt && point.entity == 0,
          "a creature that a command removed has no respawn time: its point waits for the command reset");
}

/** @return The records of a client for one interest number from the message `from` on. */
std::vector<Record> records_of(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<Record> records;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest == interest) {
                records.push_back(record);
            }
        }
    }
    return records;
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** LT6: two sessions near one creature. */
void two_sessions(const Options&) {
    LootHost world(true, {loot_point("p-wolf", 1010.0F, 1000.0F, 360.0F, 0.0F)});
    ServerHost& host = world.host;
    Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", at(1005, 1000));
    Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", at(1005, 1002));
    FakeClient a(27110);
    FakeClient b(27111);
    host.enter(a, alpha.registration());
    host.datagram(a.peer, a.report_at(1, 1005, 1000, kHeight));
    host.enter(b, beta.registration());
    host.datagram(b.peer, b.report_at(1, 1005, 1002, kHeight));
    host.steps(100, {&a, &b});
    world::Entity* first = world.creatures().find(world.creatures().spawn_points()[0].entity);
    world::Entity* ownA = world.world().find_character(kCharacterA);
    world::Entity* ownB = world.world().find_character(kCharacterB);
    check(first != nullptr && interest_of(host, 1, first->id) != 0 && interest_of(host, 2, first->id) != 0,
          "setup: the two sessions observe the creature of the spawn point");
    if (first == nullptr) {
        return;
    }
    arm_sword(*ownA);
    world.rewards().set_forced(kCharacterA, true);
    world.rewards().set_forced(kCharacterB, true);
    const world::EntityId firstId = first->id;
    const std::uint32_t firstA = interest_of(host, 1, firstId);
    const std::uint32_t firstB = interest_of(host, 2, firstId);
    const std::uint32_t experienceB = ownB->progression.experience;
    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    // Alpha hits the creature (the hit of a claim, world/combat.h), and its last hit kills it.
    (void)world.creatures().hurt(firstId, 400.0F, ownA->id);
    (void)world.creatures().hurt(firstId, kHealth, ownA->id);
    host.steps(30, {&a, &b});
    check(ownA->progression.experience > 0 && ownA->wallet.coin == 380 && ownA->skills.rank(kSword) >= 1
              && ownB->progression.experience == experienceB && ownB->wallet.coin == 0 && ownB->bag.slots.empty(),
          "the rewards go to the character of the credit rule only: Alpha gets the experience, the mastery, and the coin; Beta, "
          "near the creature, gets nothing");
    const bool ownStatesA = !slot_updates(a, kProgressionSlot, fromA).fragments.empty() && !slot_updates(a, kCurrencySlot, fromA).fragments.empty();
    const bool ownStatesB = slot_updates(b, kProgressionSlot, fromB).fragments.empty() && slot_updates(b, kCurrencySlot, fromB).fragments.empty()
                            && slot_updates(b, kBagSlot, fromB).fragments.empty();
    check(ownStatesA && ownStatesB, "the session of Alpha gets its progression and coin states; the session of Beta gets no state of its own character");
    bool deadA = false;
    bool deadB = false;
    for (const Record& record : records_of(a, firstA, fromA)) {
        deadA = deadA || fragment_of(record, kSlots[1]) != nullptr;
    }
    for (const Record& record : records_of(b, firstB, fromB)) {
        deadB = deadB || fragment_of(record, kSlots[1]) != nullptr;
    }
    check(first->creature.dead && deadA && deadB, "the two observers get the vitals state of the death of the creature");
    // The respawn time: the body leaves, and the two sessions get the new creature. The clients
    // read and acknowledge one time each second.
    for (int second = 0; second < 362; ++second) {
        host.advance(std::chrono::milliseconds(990));
        host.steps(1, {&a, &b});
    }
    world::Entity* next = world.creatures().find(world.creatures().spawn_points()[0].entity);
    const std::uint32_t nextA = next != nullptr ? interest_of(host, 1, next->id) : 0;
    const std::uint32_t nextB = next != nullptr ? interest_of(host, 2, next->id) : 0;
    bool metadataA = false;
    bool metadataB = false;
    for (const Record& record : records_of(a, nextA)) {
        metadataA = metadataA || !record.fragments.empty();
    }
    for (const Record& record : records_of(b, nextB)) {
        metadataB = metadataB || !record.fragments.empty();
    }
    check(next != nullptr && next->id != firstId && !next->creature.dead && interest_of(host, 1, firstId) == 0
              && interest_of(host, 2, firstId) == 0 && nextA != 0 && nextB != 0 && metadataA && metadataB,
          "after the respawn time the body leaves the interest tables of the two sessions, and each one gets the record of the "
          "new creature");
}

/** LT7: the load of the kills and of the respawn timers. */
void load(const Options&) {
    // A made-up world of the size of the game world: 33,856 spawn points, 78 m between two of them.
    std::vector<gamedata::SpawnPoint> points;
    constexpr float kEdge = 14274.0F;
    constexpr float kStep = 78.0F;
    std::uint32_t index = 0;
    for (float x = 0.0F; x <= kEdge; x += kStep) {
        for (float y = 0.0F; y <= kEdge; y += kStep) {
            gamedata::SpawnPoint point = loot_point("p", x, y, 360.0F, 0.0F);
            point.id = "p" + std::to_string(index++);
            points.push_back(point);
        }
    }
    LootHost world(true, points);
    // A larger loot data: the master table with 19 entries of 40 OR tables with buckets of 75 candidates.
    gamedata::LootData data = loot_data();
    std::vector<std::string> extras;
    for (unsigned extra = 0; extra < 40; ++extra) {
        const std::string name = "Test_Load_" + std::to_string(extra);
        gamedata::LootTable child = table("", false, gamedata::RollBonus::clampMax, 100000, {"Common"}, {});
        child.id = name;
        gamedata::LootBucket bucket;
        bucket.id = name + "_Bucket";
        for (unsigned item = 0; item < 75; ++item) {
            bucket.candidates.push_back(candidate(item % 2 == 0 ? "TestHide" : "TestOre", 1, 2,
                                                  {tag("MinContLevel", item % 4 * 10, item % 4 * 10 + 18), tag("Beast")}, true));
        }
        data.buckets.push_back(bucket);
        child.entries = {entry(gamedata::LootEntryKind::bucket, bucket.id.c_str(), 50000), entry(gamedata::LootEntryKind::item, "TestTrophy", 99000)};
        child.entries[0].id = bucket.id;
        data.tables.push_back(child);
        extras.push_back(name);
    }
    // The master table gets 17 of them: 19 entries, as CreatureLootMaster of the game.
    for (const std::string& name : extras) {
        gamedata::LootTable& master = data.tables[kMaster];
        if (master.entries.size() < 19) {
            master.entries.push_back(entry(gamedata::LootEntryKind::table, "", 0));
            master.entries.back().id = name;
        }
    }
    check(world.host.loot.set(data), "setup: loot tables with " + std::to_string(data.tables.size()) + " tables and "
                                         + std::to_string(data.buckets.size()) + " buckets");
    // The roll alone: 10,000 rolls of the master table.
    world::LootContext context;
    context.characterLevel = 30;
    context.creatureLevel = 8;
    context.tags = {"Beast", "Common"};
    const gamedata::LootVitals& vitals = *world.host.loot.vitals_of("Test_Wolf");
    const auto rollStarted = std::chrono::steady_clock::now();
    std::size_t drops = 0;
    for (int roll = 0; roll < 10000; ++roll) {
        drops += world.rewards().roll(vitals, context, true).size();
    }
    const double rollTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rollStarted).count();
    note("10,000 rolls of a master table with 19 entries: " + std::to_string(rollTime) + " ms, " + std::to_string(drops) + " items");
    check(drops > 10000 && rollTime < 2000.0, "the time limit: 10,000 rolls need less than 2 s");

    // 50 players, each one near the spawn points of a different place.
    const unsigned kPlayers = 50;
    std::vector<std::string> ids;
    for (unsigned player = 0; player < kPlayers; ++player) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x35000000U + player, player);
        add_player(world.host.store, "765612022554" + std::to_string(10000 + player), "Tester " + std::to_string(player), id,
                   "Loader" + std::to_string(player), std::nullopt);
        ids.emplace_back(id);
        (void)world.world().spawn_character(id, at(700.0F + 271.0F * static_cast<float>(player),
                                                   500.0F + 263.0F * static_cast<float>((player * 7) % kPlayers)));
        world.rewards().set_forced(id, true);
    }
    world.pass(0.5F);
    world::Creatures& creatures = world.creatures();
    const std::size_t alive = creatures.count();
    const std::size_t scanPoints = creatures.counts().lastScanPoints;
    // Each player kills the creatures near it (1,000 kills at most).
    std::vector<std::pair<world::EntityId, world::EntityId>> kills;
    for (const world::EntityId id : creatures.ids()) {
        const world::Entity* creature = creatures.find(id);
        world::Entity* nearest = nullptr;
        for (const std::string& character : ids) {
            world::Entity* entity = world.world().find_character(character);
            if (nearest == nullptr
                || world::horizontal_distance(entity->position, creature->position)
                       < world::horizontal_distance(nearest->position, creature->position)) {
                nearest = entity;
            }
        }
        kills.emplace_back(id, nearest->id);
        if (kills.size() == 1000) {
            break;
        }
    }
    const auto killStarted = std::chrono::steady_clock::now();
    for (const auto& [creature, character] : kills) {
        (void)creatures.hurt(creature, kHealth, character);
    }
    const double killTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - killStarted).count();
    note(std::to_string(kills.size()) + " kills with their rewards (rolls, experience, mastery, items, coin, and the rows of "
         + "the database): " + std::to_string(killTime) + " ms; " + std::to_string(world.rewards().counts().items) + " items given");
    check(kills.size() >= 500 && world.rewards().counts().credited == kills.size() && killTime < 1000.0,
          "the time limit: each kill of a creature near 50 players gives its rewards, in less than 1 s for all of them");
    // The scans with the points that wait: a scan looks at the points near the players, not at the points that wait.
    const auto scanStarted = std::chrono::steady_clock::now();
    world.pass(100.0F);
    const double scanTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scanStarted).count();
    note("200 scans with " + std::to_string(creatures.waiting_count()) + " points that wait: " + std::to_string(scanTime)
         + " ms; the last scan looked at " + std::to_string(creatures.counts().lastScanPoints) + " points (before the kills "
         + std::to_string(scanPoints) + ")");
    check(creatures.waiting_count() == kills.size() && creatures.counts().lastScanPoints == scanPoints
              && creatures.counts().lastScanPoints * 10 < points.size() && scanTime < 400.0,
          "the respawn timers cost nothing in a scan: the scan looks at the same spawn points near the players as before the "
          "kills (less than one tenth of the points of the world), 200 scans in less than 400 ms");
    world.pass(270.0F);
    check(creatures.waiting_count() == 0 && creatures.count() == alive && creatures.counts().removedByRespawn == kills.size(),
          "after the respawn time each point makes its creature again: the bodies leave the world, and the world has the same "
          "number of creatures as before the kills");
}

/** LT8: the check of the loot tables tool, tools/gamedata/check_loot_tool.py. */
void loot_tool(const Options& options) {
    // The golden folder of the options is REPOSITORY/apps/backend/tests/golden.
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_loot_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the loot tool is in the tree (the case needs --golden-dir)");
    if (!std::filesystem::is_regular_file(script)) {
        return;
    }
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "loot-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "loot-tool").string()
                                + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    // Code review of stage QG8, findings QG8R-R1 and QG8R-R2: the cases event-child and shared-event-root (19 cases).
    check(status == 0 && correct == 19 && wrong == 0,
          "the loot tool writes the document of complete tables (the entries in the sequence of their thresholds, the "
          "references resolved, the tag ranges, the odds, the empty cells), and it stops with status 1 and keeps the file that "
          "was there for a table, a bucket, or a vitals table reference that does not resolve, a quantity text, reward levels "
          "with a gap, a number that is not finite, and (code review of stage 3.5a, F3 and F4) a variant, an event, or a "
          "creature type that does not resolve, a negative reward or factor, and the flag text NONE, and (code review of stage "
          "3.3a, F13) a level difference or the odds of a bucket outside the range of the backend, and (stage 3.5b.1, format 2) the "
          "chest roots, the container floor, the loot limits, and a chest root that does not resolve, and (code review of stage QG8) "
          "a child table that is also an event root (status 1), and a root that a container and an event share (left out of both)");
}

} // namespace

std::vector<Case> loot_cases() {
    return {{"LT1", "loot: the loot tables file, a file whose parts do not fit together, no file, and the weapon mastery of the "
                    "combat tables", loot_file},
            {"LT2", "loot: the credit of a kill (the damage part, the time, dead, ignored, far, not in the world, a command, two "
                    "characters, the reset)", credit},
            {"LT3", "loot: the experience and the weapon mastery of a kill, a level up, and the states of the owner", experience},
            {"LT4", "loot: the outer chance, the roll rules, the gear score, the delivery with the loot notice cause, a full bag, "
                    "and one transaction", loot},
            {"LT5", "loot: the respawn time of a spawn point, with and with no player near, the reset, a removal by a command",
             respawn},
            {"LT6", "loot: two sessions near one creature, one kill, the rewards for one character, the death and the new creature "
                    "for the two observers", two_sessions},
            {"LT7", "loot: the load of the kills of the creatures near 50 players (591, with rolls) and of their respawn timers "
                    "in a world of 33,856 spawn points, with a time limit", load},
            {"LT8", "loot: the loot tables tool with made-up tables (runs tools/gamedata/check_loot_tool.py)", loot_tool}};
}

} // namespace soulwarden::backend::tests

#pragma once

// The world of the chest cases (stage 3.5b.1, tests/chest_tests.cpp; docs/systems/chests.md): made-up chests of the form of the files of
// tools/gamedata/chests.py and interactions.py, made-up loot tables with chest roots, two characters near the chests, and the readers
// of the Gathering state (81) and of the LootTracker state (982). All keys, ids, positions, items, and tables are made up.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/chest_data.h"
#include "gamedata/loot_tables.h"
#include "server/rep/lock_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/interaction_fixture.h"

namespace soulwarden::backend::tests::chests {

using namespace interaction;

inline constexpr std::uint32_t kGatherSlot = 36;
inline constexpr std::uint32_t kGatherType = 81;
inline constexpr std::uint32_t kTrackerSlot = 33;
inline constexpr std::uint32_t kTrackerType = 982;
inline constexpr std::uint32_t kGatherOption = 0;

/** The chests of the cases (the tag of their object). */
enum ChestTag : std::uint8_t { kSupply = 1, kElite, kLocked, kGated, kProvision, kNoLoot, kFarChest };
inline constexpr state::WorldPosition kSupplyPlace{100.0F, 203.0F, 30.0F};
inline constexpr state::WorldPosition kElitePlace{97.0F, 200.0F, 30.0F};
inline constexpr state::WorldPosition kLockedPlace{103.0F, 199.0F, 30.0F};
inline constexpr state::WorldPosition kGatedPlace{99.0F, 197.0F, 30.0F};
inline constexpr state::WorldPosition kProvisionPlace{102.0F, 202.0F, 30.0F};
inline constexpr state::WorldPosition kNoLootPlace{98.0F, 203.0F, 30.0F};
inline constexpr state::WorldPosition kFarPlace{100.0F, 600.0F, 30.0F};
/** The container floor of the gear score of each level of the made-up tables. */
inline constexpr std::uint32_t kFloor = 250;

/** @return The interaction object of a chest: GatherOption (the option row 0), the slice of a chest, the range 10 m, the sight mark. */
inline gamedata::InteractionObject chest_object(std::uint8_t tag, state::WorldPosition place, std::int32_t achievement = -1) {
    gamedata::InteractionObject object = object_of(tag, gamedata::InteractionKind::gathering, place, {kGatherOption});
    object.slice = "slices/master_lootcontainer.dynamicslice";
    object.bounds[0].rangeSq = 100.0F;
    object.bounds[0].lineOfSight = true;
    object.bounds[0].requiredAchievement = achievement;
    return object;
}

inline std::vector<gamedata::InteractionObject> chest_objects() {
    return {chest_object(kSupply, kSupplyPlace),     chest_object(kElite, kElitePlace),       chest_object(kLocked, kLockedPlace),
            chest_object(kGated, kGatedPlace, 0),     chest_object(kProvision, kProvisionPlace), chest_object(kNoLoot, kNoLootPlace),
            chest_object(kFarChest, kFarPlace)};
}

inline bool set_chest_interactions(gamedata::InteractionData& data) {
    return data.set({option_row("GatherOption", 14, true)}, {{"Lore_unlock_Test_Page_01", kPageBit}}, chest_objects());
}

inline gamedata::ChestDefinition chest_of(const gamedata::InteractionObject& object, gamedata::ChestKind kind, std::int32_t gatherable,
                                          const char* root, std::vector<std::string> tags, std::optional<std::uint32_t> level,
                                          const char* reason = "") {
    gamedata::ChestDefinition chest;
    chest.key = object.key;
    chest.actor = object.actor;
    chest.position = object.position;
    chest.variant = "Test_Variant_" + std::to_string(object.actor[0]);
    chest.name = "@Test_Chest";
    chest.gatherable = gatherable;
    chest.root = root;
    chest.kind = kind;
    chest.tags = std::move(tags);
    chest.contentLevel = level;
    chest.reason = reason;
    chest.available = chest.reason.empty();
    chest.region = "r_+00_+00";
    return chest;
}

/** The gatherable rows: 0 a supply row (2.5 s, no reset of its own), 1 an elite row (5 s, 82,800 s: the values of the game data). */
inline std::vector<gamedata::ChestGatherable> chest_gatherables() {
    return {{"LootMediumT1", 2.5F, 0.0F, 0.0F, false, ""}, {"LootLargeT3ElitePOI", 5.0F, 82800.0F, 82800.0F, true, "Gather_Test_Elite"}};
}

/**
 * @return The chests of the cases, and `far` more ordinary chests far from the characters (more than 2 km; case LC8).
 * @param farOffset The place of the far chests: from x = 5000 + farOffset on.
 */
inline std::vector<gamedata::ChestDefinition> chest_definitions(std::size_t far = 0) {
    const std::vector<gamedata::InteractionObject> objects = chest_objects();
    std::vector<gamedata::ChestDefinition> chests{
        chest_of(objects[0], gamedata::ChestKind::supply, 0, "Test_ChestRoot", {"Everfall"}, 12U),
        chest_of(objects[1], gamedata::ChestKind::elite, 1, "Test_EliteRoot", {"Everfall"}, 20U),
        chest_of(objects[2], gamedata::ChestKind::elite, 1, "Test_EliteRoot", {"Everfall"}, 20U, "a lock (a test)"),
        chest_of(objects[3], gamedata::ChestKind::supply, 0, "Test_ChestRoot", {"Everfall"}, 12U),
        chest_of(objects[4], gamedata::ChestKind::provision, 0, "Test_ChestRoot", {"Brimstone"}, 25U),
        chest_of(objects[5], gamedata::ChestKind::supply, 0, "Test_NoSuchRoot", {}, std::nullopt),
        chest_of(objects[6], gamedata::ChestKind::supply, 0, "Test_ChestRoot", {"Everfall"}, 12U),
    };
    for (std::size_t index = 0; index < far; ++index) {
        gamedata::ChestDefinition chest = chests[0];
        char key[37];
        std::snprintf(key, sizeof(key), "fa000000-0000-4000-8000-%012zx", index);
        chest.key = key;
        chest.actor = {0x0B, static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index >> 8), static_cast<std::uint8_t>(index >> 16),
                       'F', 'A', 'R', 'C', 0, 0, 0, 0, 0, 0, 0, 0};
        chest.position = {5000.0F + static_cast<float>(index % 400) * 25.0F, 5000.0F + static_cast<float>(index / 400) * 25.0F, 30.0F};
        chests.push_back(std::move(chest));
    }
    return chests;
}

inline bool set_chest_data(gamedata::ChestData& data, std::size_t far = 0) {
    return data.set(gamedata::ChestRules{60, 60}, chest_gatherables(), chest_definitions(far));
}

inline gamedata::ItemDefinition chest_item(const char* id, const char* type, const char* itemClass, std::uint32_t tier, std::uint32_t stack,
                                           std::uint32_t low = 0, std::uint32_t high = 0) {
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
    return definition;
}

inline std::vector<gamedata::ItemDefinition> chest_items() {
    return {chest_item("TestOre", "Resource", "Resource", 1, 1000), chest_item("TestHide", "Resource", "Resource", 1, 1000),
            chest_item("TestSword", "Weapon", "EquippableMainHand+Sword", 2, 1, 200, 300),
            chest_item("TestSwordT3", "Weapon", "EquippableMainHand+Sword", 3, 1, 300, 400),
            chest_item("TestArmorT2", "Armor", "EquippableChest+Armor", 2, 1, 200, 300)};
}

inline gamedata::LootEntry loot_entry(gamedata::LootEntryKind kind, const char* id, std::uint32_t threshold, std::uint32_t low = 1,
                                      std::uint32_t high = 1) {
    gamedata::LootEntry value;
    value.kind = kind;
    value.id = id;
    value.threshold = threshold;
    value.quantityMin = low;
    value.quantityMax = high;
    return value;
}

inline gamedata::LootTable loot_table(const char* id, bool all, std::uint32_t maxRoll, std::vector<std::string> conditions,
                                      std::vector<gamedata::LootEntry> entries, bool levelGearScore = false) {
    gamedata::LootTable value;
    value.id = id;
    value.all = all;
    value.bonus = gamedata::RollBonus::addToRoll;
    value.maxRoll = maxRoll;
    value.conditions = std::move(conditions);
    value.entries = std::move(entries);
    value.useLevelGearScore = levelGearScore;
    return value;
}

/**
 * The loot tables of the chests: Test_ChestRoot (AND): 3 TestOre; Test_ChestTagged (the tag condition Everfall: 2 TestHide); Test_ChestGear
 * (UseLevelGS, the roll is 1 + MinPOIContLevel: TestSword at 0, TestSwordT3 at 20: only a content level of 19 or more reaches it).
 * Test_EliteRoot (AND): TestArmorT2 and 5 TestOre. The gear score ranges of a level L: tier T from 100 + 10 T + L to 120 + 10 T + L; the
 * creature ceiling 150 + L; the container floor kFloor.
 */
inline gamedata::LootData chest_loot() {
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
        data.containerFloor[level] = kFloor;
    }
    data.modifiers = {{"Solo", {1.0F, 1.0F, 0.0F, 1.0F, std::nullopt}}};
    data.events = {{"killevent", 0.05F}};
    data.credit.timeLimit = 120.0F;
    data.credit.healthShare = 0.01F;
    data.credit.range = 200.0F;
    data.credit.receivers = 1;
    data.credit.modifiers = {{1.0F, 1.0F, 1.0F}};
    data.tables = {
        loot_table("Test_ChestRoot", true, 100, {},
                   {loot_entry(LootEntryKind::item, "TestOre", 0, 3, 3), loot_entry(LootEntryKind::table, "Test_ChestTagged", 0),
                    loot_entry(LootEntryKind::table, "Test_ChestGear", 0)}),
        loot_table("Test_ChestTagged", false, 100, {"Everfall"}, {loot_entry(LootEntryKind::item, "TestHide", 0, 2, 2)}),
        loot_table("Test_ChestGear", false, 0, {"MinPOIContLevel"},
                   {loot_entry(LootEntryKind::item, "TestSword", 0), loot_entry(LootEntryKind::item, "TestSwordT3", 20)}, true),
        loot_table("Test_EliteRoot", true, 100, {"GlobalMod"},
                   {loot_entry(LootEntryKind::item, "TestArmorT2", 0), loot_entry(LootEntryKind::item, "TestOre", 0, 5, 5)}, true),
    };
    data.chestRoots = {"Test_ChestRoot", "Test_EliteRoot"};
    return data;
}

/** The two characters (A and B, level 30) near the chests, with their clients. */
struct ChestShop : Shop {
    explicit ChestShop(const std::string& database = ":memory:", bool existing = false, bool chestData = true, std::uint32_t level = 30)
        : Shop(false, database, true, existing) {
        if (!set_chest_interactions(host.interactions)) {
            throw std::runtime_error("chest test: the interaction data was not set: " + host.interactions.error());
        }
        host.catalog.set(chest_items());
        if (!host.loot.set(chest_loot())) {
            throw std::runtime_error("chest test: the loot tables were not set: " + host.loot.error());
        }
        if (chestData && !set_chest_data(host.chests)) {
            throw std::runtime_error("chest test: the chest data was not set: " + host.chests.error());
        }
        if (!existing) {
            for (const char* character : {kCharacterA, kCharacterB}) {
                state::ProgressRow row;
                row.level = level;
                if (!host.store.set_progress(character, row)) {
                    throw std::runtime_error("chest test: no progress row");
                }
            }
        }
        host.world.set_random_seed(0x35B1C4E5ULL);
    }

    [[nodiscard]] const gamedata::InteractionObject& chest(std::uint8_t tag) const {
        return host.interactions.objects()[tag - 1U];
    }

    /** @return The block of GatherOption of a chest, as the client makes it (live check L1 of stage 3.5b.1: action 14, committed). */
    [[nodiscard]] InteractionBlock open_block(std::uint8_t tag) const {
        InteractionBlock block = block_for(host.interactions, tag, kGatherOption);
        block.text88 = "@Test_Chest";
        return block;
    }

    /** The world goes on by `milliseconds` in steps of 10 ms; the clients report their block each 100 ms (the input of a client). */
    void hold(FakeClient& client, const std::optional<InteractionBlock>& block, int milliseconds) {
        for (int passed = 0; passed < milliseconds; passed += 100) {
            report(client, block);
            host.steps(10, {&clientA, &clientB});
        }
    }

    [[nodiscard]] world::Chests& chests() {
        return host.world.chests();
    }
};

/** One Gathering state that a client got for its own character. */
struct Gather {
    std::uint8_t status{0};
    float remaining{0.0F};
    float speed{0.0F};
    std::uint32_t reference{0};
    bool active{false};
    std::uint64_t target{0};
};

inline float f32_of(std::span<const std::uint8_t> value) {
    const std::uint32_t bits = static_cast<std::uint32_t>(u64_of(value));
    float number = 0.0F;
    std::memcpy(&number, &bits, sizeof(number));
    return number;
}

/** @return The Gathering states that a client got for an interest (1: its own character), from a message index on. */
inline std::vector<Gather> gather_states(const FakeClient& client, std::uint32_t interest = 1, std::size_t from = 0) {
    std::vector<Gather> found;
    for (const Seen& seen : fragments_of(client, kGatherSlot, kGatherType, from)) {
        if (seen.interest != interest) {
            continue;
        }
        Gather gather;
        gather.status = field_of(seen.fragment, 1, 0).empty() ? 0 : field_of(seen.fragment, 1, 0)[0];
        gather.remaining = f32_of(field_of(seen.fragment, 1, 1));
        gather.speed = f32_of(field_of(seen.fragment, 1, 2));
        gather.reference = static_cast<std::uint32_t>(u64_of(field_of(seen.fragment, 1, 4)));
        gather.active = !field_of(seen.fragment, 1, 6).empty() && field_of(seen.fragment, 1, 6)[0] != 0;
        gather.target = u64_of(field_of(seen.fragment, 1, 7));
        found.push_back(gather);
    }
    return found;
}

/** One LootTracker entry: the actor and the state. */
struct TrackerEntry {
    std::array<std::uint8_t, 16> actor{};
    std::uint32_t state{0};
    std::uint64_t resetTime{0};
};

/** @return The maps of the LootTracker states that a client got for an interest, from a message index on (each map: its entries). */
inline std::vector<std::vector<TrackerEntry>> tracker_maps(const FakeClient& client, std::uint32_t interest = 1, std::size_t from = 0) {
    std::vector<std::vector<TrackerEntry>> maps;
    for (const Seen& seen : fragments_of(client, kTrackerSlot, kTrackerType, from)) {
        if (seen.interest != interest) {
            continue;
        }
        server::hub::Reader reader(field_of(seen.fragment, 0, 0));
        reader.size();
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        std::vector<TrackerEntry> entries;
        for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
            TrackerEntry entry;
            const auto actor = reader.raw(16);
            std::copy(actor.begin(), actor.end(), entry.actor.begin());
            entry.state = reader.u32();
            reader.u32();
            entry.resetTime = reader.u64();
            reader.raw(58 - 16 - 4 - 4 - 8);
            entries.push_back(entry);
        }
        maps.push_back(std::move(entries));
    }
    return maps;
}

/** @return The state of a chest in the last map (no value: no map, or no entry of the chest). */
inline std::optional<std::uint32_t> tracker_state(const FakeClient& client, const std::array<std::uint8_t, 16>& actor, std::size_t from = 0) {
    const auto maps = tracker_maps(client, 1, from);
    if (maps.empty()) {
        return std::nullopt;
    }
    for (const TrackerEntry& entry : maps.back()) {
        if (entry.actor == actor) {
            return entry.state;
        }
    }
    return std::nullopt;
}

/** @return The number of items of an id in the bag of an entity. */
inline std::uint32_t bag_count(const world::Entity& entity, const std::string& itemId) {
    std::uint32_t count = 0;
    for (const world::BagSlot& slot : entity.bag.slots) {
        if (slot.item && slot.item->itemId == itemId) {
            count += slot.item->quantity;
        }
    }
    return count;
}

} // namespace soulwarden::backend::tests::chests

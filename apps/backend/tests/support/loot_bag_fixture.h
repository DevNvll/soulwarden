#pragma once

// The world of the loot bag cases (stage 3.5b.2, tests/loot_bag_tests.cpp; docs/systems/loot-bags.md): a made-up template of the form of
// the personal loot container of tools/gamedata/interactions.py (format 3: lootContainers "personal"), made-up items and loot tables, a
// made-up creature kind, two characters A and B near the place of the kills with their clients, the requests of the client (the blocks of
// TakeAllOption and OpenStorageOption, the bodies of 2939, 2942, 2801, 1838, 826), and the readers of the states (the LootTracker entries
// of slot 33, the selected Container of slot 76, the GlobalStorage map of slot 77). All keys, ids, positions, items, and tables are made up.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/loot_tables.h"
#include "server/rep/item_requests.h"
#include "server/rep/states/state_codecs.h"
#include "support/interaction_fixture.h"
#include "support/item_decoder.h"
#include "support/test_progression.h"

namespace soulwarden::backend::tests::bags {

using namespace interaction;

inline constexpr std::uint32_t kTrackerSlot = 33;
inline constexpr std::uint32_t kTrackerType = 982;
inline constexpr std::uint32_t kBagSlot61 = 61;
inline constexpr std::uint32_t kPanelSlot = 76;
inline constexpr std::uint32_t kContainerType = 1755;
inline constexpr std::uint32_t kStorageSlot = 77;
inline constexpr std::uint32_t kStorageType = 2938;
inline constexpr std::uint32_t kLootDropType = 2027;
inline constexpr std::uint32_t kLootDropSlot = 6;
/** The option rows of the cases. */
inline constexpr std::uint32_t kTakeAllRow = 0;
inline constexpr std::uint32_t kOpenRow = 1;
/** The ids of the made-up slice of the bag (the form of slices/instanced_loot_container). */
inline constexpr std::uint64_t kBagRoot = 0x00000000BA600001ULL;
inline constexpr std::uint64_t kBagInteractEntity = 0x00000000BA600007ULL;
inline constexpr std::uint64_t kBagStorageEntity = 0x00000000BA600006ULL;
inline constexpr std::uint64_t kBagInteractComponent = 0x2953D2FECFA764BDULL;
inline constexpr std::array<std::uint8_t, 16> kBagAsset{0xBA, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
/** The place of the kills: 3 m north of A (A at 100 200, B at 101 200). */
inline constexpr state::WorldPosition kKillPlace{100.0F, 203.0F, 30.0F};
/** The lifetime and the end after the last item of the data (KillSelfDelay, FastKillSelfDelay of the slice). */
inline constexpr float kLifetime = 180.0F;
inline constexpr float kEmptyLifetime = 1.25F;

inline gamedata::InteractionTemplate bag_template() {
    gamedata::InteractionTemplate bag;
    bag.slice = "slices/test/instanced_bag.dynamicslice";
    bag.kind = gamedata::InteractionKind::lootDrop;
    bag.asset = kBagAsset;
    bag.sub = 2;
    bag.root = kBagRoot;
    bag.positionSlot = 1;
    gamedata::InteractionBound bound;
    bound.slot = 5;
    bound.component = kBagInteractComponent;
    bound.position = state::WorldPosition{0.0F, 0.0F, 0.0F};
    bound.rangeSq = 25.0F;
    bound.capacity = -1;
    bound.arc = 180.0F;
    bound.entity = kBagInteractEntity;
    bound.options = {{kTakeAllRow, kBagStorageEntity}, {kOpenRow, kBagStorageEntity}};
    bag.bounds.push_back(bound);
    return bag;
}

inline gamedata::LootContainer bag_container(float lifetime = kLifetime) {
    gamedata::LootContainer container;
    container.templateIndex = 0;
    container.kind = 1;
    container.lootDropSlot = kLootDropSlot;
    container.lifetime = lifetime;
    container.emptyLifetime = kEmptyLifetime;
    container.hideDelay = 1.5F;
    container.rarityParticles = 5;
    return container;
}

/** The interaction data: the bag template, its two option rows, and (with `containers`) the loot container "personal" (format 3). */
inline bool set_bag_interactions(gamedata::InteractionData& data, bool containers = true, float lifetime = kLifetime) {
    if (!data.set({option_row("TakeAllOption", 28, true), option_row("OpenStorageOption", 26, true)}, {}, {}, {bag_template()})) {
        return false;
    }
    return !containers || data.set_loot_containers({{"personal", bag_container(lifetime)}});
}

inline gamedata::ItemDefinition bag_item(const char* id, const char* type, const char* itemClass, std::uint32_t tier, std::uint32_t stack,
                                         std::uint32_t low = 0, std::uint32_t high = 0, bool bound = false) {
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
    definition.bindOnPickup = bound;
    return definition;
}

/** The items: index K + 1 for item K. */
inline std::vector<gamedata::ItemDefinition> bag_items() {
    return {bag_item("TestSword", "Weapon", "EquippableMainHand+Sword", 2, 1, 200, 300),
            bag_item("TestOre", "Resource", "Resource", 1, 1000),
            bag_item("TestHide", "Resource", "Resource", 1, 1000),
            bag_item("TestBoundHelm", "Armor", "EquippableHead+Armor+Light", 1, 1, 75, 75, true),
            bag_item("TestStone", "Resource", "Resource", 1, 1)};
}

inline gamedata::CreatureSlice bag_wolf_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/bag-wolf.dynamicslice";
    slice.asset.fill(0x71);
    slice.subId = 2;
    slice.root = 0x1000000000000071ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = 5;
    slice.vitalsSlot = 9;
    slice.markerSlot = 15;
    slice.slots = {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Death", "r_Death", 4, true}};
    return slice;
}

inline gamedata::CreatureKind bag_wolf_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_BagWolf";
    kind.vitals = "Test_BagWolf";
    kind.category = "Test_BagWolf_Category";
    kind.vitalsId = 0x71717171;
    kind.categoryId = 0x71717172;
    kind.name = "Test_BagWolf";
    kind.vitalsName = "Test_BagWolf";
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 20;
    kind.bodies = {{0, 100}};
    return kind;
}

inline constexpr float kWolfHealth = 1000.0F;

/** The loot tables: Test_BagRoot (AND): TestSword (gear score 210 to 220) and 3 TestOre; the wolf drops it (the loot switch). */
inline gamedata::LootData bag_loot() {
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
    }
    data.modifiers = {{"Solo", {1.0F, 1.0F, 0.0F, 1.0F, std::nullopt}}};
    data.events = {{"killevent", 0.05F}};
    data.credit.timeLimit = 120.0F;
    data.credit.healthShare = 0.01F;
    data.credit.range = 200.0F;
    data.credit.receivers = 10;
    data.credit.modifiers.assign(10, {1.0F, 1.0F, 1.0F});
    gamedata::LootTable root;
    root.id = "Test_BagRoot";
    root.all = true;
    root.bonus = gamedata::RollBonus::ignoreBonus;
    root.maxRoll = 0;
    gamedata::LootEntry sword;
    sword.kind = LootEntryKind::item;
    sword.id = "TestSword";
    sword.quantityMin = 1;
    sword.quantityMax = 1;
    sword.gearScoreMin = 210;
    sword.gearScoreMax = 220;
    gamedata::LootEntry ore;
    ore.kind = LootEntryKind::item;
    ore.id = "TestOre";
    ore.quantityMin = 3;
    ore.quantityMax = 3;
    root.entries = {sword, ore};
    data.tables = {root};
    gamedata::LootVitals vitals;
    vitals.id = "Test_BagWolf";
    vitals.creatureType = "Solo";
    vitals.canDropLoot = true;
    vitals.dropChance = 0.1F;
    vitals.table = 0;
    vitals.tags = {"Beast"};
    data.bases = {vitals};
    data.vitals = {{"test_bagwolf", 0}};
    return data;
}

/** The two characters (A and B) with their clients, the bag data, and the kills. */
struct BagShop : Shop {
    explicit BagShop(const std::string& database = ":memory:", bool existing = false, bool containers = true, float lifetime = kLifetime)
        : Shop(false, database, true, existing) {
        if (!set_bag_interactions(host.interactions, containers, lifetime)) {
            throw std::runtime_error("bag test: the interaction data was not set: " + host.interactions.error());
        }
        host.catalog.set(bag_items());
        if (!host.loot.set(bag_loot())) {
            throw std::runtime_error("bag test: the loot tables were not set: " + host.loot.error());
        }
        if (!host.creatures.set_full({bag_wolf_slice()}, {bag_wolf_kind()}, {}, {{20, kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("bag test: the creature data was not set: " + host.creatures.error());
        }
        set_progression(host);
        host.world.set_random_seed(0x35B2B465ULL);
    }

    [[nodiscard]] world::LootBags& bags() {
        return host.world.loot_bags();
    }

    /** A kill of a wolf at a place: the hits of the characters (each one more than 1 percent of the health). */
    std::vector<world::KillReward> kill(const std::vector<const char*>& characters, const state::WorldPosition& place = kKillPlace) {
        world::Entity* wolf = host.world.creatures().spawn("Test_BagWolf", place, 0.0F).entity;
        if (wolf == nullptr) {
            throw std::runtime_error("bag test: no wolf");
        }
        const world::EntityId id = wolf->id;
        for (std::size_t index = 0; index < characters.size(); ++index) {
            world::Entity* character = host.world.find_character(characters[index]);
            host.world.rewards().set_forced(characters[index], true);
            const float damage = index + 1 == characters.size() ? kWolfHealth : 100.0F;
            (void)host.world.creatures().hurt(id, damage, character->id);
        }
        return {};
    }

    /** @return The bag of a character that came last (the bag with the latest end), or null. */
    [[nodiscard]] const world::LootBag* bag_of(const char* character) {
        const world::LootBagsView view = bags().view(character);
        return view.bags.empty() ? nullptr : bags().find(view.bags.back().key);
    }

    /** @return The block of an option of a bag, as the client makes it (live check L5: G of the bag, the XOR rule of the server child). */
    [[nodiscard]] InteractionBlock bag_block(const world::LootBag& bag, std::uint32_t row) const {
        const world::Entity& entity = *const_cast<ServerHost&>(host).world.find(bag.entity);
        const std::uint64_t k = kBagRoot ^ entity.id;
        const gamedata::InteractionOption& option = host.interactions.options()[row];
        InteractionBlock block;
        block.actor = entity.actor.bytes;
        block.group = entity.id;
        block.target = (kBagInteractEntity ^ k) + kBagInteractComponent;
        block.action = option.action;
        block.baseAction = option.action;
        block.optionEntity = kBagStorageEntity ^ k;
        block.committed = true;
        block.optionId = option.id;
        block.text88 = "@inv_DroppedItems";
        return block;
    }

    /** The body of 2939 (an `item`: 2942 or 2801) with a container reference: the selected Container of `owner` by default. */
    [[nodiscard]] static Bytes bag_body(const world::Entity& sender, const world::Entity& owner, bool item = false,
                                        const world::InstanceId& instance = {}, std::uint32_t quantity = 0) {
        server::hub::Writer body;
        body.u64(0xAAC3F4219484F844ULL);
        body.u64(sender.id);
        body.raw(owner.actor.bytes);
        body.u64(owner.id);
        body.u64(0xDE23CE47F759DECEULL);
        if (item) {
            body.raw(instance);
            body.u32(quantity);
        }
        return body.data();
    }

    void send(FakeClient& client, std::uint32_t type, const Bytes& body, int steps = 3) {
        const bool isA = &client == &clientA;
        world::Entity* own = host.world.find_character(isA ? kCharacterA : kCharacterB);
        host.datagram(client.peer, client.component_message(type, own->actor.bytes, body));
        host.steps(steps, {&clientA, &clientB});
    }

    /** The world clock goes on by `milliseconds` in steps of 10 ms, with the two clients. */
    void pass(int milliseconds) {
        host.steps(milliseconds / 10, {&clientA, &clientB});
    }
};

/** One LootTracker entry that a client got: the actor, the state, the kind, the selector. */
struct BagEntry {
    std::array<std::uint8_t, 16> actor{};
    std::uint32_t state{0};
    std::uint32_t kind{0};
    std::uint8_t selector{0};
};

/** @return The maps of the LootTracker states of the own character of a client (interest 1), from a message index on. */
inline std::vector<std::vector<BagEntry>> tracker_maps(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<BagEntry>> maps;
    for (const Seen& seen : fragments_of(client, kTrackerSlot, kTrackerType, from)) {
        if (seen.interest != 1) {
            continue;
        }
        server::hub::Reader reader(field_of(seen.fragment, 0, 0));
        reader.size();
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        std::vector<BagEntry> entries;
        for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
            BagEntry entry;
            const auto actor = reader.raw(16);
            std::copy(actor.begin(), actor.end(), entry.actor.begin());
            entry.state = reader.u32();
            entry.kind = reader.u32();
            reader.raw(8 + 8 + 1 + 12 + 4);
            entry.selector = reader.u8();
            entries.push_back(entry);
        }
        maps.push_back(std::move(entries));
    }
    return maps;
}

/** @return The entry of an actor in the last map of a client, or no value. */
inline std::optional<BagEntry> tracker_entry(const FakeClient& client, const std::array<std::uint8_t, 16>& actor, std::size_t from = 0) {
    const auto maps = tracker_maps(client, from);
    if (maps.empty()) {
        return std::nullopt;
    }
    for (const BagEntry& entry : maps.back()) {
        if (entry.actor == actor) {
            return entry;
        }
    }
    return std::nullopt;
}

/** @return The selected Containers (slot 76) of the own character of a client: each full vector that it got. */
inline std::vector<WireContainer> panels(const FakeClient& client, std::size_t from = 0) {
    std::vector<WireContainer> found;
    for (const Seen& seen : fragments_of(client, kPanelSlot, kContainerType, from)) {
        if (seen.interest == 1) {
            found.push_back(read_container(field_of(seen.fragment, 0, 0)));
        }
    }
    return found;
}

/** One key of a GlobalStorage map: the actor and its items. */
struct StorageEntry {
    std::array<std::uint8_t, 16> actor{};
    std::vector<WireItem> items;
};

/** @return The GlobalStorage maps (slot 77) of the own character of a client. */
inline std::vector<std::vector<StorageEntry>> storage_maps(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<StorageEntry>> maps;
    for (const Seen& seen : fragments_of(client, kStorageSlot, kStorageType, from)) {
        if (seen.interest != 1) {
            continue;
        }
        server::hub::Reader reader(field_of(seen.fragment, 0, 0));
        reader.size();
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t keys = reader.size();
        std::vector<StorageEntry> map;
        for (std::uint32_t key = 0; key < keys && reader.ok(); ++key) {
            StorageEntry entry;
            const auto actor = reader.raw(16);
            std::copy(actor.begin(), actor.end(), entry.actor.begin());
            const std::uint32_t items = reader.size();
            for (std::uint32_t item = 0; item < items && reader.ok(); ++item) {
                WireItem value;
                read_item(reader, value);
                entry.items.push_back(value);
            }
            map.push_back(std::move(entry));
        }
        maps.push_back(std::move(map));
    }
    return maps;
}

/** @return The number of fragments of the record of an entity (any slot) that a client got: a record of a bag that it must not get. */
inline std::size_t fragments_of_entity_type(const FakeClient& client, std::uint32_t type, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                count += fragment.type == type ? 1 : 0;
            }
        }
    }
    return count;
}

/** @return The quantity of an item id in a bag of the world. */
inline std::uint32_t held(const world::Bag& bag, const std::string& itemId) {
    std::uint32_t count = 0;
    for (const world::BagSlot& slot : bag.slots) {
        if (slot.item && slot.item->itemId == itemId) {
            count += slot.item->quantity;
        }
    }
    return count;
}

/** @return The stack of an instance id in a bag of the world, or null. */
inline const world::ItemInstance* stack_of(const world::Bag& bag, const world::InstanceId& instance) {
    for (const world::BagSlot& slot : bag.slots) {
        if (slot.item && slot.item->instance == instance) {
            return &*slot.item;
        }
    }
    return nullptr;
}

/** @return The quantity of an item id in a part of a bag. */
inline std::uint32_t in_part(const world::LootBag& bag, const char* character, const std::string& itemId) {
    const auto part = bag.parts.find(character);
    std::uint32_t count = 0;
    if (part != bag.parts.end()) {
        for (const world::ItemInstance& item : part->second.items) {
            count += item.itemId == itemId ? item.quantity : 0;
        }
    }
    return count;
}

} // namespace soulwarden::backend::tests::bags

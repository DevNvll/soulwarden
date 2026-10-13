// Cases for the equipment (stage 1.4): the slot rules, the wire form of the paperdoll state
// for the owner and for an observer, equip, unequip, and swap, the requests of a client with
// the failure reply, two sessions, the persistence, the items of the archetype of a new
// character, the development routes, and a paperdoll in more than one bundle.

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "core/encoding/base64.h"
#include "gamedata/item_catalog.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/item_requests.h"
#include "server/rep/states/state_codecs.h"
#include "state/creation_data.h"
#include "state/world.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/item_decoder.h"
#include "support/paperdoll_decoder.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/inventory.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using server::rep::View;
using world::ItemError;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-00000000000c";
constexpr const char* kCharacterNone = "dddddddd-0000-4000-8000-00000000000d";
constexpr float kHeight = 50.0F;
/** Largest number of data bytes of one carrier message (protocol notes 3.2). */
constexpr std::size_t kCarrierMessage = 1024;
constexpr std::uint32_t kPaperdollSlot = 62;
constexpr std::uint32_t kPaperdollType = 3183;

// The item indexes of the examples of the research (equipment.md 8.1): numbers of the wire
// examples, with no item table in the repository.
constexpr std::uint32_t kExampleChest = 1323;
constexpr std::uint32_t kExampleSword = 5278;

// The indexes of the made-up catalog of set_catalog.
constexpr std::uint32_t kSwordIndex = 1;
constexpr std::uint32_t kChestIndex = 2;

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(ServerHost& host)
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight})) {}
};

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

gamedata::ItemDefinition gear(const char* id, const char* type, const char* itemClass, std::uint32_t maxStack = 1,
                              std::uint32_t durability = 250, std::uint32_t gearScore = 60) {
    gamedata::ItemDefinition item;
    item.id = id;
    item.nameKey = std::string("@") + id + "_MasterName";
    item.type = type;
    item.itemClass = itemClass;
    item.maxStack = maxStack;
    item.durability = durability;
    item.minGearScore = gearScore;
    item.maxGearScore = gearScore;
    item.gearScoreOverride = gearScore;
    item.bindOnPickup = maxStack == 1;
    item.weight = 10;
    return item;
}

/** The class texts are class texts of the item tables of the game, with made-up item ids. */
std::vector<gamedata::ItemDefinition> test_items() {
    return {gear("TestSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 1, 1000, 100), // 1
            gear("TestChest", "Armor", "EquippableChest+Armor+Light"),                          // 2
            gear("TestLegs", "Armor", "EquippableLegs+Armor+Light"),                            // 3
            gear("TestHead", "Armor", "EquippableHead+Armor+Light"),                            // 4
            gear("TestShield", "Weapon", "Weapon+EquippableOffHand+Shield+RoundShield"),        // 5
            gear("TestOre", "Resource", "Resource", 100, 0, 0),                                 // 6
            gear("TestAmulet", "Armor", "EquippableAmulet+Jewelry"),                            // 7
            gear("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 1, 1000, 100),     // 8
            gear("TestPotion", "Consumable", "Consumable", 10, 0, 0),                           // 9
            gear("TestHands", "Armor", "EquippableHands+Armor+Light"),                          // 10
            gear("TestFeet", "Armor", "EquippableFeet+Armor+Light")};                           // 11
}

void set_catalog(ServerHost& host) {
    host.catalog.set(test_items());
}

/**
 * The paperdoll of a client: it applies each fragment as the client does (protocol notes
 * 4.11). The client of the owner reads the item lists; a different client reads the item looks.
 */
struct ClientPaperdoll {
    /** The value of isLocalPlayer. */
    bool local{false};
    /** Slot to item, for the client of the owner. */
    std::map<std::uint32_t, WireItem> items;
    /** Slot to item look, for the client of an observer. */
    std::map<std::uint32_t, WireLook> looks;
    Bytes sheathe = Bytes(7, 0);
    Bytes active = Bytes(7, 0);
    /** True when a fragment had lists that the client does not use: different lengths, or a list of the wrong view. */
    bool fault{false};
    unsigned fragments{0};
    /** The slots and the causes of the last fragment, in the sequence of its lists. */
    std::vector<std::uint32_t> lastSlots;
    std::vector<std::uint32_t> lastActions;
    /** True when the last fragment had group 1 (the fields of the owner). */
    bool lastOwnerFields{false};

    void entries(const Fragment& fragment, std::pair<unsigned, unsigned> slotField, std::pair<unsigned, unsigned> itemField,
                 std::pair<unsigned, unsigned> durabilityField) {
        const bool hasSlots = fragment.fields.count(slotField) != 0;
        const bool hasItems = fragment.fields.count(itemField) != 0;
        const bool hasDurability = fragment.fields.count(durabilityField) != 0;
        if (!hasItems && !hasDurability) {
            return;
        }
        if (!hasSlots || !hasItems || !hasDurability || !local) {
            fault = true; // The client uses the three lists together, and only as the local player.
            return;
        }
        std::vector<std::uint32_t> slots;
        std::vector<std::uint32_t> durability;
        std::vector<WireItem> list;
        server::hub::Reader slotReader(fragment.fields.at(slotField));
        server::hub::Reader itemReader(fragment.fields.at(itemField));
        server::hub::Reader durabilityReader(fragment.fields.at(durabilityField));
        if (!read_number_list(slotReader, slots) || !read_item_list(itemReader, list) || !read_number_list(durabilityReader, durability)
            || slots.size() != list.size() || slots.size() != durability.size()) {
            fault = true; // The client does nothing for lists with different lengths.
            return;
        }
        // The client does the entries with the cause "unequipped" (5) first.
        for (const bool firstPass : {true, false}) {
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if ((list[index].action == world::sync::kItemUnequipped) != firstPass) {
                    continue;
                }
                if (list[index].empty()) {
                    items.erase(slots[index]);
                } else {
                    fault = fault || durability[index] != list[index].durability;
                    items[slots[index]] = list[index];
                }
            }
        }
        for (std::size_t index = 0; index < slots.size(); ++index) {
            lastSlots.push_back(slots[index]);
            lastActions.push_back(list[index].action);
        }
    }

    void apply(const Fragment& fragment) {
        ++fragments;
        lastSlots.clear();
        lastActions.clear();
        lastOwnerFields = false;
        for (const auto& [key, value] : fragment.fields) {
            lastOwnerFields = lastOwnerFields || key.first == 1;
        }
        if (fragment.fields.count({1, 6}) != 0) {
            local = fragment.fields.at({1, 6})[0] == 1;
        }
        if (fragment.fields.count({2, 0}) != 0) {
            sheathe = fragment.fields.at({2, 0});
        }
        if (fragment.fields.count({2, 2}) != 0) {
            active = fragment.fields.at({2, 2});
        }
        entries(fragment, {2, 3}, {1, 1}, {1, 0});
        entries(fragment, {1, 3}, {1, 2}, {1, 4});
        if (fragment.fields.count({2, 4}) != 0) {
            // The path of a different player: the slot list and the list of the item looks.
            std::vector<std::uint32_t> slots;
            std::vector<WireLook> list;
            if (local || fragment.fields.count({2, 3}) == 0) {
                fault = true;
                return;
            }
            server::hub::Reader slotReader(fragment.fields.at({2, 3}));
            server::hub::Reader lookReader(fragment.fields.at({2, 4}));
            if (!read_number_list(slotReader, slots) || !read_look_list(lookReader, list) || slots.size() != list.size()) {
                fault = true;
                return;
            }
            for (std::size_t index = 0; index < slots.size(); ++index) {
                if (list[index].index == 0) {
                    looks.erase(slots[index]);
                } else {
                    looks[slots[index]] = list[index];
                }
                lastSlots.push_back(slots[index]);
            }
        }
    }

    [[nodiscard]] bool bit(const Bytes& map, std::uint32_t slot) const {
        return (map[slot / 8] & (1U << (slot % 8))) != 0;
    }
    /** @return The bits of a map as text, for example "24 25 ". */
    [[nodiscard]] std::string bits(const Bytes& map) const {
        std::string text;
        for (std::uint32_t slot = 0; slot < 56; ++slot) {
            if (bit(map, slot)) {
                text += std::to_string(slot) + " ";
            }
        }
        return text;
    }
    /** @return The content as text: "slot:index " for each slot that has an item. */
    [[nodiscard]] std::string text() const {
        std::string out;
        for (const auto& [slot, item] : items) {
            out += std::to_string(slot) + ":" + std::to_string(item.index) + " ";
        }
        for (const auto& [slot, look] : looks) {
            out += std::to_string(slot) + ":" + std::to_string(look.index) + " ";
        }
        return out;
    }
};

/** What a client got for the equipment of one entity from the message `from` on. */
struct GearUpdates {
    /** Each fragment of the paperdoll, in sequence. */
    std::vector<Fragment> fragments;
    /** Each record that has a fragment of the paperdoll: its fragments as text. */
    std::vector<std::string> records;
    /** The bodies of the messages "equip item fail" (type 3626). */
    std::vector<Bytes> fails;
    unsigned bundles{0};
    std::size_t largestFrame{0};
    /** True when each bundle decodes to its last byte. */
    bool complete{true};
};

GearUpdates gear_updates(const FakeClient& client, std::size_t from = 0, std::uint32_t interest = 1) {
    GearUpdates updates;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == server::rep::kTypeEquipItemFail) {
            updates.fails.push_back(client.got[index].body);
        }
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        updates.complete = updates.complete && bundle.complete;
        bool has = false;
        for (const Record& record : bundle.records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == kPaperdollSlot && fragment.type == kPaperdollType) {
                    updates.fragments.push_back(fragment);
                    updates.records.push_back(order_of(record));
                    has = true;
                }
            }
        }
        if (has) {
            ++updates.bundles;
            updates.largestFrame = std::max(updates.largestFrame, client.got[index].frame.size());
        }
    }
    return updates;
}

/** Applies the paperdoll fragments that a client got from the message `from` on. @return Their number. */
std::size_t follow(ClientPaperdoll& doll, const FakeClient& client, std::size_t& from, std::uint32_t interest = 1) {
    const GearUpdates updates = gear_updates(client, from, interest);
    for (const Fragment& fragment : updates.fragments) {
        doll.apply(fragment);
    }
    doll.fault = doll.fault || !updates.complete;
    from = client.got.size();
    return updates.fragments.size();
}

/** @return The rows of a character as text: "container:slot:item:quantity " for each row (1 the bag, 2 the paperdoll). */
std::string rows_text(state::Store& store, const std::string& characterId) {
    std::string text;
    for (const state::ItemRow& row : store.list_items(characterId)) {
        text += std::to_string(row.container) + ":" + std::to_string(row.slot) + ":" + row.itemId + ":" + std::to_string(row.quantity) + " ";
    }
    return text;
}

/** @return The equipment of an entity as text: "slot:item " for each slot that has an item. */
std::string equipment_text(const world::Equipment& equipment) {
    std::string text;
    for (const auto& [slot, content] : equipment.slots) {
        if (content.item) {
            text += std::to_string(slot) + ":" + content.item->itemId + " ";
        }
    }
    return text;
}

world::InstanceId bag_instance(ServerHost& host, const char* characterId, std::uint32_t slot) {
    const world::Entity* entity = host.world.find_character(characterId);
    if (entity == nullptr || slot >= entity->bag.slots.size() || !entity->bag.slots[slot].item) {
        return {};
    }
    return entity->bag.slots[slot].item->instance;
}

world::InstanceId worn_instance(ServerHost& host, const char* characterId, std::uint32_t slot) {
    const world::Entity* entity = host.world.find_character(characterId);
    const world::ItemInstance* item = entity != nullptr ? entity->equipment.item(slot) : nullptr;
    return item != nullptr ? item->instance : world::InstanceId{};
}

/** The paperdoll facet target of an entity in the tests: the backend copies it into its reply. */
std::uint64_t facet_of(std::uint64_t entity) {
    return entity + 0x62;
}

/** Body of PaperdollComponentServerFacet_ServerEquipItem (type 3895), from the writer of the client. */
Bytes equip_body(std::uint64_t entity, std::uint32_t bagSlot, std::uint32_t paperdollSlot, const world::InstanceId& instance,
                 std::uint32_t quantity = 1, std::uint8_t flag = 0) {
    server::hub::Writer body;
    body.u64(facet_of(entity));
    body.u64(entity);
    body.u32(bagSlot);
    body.u32(paperdollSlot);
    body.u32(quantity);
    body.raw(instance);
    body.u8(flag);
    return body.data();
}

/** Body of PaperdollComponentServerFacet_ServerUnequipItem (type 3896). */
Bytes unequip_body(std::uint64_t entity, std::uint32_t paperdollSlot, std::uint32_t bagSlot = 0xFFFFFFFF, std::uint32_t quantity = 1,
                   std::uint8_t flag = 1) {
    server::hub::Writer body;
    body.u64(facet_of(entity));
    body.u64(entity);
    body.u32(paperdollSlot);
    body.u32(bagSlot);
    body.u32(quantity);
    body.u8(flag);
    return body.data();
}

/** Body of PaperdollComponentServerFacet_ServerSwapItems (type 3900). */
Bytes swap_body(std::uint64_t entity, std::uint32_t first, std::uint32_t second, const world::InstanceId& firstInstance,
                const world::InstanceId& secondInstance, std::uint8_t count = 0) {
    server::hub::Writer body;
    body.u64(facet_of(entity));
    body.u64(entity);
    body.u32(first);
    body.u32(second);
    body.raw(firstInstance);
    body.raw(secondInstance);
    body.u8(count);
    return body.data();
}

world::ItemInstance example_item(std::uint32_t index, std::uint8_t lastByte, std::uint32_t durability, std::uint32_t gearScore) {
    world::ItemInstance item;
    item.instance = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, lastByte};
    item.itemId = "example";
    item.index = index;
    item.quantity = 1;
    item.gearScore = gearScore;
    item.durability = durability;
    item.maxDurability = durability;
    return item;
}

Fragment decode_paperdoll(const Bytes& state, bool& ok) {
    Fragment fragment;
    fragment.slot = kPaperdollSlot;
    fragment.type = kPaperdollType;
    std::size_t position = 0;
    ok = detail::read_state(state, position, fragment) && position == state.size();
    return fragment;
}

/** G1: the slot rules, and the bytes of the paperdoll state for the two views against the examples. */
void wire_bytes(const Options&) {
    using namespace server::rep;
    using namespace world::paperdoll;
    check(valid_slot(0) && valid_slot(20) && !valid_slot(21) && !valid_slot(22) && !valid_slot(23) && valid_slot(24) && valid_slot(49)
              && !valid_slot(50) && !valid_slot(51) && !valid_slot(0xFFFFFFFF),
          "the slots of the paperdoll are 0 to 20 and 24 to 49 (the slot database of the player has no row for 21, 22, 23)");
    check(slot_accepts(kChest, "EquippableChest+Armor+Light") && !slot_accepts(kHead, "EquippableChest+Armor+Light")
              && slot_accepts(kHead, "EquippableHead+Armor+Heavy") && slot_accepts(kHands, "EquippableHands+Armor")
              && slot_accepts(kLegs, "EquippableLegs+Armor") && slot_accepts(kFeet, "EquippableFeet+Armor")
              && slot_accepts(kAmulet, "EquippableAmulet+Jewelry") && slot_accepts(kToken, "EquippableToken+Jewelry")
              && slot_accepts(kRing, "EquippableRing+Jewelry") && !slot_accepts(kRing, "EquippableAmulet+Jewelry"),
          "each armor slot and each jewelry slot takes only an item with the class tag of the slot");
    check(slot_accepts(24, "Weapon+EquippableMainHand+Sword+Melee") && slot_accepts(25, "Weapon+EquippableTwoHand+Bow+Ranged")
              && slot_accepts(26, "Weapon+EquippableMainHand+Sword+Melee") && !slot_accepts(24, "Weapon+EquippableOffHand+Shield+RoundShield")
              && slot_accepts(27, "Weapon+EquippableOffHand+Shield+RoundShield") && slot_accepts(29, "Weapon+EquippableOffHand+Shield")
              && !slot_accepts(27, "Weapon+EquippableMainHand+Sword+Melee") && !slot_accepts(24, "EquippableChest+Armor+Light"),
          "a weapon slot takes a main hand item or a two hand item, and an off hand slot takes an off hand item");
    check(slot_accepts(33, "EquippableTool+Axe+LoggingAxe") && slot_accepts(36, "EquippableTool+Axe+Pickaxe") && !slot_accepts(33, "EquippableTool+Axe+Pickaxe")
              && slot_accepts(30, "Bag") && slot_accepts(32, "Bag+Named") && slot_accepts(45, "HeartGem") && slot_accepts(14, "Consumable")
              && slot_accepts(19, "Blueprint") && !slot_accepts(20, "Consumable") && slot_accepts(1, "equippablechest+armor")
              && !slot_accepts(1, "EquippableChestX") && !slot_accepts(1, "") && !slot_accepts(12, "Ammo") && !slot_accepts(13, "Ammo")
              && !slot_accepts(9, "EquippableTool") && !slot_accepts(21, "EquippableChest") && !slot_accepts(50, "EquippableChest"),
          "the tool slots, the bag slots, the heart rune slot, and the quick slots have their class tags; the compare "
          "ignores the letter case and needs a full tag; a slot with no rule (the ammunition slots, 9, 21, 50) takes no item");
    check(visible_slot(0) && visible_slot(4) && !visible_slot(5) && !visible_slot(23) && visible_slot(24) && visible_slot(29) && !visible_slot(30),
          "the slots whose item shows on the body are 0 to 4 and 24 to 29");

    const world::ItemInstance chest = example_item(kExampleChest, 0x01, 250, 60);
    const world::ItemInstance sword = example_item(kExampleSword, 0x02, 1000, 100);
    // equipment.md 8.1: the two items, 46 bytes each.
    const std::string chestBytes = "00013a16895185e5" "ba03" "00" "3c" "0000000000" "00000000"
                                   "a0a1a2a3a4a5a6a7a8a9aaabacadae01" "00" "00000000" "00000000";
    const std::string swordBytes = "00013a168b346206" "a80f" "00" "64" "0000000000" "00000000"
                                   "a0a1a2a3a4a5a6a7a8a9aaabacadae02" "00" "00000000" "00000000";
    check(hex_text(encode_item(&chest, 501, world::sync::kLoad)) == chestBytes && chestBytes.size() == 92
              && hex_text(encode_item(&sword, 501, world::sync::kLoad)) == swordBytes,
          "an item of the paperdoll is the element of a container with the slot code 501: the chest armor and the sword "
          "are the 46 bytes of the examples (cause 0x11)");
    check(hex_text(encode_item(&chest, 501, world::sync::kItemEquipped)).substr(0, 16) == "0001084ad97185e5"
              && hex_text(encode_item(&sword, 501, world::sync::kItemEquipped)).substr(0, 16) == "0001084adb546206",
          "a new item has the cause 4 (item equipped): the packed numbers of the examples");
    ItemWire removal;
    removal.emptyAction = world::sync::kItemUnequipped;
    const std::string emptyBytes = "000016f998300000" + std::string(74, '0');
    check(hex_text(encode_item(nullptr, 501, world::sync::kLoad, removal)) == emptyBytes && emptyBytes.size() == 90,
          "the entry of a slot that lost its item: quantity 0, cause 5 (item unequipped), slot code 501, and 37 zero bytes");
    check(hex_text(encode_item_look(&chest)) == "ab14" "00" "00000000" "00000000" && hex_text(encode_item_look(&sword)) == "9e52" "00" "00000000" "00000000"
              && hex_text(encode_item_look(nullptr)) == "00" "00" "00000000" "00000000"
              && hex_text(encode_item_look(&chest, ItemWire{false})) == "ab14" "00",
          "the look of an item for an observer: the item index (1323, 5278) as a size, flags 0, and the appearance id and "
          "the skin dye when the transmog setting is on; the index 0 for no item");

    world::Entity entity;
    const PartsRequest fresh{0, 900};
    std::vector<Bytes> parts = encode_paperdoll_state(entity, View::owner, fresh);
    check(parts.size() == 1 && hex_text(parts[0]) == "02" "40" "01" && encode_paperdoll_state(entity, View::observer, fresh).empty(),
          "a character with no equipment: the owner gets only isLocalPlayer (group 1, field 6), and an observer gets no fragment");

    // equipment.md 8.2: the fragment with only the chest armor (state of 67 bytes).
    entity.equipment.slots[1].item = chest;
    entity.equipment.slots[1].changed = 5;
    parts = encode_paperdoll_state(entity, View::owner, fresh);
    const std::string chestOnly = "06" "43" "000001000000fa" "000001" + chestBytes + "01" "08" "00000100000001";
    check(parts.size() == 1 && hex_text(parts[0]) == chestOnly && parts[0].size() == 67,
          "owner view, the chest armor only: the 67 state bytes of the example (visibleDurability, visibleFullItemData, "
          "isLocalPlayer, visiblePaperdollSlots; no map)");

    // equipment.md 8.2: the first fragment with two items (state of 135 bytes).
    entity.equipment.slots[24].item = sword;
    entity.equipment.slots[24].changed = 6;
    entity.equipment.weaponsChanged = 6;
    parts = encode_paperdoll_state(entity, View::owner, fresh);
    const std::string twoItems = "06" "43" "000002000000fa000003e8" "000002" + chestBytes + swordBytes + "01"
                                 "0d" "00000001000000" "00000001000000" "0000020000000100000018";
    bool decoded = false;
    const Fragment ownerFragment = parts.size() == 1 ? decode_paperdoll(parts[0], decoded) : Fragment{};
    check(parts.size() == 1 && hex_text(parts[0]) == twoItems && parts[0].size() == 135 && decoded && ownerFragment.fields.size() == 6,
          "owner view, the chest armor and the sword: the 135 state bytes of the example, with sheatheMap and activeMap "
          "of slot 24; the fragment decodes to its last byte");

    // The observer view: the fields of group 2 only. The bytes had a live proof in stage 1.4.
    parts = encode_paperdoll_state(entity, View::observer, fresh);
    const std::string observerBytes = "04" "1d" "00000001000000" "00000001000000" "0000020000000100000018"
                                      "000002" "ab14" "00" "00000000" "00000000" "9e52" "00" "00000000" "00000000";
    const Fragment observerFragment = parts.size() == 1 ? decode_paperdoll(parts[0], decoded) : Fragment{};
    bool onlyCommon = true;
    for (const auto& [key, value] : observerFragment.fields) {
        onlyCommon = onlyCommon && key.first == 2;
    }
    check(parts.size() == 1 && hex_text(parts[0]) == observerBytes && parts[0].size() == 52 && decoded && onlyCommon
              && observerFragment.fields.size() == 4,
          "observer view of the same character: 52 state bytes with sheatheMap, activeMap, visiblePaperdollSlots, and "
          "visibleItemVisualData (group 2), and no field of group 1");

    // An amulet is in the lists that are not visible, and no observer gets it.
    world::Entity jewelry;
    jewelry.equipment.slots[5].item = example_item(5562, 0x03, 375, 200);
    parts = encode_paperdoll_state(jewelry, View::owner, fresh);
    const Fragment jewelryFragment = parts.size() == 1 ? decode_paperdoll(parts[0], decoded) : Fragment{};
    check(parts.size() == 1 && decoded && jewelryFragment.fields.size() == 4 && jewelryFragment.fields.count({1, 2}) == 1
              && jewelryFragment.fields.count({1, 3}) == 1 && jewelryFragment.fields.count({1, 4}) == 1
              && jewelryFragment.fields.count({1, 6}) == 1 && hex_text(jewelryFragment.fields.at({1, 3})) == "00000100000005"
              && hex_text(jewelryFragment.fields.at({1, 4})) == "00000100000177"
              && encode_paperdoll_state(jewelry, View::observer, fresh).empty(),
          "an amulet (slot 5): the owner gets it in the lists nonVisibleFullItemData, nonVisiblePaperdollSlots, and "
          "nonVisibleDurability; an observer gets no fragment");

    // A later update has only the slots that changed. The chest armor goes at revision 9.
    entity.equipment.slots[1].item.reset();
    entity.equipment.slots[1].action = world::sync::kItemUnequipped;
    entity.equipment.slots[1].changed = 9;
    parts = encode_paperdoll_state(entity, View::owner, PartsRequest{8, 900});
    check(parts.size() == 1 && hex_text(parts[0]) == "06" "43" "00000100000000" "000001" + emptyBytes + "01" "08" "00000100000001",
          "owner view of an update: only the slot that changed, with the entry that has no item (cause 5) and the "
          "durability 0; the sword is not in the lists, and the maps are not there (no weapon slot changed)");
    parts = encode_paperdoll_state(entity, View::observer, PartsRequest{8, 900});
    check(parts.size() == 1 && hex_text(parts[0]) == "04" "18" "00000100000001" "000001" "00" "00" "00000000" "00000000",
          "observer view of the same update: slot 1 with the item index 0");
    check(encode_paperdoll_state(entity, View::owner, PartsRequest{9, 900}).empty()
              && encode_paperdoll_state(entity, View::observer, PartsRequest{9, 900}).empty(),
          "a session that has the entity at the newest revision gets no fragment");
    // A session with no record gets only the slots that have an item.
    parts = encode_paperdoll_state(entity, View::owner, fresh);
    const Fragment afterRemoval = parts.size() == 1 ? decode_paperdoll(parts[0], decoded) : Fragment{};
    check(parts.size() == 1 && decoded && hex_text(afterRemoval.fields.at({2, 3})) == "00000100000018",
          "a session with no record of the entity gets only the slots that have an item (slot 24)");
    // A session that must get each slot again also gets the slot that lost its item.
    entity.equipment.reset = 12;
    parts = encode_paperdoll_state(entity, View::owner, PartsRequest{10, 900});
    const Fragment again = parts.size() == 1 ? decode_paperdoll(parts[0], decoded) : Fragment{};
    check(parts.size() == 1 && decoded && hex_text(again.fields.at({2, 3})) == "0000020000000100000018" && again.fields.count({2, 0}) == 1,
          "a session that gets each slot again (a mark on each state) gets the slot with no item too, and the maps");
}

/** G2: equip, unequip, and swap through the inventory system, with what the session of the owner gets. */
void equip_and_unequip(const Options&) {
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Inventory& inventory = host.world.inventory();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    ClientPaperdoll doll;
    std::size_t from = 0;
    check(follow(doll, a, from) == 1 && doll.local && doll.items.empty() && !doll.fault && doll.bits(doll.sheathe).empty(),
          "the first record of a character with no equipment has one paperdoll fragment: isLocalPlayer, and no item");

    // Bag: 0 sword, 1 chest, 2 legs, 3 ore, 4 shield, 5 amulet, 6 bow.
    for (const char* item : {"TestSword", "TestChest", "TestLegs"}) {
        inventory.give(kCharacterA, item, 1);
    }
    inventory.give(kCharacterA, "TestOre", 30);
    for (const char* item : {"TestShield", "TestAmulet", "TestBow"}) {
        inventory.give(kCharacterA, item, 1);
    }
    host.steps(2, {&a});
    from = a.got.size();
    const world::InstanceId chestId = bag_instance(host, kCharacterA, 1);

    world::ItemResult result = inventory.equip(kCharacterA, 1);
    host.steps(2, {&a});
    const GearUpdates first = gear_updates(a, from);
    follow(doll, a, from);
    check(result.ok() && result.inWorld && result.slots == std::vector<std::uint32_t>{1} && result.equipmentSlots == std::vector<std::uint32_t>{1}
              && rows_text(host.store, kCharacterA)
                     == "1:0:TestSword:1 1:2:TestLegs:1 1:3:TestOre:30 1:4:TestShield:1 1:5:TestAmulet:1 1:6:TestBow:1 2:1:TestChest:1 ",
          "equip of the chest armor with no slot: it goes into slot 1 of the paperdoll, and its row has the container 2 "
          "(the paperdoll) and the slot 1");
    check(first.fragments.size() == 1 && first.records.size() == 1 && first.records[0] == "(62,3183) (61,1755) " && !doll.fault
              && doll.text() == "1:2 " && doll.lastSlots == std::vector<std::uint32_t>{1}
              && doll.lastActions == std::vector<std::uint32_t>{world::sync::kItemEquipped}
              && doll.items.at(1).instance == chestId && doll.items.at(1).slot == 501 && doll.items.at(1).durability == 250
              && doll.items.at(1).gearScore == 60 && doll.items.at(1).flags == world::kItemBound,
          "the session of the owner gets one record with the paperdoll fragment and the bag fragment: slot 1 has the item "
          "(cause 4, the same instance id, slot code 501, durability 250, gear score 60)");
    check(alpha->bag.slots[1].item == std::nullopt && equipment_text(alpha->equipment) == "1:TestChest ",
          "the entity has the item in its equipment, and the slot of the bag is empty");

    result = inventory.equip(kCharacterA, 0, 24);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && doll.text() == "1:2 24:1 " && doll.lastSlots == std::vector<std::uint32_t>{24} && doll.bits(doll.sheathe) == "24 "
              && doll.bits(doll.active) == "24 " && !doll.fault,
          "equip of the sword in slot 24: the update has only slot 24, and the maps have the sheathe bit and the active "
          "bit of slot 24");
    result = inventory.equip(kCharacterA, 6);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && result.equipmentSlots == std::vector<std::uint32_t>{25} && doll.text() == "1:2 24:1 25:8 "
              && doll.bits(doll.sheathe) == "24 25 " && doll.bits(doll.active) == "24 ",
          "equip of the bow with no slot: it goes into the first weapon slot that is free (25); weapon 1 stays the active one");
    result = inventory.equip(kCharacterA, 4, 27);
    inventory.equip(kCharacterA, 5);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && doll.text() == "1:2 5:7 24:1 25:8 27:5 " && doll.bits(doll.sheathe) == "24 25 27 " && doll.bits(doll.active) == "24 27 "
              && !doll.fault,
          "the shield goes into the off hand slot of weapon 1 (27) and is active with it; the amulet goes into slot 5");

    // Each wrong request changes nothing.
    const std::string rows = rows_text(host.store, kCharacterA);
    const std::string worn = equipment_text(alpha->equipment);
    const world::InstanceId legsId = bag_instance(host, kCharacterA, 2);
    check(inventory.equip(kCharacterA, 3, 1).error == ItemError::slotRefusesItem && inventory.equip(kCharacterA, 2, 0).error == ItemError::slotRefusesItem
              && inventory.equip(kCharacterA, 2, 24).error == ItemError::slotRefusesItem
              && inventory.equip(kCharacterA, 2, 12).error == ItemError::slotRefusesItem,
          "equip is refused for a slot that does not take the item: ore in the chest slot, leg armor in the head slot "
          "and in a weapon slot, and each item in a slot with no rule");
    check(inventory.equip(kCharacterA, 2, 50).error == ItemError::badEquipSlot && inventory.equip(kCharacterA, 2, 22).error == ItemError::badEquipSlot
              && inventory.equip(kCharacterA, 2, 0xFFFFFFFF).error == ItemError::badEquipSlot,
          "equip is refused for a slot number that is out of range (50, -1) or not in the slot database (22)");
    check(inventory.equip(kCharacterA, 1, 1).error == ItemError::emptySlot && inventory.equip(kCharacterA, 7, 1).error == ItemError::badSlot
              && inventory.equip(kCharacterA, 499).error == ItemError::badSlot && inventory.equip(kCharacterA, 2, 3, &chestId).error == ItemError::wrongInstance
              && inventory.equip(kCharacterA, 2, 3, &legsId, 2).error == ItemError::badQuantity
              && inventory.equip(kCharacterA, 2, 3, &legsId, 0).error == ItemError::badQuantity
              && inventory.equip(kCharacterA, 3).error == ItemError::noEquipSlot
              && inventory.equip(kCharacterNone, 0).error == ItemError::unknownCharacter,
          "equip is refused for an item that is not in the bag (an empty slot, a slot that is out of range, a different "
          "item instance), a quantity that is not the quantity of the stack, an item with no slot of its own when the "
          "request names no slot, and an unknown character");
    check(inventory.unequip(kCharacterA, 0).error == ItemError::emptySlot && inventory.unequip(kCharacterA, 50).error == ItemError::badEquipSlot
              && inventory.unequip(kCharacterA, 21).error == ItemError::badEquipSlot && inventory.unequip(kCharacterA, 1, 2).error == ItemError::targetInUse
              && inventory.unequip(kCharacterA, 1, 500).error == ItemError::badSlot
              && inventory.unequip(kCharacterA, 1, std::nullopt, 2).error == ItemError::badQuantity,
          "unequip is refused for a slot with no item, a slot that is out of range, a bag slot that has an item or that "
          "is out of range, and a quantity that is not the quantity of the stack");
    check(inventory.swap_equipped(kCharacterA, 24, 1).error == ItemError::slotRefusesItem
              && inventory.swap_equipped(kCharacterA, 24, 27).error == ItemError::slotRefusesItem
              && inventory.swap_equipped(kCharacterA, 24, 24).error == ItemError::sameSlot
              && inventory.swap_equipped(kCharacterA, 2, 3).error == ItemError::emptySlot
              && inventory.swap_equipped(kCharacterA, 24, 50).error == ItemError::badEquipSlot
              && inventory.swap_equipped(kCharacterA, 24, 25, &chestId, nullptr).error == ItemError::wrongInstance,
          "swap is refused when an item is not an item for its new slot, for one slot two times, for two slots with no "
          "item, for a slot that is out of range, and for a different item instance");
    host.steps(2, {&a});
    check(rows_text(host.store, kCharacterA) == rows && equipment_text(alpha->equipment) == worn && follow(doll, a, from) == 0,
          "no request that was refused changed a row or the entity, and the session got no paperdoll fragment");

    // An equip into a slot that has an item: the two items change places.
    inventory.give(kCharacterA, "TestChest", 1);
    const world::InstanceId secondChest = bag_instance(host, kCharacterA, 0);
    result = inventory.equip(kCharacterA, 0);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && bag_instance(host, kCharacterA, 0) == chestId && worn_instance(host, kCharacterA, 1) == secondChest
              && doll.items.at(1).instance == secondChest && alpha->bag.slots[0].action == world::sync::kItemUnequipped,
          "equip into a slot that has an item: the new item is in the slot, and the old item is in the slot of the bag");

    result = inventory.swap_equipped(kCharacterA, 24, 25);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && doll.text() == "1:2 5:7 24:8 25:1 27:5 " && doll.lastSlots == (std::vector<std::uint32_t>{24, 25}) && !doll.fault,
          "swap of the weapon slots 24 and 25: the sword and the bow change places");
    result = inventory.swap_equipped(kCharacterA, 26, 25);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && doll.text() == "1:2 5:7 24:8 26:1 27:5 " && doll.lastSlots == (std::vector<std::uint32_t>{25, 26})
              && doll.lastActions == (std::vector<std::uint32_t>{world::sync::kItemUnequipped, world::sync::kItemEquipped})
              && doll.bits(doll.sheathe) == "24 26 27 ",
          "swap with a slot that has no item: the sword moves from slot 25 to slot 26 (the entry of slot 25 has no item, "
          "cause 5), and the sheathe bits follow");

    result = inventory.unequip(kCharacterA, 1);
    host.steps(2, {&a});
    const GearUpdates removal = gear_updates(a, from);
    follow(doll, a, from);
    check(result.ok() && result.slots == std::vector<std::uint32_t>{1} && doll.text() == "5:7 24:8 26:1 27:5 "
              && removal.records.size() == 1 && removal.records[0] == "(62,3183) (61,1755) " && bag_instance(host, kCharacterA, 1) == secondChest
              && doll.lastActions == std::vector<std::uint32_t>{world::sync::kItemUnequipped} && !doll.fault,
          "unequip with no bag slot: the chest armor goes into the first empty slot of the bag (1), and the client gets "
          "the entry of slot 1 with no item");
    result = inventory.unequip(kCharacterA, 24, 9);
    host.steps(2, {&a});
    follow(doll, a, from);
    check(result.ok() && result.slots == (std::vector<std::uint32_t>{7, 8, 9}) && alpha->bag.slots[9].item->itemId == "TestBow"
              && doll.text() == "5:7 26:1 27:5 " && doll.bits(doll.active) == "26 " && doll.bits(doll.sheathe) == "26 27 ",
          "unequip into a named bag slot (9): the bag gets the slot; weapon 3 is now the first weapon, thus the active one");

    // A character that is not in the world: the rows change.
    inventory.give(kCharacterB, "TestLegs", 1);
    result = inventory.equip(kCharacterB, 0);
    check(result.ok() && !result.inWorld && rows_text(host.store, kCharacterB) == "2:3:TestLegs:1 ",
          "equip for a character that is not in the world changes its rows");
    result = inventory.unequip(kCharacterB, 3);
    check(result.ok() && !result.inWorld && rows_text(host.store, kCharacterB) == "1:0:TestLegs:1 ",
          "unequip for a character that is not in the world changes its rows");

    ServerHost bare;
    const Player gamma = add_player(bare.store, kSteamIdA, "Tester A", kCharacterC, "Gamma", std::nullopt);
    check(bare.world.inventory().equip(kCharacterC, 0).error == ItemError::noCatalog
              && bare.world.inventory().swap_equipped(kCharacterC, 24, 25).error == ItemError::noCatalog,
          "with no item catalog the backend equips no item (the class of an item comes from the catalog)");
}

/** G3: the requests of a client for its equipment, with a correct and with a wrong body, and the failure reply. */
void client_requests(const Options&) {
    using namespace server::rep;
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    world::Inventory& inventory = host.world.inventory();
    // Bag of A: 0 sword, 1 chest, 2 legs, 3 ore, 4 bow. Bag of B: 0 chest.
    for (const char* item : {"TestSword", "TestChest", "TestLegs"}) {
        inventory.give(kCharacterA, item, 1);
    }
    inventory.give(kCharacterA, "TestOre", 30);
    inventory.give(kCharacterA, "TestBow", 1);
    inventory.give(kCharacterB, "TestChest", 1);
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    const world::InstanceId swordId = bag_instance(host, kCharacterA, 0);
    const world::InstanceId chestId = bag_instance(host, kCharacterA, 1);
    const world::InstanceId legsId = bag_instance(host, kCharacterA, 2);
    const world::InstanceId oreId = bag_instance(host, kCharacterA, 3);
    const world::InstanceId bowId = bag_instance(host, kCharacterA, 4);
    const world::InstanceId chestOfB = bag_instance(host, kCharacterB, 0);
    host.steps(2, {&a, &b});
    ClientPaperdoll doll;
    std::size_t from = 0;
    follow(doll, a, from);
    const auto send = [&](FakeClient& client, const world::Entity* route, std::uint32_t type, const Bytes& body) {
        host.datagram(client.peer, client.component_message(type, route->actor.bytes, body));
        host.steps(2, {&a, &b});
    };

    check(equip_body(alpha->id, 0, 24, swordId).size() == 45 && unequip_body(alpha->id, 3).size() == 29
              && swap_body(alpha->id, 25, 24, {}, swordId).size() == 57 && read_equip_request(equip_body(alpha->id, 0, 24, swordId)).has_value()
              && read_unequip_request(unequip_body(alpha->id, 3)).has_value() && read_swap_request(swap_body(alpha->id, 25, 24, {}, swordId)).has_value(),
          "the bodies of the tests have the sizes of the writers of the client: equip 45 bytes, unequip 29 bytes, swap 57 bytes");
    // The bodies of the live client (stage 1.4): an equip of the item of bag slot 0 in slot 24,
    // an unequip of slot 3 to the first free bag slot, and a swap of the slots 25 and 24.
    const world::InstanceId liveId = {0x99, 0x81, 0xB7, 0x22, 0x7A, 0x47, 0xFB, 0xE0, 0x54, 0xA8, 0x6A, 0x21, 0x7E, 0x28, 0x22, 0x42};
    const auto liveEquip = read_equip_request(equip_body(alpha->id, 0, 24, liveId, 1, 0));
    const auto liveUnequip = read_unequip_request(unequip_body(alpha->id, 3, 0xFFFFFFFF, 1, 1));
    check(hex_text(equip_body(alpha->id, 0, 24, liveId, 1, 0)).substr(32) == "0000000000000018000000019981b7227a47fbe054a86a217e28224200"
              && liveEquip && liveEquip->bagSlot == 0 && liveEquip->paperdollSlot == 24 && liveEquip->quantity == 1 && liveEquip->instance == liveId
              && hex_text(unequip_body(alpha->id, 3, 0xFFFFFFFF, 1, 1)).substr(32) == "00000003ffffffff0000000101" && liveUnequip
              && liveUnequip->paperdollSlot == 3 && liveUnequip->bagSlot == -1 && liveUnequip->flag,
          "the readers read the bodies of the live client: equip (bag slot 0, slot 24, quantity 1, the instance id, flag "
          "0) and unequip (slot 3, bag slot -1, quantity 1, flag 1)");

    // Equip (type 3895).
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 0, 24, swordId));
    follow(doll, a, from);
    check(rows_text(host.store, kCharacterA) == "1:1:TestChest:1 1:2:TestLegs:1 1:3:TestOre:30 1:4:TestBow:1 2:24:TestSword:1 "
              && doll.text() == "24:1 " && !doll.fault && doll.bits(doll.active) == "24 " && gear_updates(a).fails.empty()
              && log_count("rep: equip request (type 3895, bag slot 0, paperdoll slot 24): done [s1 aaaaaaaa]") == 1,
          "equip request with a correct body: the sword is in slot 24 (row and client), the log has the line, and the "
          "client gets no reply message");
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 1, 1, chestId));
    follow(doll, a, from);
    check(doll.text() == "1:2 24:1 " && equipment_text(alpha->equipment) == "1:TestChest 24:TestSword ", "a second equip request: the chest armor is in slot 1");

    const std::string rowsA = rows_text(host.store, kCharacterA);
    const std::string rowsB = rows_text(host.store, kCharacterB);
    std::size_t messagesA = a.got.size();
    const std::size_t messagesB = b.got.size();
    const auto fails = [&] { return gear_updates(a, messagesA).fails.size(); };
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 3, 1, oreId, 30));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 0, legsId));
    check(log_count("rep: equip request (type 3895, bag slot 3, paperdoll slot 1): the slot of the paperdoll does not take this item: message \"equip item fail\" sent") == 1
              && log_count("rep: equip request (type 3895, bag slot 2, paperdoll slot 0): the slot of the paperdoll does not take this item") == 1
              && fails() == 2,
          "an equip request for a slot that does not take the item (ore in the chest slot, leg armor in the head slot) "
          "is refused, with the failure reply");
    const GearUpdates refused = gear_updates(a, messagesA);
    server::hub::Writer expected;
    expected.u64(facet_of(alpha->id));
    expected.u64(alpha->id);
    expected.u32(0);
    check(refused.fails.size() == 2 && refused.fails[0] == expected.data() && refused.fails[0].size() == 20 && refused.fails[1] == expected.data(),
          "the failure reply is the message type 3626 with 20 bytes: the facet target and the entity of the request, and "
          "the reason 0");
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 0, 24, swordId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 3, chestId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 3, chestOfB));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 9, 3, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 0xFFFFFFFF, 3, legsId));
    check(log_count("rep: equip request (type 3895, bag slot 0, paperdoll slot 24): the slot has no item") == 1
              && log_count("rep: equip request (type 3895, bag slot 2, paperdoll slot 3): the slot has a different item instance") == 2
              && log_count("rep: equip request (type 3895, bag slot 9, paperdoll slot 3): the slot number is not a slot of the bag") == 1
              && log_count("rep: equip request (type 3895, bag slot -1, paperdoll slot 3): the slot number is not a slot of the bag") == 1
              && fails() == 7,
          "an equip request for an item that is not in the bag of the character is refused: an empty slot, the instance "
          "of a different slot, the instance of an item of a different character, a slot after the end, and the slot -1");
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 50, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 51, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 22, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 52, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 0xFFFFFFFF, legsId));
    check(log_count("the slot number is not a slot of the paperdoll: message \"equip item fail\" sent") == 3
              && log_count("rep: equip request (type 3895, 45 bytes) does not have the form of the request: no change, message \"equip item fail\" (type 3626) sent") == 2
              && fails() == 12,
          "an equip request for a slot that is out of range is refused: 50, 51, and 22 are not slots of the paperdoll, "
          "and the reader refuses a number above 51");
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 3, legsId, 2));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 3, legsId, 10001));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 502, 3, legsId));
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 2, 3, legsId, 1, 2));
    Bytes shortEquip = equip_body(alpha->id, 2, 3, legsId);
    shortEquip.pop_back();
    send(a, alpha, kTypeEquipItem, shortEquip);
    Bytes longEquip = equip_body(alpha->id, 2, 3, legsId);
    longEquip.push_back(0);
    send(a, alpha, kTypeEquipItem, longEquip);
    check(log_count("rep: equip request (type 3895, bag slot 2, paperdoll slot 3): the quantity is not in the permitted range") == 1
              && log_count("rep: equip request (type 3895, 45 bytes) does not have the form of the request") == 5
              && log_count("rep: equip request (type 3895, 44 bytes) does not have the form of the request") == 1
              && log_count("rep: equip request (type 3895, 46 bytes) does not have the form of the request") == 1 && fails() == 18,
          "an equip request is refused for a quantity that is not the quantity of the stack, a quantity above 10000, a "
          "bag slot above 501, a flag that is not 0 or 1, a body that is too short, and a body with a byte after its end");
    // The character of a request is the character of the session.
    send(a, alpha, kTypeEquipItem, equip_body(beta->id, 0, 1, chestOfB));
    send(a, beta, kTypeEquipItem, equip_body(beta->id, 0, 1, chestOfB));
    send(a, alpha, kTypeUnequipItem, unequip_body(beta->id, 1));
    send(a, alpha, kTypeSwapItems, swap_body(beta->id, 24, 25, {}, {}));
    Bytes noCall(10, 0);
    send(a, alpha, kTypeEquipItem, noCall);
    check(log_count("rep: equip request (type 3895) names the entity 544f5a4100000004, which is not the entity of the session: no change [s1 aaaaaaaa]") == 2
              && log_count("rep: unequip request (type 3896) names the entity 544f5a4100000004, which is not the entity of the session") == 1
              && log_count("rep: swap request (type 3900) names the entity 544f5a4100000004, which is not the entity of the session") == 1
              && log_count("rep: equip request (type 3895, 10 bytes) does not have the form of the request: no change [s1 aaaaaaaa]") == 1
              && fails() == 18 && gear_updates(b, messagesB).fails.empty(),
          "a request of session A that names the entity of B is refused with no reply, also with the route of that "
          "entity; a body with no target and no entity gets no reply; session B gets no failure reply");

    // Unequip (type 3896).
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 0));
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 50));
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 1, 2));
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 1, 0xFFFFFFFF, 2));
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 52));
    Bytes shortUnequip = unequip_body(alpha->id, 1);
    shortUnequip.pop_back();
    send(a, alpha, kTypeUnequipItem, shortUnequip);
    check(log_count("rep: unequip request (type 3896, paperdoll slot 0, bag slot -1): the slot has no item: message \"equip item fail\" sent") == 1
              && log_count("rep: unequip request (type 3896, paperdoll slot 50, bag slot -1): the slot number is not a slot of the paperdoll") == 1
              && log_count("rep: unequip request (type 3896, paperdoll slot 1, bag slot 2): the target slot has an item") == 1
              && log_count("rep: unequip request (type 3896, paperdoll slot 1, bag slot -1): the quantity is not in the permitted range") == 1
              && log_count("rep: unequip request (type 3896, 29 bytes) does not have the form of the request") == 1
              && log_count("rep: unequip request (type 3896, 28 bytes) does not have the form of the request") == 1 && fails() == 24,
          "an unequip request is refused for a slot with no item, a slot that is out of range (50, and 52 in the "
          "reader), a bag slot that has an item, a wrong quantity, and a body that is too short; each one gets the "
          "failure reply");

    // Swap (type 3900).
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 1, 24, chestId, swordId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 25, 24, {}, chestId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 25, 24, swordId, swordId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 25, 26, {}, {}));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 24, 24, swordId, swordId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 50, 24, {}, swordId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 25, 24, {}, swordId, 31));
    check(log_count("rep: swap request (type 3900, paperdoll slots 1 and 24): the slot of the paperdoll does not take this item: message \"equip item fail\" sent") == 1
              && log_count("rep: swap request (type 3900, paperdoll slots 25 and 24): the slot has a different item instance") == 2
              && log_count("rep: swap request (type 3900, paperdoll slots 25 and 26): the slot has no item") == 1
              && log_count("rep: swap request (type 3900, paperdoll slots 24 and 24): the source and the target are the same slot") == 1
              && log_count("rep: swap request (type 3900, paperdoll slots 50 and 24): the slot number is not a slot of the paperdoll") == 1
              && log_count("rep: swap request (type 3900, 57 bytes) does not have the form of the request") == 1 && fails() == 31,
          "a swap request is refused when an item is not an item for its new slot, for a different item instance (also "
          "an instance for a slot with no item), for two slots with no item, for one slot two times, for a slot that is "
          "out of range, and for a last byte above 30");
    host.steps(2, {&a, &b});
    check(rows_text(host.store, kCharacterA) == rowsA && rows_text(host.store, kCharacterB) == rowsB && gear_updates(a, messagesA).fragments.empty()
              && gear_updates(b, messagesB).fragments.empty() && equipment_text(alpha->equipment) == "1:TestChest 24:TestSword ",
          "no request that was refused changed a row or the equipment of A or of B, and no session got a paperdoll fragment");

    from = a.got.size();
    messagesA = a.got.size();
    // The body of the live client for a drag of weapon 1 to weapon 2: the slot of the drop (25) first.
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 25, 24, {}, swordId));
    follow(doll, a, from);
    check(doll.text() == "1:2 25:1 " && rows_text(host.store, kCharacterA) == "1:2:TestLegs:1 1:3:TestOre:30 1:4:TestBow:1 2:1:TestChest:1 2:25:TestSword:1 "
              && log_count("rep: swap request (type 3900, paperdoll slots 25 and 24): done [s1 aaaaaaaa]") == 1 && !doll.fault,
          "swap request with a correct body (the form of the live client: the target slot 25 with no instance, then the "
          "source slot 24 with its instance): the sword is in slot 25");
    send(a, alpha, kTypeEquipItem, equip_body(alpha->id, 4, 24, bowId));
    send(a, alpha, kTypeSwapItems, swap_body(alpha->id, 24, 25, bowId, swordId));
    follow(doll, a, from);
    check(doll.text() == "1:2 24:1 25:8 ", "a swap request for two slots with items: the bow and the sword change places");
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 1));
    follow(doll, a, from);
    check(doll.text() == "24:1 25:8 " && alpha->bag.slots[0].item->itemId == "TestChest"
              && log_count("rep: unequip request (type 3896, paperdoll slot 1, bag slot -1): done [s1 aaaaaaaa]") == 1,
          "unequip request with the bag slot -1: the chest armor goes into the first empty slot of the bag (0)");
    send(a, alpha, kTypeUnequipItem, unequip_body(alpha->id, 25, 7));
    follow(doll, a, from);
    check(doll.text() == "24:1 " && alpha->bag.slots[7].item->itemId == "TestBow" && !doll.fault && gear_updates(a, messagesA).fails.empty(),
          "unequip request with a bag slot: the bow goes into slot 7; no correct request got a failure reply");
    check(host.session(1).message_counts().componentCalls > 40 && !host.session(1).closing() && !host.session(2).closing(),
          "the sessions continue after each wrong request");

    // The research switch for a live check of the failure reply: each request is refused.
    service::RepServerOptions research;
    research.serverVersion = "test-version";
    research.research.refuseEquipment = true;
    ServerHost switched(research);
    set_catalog(switched);
    const Player gamma = add_player(switched.store, kSteamIdA, "Tester A", kCharacterC, "Gamma", std::nullopt);
    FakeClient c(27000);
    switched.enter(c, gamma.registration());
    switched.world.inventory().give(kCharacterC, "TestChest", 1);
    const world::Entity* own = switched.world.find_character(kCharacterC);
    switched.datagram(c.peer, c.component_message(kTypeEquipItem, own->actor.bytes,
                                                  equip_body(own->id, 0, 1, own->bag.slots[0].item->instance)));
    switched.steps(2, {&c});
    check(gear_updates(c).fails.size() == 1 && rows_text(switched.store, kCharacterC) == "1:0:TestChest:1 "
              && log_count("rep: RESEARCH SWITCH: equip request (type 3895) refused with no check") == 1,
          "with the research switch 'refuse-equipment' a correct equip request changes nothing and gets the failure reply");
}

/** G4: two sessions: the owner view and the observer view of the equipment. */
void two_sessions(const Options&) {
    ServerHost host(observing());
    set_catalog(host);
    const TwoPlayers players(host);
    world::Inventory& inventory = host.world.inventory();
    // B has leg armor before its entry: A gets it in the first record of B.
    inventory.give(kCharacterB, "TestLegs", 1);
    inventory.equip(kCharacterB, 0);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    // The first input report with a camera starts the interest scan of a session.
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, kHeight));
    host.steps(60, {&a, &b});
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "each of the two sessions observes the entity of the other character");
    ClientPaperdoll ownA, ownB, bOfA, aOfB;
    std::size_t fromOwnA = 0, fromOwnB = 0, fromBOfA = 0, fromAOfB = 0;
    follow(ownA, a, fromOwnA, 1);
    follow(ownB, b, fromOwnB, 1);
    const std::size_t firstOfA = follow(bOfA, b, fromBOfA, 2);
    const GearUpdates firstOfB = gear_updates(a, 0, 2);
    follow(aOfB, a, fromAOfB, 2);
    check(ownB.local && ownB.text() == "3:3 " && !aOfB.local && aOfB.text() == "3:3 " && aOfB.looks.count(3) == 1 && aOfB.items.empty()
              && !aOfB.fault && !aOfB.lastOwnerFields && firstOfB.records.size() == 1
              && firstOfB.records[0] == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) (62,3183) ",
          "B has leg armor at its entry: its own session gets the item (owner view), and session A gets the look of slot "
          "3 in the first record of B (observer view: 7 fragments, no field of group 1 of the paperdoll)");
    check(firstOfA == 0 && bOfA.text().empty(), "A has no equipment: session B gets no paperdoll fragment in the record of A");

    // An action of A.
    for (const char* item : {"TestChest", "TestSword", "TestAmulet"}) {
        inventory.give(kCharacterA, item, 1);
    }
    host.steps(2, {&a, &b});
    fromOwnA = a.got.size();
    fromBOfA = b.got.size();
    fromOwnB = b.got.size();
    inventory.equip(kCharacterA, 0);
    inventory.equip(kCharacterA, 1);
    host.steps(2, {&a, &b});
    const GearUpdates seenByB = gear_updates(b, fromBOfA, 2);
    follow(ownA, a, fromOwnA, 1);
    follow(bOfA, b, fromBOfA, 2);
    check(ownA.text() == "1:2 24:1 " && ownA.items.at(1).instance == worn_instance(host, kCharacterA, 1) && !ownA.fault,
          "A equips a chest armor and a sword: session A gets the two items with their instance ids (owner view)");
    check(bOfA.text() == "1:2 24:1 " && bOfA.items.empty() && bOfA.looks.at(1).index == kChestIndex && bOfA.looks.at(24).index == kSwordIndex
              && bOfA.bits(bOfA.sheathe) == "24 " && bOfA.bits(bOfA.active) == "24 " && !bOfA.fault && !bOfA.lastOwnerFields
              && seenByB.records.size() == 1 && seenByB.records[0] == "(62,3183) ",
          "session B gets the observer view of A: a record with only the paperdoll fragment, the looks of the slots 1 and "
          "24, the maps, and no item list, no durability, and no isLocalPlayer");
    check(follow(ownB, b, fromOwnB, 1) == 0 && ownB.text() == "3:3 " && equipment_text(beta->equipment) == "3:TestLegs "
              && rows_text(host.store, kCharacterB) == "2:3:TestLegs:1 ",
          "the action of A changes nothing of B: its equipment, its rows, and what its own session has");
    bool leak = false;
    for (const Sent& sent : b.got) {
        if (sent.type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(sent.body).records) {
            if (record.interest != 2) {
                continue;
            }
            leak = leak || fragment_of(record, 60) != nullptr || fragment_of(record, 61) != nullptr;
            const Fragment* paperdoll = fragment_of(record, 62);
            for (unsigned field = 0; paperdoll != nullptr && field < 12; ++field) {
                leak = leak || paperdoll->fields.count({1, field}) != 0;
            }
        }
    }
    check(!leak, "no record of A for session B has a fragment of the bag or of the coin, or a field of group 1 of the paperdoll");

    // An item that does not show on the body: no observer gets it.
    inventory.equip(kCharacterA, 2);
    host.steps(2, {&a, &b});
    follow(ownA, a, fromOwnA, 1);
    check(ownA.text() == "1:2 5:7 24:1 " && follow(bOfA, b, fromBOfA, 2) == 0 && bOfA.text() == "1:2 24:1 ",
          "A equips an amulet: session A gets it, and session B gets no fragment (the slot has no look)");

    // A request of B for the equipment of A.
    const std::string rowsA = rows_text(host.store, kCharacterA);
    host.datagram(b.peer, b.component_message(server::rep::kTypeUnequipItem, alpha->actor.bytes, unequip_body(alpha->id, 1)));
    host.datagram(b.peer, b.component_message(server::rep::kTypeEquipItem, beta->actor.bytes,
                                              equip_body(beta->id, 0, 24, worn_instance(host, kCharacterA, 24))));
    host.steps(2, {&a, &b});
    check(rows_text(host.store, kCharacterA) == rowsA && equipment_text(alpha->equipment) == "1:TestChest 5:TestAmulet 24:TestSword "
              && follow(ownA, a, fromOwnA, 1) == 0 && gear_updates(a).fails.empty() && gear_updates(b).fails.size() == 1,
          "a request of session B for the entity of A, and a request of B that names an item of A, change nothing of A; "
          "session A gets no message for them");

    // An action of B: A gets the observer view of it.
    inventory.unequip(kCharacterB, 3);
    host.steps(2, {&a, &b});
    follow(ownB, b, fromOwnB, 1);
    follow(aOfB, a, fromAOfB, 2);
    check(ownB.text().empty() && aOfB.text().empty() && aOfB.lastSlots == std::vector<std::uint32_t>{3} && !aOfB.fault
              && follow(ownA, a, fromOwnA, 1) == 0 && ownA.text() == "1:2 5:7 24:1 ",
          "B takes its leg armor off: session A gets slot 3 of B with the item index 0, and the equipment of A does not change");

    // An entity with no session: each session gets its gear in its first record.
    const Player gamma = add_player(host.store, "76561202255233010", "Tester C", kCharacterC, "Gamma", std::nullopt);
    inventory.give(kCharacterC, "TestChest", 1);
    inventory.give(kCharacterC, "TestBow", 1);
    inventory.equip(kCharacterC, 0);
    inventory.equip(kCharacterC, 1);
    const std::size_t beforeSpawn = a.got.size();
    check(host.world.spawn_character(kCharacterC, state::WorldPosition{101.0F, 201.0F, kHeight}) == world::SpawnError::none,
          "a third character with equipment comes into the world as an entity with no session");
    host.steps(60, {&a, &b});
    ClientPaperdoll aOfC;
    std::size_t fromAOfC = beforeSpawn;
    const GearUpdates spawned = gear_updates(a, beforeSpawn, 3);
    follow(aOfC, a, fromAOfC, 3);
    check(spawned.records.size() == 1 && spawned.records[0] == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) (62,3183) "
              && aOfC.text() == "1:2 24:8 " && aOfC.bits(aOfC.sheathe) == "24 " && !aOfC.fault && !aOfC.local,
          "session A gets the record of the entity with its gear: 7 fragments, the looks of the chest armor and of the bow");
}

struct Sink : world::SessionSink {
    bool lost{false};
    void character_lost(world::LostCause) override {
        lost = true;
    }
};

/** G5: the equipment after a new entry, in a new world object, and a write that fails. */
void persistence(const Options& options) {
    {
        ServerHost host;
        set_catalog(host);
        const TwoPlayers players(host);
        world::Inventory& inventory = host.world.inventory();
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        for (const char* item : {"TestChest", "TestSword", "TestOre", "TestAmulet"}) {
            inventory.give(kCharacterA, item, 1);
        }
        inventory.equip(kCharacterA, 0);
        inventory.equip(kCharacterA, 1);
        inventory.equip(kCharacterA, 3);
        const world::InstanceId chestId = worn_instance(host, kCharacterA, 1);
        host.steps(2, {&a});
        host.datagram(a.peer, a.carrier_close());
        host.steps(2, {&a});
        check(host.world.entities().empty() && rows_text(host.store, kCharacterA) == "1:2:TestOre:1 2:1:TestChest:1 2:5:TestAmulet:1 2:24:TestSword:1 ",
              "the character leaves the world: its rows have the three items of the paperdoll (container 2) and the ore of the bag");

        FakeClient again(27002);
        host.enter(again, players.alpha.registration());
        ClientPaperdoll doll;
        std::size_t from = 0;
        const GearUpdates first = gear_updates(again);
        follow(doll, again, from);
        check(first.fragments.size() == 1 && first.records[0] == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) (27,129) (62,3183) (60,3786) (61,1755) (65,911) (67,3681) "
                  && doll.text() == "1:2 5:7 24:1 " && doll.local && !doll.fault && doll.items.at(1).instance == chestId
                  && doll.items.at(1).action == world::sync::kLoad && doll.bits(doll.sheathe) == "24 " && doll.bits(doll.active) == "24 "
                  && log_count("has 1 item(s) in 3 bag slot(s), 3 item(s) in its paperdoll") == 1,
              "a new session of the character gets the equipment in its first record (cause 0x11, the same instance ids, "
              "the maps of the sword), and the log has the count");

        // A second world object on the same database: a new backend process.
        ManualClock clock;
        world::World second(clock, host.store, host.tables, ServerHost::with_game_data(host.catalog, host.progression));
        Sink sink;
        const world::EnterResult entered = second.enter(1, sink, kCharacterA, players.alpha.account.personaId);
        check(entered.error == world::EnterError::none && equipment_text(entered.entity->equipment) == "1:TestChest 5:TestAmulet 24:TestSword "
                  && entered.entity->equipment.item(1)->instance == chestId && entered.entity->equipment.item(1)->index == kChestIndex
                  && entered.entity->bag.slots.size() == 3 && entered.entity->bag.slots[2].item->itemId == "TestOre",
              "a new world object on the same database has the equipment and the bag of the character");
    }

    // A move between the bag and the paperdoll is one transaction: a write that fails changes nothing.
    const std::filesystem::path path = options.workDirectory / "equipment-rollback.sqlite3";
    std::filesystem::remove(path);
    state::Store store;
    const bool opened = store.open(path.string());
    const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    ManualClock clock;
    gamedata::AppearanceTables tables;
    gamedata::ItemCatalog catalog;
    catalog.set(test_items());
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &catalog;
    world::World world(clock, store, tables, worldOptions);
    Sink sink;
    const bool entered = world.enter(1, sink, kCharacterA, player.account.personaId).error == world::EnterError::none;
    world.inventory().give(kCharacterA, "TestChest", 1);
    world.inventory().give(kCharacterA, "TestLegs", 1);
    const bool worn = world.inventory().equip(kCharacterA, 1).ok();
    sqlite3* connection = nullptr;
    sqlite3_open(path.string().c_str(), &connection);
    const bool trigger = sqlite3_exec(connection,
                                      "CREATE TRIGGER refuse_paperdoll BEFORE INSERT ON items WHEN NEW.container=2 AND NEW.slot=1 "
                                      "BEGIN SELECT RAISE(FAIL, 'test write failure'); END",
                                      nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    check(opened && entered && worn && trigger, "a database file with a character that has leg armor on and a chest armor in its bag");
    const world::Entity* entity = world.find_character(kCharacterA);
    const world::Revision before = world.revision();
    const world::ItemResult failed = world.inventory().equip(kCharacterA, 0);
    check(failed.error == ItemError::storeFailed && rows_text(store, kCharacterA) == "1:0:TestChest:1 2:3:TestLegs:1 "
              && entity->bag.slots[0].item && equipment_text(entity->equipment) == "3:TestLegs " && world.revision() == before,
          "an equip whose write fails: the two rows stay as they were (the item is in the bag), the entity does not "
          "change, and no state has a mark");
    const world::ItemResult unequipped = world.inventory().unequip(kCharacterA, 3);
    check(unequipped.ok() && rows_text(store, kCharacterA) == "1:0:TestChest:1 1:1:TestLegs:1 ", "a later change that the database accepts works");
}

/** G6: the items of the archetype for a new character. */
void archetype_items(const Options&) {
    using nlohmann::json;
    ServerHost host;
    set_catalog(host);
    // The entries of a backstory row of the game: the head has the quantity 0.
    host.catalog.set_backstory("Archetype_Test", {{"TestHead", 0, 0},     {"TestChest", 1, 0},  {"TestHands", 1, 0},
                                                   {"TestLegs", 1, 0},     {"TestFeet", 1, 0},   {"TestSword", 1, 0},
                                                   {"TestBow", 1, 0},      {"TestSword", 1, 0},  {"TestShield", 0, 0},
                                                   {"TestOre", 130, 0},    {"NoSuchItem", 1, 0}, {"TestAmulet", 1, 0}});
    const auto text_of_rows = [](const std::vector<state::ItemRow>& rows) {
        std::string text;
        for (const state::ItemRow& row : rows) {
            text += std::to_string(row.container) + ":" + std::to_string(row.slot) + ":" + row.itemId + ":" + std::to_string(row.quantity) + " ";
        }
        return text;
    };
    const std::vector<state::ItemRow> rows = world::start_items(host.catalog, "archetype_test");
    std::set<std::string> instances;
    for (const state::ItemRow& row : rows) {
        instances.insert(row.instanceId);
    }
    check(text_of_rows(rows) == "2:1:TestChest:1 2:2:TestHands:1 2:3:TestLegs:1 2:4:TestFeet:1 2:24:TestSword:1 2:25:TestBow:1 "
                                "1:0:TestSword:1 1:1:TestOre:100 1:2:TestOre:30 2:5:TestAmulet:1 "
              && instances.size() == rows.size() && rows[0].instanceId.size() == 32 && rows[0].durability == 250 && rows[0].gearScore == 60
              && rows[0].flags == world::kItemBound,
          "the start items of an archetype: each armor piece and the first two weapons are in their slots of the "
          "paperdoll, a third weapon and the ore (two stacks) are in the bag; an entry with the quantity 0 and an "
          "unknown item give no row; each row has its own instance id and the values of the item table");
    check(world::start_items(host.catalog, "Archetype_None").empty() && world::start_items(host.catalog, "").empty()
              && world::start_items(gamedata::ItemCatalog{}, "Archetype_Test").empty(),
          "an archetype that the catalog does not have, a character with no archetype, and a backend with no catalog "
          "give no start item");

    // The create route gives a new character the items of its archetype. The catalog has the
    // list of the backstories that the character creation can send.
    host.catalog.set_creation_backstories({"Archetype_Test", "Archetype_None"});
    state::AccountOrigin origin{};
    const auto account = host.store.login_account(kSteamIdA, "Tester A", false, origin);
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_gateway_routes(router, services);
    const auto create = [&](const std::string& name, const std::string& archetype) {
        state::AppearanceNames appearance;
        appearance.fill("none");
        appearance[state::kAppearanceGender] = "Male";
        service::HttpRequest request;
        request.method = "POST";
        request.host = "gateway.test";
        request.path = "/prod/game/worlds/" + state::local_world().worldId + "/characters";
        request.target = request.path;
        request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", account->personaId}}.dump()));
        request.body = json{{"Name", name},
                            {"CharacterCreationParams", state::make_creation_data(name, appearance, state::local_world().worldId, archetype)}}
                           .dump();
        std::string route;
        return router.dispatch(request, route);
    };
    service::HttpResponse reply = create("Starter", "Archetype_Test");
    const std::string createdId = reply.status == 200 ? json::parse(reply.body)["Character"]["CharacterId"].get<std::string>() : std::string{};
    const auto created = host.store.find_character(createdId);
    check(reply.status == 200 && created && created->archetype == "Archetype_Test"
              && rows_text(host.store, createdId)
                     == "1:0:TestSword:1 1:1:TestOre:100 1:2:TestOre:30 2:1:TestChest:1 2:2:TestHands:1 2:3:TestLegs:1 2:4:TestFeet:1 "
                        "2:5:TestAmulet:1 2:24:TestSword:1 2:25:TestBow:1 "
              && log_count("archetype Archetype_Test, creation data") == 1 && log_count("10 start item(s)") == 1,
          "the create route with the creation data of an archetype: the character has the rows of its start items");
    reply = create("Plain", "Archetype_None");
    const std::string plainId = reply.status == 200 ? json::parse(reply.body)["Character"]["CharacterId"].get<std::string>() : std::string{};
    check(reply.status == 200 && host.store.find_character(plainId) && host.store.list_items(plainId).empty()
              && log_count("0 start item(s) (the item catalog has no items for this archetype") == 1,
          "the create route with a backstory of the creation that has no items in the catalog: the character is made "
          "with no item, and the log says so");

    // The new character enters the world with its clothing.
    FakeClient client(27000);
    host.enter(client, Registration{createdId, account->personaId, kSteamIdA, "Tester A"});
    ClientPaperdoll doll;
    std::size_t from = 0;
    follow(doll, client, from);
    const world::Entity* entity = host.world.find_character(createdId);
    check(entity != nullptr && doll.local && doll.text() == "1:2 2:10 3:3 4:11 5:7 24:1 25:8 " && !doll.fault && doll.bits(doll.active) == "24 "
              && doll.bits(doll.sheathe) == "24 25 " && entity->bag.slots.size() == 3,
          "the new character enters the world: its session gets the clothing, the amulet, and the two weapons in the "
          "first record");

    // A start item that the database refuses: no character with a part of its items.
    const std::size_t characters = host.store.list_characters(account->personaId).size();
    state::Character broken;
    broken.characterId = "eeeeeeee-0000-4000-8000-00000000000e";
    broken.personaId = account->personaId;
    broken.worldId = state::local_world().worldId;
    broken.name = "Broken";
    broken.createdDate = "2026-01-01T00:00:00.000Z";
    broken.modifiedDate = broken.createdDate;
    std::vector<state::ItemRow> twice = world::start_items(host.catalog, "Archetype_Test");
    twice[1].slot = twice[0].slot; // Two items for one slot.
    check(host.store.insert_character(broken, twice) == state::InsertResult::failed && !host.store.find_character(broken.characterId)
              && host.store.list_items(broken.characterId).empty() && host.store.list_characters(account->personaId).size() == characters,
          "an insert whose item rows the database refuses makes no character and no item row (one transaction)");
}

/** G7: the development routes for the equipment. */
void equipment_routes(const Options&) {
    using nlohmann::json;
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_dev_routes(router, services);
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& serviceHost = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        request.host = serviceHost;
        std::string route;
        return router.dispatch(request, route);
    };
    const std::string forA = std::string("character=") + kCharacterA;
    const std::string forB = std::string("character=") + kCharacterB;
    for (const char* item : {"TestChest", "TestSword", "TestOre"}) {
        host.world.inventory().give(kCharacterA, item, 1);
    }
    host.world.inventory().give(kCharacterB, "TestLegs", 1);

    service::HttpResponse reply = send("POST", "/v1/dev/items/equip", forA + "&from=0");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "world" && json::parse(reply.body)["slots"] == json::array({0})
              && json::parse(reply.body)["equipmentSlots"] == json::array({1}),
          "equip route with no slot, for a character that is in the world: reply 200 with the slot of the bag and the "
          "slot of the paperdoll that changed");
    reply = send("POST", "/v1/dev/items/equip", forA + "&from=1&slot=25");
    check(reply.status == 200 && json::parse(reply.body)["equipmentSlots"] == json::array({25}), "equip route with a slot: the sword is in slot 25");
    reply = send("POST", "/v1/dev/items/equip", forB + "&from=0");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "stored" && rows_text(host.store, kCharacterB) == "2:3:TestLegs:1 ",
          "equip route for a character that is not in the world: applied to the rows");
    reply = send("GET", "/v1/dev/items", forA);
    const json list = json::parse(reply.body);
    check(reply.status == 200 && list["items"].size() == 1 && list["equipment"].size() == 2 && list["equipment"][0]["slot"] == 1
              && list["equipment"][0]["item"] == "TestChest" && list["equipment"][1]["slot"] == 25 && list["equipment"][1]["item"] == "TestSword"
              && list["equipment"][1]["index"] == kSwordIndex && list["equipment"][1]["instance"].get<std::string>().size() == 32,
          "the list route gives the equipment: each item with its slot of the paperdoll, id, index, and instance id");
    const std::string before = rows_text(host.store, kCharacterA);
    check(send("POST", "/v1/dev/items/equip", forA).status == 400 && send("POST", "/v1/dev/items/equip", forA + "&from=abc").status == 400
              && send("POST", "/v1/dev/items/equip", forA + "&from=2&slot=50").status == 400
              && send("POST", "/v1/dev/items/equip", forA + "&from=2&slot=1").status == 400 && send("POST", "/v1/dev/items/equip", forA + "&from=2").status == 400
              && send("POST", "/v1/dev/items/equip", forA + "&from=0").status == 400 && send("POST", "/v1/dev/items/equip", forA + "&from=2&slot=22").status == 400
              && send("POST", "/v1/dev/items/unequip", forA).status == 400 && send("POST", "/v1/dev/items/unequip", forA + "&slot=0").status == 400
              && send("POST", "/v1/dev/items/unequip", forA + "&slot=50").status == 400 && send("POST", "/v1/dev/items/unequip", forA + "&slot=1&to=2").status == 400
              && send("POST", "/v1/dev/items/unequip", forA + "&slot=1&to=500").status == 400,
          "the routes refuse a request with no slot, a number that is not a whole number, a slot that is out of range or "
          "that does not take the item, an item with no slot of its own, an empty slot, and a bag slot that has an item "
          "(reply 400)");
    check(send("POST", "/v1/dev/items/equip", std::string("character=") + kCharacterNone + "&from=0").status == 404
              && send("POST", "/v1/dev/items/unequip", std::string("character=") + kCharacterNone + "&slot=1").status == 404
              && send("POST", "/v1/dev/items/equip", forA + "&from=2&slot=1", "gateway.test").status == 403
              && send("POST", "/v1/dev/items/unequip", forA + "&slot=1", "gateway.test").status == 403 && rows_text(host.store, kCharacterA) == before,
          "an unknown character gives the reply 404, a request with a service host the reply 403, and no request that "
          "was refused changed a row");
    reply = send("POST", "/v1/dev/items/unequip", forA + "&slot=25&to=7");
    check(reply.status == 200 && json::parse(reply.body)["slots"] == json::array({3, 4, 5, 6, 7})
              && send("POST", "/v1/dev/items/unequip", forA + "&slot=1").status == 200
              && rows_text(host.store, kCharacterA) == "1:0:TestChest:1 1:2:TestOre:1 1:7:TestSword:1 ",
          "unequip route with a bag slot and with no bag slot: the items are in the bag again");
}

/**
 * G9: a create request with a backstory that is not a backstory of the character creation
 * (case RV1 of the review of stage 1.4).
 */
void creation_backstory(const Options&) {
    using nlohmann::json;
    const auto create = [](ServerHost& host, const state::Account& account, const std::string& name, const std::string& backstory) {
        server::http::Router router;
        const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
        server::http::register_gateway_routes(router, services);
        state::AppearanceNames appearance;
        appearance.fill("none");
        appearance[state::kAppearanceGender] = "Male";
        service::HttpRequest request;
        request.method = "POST";
        request.host = "gateway.test";
        request.path = "/prod/game/worlds/" + state::local_world().worldId + "/characters";
        request.target = request.path;
        request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", account.personaId}}.dump()));
        request.body = json{{"Name", name},
                            {"CharacterCreationParams", state::make_creation_data(name, appearance, state::local_world().worldId, backstory)}}
                           .dump();
        std::string route;
        return router.dispatch(request, route);
    };
    const auto id_of = [](const service::HttpResponse& reply) {
        return reply.status == 200 ? json::parse(reply.body)["Character"]["CharacterId"].get<std::string>() : std::string{};
    };

    ServerHost host;
    set_catalog(host);
    // The backstory table of the game has rows that are not archetypes of the creation.
    host.catalog.set_backstory("DebugSupplies", {{"TestOre", 99, 0}, {"TestSword", 1, 0}});
    host.catalog.set_backstory("Archetype_Test", {{"TestChest", 1, 0}});
    host.catalog.set_creation_backstories({"Archetype_Test", "Backstory1"});
    state::AccountOrigin origin{};
    const auto account = host.store.login_account(kSteamIdA, "Tester A", false, origin);
    service::HttpResponse reply = create(host, *account, "Reviewone", "DebugSupplies");
    check(reply.status == 400 && json::parse(reply.body)["message"] == "Backstory is not permitted"
              && host.store.list_characters(account->personaId).empty() && !host.store.character_name_exists("Reviewone")
              && log_count("is not a backstory of the character creation") == 1 && log_count("DebugSupplies") == 0,
          "a create request with a backstory that has items and that is not a backstory of the character creation: "
          "reply 400, no character, and the log has the cause with no text of the request");
    reply = create(host, *account, "Reviewtwo", "debugsupplies");
    check(reply.status == 400 && create(host, *account, "Reviewthree", "NoSuchBackstory").status == 400
              && host.store.list_characters(account->personaId).empty(),
          "the same with a different letter case, and with a backstory that the catalog does not have");
    reply = create(host, *account, "Starter", "archetype_test");
    const std::string starter = id_of(reply);
    reply = create(host, *account, "Default", "Backstory1");
    const std::string plain = id_of(reply);
    check(!starter.empty() && rows_text(host.store, starter) == "2:1:TestChest:1 " && !plain.empty() && host.store.list_items(plain).empty(),
          "a backstory of the creation makes the character with its items, and the default backstory of the client, "
          "which has no item, makes a character with no item");

    // A catalog file of an earlier tool has the backstories and no list of the creation.
    ServerHost old;
    set_catalog(old);
    old.catalog.set_backstory("DebugSupplies", {{"TestOre", 99, 0}});
    old.catalog.set_backstory("Archetype_Test", {{"TestChest", 1, 0}});
    const auto other = old.store.login_account(kSteamIdA, "Tester A", false, origin);
    const std::string first = id_of(create(old, *other, "Oldone", "DebugSupplies"));
    const std::string second = id_of(create(old, *other, "Oldtwo", "Archetype_Test"));
    check(!first.empty() && old.store.list_items(first).empty() && !second.empty() && old.store.list_items(second).empty(),
          "with a catalog that has no list of the backstories of the creation, a new character gets no item for any "
          "backstory");
    // No catalog: the documented behavior stays.
    ServerHost bare;
    const auto third = bare.store.login_account(kSteamIdA, "Tester A", false, origin);
    const std::string none = id_of(create(bare, *third, "Bareone", "DebugSupplies"));
    check(!none.empty() && bare.store.list_items(none).empty(), "with no catalog the character is made with no item");
}

/** G10: an unequip into a full bag (case RC1 of the review of stage 1.4). */
void full_bag(const Options&) {
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Inventory& inventory = host.world.inventory();
    const world::Entity* entity = host.world.find_character(kCharacterA);
    check(inventory.give(kCharacterA, "TestSword", 1).ok() && inventory.equip(kCharacterA, 0, 24).ok()
              && inventory.give(kCharacterA, "TestChest", 500).ok(),
          "a character with a sword in weapon slot 1 and an item in each of the 500 slots of its bag");
    const world::Revision revision = host.world.revision();
    const std::string rows = rows_text(host.store, kCharacterA);
    check(inventory.unequip(kCharacterA, 24).error == ItemError::bagFull && entity->equipment.item(24) != nullptr
              && host.world.revision() == revision && rows_text(host.store, kCharacterA) == rows,
          "an unequip into a full bag is refused, and it changes no row and no entity");
    check(inventory.equip(kCharacterA, 0, 1).ok() && inventory.unequip(kCharacterA, 24).ok()
              && host.store.list_items(kCharacterA).size() == 501 && entity->bag.slots[0].item->itemId == "TestSword",
          "an equip makes a slot of the bag free, and the unequip then puts the sword into it");
}

/** G11: each short body of each equipment request (case RC2 of the review of stage 1.4). */
void short_bodies(const Options&) {
    using namespace server::rep;
    const Bytes bodies[] = {equip_body(2, 0, 24, {}), unequip_body(2, 24), swap_body(2, 24, 25, {}, {})};
    bool refused = true;
    for (std::size_t kind = 0; kind < 3; ++kind) {
        for (std::size_t size = 0; size < bodies[kind].size(); ++size) {
            const auto body = std::span<const std::uint8_t>(bodies[kind]).first(size);
            refused = refused && (kind == 0 ? !read_equip_request(body) : kind == 1 ? !read_unequip_request(body) : !read_swap_request(body));
        }
    }
    check(refused && read_equip_request(bodies[0]) && read_unequip_request(bodies[1]) && read_swap_request(bodies[2]),
          "each start of the body of an equip, an unequip, and a swap request that is shorter than the body is refused, "
          "and the full body is read");
}

/** G8: a paperdoll that is larger than one carrier message. */
void large_paperdoll(const Options&) {
    ServerHost host(observing());
    // One item for each slot that has a rule: the class text and the slot.
    const std::vector<std::pair<const char*, std::uint32_t>> slots = {
        {"EquippableHead+Armor", 0}, {"EquippableChest+Armor", 1}, {"EquippableHands+Armor", 2}, {"EquippableLegs+Armor", 3},
        {"EquippableFeet+Armor", 4}, {"EquippableAmulet+Jewelry", 5}, {"EquippableToken+Jewelry", 6}, {"EquippableRing+Jewelry", 7},
        {"Consumable", 14}, {"Consumable", 15}, {"Consumable", 16}, {"Consumable", 17}, {"Weapon+EquippableMainHand+Sword", 24},
        {"Weapon+EquippableTwoHand+Bow", 25}, {"Weapon+EquippableMainHand+Hatchet", 26}, {"Weapon+EquippableOffHand+Shield", 27},
        {"Weapon+EquippableOffHand+Shield", 29}, {"Bag", 30}, {"Bag", 31}, {"Bag", 32}, {"EquippableTool+Axe+LoggingAxe", 33},
        {"EquippableTool+Axe+Sickle", 34}, {"EquippableTool+Axe+SkinningKnife", 35}, {"EquippableTool+Axe+Pickaxe", 36},
        {"EquippableTool+FishingPole", 37}, {"Weapon+EquippableTool+Axe+SoulwardenStaff", 38}, {"EquippableTool+InstrumentFlute", 39},
        {"HeartGem", 45}};
    std::vector<gamedata::ItemDefinition> items;
    std::vector<std::string> names;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        names.push_back("LargeItem" + std::to_string(index));
    }
    for (std::size_t index = 0; index < slots.size(); ++index) {
        items.push_back(gear(names[index].c_str(), "Armor", slots[index].first, 1, 3000, 700));
    }
    host.catalog.set(std::move(items));
    const TwoPlayers players(host);
    world::Inventory& inventory = host.world.inventory();
    bool worn = true;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        worn = worn && inventory.give(kCharacterA, names[index], 1).ok() && inventory.equip(kCharacterA, 0, slots[index].second).ok();
    }
    check(worn && host.store.list_items(kCharacterA).size() == slots.size(), "a character with an item in each of 28 slots of its paperdoll");

    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, kHeight));
    host.steps(60, {&a, &b});
    ClientPaperdoll own;
    std::size_t from = 0;
    const GearUpdates first = gear_updates(a);
    follow(own, a, from);
    std::size_t largest = 0;
    for (const Sent& sent : a.got) {
        largest = std::max(largest, sent.type == 8 ? sent.frame.size() : 0);
    }
    for (const Sent& sent : b.got) {
        largest = std::max(largest, sent.type == 8 ? sent.frame.size() : 0);
    }
    note("the owner got " + std::to_string(first.fragments.size()) + " paperdoll fragments in " + std::to_string(first.bundles)
         + " bundles; the largest state bundle frame of the two sessions has " + std::to_string(largest) + " bytes");
    check(first.fragments.size() >= 2 && first.bundles == first.fragments.size() && own.items.size() == slots.size() && !own.fault && own.local
              && first.complete,
          "the first record of the owner has the paperdoll in more than one fragment, each one in a bundle of its own, "
          "and the client has each of the 28 items after them");
    check(largest <= kCarrierMessage, "each state bundle is one carrier message (1,024 bytes at most)");
    bool allSlots = true;
    for (const auto& [itemClass, slot] : slots) {
        allSlots = allSlots && own.items.count(slot) == 1 && own.items.at(slot).gearScore == 700 && own.items.at(slot).durability == 3000;
    }
    check(allSlots && own.bits(own.sheathe) == "24 25 26 27 29 " && own.bits(own.active) == "24 27 ",
          "each slot has its item with the gear score and the durability, and the maps have each weapon slot");
    ClientPaperdoll seen;
    std::size_t fromB = 0;
    follow(seen, b, fromB, 2);
    check(seen.looks.size() == 10 && seen.items.empty() && !seen.fault && seen.text().rfind("0:1 1:2 2:3 3:4 4:5 24:", 0) == 0,
          "the observer gets only the 10 slots with a look (the armor and the weapons)");
}

} // namespace

std::vector<Case> equipment_cases() {
    return {
        {"G1", "paperdoll state: the slot rules, and the wire bytes for the owner view and for the observer view against the examples", wire_bytes},
        {"G2", "equip, unequip, and swap: the rows, what the session of the owner gets, and each refusal", equip_and_unequip},
        {"G3", "client requests: equip (type 3895), unequip (type 3896), and swap (type 3900) with a correct and with a wrong body; the failure reply (type 3626)", client_requests},
        {"G4", "two sessions: the equipment of A does not change for an action of B, and B gets the observer view of A", two_sessions},
        {"G5", "persistence: the equipment after a new entry, in a new world object, and a write that fails changes nothing", persistence},
        {"G6", "a new character gets the items of its archetype: the rows of the create route, and the first record", archetype_items},
        {"G7", "development routes for the equipment: equip, unequip, the list, and each refusal", equipment_routes},
        {"G9", "a create request with a backstory that is not of the character creation (review case RV1)", creation_backstory},
        {"G10", "an unequip into a full bag, and after an equip made a slot free (review case RC1)", full_bag},
        {"G11", "each short body of each equipment request is refused (review case RC2)", short_bodies},
        {"G8", "a paperdoll that is larger than one carrier message: more than one bundle, each one below the size limit", large_paperdoll},
    };
}

} // namespace soulwarden::backend::tests

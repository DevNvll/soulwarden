// Cases for the inventory (stage 1.3): the item catalog, the wire form of the container state
// and of the currency state, the commands, the bag operations, the requests of a client, two
// sessions, the persistence, and the size limit of a bundle.

#include <algorithm>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/item_catalog.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/item_requests.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/item_decoder.h"
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
constexpr const char* kCharacterNone = "cccccccc-0000-4000-8000-00000000000c";
constexpr float kHeight = 50.0F;
/** Largest number of data bytes of one carrier message (protocol notes 3.2). */
constexpr std::size_t kCarrierMessage = 1024;

// The item indexes of the examples of the research (inventory.md 8.1): numbers of the wire
// examples, with no item table in the repository.
constexpr std::uint32_t kExampleSword = 5278;
constexpr std::uint32_t kExampleOre = 2956;

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(ServerHost& host)
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight})) {}
};

gamedata::ItemDefinition definition(const char* id,
                                    const char* type,
                                    std::uint32_t maxStack,
                                    std::uint32_t durability = 0,
                                    std::uint32_t gearScore = 0,
                                    bool bindOnPickup = false) {
    gamedata::ItemDefinition item;
    item.id = id;
    item.nameKey = std::string("@") + id + "_MasterName";
    item.type = type;
    item.itemClass = type;
    item.maxStack = maxStack;
    item.durability = durability;
    item.minGearScore = gearScore;
    item.maxGearScore = gearScore;
    item.gearScoreOverride = gearScore;
    item.bindOnPickup = bindOnPickup;
    item.weight = 2;
    return item;
}

/** A made-up catalog: the sword has the index 1, the ore 2, the potion 3, the item with no type 4. */
void set_catalog(ServerHost& host) {
    host.catalog.set({definition("TestSword", "Weapon", 1, 500, 100, true), definition("TestOre", "Resource", 100),
                      definition("TestPotion", "Consumable", 10), definition("TestNoType", "", 1)});
}

/** The bag of a client: it applies each Container value as the client does (protocol notes 4.10). */
struct ClientBag {
    std::vector<WireItem> slots;
    /** True when a value did not decode, had an erase entry or a sequence, or set an element after the end. */
    bool fault{false};
    unsigned fulls{0};
    unsigned journals{0};

    void apply(const WireContainer& value) {
        if (!value.ok || value.erases != 0 || value.sequence) {
            fault = true;
            return;
        }
        if (value.full) {
            ++fulls;
            slots.clear();
            for (const auto& [index, item] : value.elements) {
                slots.push_back(item);
            }
            return;
        }
        ++journals;
        for (const auto& [index, item] : value.elements) {
            if (index < slots.size()) {
                slots[index] = item;
            } else if (index == slots.size()) {
                slots.push_back(item);
            } else {
                fault = true;
            }
        }
    }

    [[nodiscard]] unsigned items() const {
        return static_cast<unsigned>(std::count_if(slots.begin(), slots.end(), [](const WireItem& item) { return !item.empty(); }));
    }
};

/** What a client got for the items of one entity from the message `from` on. */
struct ItemUpdates {
    /** Each Container value, in sequence. */
    std::vector<WireContainer> bag;
    /** Each coin amount, in sequence. */
    std::vector<std::uint64_t> coin;
    /** Number of bundles that have a fragment of the bag. */
    unsigned bagBundles{0};
    /** Size of the largest frame of a state bundle. */
    std::size_t largestFrame{0};
    /** True when each bundle decodes to its last byte. */
    bool complete{true};
};

ItemUpdates item_updates(const FakeClient& client, std::size_t from = 0, std::uint32_t interest = 1) {
    ItemUpdates updates;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        updates.complete = updates.complete && bundle.complete;
        updates.largestFrame = std::max(updates.largestFrame, client.got[index].frame.size());
        bool bag = false;
        for (const Record& record : bundle.records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == 61 && fragment.type == 1755 && fragment.fields.count({0, 0}) != 0) {
                    updates.bag.push_back(read_container(fragment.fields.at({0, 0})));
                    bag = true;
                }
                if (fragment.slot == 60 && fragment.type == 3786 && fragment.fields.count({0, 0}) != 0) {
                    server::hub::Reader reader(fragment.fields.at({0, 0}));
                    updates.coin.push_back(reader.u64());
                }
            }
        }
        updates.bagBundles += bag ? 1 : 0;
    }
    return updates;
}

/** Applies the Container values that a client got from the message `from` on to its bag. */
void follow(ClientBag& bag, const FakeClient& client, std::size_t& from) {
    const ItemUpdates updates = item_updates(client, from);
    for (const WireContainer& value : updates.bag) {
        bag.apply(value);
    }
    bag.fault = bag.fault || !updates.complete;
    from = client.got.size();
}

/** @return The rows of a character as text: "slot:item:quantity:flags " for each row. */
std::string rows_text(state::Store& store, const std::string& characterId) {
    std::string text;
    for (const state::ItemRow& row : store.list_items(characterId)) {
        text += std::to_string(row.slot) + ":" + row.itemId + ":" + std::to_string(row.quantity) + ":" + std::to_string(row.flags) + " ";
    }
    return text;
}

/** @return The bag of a client as text, in the form of rows_text, with the item index in place of the id. */
std::string bag_text(const ClientBag& bag) {
    std::string text;
    for (std::size_t slot = 0; slot < bag.slots.size(); ++slot) {
        const WireItem& item = bag.slots[slot];
        if (!item.empty()) {
            text += std::to_string(slot) + ":" + std::to_string(item.index) + ":" + std::to_string(item.quantity) + ":"
                    + std::to_string(item.flags) + " ";
        }
    }
    return text;
}

world::InstanceId instance_at(ServerHost& host, const char* characterId, std::uint32_t slot) {
    const world::Entity* entity = host.world.find_character(characterId);
    if (entity == nullptr || slot >= entity->bag.slots.size() || !entity->bag.slots[slot].item) {
        return {};
    }
    return entity->bag.slots[slot].item->instance;
}

struct DropLine {
    std::uint32_t slot;
    world::InstanceId instance;
    std::uint32_t quantity;
};

/** Body of ItemDropComponentServerFacet_DropInventoryItems (type 1838), from the writer of the client. */
Bytes drop_body(std::uint64_t entity, const std::vector<DropLine>& lines, std::uint32_t count = 0xFFFFFFFF) {
    server::hub::Writer body;
    body.u64(entity + 0x40); // Facet target: the backend reads the entity field, not this value.
    body.u64(entity);
    body.size(count == 0xFFFFFFFF ? static_cast<std::uint32_t>(lines.size()) : count);
    for (const DropLine& line : lines) {
        body.u32(line.slot);
        body.raw(line.instance);
        body.u32(0xFFFFFFFF); // A slot value whose meaning is not known: "none".
        body.u32(line.quantity);
    }
    return body.data();
}

/** Body of ItemRepairComponentServerFacet_RequestSetLockState (type 2549), from the writer of the client. */
Bytes lock_body(std::uint64_t entity, std::uint32_t containerType, std::uint32_t slot, const world::InstanceId& instance,
                std::uint8_t locked) {
    server::hub::Writer body;
    body.u64(entity + 0x41);
    body.u64(entity);
    body.u32(containerType);
    body.u32(slot);
    body.raw(instance);
    body.u32(1);
    body.u8(locked);
    return body.data();
}

/** B1: the catalog file of the tool. */
void catalog_file(const Options& options) {
    using nlohmann::json;
    const std::filesystem::path path = options.workDirectory / "item-catalog.json";
    const json columns = {"id", "name", "type", "class", "tier", "weight", "maxStack", "durability", "minGearScore",
                          "maxGearScore", "gearScoreOverride", "bindOnPickup"};
    const json items = json::array({
        json::array({"TestSword", "@TestSword_MasterName", "Weapon", "Weapon+Sword", 1, 30, 1, 1000, 100, 120, 100, true}),
        json::array({"TestOre", "@TestOre_MasterName", "Resource", "Resource+Ore", 1, 2, 10000, 0, 0, 0, 0, false}),
        json::array({"House_Table", "@House_Table_MasterName", "HousingItem", "", 2, 10, 1, 0, 0, 0, 0, true}),
    });
    const auto write = [&](const json& document) { std::ofstream(path, std::ios::binary) << document.dump(); };

    gamedata::ItemCatalog catalog;
    check(!catalog.load((options.workDirectory / "no-such-file.json").string()) && !catalog.loaded(),
          "a catalog file that is not there: no catalog, and the backend continues");
    write({{"format", 1}, {"count", 4}, {"columns", columns}, {"items", items}});
    check(catalog.load(path.string()) && catalog.loaded() && catalog.item_count() == 3 && catalog.list_size() == 4,
          "the catalog file of the tool loads: 3 items, and the list of the client has 4 entries (entry 0 is no item)");
    const gamedata::ItemDefinition* sword = catalog.find("TestSword");
    const gamedata::ItemDefinition* ore = catalog.find("testore");
    check(sword != nullptr && sword->index == 1 && ore != nullptr && ore->index == 2 && catalog.find("House_Table")->index == 3,
          "item number K of the file has the index K + 1 on the wire, and the lookup by id ignores the letter case");
    check(sword != nullptr && sword->nameKey == "@TestSword_MasterName" && sword->type == "Weapon" && sword->itemClass == "Weapon+Sword"
              && sword->weight == 30 && sword->maxStack == 1 && sword->durability == 1000 && sword->minGearScore == 100
              && sword->maxGearScore == 120 && sword->gearScoreOverride == 100 && sword->bindOnPickup && ore->maxStack == 10000,
          "an item has its name key, type, class, weight, stack limit, durability, gear score range, and bind flag");
    check(catalog.by_index(2) == ore && catalog.by_index(0) == nullptr && catalog.by_index(4) == nullptr && catalog.find("Nothing") == nullptr,
          "the lookup by index gives the item, and no item for index 0, for an index after the end, and for an unknown id");
    check(gamedata::item_id_crc("1hSwordT1") == 0x98EDDB41 && gamedata::item_id_crc("OreT1") == 0x48E7CA95,
          "the CRC-32 of the lower-case item id is the value of the list of the client (examples of the protocol notes)");

    gamedata::ItemCatalog other;
    json wrongColumns = columns;
    wrongColumns[5] = "mass";
    write({{"format", 1}, {"count", 4}, {"columns", wrongColumns}, {"items", items}});
    const bool columnsRefused = !other.load(path.string());
    write({{"format", 1}, {"count", 9}, {"columns", columns}, {"items", items}});
    const bool countRefused = !other.load(path.string());
    std::ofstream(path, std::ios::binary) << "not a JSON document";
    check(columnsRefused && countRefused && !other.load(path.string()) && !other.loaded(),
          "a file with different columns, with a count that is not the number of items plus 1, or with no JSON document "
          "does not load");
}

/** B2: the bytes of the two states against the examples of the research. */
void wire_bytes(const Options&) {
    using namespace server::rep;
    ManualClock clock;
    world::ItemInstance sword;
    sword.instance = {0xA2, 0x07, 0x74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    sword.itemId = "example sword";
    sword.index = kExampleSword;
    sword.quantity = 1;
    sword.gearScore = 100;
    sword.durability = 1000;
    sword.maxDurability = 1000;
    sword.chrono = 1;
    world::ItemInstance ore;
    ore.instance = {0xA2, 0x07, 0x74, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
    ore.itemId = "example ore";
    ore.index = kExampleOre;
    ore.quantity = 10;
    ore.chrono = 2;

    // inventory.md 8.1: element 0 (46 bytes) and element 1 (45 bytes).
    const std::string swordBytes = "00013643e7e46206" "a80f" "01" "64" "0000000000" "00000000"
                                   "a2077400000000000000000000000001" "00" "00000000" "00000000";
    const std::string oreBytes = "0009d49a72d8e28c" "00" "02" "00" "0000000000" "00000000"
                                 "a2077400000000000000000000000002" "00" "00000000" "00000000";
    check(hex_text(encode_item(&sword, 0, world::sync::kLoad)) == swordBytes && swordBytes.size() == 92,
          "an item with the transmog part: 1 sword in slot 0 is the 46 bytes of the example (quantity 1, cause 0x11, "
          "item index 5278, durability 1000 of 1000, gear score 100)");
    check(hex_text(encode_item(&ore, 1, world::sync::kLoad)) == oreBytes && oreBytes.size() == 90,
          "a stack of 10 ore in slot 1 is the 45 bytes of the example (item index 2956, no durability)");
    check(hex_text(encode_item(&sword, 0, world::sync::kLoad, ItemWire{false})) == swordBytes.substr(0, 76)
              && hex_text(encode_item(&ore, 1, world::sync::kLoad, ItemWire{false})) == oreBytes.substr(0, 74),
          "an item with no transmog part: the same bytes with no appearance id and no skin dye bytes (8 bytes less)");
    // inventory.md 4.2: the empty element.
    const std::string emptyBytes = "000044f0b0b00000" + std::string(74, '0');
    check(hex_text(encode_item(nullptr, 7, world::sync::kItemRemoved)) == emptyBytes && emptyBytes.size() == 90,
          "an empty slot is the empty element of the example: quantity 0, cause 0x11, slot code 501, item index 0, and "
          "37 zero bytes");
    world::ItemInstance unknown = ore;
    unknown.index = 0;
    check(hex_text(encode_item(&unknown, 1, world::sync::kLoad)) == emptyBytes,
          "an item that the catalog does not have (index 0) is the empty element");
    WireItem decoded;
    const Bytes swordWire = encode_item(&sword, 3, world::sync::kLockStateChanged);
    server::hub::Reader reader(swordWire);
    check(read_item(reader, decoded) && reader.left() == 0 && decoded.quantity == 1 && decoded.action == 28 && decoded.slot == 3
              && decoded.index == kExampleSword && decoded.maxDurability == 1000 && decoded.durability == 1000
              && decoded.chrono == 1 && decoded.gearScore == 100 && decoded.instance == sword.instance,
          "the decoder of the tests, written from the reader of the client, reads each value of an item back");

    world::Entity entity;
    entity.bag.slots.resize(2);
    entity.bag.slots[0].item = sword;
    entity.bag.slots[1].item = ore;
    const std::vector<Bytes> full = encode_container_state(entity, View::owner, PartsRequest{0, 900});
    check(full.size() == 1 && hex_text(full[0]) == "0101" "000002" + swordBytes + oreBytes,
          "container state, owner, first record: group 0, field 0 (Container), and the full vector of the example (no "
          "changes, no sequence, 2 elements)");
    check(encode_container_state(entity, View::observer, PartsRequest{0, 900}).empty()
              && encode_currency_state(entity, View::observer, clock).empty(),
          "the observer view of the container state and of the currency state has no fragment");

    // inventory.md 8.3: the journal that sets slot 1 to 25 ore, and the journal that clears slot 1.
    entity.bag.slots[1].item->quantity = 25;
    entity.bag.slots[1].changed = 5;
    std::vector<Bytes> journal = encode_container_state(entity, View::owner, PartsRequest{4, 900});
    check(journal.size() == 1
              && hex_text(journal[0])
                     == "0101" "01010100" "001831d21ad8e28c" "00" "02" "00" "0000000000" "00000000"
                        "a2077400000000000000000000000002" "00" "00000000" "00000000",
          "container state, owner, after a change: a journal with one entry that sets element 1 (1 change, mask 01, "
          "index 1, no sequence, the item with quantity 25)");
    entity.bag.slots[1].item.reset();
    entity.bag.slots[1].changed = 6;
    journal = encode_container_state(entity, View::owner, PartsRequest{5, 900});
    check(journal.size() == 1 && hex_text(journal[0]) == "0101" "01010100" + emptyBytes,
          "a slot that lost its item goes out as a journal entry with the empty element, not as an erase entry");
    check(encode_container_state(entity, View::owner, PartsRequest{6, 900}).empty(),
          "a session that has each change gets no fragment");
    entity.bag.reset = 9;
    check(encode_container_state(entity, View::owner, PartsRequest{6, 900}).size() == 1
              && read_container(std::span<const std::uint8_t>(encode_container_state(entity, View::owner, PartsRequest{6, 900})[0]).subspan(2)).full,
          "after a reset of the bag (the full record again), the session gets the full vector");

    // inventory.md 8.1: the coin fragment for 123.45 coin.
    entity.wallet.coin = 12345;
    check(hex_text(encode_currency_state(entity, View::owner, clock)) == "0101" "0000000000003039",
          "currency state, owner: group 0, field 0, and the amount 12345 (123.45 coin) as 64 bits");

    // progression.md 2.1: the fragment of a level 1 character, and of level 20 with 84,100 experience points.
    check(hex_text(encode_progression_state(entity, View::owner, clock)) == "03" "03" "00000000" "00000000" "03" "00000000" "00000000",
          "progression state, owner: level 1 goes out as 0 (the number of the screen minus 1), with the bonus level, the "
          "experience, and the rested experience 0: the bytes of the example of the research");
    entity.progression.level = 20;
    entity.progression.experience = 84100;
    check(hex_text(encode_progression_state(entity, View::owner, clock)) == "03" "03" "00000013" "00000000" "03" "00014884" "00000000"
              && hex_text(encode_progression_state(entity, View::observer, clock)) == "01" "03" "00000013" "00000000",
          "level 20 with 84,100 experience points is the example of the research; the observer view has the level and "
          "no experience (group 0 only)");
}

/** B3: the commands give, remove, and coin, with what the session of the owner gets. */
void give_and_remove(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Inventory& inventory = host.world.inventory();
    ClientBag bag;
    std::size_t from = 0;
    follow(bag, a, from);
    check(bag.fulls == 1 && bag.slots.empty() && !bag.fault && item_updates(a).coin == std::vector<std::uint64_t>{0},
          "the first record of a character with no item has the full vector with 0 elements and the coin 0");

    check(inventory.give(kCharacterA, "TestSword", 1).error == ItemError::noCatalog && rows_text(host.store, kCharacterA).empty(),
          "a world with no item catalog gives no item");
    set_catalog(host);

    const world::ItemResult sword = inventory.give(kCharacterA, "TestSword", 1);
    const world::ItemResult ore = inventory.give(kCharacterA, "testore", 250);
    const world::ItemResult potion = inventory.give(kCharacterA, "TestPotion", 3);
    check(sword.ok() && sword.inWorld && sword.slots == std::vector<std::uint32_t>{0} && ore.ok()
              && ore.slots == (std::vector<std::uint32_t>{1, 2, 3}) && potion.ok() && potion.slots == std::vector<std::uint32_t>{4},
          "give: 1 sword goes to slot 0; 250 ore with the stack limit 100 make the stacks 100, 100, 50 in the slots 1 to "
          "3; 3 potions go to slot 4");
    check(rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:100:0 2:TestOre:100:0 3:TestOre:50:0 4:TestPotion:3:0 ",
          "each stack is one row with the character id as its owner key; the sword is bound (bind on pickup)");
    host.steps(2, {&a});
    follow(bag, a, from);
    check(!bag.fault && bag.fulls == 1 && bag.journals == 1 && bag_text(bag) == "0:1:1:1 1:2:100:0 2:2:100:0 3:2:50:0 4:3:3:0 ",
          "the session of the owner gets one journal that adds the five elements at the end of its vector, with the item "
          "indexes of the catalog");
    check(bag.slots.size() == 5 && bag.slots[0].gearScore == 100 && bag.slots[0].durability == 500 && bag.slots[0].maxDurability == 500
              && bag.slots[0].action == world::sync::kItemAdded && bag.slots[0].slot == 0 && bag.slots[3].slot == 3
              && bag.slots[1].durability == 0,
          "a new sword has the gear score and the durability of the item table, the cause 'item added', and its slot number");
    std::set<std::string> instances;
    for (const WireItem& item : bag.slots) {
        instances.insert(hex_text(Bytes(item.instance.begin(), item.instance.end())));
    }
    check(instances.size() == 5 && instances.count(std::string(32, '0')) == 0,
          "each stack has its own instance id, and no instance id is zero");

    const world::ItemResult more = inventory.give(kCharacterA, "TestOre", 60);
    check(more.ok() && more.slots == (std::vector<std::uint32_t>{3, 5})
              && rows_text(host.store, kCharacterA).find("3:TestOre:100:0 4:TestPotion:3:0 5:TestOre:10:0 ") != std::string::npos,
          "60 ore more: 50 go to the stack that is not full, and 10 make a new stack in the first empty slot");
    const std::string instanceOfSlot3 = hex_text(Bytes(bag.slots[3].instance.begin(), bag.slots[3].instance.end()));
    host.steps(2, {&a});
    follow(bag, a, from);
    check(!bag.fault && bag.slots.size() == 6 && bag.slots[3].quantity == 100
              && hex_text(Bytes(bag.slots[3].instance.begin(), bag.slots[3].instance.end())) == instanceOfSlot3,
          "a stack that gets a larger quantity keeps its instance id");

    const std::string before = rows_text(host.store, kCharacterA);
    const std::size_t messages = a.got.size();
    check(inventory.give(kCharacterA, "NoSuchItem", 1).error == ItemError::unknownItem
              && inventory.give(kCharacterA, "TestNoType", 1).error == ItemError::noItemType
              && inventory.give(kCharacterA, "TestOre", 0).error == ItemError::badQuantity
              && inventory.give(kCharacterA, "TestOre", world::kGiveLimit + 1).error == ItemError::badQuantity
              && inventory.give(kCharacterNone, "TestOre", 1).error == ItemError::unknownCharacter,
          "give refuses an item that the catalog does not have, an item with no type, the quantity 0, a quantity above the "
          "limit, and a character that the store does not have");
    check(inventory.give(kCharacterA, "TestSword", 495).error == ItemError::bagFull,
          "give refuses a quantity that the bag has no slots for (500 slots): the full give, not a part of it");
    check(inventory.remove(kCharacterA, 99).error == ItemError::badSlot && inventory.remove(kCharacterA, 1, 101).error == ItemError::badQuantity
              && inventory.remove(kCharacterA, 1, 0).error == ItemError::badQuantity,
          "remove refuses a slot that the bag does not have and a quantity that is 0 or larger than the stack");
    host.steps(2, {&a});
    check(rows_text(host.store, kCharacterA) == before && item_updates(a, messages).bag.empty(),
          "a command that is refused changes no row, and the session gets no fragment of the bag");

    check(inventory.remove(kCharacterA, 2).ok() && inventory.remove(kCharacterA, 1, 30).ok()
              && inventory.remove(kCharacterA, 2).error == ItemError::emptySlot,
          "remove takes the full stack of slot 2 and 30 of slot 1; a second removal from the empty slot is refused");
    host.steps(2, {&a});
    follow(bag, a, from);
    check(!bag.fault && bag.slots.size() == 6 && bag.slots[2].empty() && bag.slots[1].quantity == 70
              && bag.slots[1].action == world::sync::kItemRemoved
              && rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:70:0 3:TestOre:100:0 4:TestPotion:3:0 5:TestOre:10:0 ",
          "the client gets the empty element for slot 2 and the quantity 70 for slot 1 (cause 'item removed'); the vector "
          "keeps its length, and the row of the removed stack is gone");
    check(inventory.give(kCharacterA, "TestSword", 1).slots == std::vector<std::uint32_t>{2},
          "a new stack gets the first empty slot (slot 2)");

    const std::size_t coinFrom = a.got.size();
    check(inventory.set_coin(kCharacterA, 12345).ok() && host.store.currency(kCharacterA, state::kCurrencyCoin) == 12345,
          "the coin command writes the amount in hundredths (12345 is 123.45 coin)");
    host.steps(2, {&a});
    check(item_updates(a, coinFrom).coin == std::vector<std::uint64_t>{12345} && item_updates(a, coinFrom).bag.size() == 1,
          "the session of the owner gets the currency state with the amount");
    check(inventory.set_coin(kCharacterA, world::kWalletLimit + 1).error == ItemError::badQuantity
              && inventory.set_coin(kCharacterNone, 5).error == ItemError::unknownCharacter
              && host.store.currency(kCharacterA, state::kCurrencyCoin) == 12345,
          "the coin command refuses an amount above the wallet limit of the client and an unknown character");

    // A character that is not in the world: the rows change, and its next session gets them.
    const world::ItemResult stored = inventory.give(kCharacterB, "TestOre", 5);
    check(stored.ok() && !stored.inWorld && rows_text(host.store, kCharacterB) == "0:TestOre:5:0 "
              && inventory.set_coin(kCharacterB, 700).ok() && host.world.find_character(kCharacterB) == nullptr,
          "a command for a character that is not in the world changes its rows, and the world gets no entity for it");
    const auto holdings = inventory.holdings(kCharacterB);
    check(holdings && !holdings->inWorld && holdings->bag.slots.size() == 1 && holdings->wallet.coin == 700
              && !inventory.holdings(kCharacterNone),
          "the list command reads the bag and the coin of a character that is not in the world from the rows");
    FakeClient b(27001);
    host.enter(b, players.beta.registration());
    ClientBag bagB;
    std::size_t fromB = 0;
    follow(bagB, b, fromB);
    check(!bagB.fault && bag_text(bagB) == "0:2:5:0 " && item_updates(b).coin == std::vector<std::uint64_t>{700}
              && bagB.slots[0].action == world::sync::kLoad,
          "its first record has the stored stack with the cause 'load' and the stored coin");
    check(log_count("inventory: give of 250 testore of character " + std::string(kCharacterA) + ": changed slot(s) 1, 2, 3 (the character is in the world) [s1 aaaaaaaa]") == 1
              && log_count("refused: the item catalog has no item with this id") == 1,
          "the log has one line for each command, with the session of the character");
}

/** B4: move, swap, stack, and split inside the bag. */
void bag_operations(const Options&) {
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Inventory& inventory = host.world.inventory();
    ClientBag bag;
    std::size_t from = 0;
    check(inventory.give(kCharacterA, "TestSword", 1).ok() && inventory.give(kCharacterA, "TestOre", 80).ok(),
          "start: a sword in slot 0 and 80 ore in slot 1");
    const world::InstanceId oreInstance = instance_at(host, kCharacterA, 1);

    const world::ItemResult split = inventory.split(kCharacterA, 1, 30);
    check(split.ok() && split.slots == (std::vector<std::uint32_t>{1, 2})
              && rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:50:0 2:TestOre:30:0 "
              && instance_at(host, kCharacterA, 1) == oreInstance && instance_at(host, kCharacterA, 2) != oreInstance
              && instance_at(host, kCharacterA, 2) != world::InstanceId{},
          "split: 30 of the 80 ore make a new stack in the first empty slot; the first stack keeps its instance id, and "
          "the new stack has a new one");

    const world::ItemResult move = inventory.move(kCharacterA, 0, 5);
    check(move.ok() && move.slots == (std::vector<std::uint32_t>{0, 3, 4, 5})
              && rows_text(host.store, kCharacterA) == "1:TestOre:50:0 2:TestOre:30:0 5:TestSword:1:1 ",
          "move to an empty slot after the end: the sword goes to slot 5, and the vector gets the empty slots 3 and 4");
    host.steps(2, {&a});
    follow(bag, a, from);
    check(!bag.fault && bag.slots.size() == 6 && bag_text(bag) == "1:2:50:0 2:2:30:0 5:1:1:1 "
              && bag.slots[5].action == world::sync::kContainerTransfer,
          "the client has 6 elements: the ore stacks, three empty slots, and the sword in slot 5 (cause 'container transfer')");

    check(inventory.move(kCharacterA, 2, 1).ok() && rows_text(host.store, kCharacterA) == "1:TestOre:80:0 5:TestSword:1:1 ",
          "stack: a stack that moves to a stack of the same item that is not full goes into it (50 and 30 make 80)");
    check(inventory.give(kCharacterA, "TestOre", 10).ok() && inventory.split(kCharacterA, 1, 30, 0).ok()
              && rows_text(host.store, kCharacterA) == "0:TestOre:30:0 1:TestOre:60:0 5:TestSword:1:1 ",
          "split with a target: 30 of the 90 ore go to the empty slot 0");
    check(inventory.give(kCharacterA, "TestOre", 110).ok()
              && rows_text(host.store, kCharacterA) == "0:TestOre:100:0 1:TestOre:100:0 5:TestSword:1:1 ",
          "110 ore more fill the two stacks to the stack limit (100 and 100)");
    check(inventory.remove(kCharacterA, 1, 20).ok() && inventory.remove(kCharacterA, 0, 70).ok()
              && inventory.move(kCharacterA, 0, 1).ok() && rows_text(host.store, kCharacterA) == "0:TestOre:10:0 1:TestOre:100:0 5:TestSword:1:1 ",
          "stack with a limit: 30 ore move to a stack of 80; it takes 20 (the stack limit is 100), and 10 stay in the source");
    check(inventory.move(kCharacterA, 5, 1).ok() && rows_text(host.store, kCharacterA) == "0:TestOre:10:0 1:TestSword:1:1 5:TestOre:100:0 ",
          "swap: an item that moves to a slot with a different item changes places with it");
    check(inventory.move(kCharacterA, 0, 5).ok() && rows_text(host.store, kCharacterA) == "0:TestOre:100:0 1:TestSword:1:1 5:TestOre:10:0 ",
          "a stack that moves to a full stack of the same item changes places with it");
    host.steps(2, {&a});
    follow(bag, a, from);
    check(!bag.fault && bag_text(bag) == "0:2:100:0 1:1:1:1 5:2:10:0 " && bag.slots.size() == 6,
          "the client follows each operation: its bag is equal to the bag of the world");

    const std::string before = rows_text(host.store, kCharacterA);
    const std::size_t messages = a.got.size();
    check(inventory.move(kCharacterA, 99, 0).error == ItemError::badSlot && inventory.move(kCharacterA, 0, world::kMaxContainerSlots).error == ItemError::badSlot
              && inventory.move(kCharacterA, 3, 0).error == ItemError::emptySlot && inventory.move(kCharacterA, 1, 1).error == ItemError::sameSlot
              && inventory.move(kCharacterNone, 0, 1).error == ItemError::unknownCharacter,
          "move refuses a source that is out of range, a target that is out of range, an empty source, the same slot, and "
          "an unknown character");
    check(inventory.split(kCharacterA, 0, 100).error == ItemError::badQuantity && inventory.split(kCharacterA, 0, 0).error == ItemError::badQuantity
              && inventory.split(kCharacterA, 1, 1).error == ItemError::badQuantity && inventory.split(kCharacterA, 0, 5, 1).error == ItemError::targetInUse
              && inventory.split(kCharacterA, 3, 1).error == ItemError::emptySlot && inventory.split(kCharacterA, 400, 1).error == ItemError::badSlot
              && inventory.split(kCharacterA, 0, 5, 0).error == ItemError::sameSlot,
          "split refuses a quantity that is 0 or not smaller than the stack, a target that has an item, an empty source, a "
          "source that is out of range, and the source as the target");
    host.steps(2, {&a});
    check(rows_text(host.store, kCharacterA) == before && item_updates(a, messages).bag.empty(),
          "an operation that is refused changes no row, and the session gets no fragment of the bag");
    check(inventory.set_locked(kCharacterA, 1, nullptr, true).ok() && inventory.split(kCharacterA, 0, 40).ok()
              && rows_text(host.store, kCharacterA) == "0:TestOre:60:0 1:TestSword:1:3 2:TestOre:40:0 5:TestOre:10:0 ",
          "the lock command sets the lock flag of an item (flags 3: bound and locked)");
}

/** B5: the requests of a client for its bag, with a correct and with a wrong body. */
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
    inventory.give(kCharacterA, "TestSword", 1);
    inventory.give(kCharacterA, "TestOre", 80);
    inventory.give(kCharacterA, "TestPotion", 5);
    inventory.give(kCharacterB, "TestOre", 40);
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    const world::InstanceId swordId = instance_at(host, kCharacterA, 0);
    const world::InstanceId oreId = instance_at(host, kCharacterA, 1);
    const world::InstanceId potionId = instance_at(host, kCharacterA, 2);
    const world::InstanceId oreOfB = instance_at(host, kCharacterB, 0);
    host.steps(2, {&a, &b});
    ClientBag bag;
    std::size_t from = 0;
    follow(bag, a, from);
    const auto send = [&](FakeClient& client, const world::Entity* route, std::uint32_t type, const Bytes& body) {
        host.datagram(client.peer, client.component_message(type, route->actor.bytes, body));
        host.steps(2, {&a, &b});
    };

    check(read_drop_request(drop_body(alpha->id, {{1, oreId, 3}})).has_value() && drop_body(alpha->id, {{1, oreId, 3}}).size() == 45
              && read_lock_request(lock_body(alpha->id, 1, 0, swordId, 1)).has_value() && lock_body(alpha->id, 1, 0, swordId, 1).size() == 45,
          "the bodies of the tests have the sizes of the writers of the client: a drop request with one entry is 45 "
          "bytes, and a lock request is 45 bytes");

    // Drop (type 1838).
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 3}}));
    follow(bag, a, from);
    check(rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:77:0 2:TestPotion:5:0 " && !bag.fault
              && bag.slots[1].quantity == 77 && bag.slots[1].action == world::sync::kItemRemoved,
          "drop request with a correct body: 3 of the 80 ore are gone; the row has 77, and the client gets the slot with 77 "
          "(cause 'item removed')");
    check(log_count("rep: drop request (type 1838, 1 entry): done [s1 aaaaaaaa]") == 1, "the log has the line of the request");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 7}, {2, potionId, 5}}));
    follow(bag, a, from);
    check(rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:70:0 " && bag.slots[2].empty() && bag.slots.size() == 3,
          "a drop request with two entries: 7 ore and the full stack of 5 potions are gone, and slot 2 is an empty slot");

    const std::string rowsA = rows_text(host.store, kCharacterA);
    const std::string rowsB = rows_text(host.store, kCharacterB);
    const std::size_t messagesA = a.got.size();
    const std::size_t messagesB = b.got.size();
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{501, oreId, 1}}));
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{0xFFFFFFFF, oreId, 1}}));
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{2, potionId, 1}}));
    check(log_count("rep: drop request (type 1838, 1 entry): the slot number is not a slot of the bag") == 2
              && log_count("rep: drop request (type 1838, 1 entry): the slot has no item") == 1,
          "a drop request with a slot that is out of range (501, and -1) or that is empty is refused");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 71}}));
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 0}}));
    check(log_count("rep: drop request (type 1838, 1 entry): the quantity is not in the permitted range") == 2,
          "a drop request with a quantity that is larger than the stack, or 0, is refused");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, swordId, 1}}));
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{0, oreOfB, 1}}));
    check(log_count("rep: drop request (type 1838, 1 entry): the slot has a different item instance") == 2,
          "a drop request that names a different item instance for the slot is refused: the instance of a different "
          "slot, and the instance of an item of a different character");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{0, swordId, 1}, {1, oreId, 999}}));
    check(log_count("rep: drop request (type 1838, 2 entries): the quantity is not in the permitted range") == 1,
          "a drop request with one correct entry and one wrong entry is refused as a whole");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 60}, {1, oreId, 60}}));
    check(log_count("rep: drop request (type 1838, 2 entries): the quantity is not in the permitted range") == 2,
          "two entries for one slot cannot take more than the slot has");
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {}));
    Bytes extra = drop_body(alpha->id, {{1, oreId, 1}});
    extra.push_back(0);
    send(a, alpha, kTypeDropInventoryItems, extra);
    Bytes cut = drop_body(alpha->id, {{1, oreId, 1}});
    cut.resize(cut.size() - 3);
    send(a, alpha, kTypeDropInventoryItems, cut);
    send(a, alpha, kTypeDropInventoryItems, drop_body(alpha->id, {{1, oreId, 1}}, 502));
    check(log_count("rep: drop request (type 1838, 0 entries): the request has no entry") == 1
              && log_count("rep: drop request (type 1838, 46 bytes) does not have the form of the request: no change") == 2
              && log_count("rep: drop request (type 1838, 42 bytes) does not have the form of the request: no change") == 1,
          "a drop request with no entry, with a byte after its last entry, with a body that is too short, or with a count "
          "above 501 is refused");
    // The character of a request is the character of the session: a body that names the
    // entity of a different character changes nothing, also with the route of that entity.
    send(a, alpha, kTypeDropInventoryItems, drop_body(beta->id, {{0, oreOfB, 10}}));
    send(a, beta, kTypeDropInventoryItems, drop_body(beta->id, {{0, oreOfB, 10}}));
    check(log_count("rep: drop request (type 1838) names the entity 544f5a4100000004, which is not the entity of the session: no change [s1 aaaaaaaa]") == 2,
          "a drop request of session A for the entity of B (an item of a different character) is refused");

    // Lock (type 2549).
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 2, 0, swordId, 1));
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 500, swordId, 1));
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 2, swordId, 1));
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 0, oreId, 1));
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 0, swordId, 2));
    send(a, alpha, kTypeSetLockState, lock_body(beta->id, 1, 0, oreOfB, 1));
    Bytes shortLock = lock_body(alpha->id, 1, 0, swordId, 1);
    shortLock.pop_back();
    send(a, alpha, kTypeSetLockState, shortLock);
    check(log_count("rep: lock request (type 2549) for container type 2: the backend has only the bag: no change") == 1
              && log_count("rep: lock request (type 2549, slot 500, lock): the slot number is not a slot of the bag") == 1
              && log_count("rep: lock request (type 2549, slot 2, lock): the slot has no item") == 1
              && log_count("rep: lock request (type 2549, slot 0, lock): the slot has a different item instance") == 1
              && log_count("rep: lock request (type 2549, 45 bytes) does not have the form of the request") == 1
              && log_count("rep: lock request (type 2549, 44 bytes) does not have the form of the request") == 1
              && log_count("rep: lock request (type 2549) names the entity 544f5a4100000004, which is not the entity of the session") == 1,
          "a lock request is refused for a container that is not the bag, a slot that is out of range, an empty slot, a "
          "different item instance, a lock value that is not 0 or 1, a body that is too short, and the entity of a "
          "different character");
    host.steps(2, {&a, &b});
    check(rows_text(host.store, kCharacterA) == rowsA && rows_text(host.store, kCharacterB) == rowsB
              && item_updates(a, messagesA).bag.empty() && item_updates(b, messagesB).bag.empty(),
          "no request that was refused changed a row of A or of B, and no session got a fragment of a bag");

    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 0, swordId, 1));
    follow(bag, a, from);
    check(rows_text(host.store, kCharacterA) == "0:TestSword:1:3 1:TestOre:70:0 " && !bag.fault
              && bag.slots[0].flags == (world::kItemBound | world::kItemLocked) && bag.slots[0].action == world::sync::kLockStateChanged
              && log_count("rep: lock request (type 2549, slot 0, lock): done [s1 aaaaaaaa]") == 1,
          "lock request with a correct body: the sword has the lock flag in its row, and the client gets the slot with the "
          "flag (cause 'lock state changed')");
    send(a, alpha, kTypeSetLockState, lock_body(alpha->id, 1, 0, swordId, 0));
    follow(bag, a, from);
    check(rows_text(host.store, kCharacterA) == "0:TestSword:1:1 1:TestOre:70:0 " && bag.slots[0].flags == world::kItemBound,
          "a lock request with the value 0 clears the lock flag");
    check(host.session(1).message_counts().componentCalls > 20 && !host.session(1).closing() && !host.session(2).closing(),
          "the sessions continue after each wrong request");
}

/** B6: two sessions: the bag is a state of the owner only. */
void two_sessions(const Options&) {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    ServerHost host(options);
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    // The first input report with a camera starts the interest scan of a session.
    host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
    host.datagram(b.peer, b.input_at(1, 102.0F, 200.0F, kHeight));
    host.steps(60, {&a, &b});
    world::Inventory& inventory = host.world.inventory();
    const world::Entity* alpha = host.world.find_character(kCharacterA);
    const world::Entity* beta = host.world.find_character(kCharacterB);
    check(host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "each of the two sessions observes the entity of the other character");
    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    inventory.give(kCharacterA, "TestSword", 1);
    inventory.give(kCharacterA, "TestOre", 80);
    inventory.set_coin(kCharacterA, 5000);
    host.steps(4, {&a, &b});
    const ItemUpdates ownA = item_updates(a, fromA);
    check(ownA.bag.size() == 1 && ownA.bag[0].elements.size() == 2 && ownA.coin == std::vector<std::uint64_t>{5000},
          "session A, the owner, gets the bag and the coin of its character");
    bool observerFragments = false;
    unsigned recordsOfA = 0;
    for (const Bundle& bundle : [&] {
             std::vector<Bundle> bundles;
             for (const Sent& sent : b.got) {
                 if (sent.type == 8) {
                     bundles.push_back(read_bundle(sent.body));
                 }
             }
             return bundles;
         }()) {
        for (const Record& record : bundle.records) {
            if (record.interest != 2) {
                continue;
            }
            ++recordsOfA;
            observerFragments = observerFragments || fragment_of(record, 60) != nullptr || fragment_of(record, 61) != nullptr;
        }
    }
    check(recordsOfA >= 1 && !observerFragments,
          "session B observes the entity of A and gets no fragment of slot 60 and of slot 61 for it: not in the first "
          "record, and not after a change");
    check(item_updates(b, fromB).bag.empty() && item_updates(b, fromB).coin.empty() && rows_text(host.store, kCharacterB).empty()
              && beta->bag.slots.empty() && beta->wallet.coin == 0,
          "the actions for A change nothing for B: no fragment of its own bag, no row, no coin");

    // B sends a request with the route and the entity of A, which B observes.
    const world::InstanceId oreOfA = instance_at(host, kCharacterA, 1);
    const std::string rowsA = rows_text(host.store, kCharacterA);
    host.datagram(b.peer, b.component_message(server::rep::kTypeDropInventoryItems, alpha->actor.bytes, drop_body(alpha->id, {{1, oreOfA, 80}})));
    host.datagram(b.peer, b.component_message(server::rep::kTypeDropInventoryItems, beta->actor.bytes, drop_body(beta->id, {{1, oreOfA, 80}})));
    host.steps(4, {&a, &b});
    check(rows_text(host.store, kCharacterA) == rowsA && item_updates(a, fromA).bag.size() == 1
              && log_count("which is not the entity of the session: no change [s2 bbbbbbbb]") == 1
              && log_count("rep: drop request (type 1838, 1 entry): the slot number is not a slot of the bag [s2 bbbbbbbb]") == 1,
          "a drop request of session B for the item of A is refused, with the entity of A and with its own entity: the "
          "backend finds the character from the session");
    inventory.give(kCharacterB, "TestPotion", 2);
    host.steps(4, {&a, &b});
    check(item_updates(b, fromB).bag.size() == 1 && item_updates(a, fromA).bag.size() == 1,
          "an item for B goes to session B only");
}

/** B7: the items after a new entry of the character, and in a new world object on the same database. */
void persistence(const Options&) {
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    world::Inventory& inventory = host.world.inventory();
    inventory.give(kCharacterA, "TestSword", 1);
    inventory.give(kCharacterA, "TestOre", 80);
    inventory.give(kCharacterA, "TestPotion", 4);
    inventory.remove(kCharacterA, 1);
    inventory.set_locked(kCharacterA, 0, nullptr, true);
    inventory.set_coin(kCharacterA, 4321);
    const world::InstanceId swordId = instance_at(host, kCharacterA, 0);
    const world::InstanceId potionId = instance_at(host, kCharacterA, 2);
    host.steps(2, {&a});
    host.datagram(a.peer, a.carrier_close());
    host.tick();
    check(host.world.entities().empty() && rows_text(host.store, kCharacterA) == "0:TestSword:1:3 2:TestPotion:4:0 ",
          "the character leaves the world: the world has no entity, and the rows have the items");

    FakeClient again(27001);
    host.enter(again, players.alpha.registration());
    ClientBag bag;
    std::size_t from = 0;
    follow(bag, again, from);
    check(!bag.fault && bag.fulls == 1 && bag.journals == 0 && bag.slots.size() == 3 && bag_text(bag) == "0:1:1:3 2:3:4:0 "
              && bag.slots[1].empty() && item_updates(again).coin == std::vector<std::uint64_t>{4321},
          "the new session of the character gets the same items in the same slots, with the lock flag, an empty slot 1, "
          "and the coin");
    check(bag.slots[0].instance == swordId && bag.slots[2].instance == potionId && bag.slots[0].action == world::sync::kLoad
              && bag.slots[0].gearScore == 100 && bag.slots[0].durability == 500,
          "an item keeps its instance id, its gear score, and its durability; the cause of each element is 'load'");
    const world::ItemResult next = inventory.give(kCharacterA, "TestOre", 5);
    check(next.ok() && next.slots == std::vector<std::uint32_t>{1} && host.world.find_character(kCharacterA)->bag.slots[1].item->chrono == 4,
          "the order counter of the bag continues after the load (the next new stack has the number 4)");

    // A new world object on the same database: the state after a stop and a new start of the backend.
    world::World second(host.clock, host.store, host.tables, ServerHost::with_game_data(host.catalog, host.progression));
    check(second.spawn_character(kCharacterA, state::WorldPosition{1.0F, 2.0F, 3.0F}) == world::SpawnError::none,
          "a second world object reads the character from the same store");
    const world::Entity* loaded = second.find_character(kCharacterA);
    check(loaded != nullptr && loaded->bag.slots.size() == 3 && loaded->bag.slots[0].item && loaded->bag.slots[0].item->instance == swordId
              && loaded->bag.slots[0].item->flags == 3 && loaded->bag.slots[1].item && loaded->bag.slots[1].item->quantity == 5
              && loaded->bag.slots[2].item && loaded->bag.slots[2].item->quantity == 4 && loaded->wallet.coin == 4321
              && loaded->bag.nextChrono == 5,
          "it has the same items, the lock flag, the quantities, the coin, and the order counter");

    // A row whose item the catalog does not have: the slot is empty for the client, and the row stays.
    check(host.store.write_items(kCharacterB, {}, {state::ItemRow{"00112233445566778899aabbccddeeff", state::kContainerBag, 0, "ItemOfALaterBuild", 3, 0, 0, 0, 0, 1}}),
          "a row with an item id that the catalog does not have");
    FakeClient b(27002);
    host.enter(b, players.beta.registration());
    ClientBag bagB;
    std::size_t fromB = 0;
    follow(bagB, b, fromB);
    check(!bagB.fault && bagB.slots.size() == 1 && bagB.slots[0].empty() && rows_text(host.store, kCharacterB) == "0:ItemOfALaterBuild:3:0 "
              && log_count("1 item(s) of character " + std::string(kCharacterB) + " are not in the item catalog") == 1,
          "its slot is the empty element for the client, the row stays, and the log has one line");
    check(inventory.give(kCharacterB, "TestOre", 1).slots == std::vector<std::uint32_t>{1} && rows_text(host.store, kCharacterB) == "0:ItemOfALaterBuild:3:0 1:TestOre:1:0 ",
          "a new item does not take the slot of the item that the catalog does not have");

    // The delete of a character removes its rows.
    check(host.store.delete_character(kCharacterB) == state::DeleteResult::deleted && host.store.list_items(kCharacterB).empty()
              && host.store.currency(kCharacterB, state::kCurrencyCoin) == 0 && !host.store.list_items(kCharacterA).empty(),
          "the delete of a character removes its item rows and its currency rows, and no row of a different character");
    check(!host.store.write_items(kCharacterB, {}, {state::ItemRow{world::instance_text(swordId), state::kContainerBag, 9, "TestSword", 1, 0, 0, 0, 0, 1}})
              && rows_text(host.store, kCharacterA).find("0:TestSword:1:3 ") == 0 && host.store.list_items(kCharacterB).empty(),
          "the store refuses a row with the instance id of an item of a different character, and the row of that item "
          "does not change");
}

/** B8: a bag that is larger than one carrier message. */
void large_bag(const Options&) {
    ServerHost host;
    set_catalog(host);
    const TwoPlayers players(host);
    world::Inventory& inventory = host.world.inventory();
    // 60 swords: 60 stacks, 46 bytes each on the wire.
    check(inventory.give(kCharacterA, "TestSword", 60).ok() && host.store.list_items(kCharacterA).size() == 60,
          "a character that is not in the world gets 60 swords: 60 rows");
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    host.steps(2, {&a});
    const ItemUpdates first = item_updates(a);
    ClientBag bag;
    std::size_t from = 0;
    follow(bag, a, from);
    note("the bag of 60 items: " + std::to_string(first.bag.size()) + " fragments in " + std::to_string(first.bagBundles)
         + " bundles, largest bundle frame " + std::to_string(first.largestFrame) + " bytes");
    check(first.bag.size() >= 3 && first.bag[0].full && !first.bag[1].full && first.bagBundles == first.bag.size(),
          "the first record of the bag goes out in more than one bundle: the full vector with the first elements, then "
          "journals, each fragment in its own bundle");
    check(first.largestFrame <= kCarrierMessage && first.complete,
          "each bundle is one carrier message (1,024 bytes at most) and decodes to its last byte");
    std::set<std::string> instances;
    bool slotsInOrder = true;
    for (std::size_t slot = 0; slot < bag.slots.size(); ++slot) {
        instances.insert(hex_text(Bytes(bag.slots[slot].instance.begin(), bag.slots[slot].instance.end())));
        slotsInOrder = slotsInOrder && bag.slots[slot].slot == slot && bag.slots[slot].action == world::sync::kLoad;
    }
    check(!bag.fault && bag.slots.size() == 60 && bag.items() == 60 && instances.size() == 60 && slotsInOrder,
          "the client makes the vector of 60 elements from them: each journal adds its elements at the end, in sequence");
    std::vector<Bundle> bundles;
    for (const Sent& sent : a.got) {
        if (sent.type == 8) {
            bundles.push_back(read_bundle(sent.body));
        }
    }
    bool gap = false;
    for (std::size_t index = 0; index < bundles.size(); ++index) {
        gap = gap || bundles[index].sequence != index + 1;
    }
    check(!gap && bundles.size() >= 4 && bundles[1].records.size() == 1 && fragment_of(bundles[1].records[0], 0) != nullptr,
          "the bundle sequences have no gap, and the first record starts with the metadata state");

    const std::size_t messages = a.got.size();
    check(inventory.set_locked(kCharacterA, 37, nullptr, true).ok(), "one item of the 60 changes (the lock of slot 37)");
    host.steps(2, {&a});
    const ItemUpdates change = item_updates(a, messages);
    follow(bag, a, from);
    check(change.bag.size() == 1 && !change.bag[0].full && change.bag[0].elements.size() == 1 && change.bag[0].elements[0].first == 37
              && bag.slots[37].flags == 3 && bag.items() == 60,
          "the session gets one journal with one entry for slot 37, not the bag again");

    host.world.touch_all(host.world.find_character(kCharacterA)->id);
    const std::size_t before = a.got.size();
    host.steps(2, {&a});
    const ItemUpdates again = item_updates(a, before);
    follow(bag, a, from);
    check(again.bag.size() == first.bag.size() && again.bag[0].full && again.largestFrame <= kCarrierMessage && !bag.fault
              && bag.slots.size() == 60 && bag.items() == 60,
          "the full record again (the command 'touch') has the full bag again, in the same bundles");
    bool metadataAgain = false;
    bool otherStates = false;
    for (std::size_t index = before; index < a.got.size(); ++index) {
        if (a.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(a.got[index].body).records) {
            metadataAgain = metadataAgain || fragment_of(record, 0) != nullptr;
            otherStates = otherStates || (fragment_of(record, 9) != nullptr && fragment_of(record, 41) != nullptr);
        }
    }
    check(!metadataAgain && otherStates,
          "the full record again for the owner has the player state and the vitals state, and no second metadata state "
          "(the client of the owner closes its connection for a second one)");

    FakeClient b(27001);
    host.enter(b, players.beta.registration());
    const std::size_t messagesB = b.got.size();
    check(inventory.give(kCharacterB, "TestSword", 60).ok(), "a character that is in the world gets 60 swords");
    host.steps(2, {&b});
    const ItemUpdates given = item_updates(b, messagesB);
    ClientBag bagB;
    std::size_t fromB = 0;
    follow(bagB, b, fromB);
    check(given.bag.size() >= 3 && given.largestFrame <= kCarrierMessage && !bagB.fault && bagB.items() == 60,
          "its session gets the 60 new elements as journals in more than one bundle, each one in one carrier message");
}

/** B9: the development routes for the items. */
void item_routes(const Options&) {
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

    service::HttpResponse reply = send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=150");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "world" && json::parse(reply.body)["slots"] == json::array({0, 1}),
          "give route for a character that is in the world: reply 200 with the slots 0 and 1, applied in the world");
    reply = send("POST", "/v1/dev/items/give", forB + "&item=TestSword");
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "stored" && rows_text(host.store, kCharacterB) == "0:TestSword:1:1 ",
          "give route for a character that is not in the world, with no quantity: one item, applied to the rows");
    check(send("POST", "/v1/dev/items/coin", forA + "&amount=12345").status == 200 && send("POST", "/v1/dev/items/lock", forA + "&slot=1&locked=1").status == 200
              && send("POST", "/v1/dev/items/split", forA + "&slot=0&quantity=40").status == 200
              && send("POST", "/v1/dev/items/move", forA + "&from=2&to=7").status == 200
              && send("POST", "/v1/dev/items/remove", forA + "&slot=0&quantity=10").status == 200
              && rows_text(host.store, kCharacterA) == "0:TestOre:50:0 1:TestOre:50:2 7:TestOre:40:0 ",
          "the routes coin, lock, split, move, and remove do their commands");
    reply = send("GET", "/v1/dev/items", forA);
    const json list = json::parse(reply.body);
    check(reply.status == 200 && list["inWorld"] == true && list["coin"] == 12345 && list["slots"] == 8 && list["items"].size() == 3
              && list["items"][1]["slot"] == 1 && list["items"][1]["item"] == "TestOre" && list["items"][1]["quantity"] == 50
              && list["items"][1]["locked"] == true && list["items"][1]["index"] == 2
              && list["items"][1]["instance"].get<std::string>().size() == 32,
          "the list route gives the coin, the slot count, and each item with its slot, id, index, quantity, flags, and "
          "instance id");

    const std::string before = rows_text(host.store, kCharacterA);
    check(send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=abc").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=0").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=-5").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=1e3").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=TestOre&quantity=100001").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&quantity=5").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=NoSuchItem").status == 400
              && send("POST", "/v1/dev/items/give", forA + "&item=TestOre&action=64").status == 400,
          "the give route refuses a quantity that is not a whole number from 1 to 100000, a request with no item, an "
          "unknown item, and a cause above 63 (reply 400)");
    check(send("POST", "/v1/dev/items/remove", forA + "&slot=500").status == 400 && send("POST", "/v1/dev/items/remove", forA + "&slot=-1").status == 400
              && send("POST", "/v1/dev/items/remove", forA + "&slot=3").status == 400
              && send("POST", "/v1/dev/items/remove", forA + "&slot=0&quantity=51").status == 400
              && send("POST", "/v1/dev/items/move", forA + "&from=0").status == 400
              && send("POST", "/v1/dev/items/split", forA + "&slot=0&quantity=50").status == 400
              && send("POST", "/v1/dev/items/lock", forA + "&slot=0&locked=2").status == 400
              && send("POST", "/v1/dev/items/coin", forA + "&amount=100000001").status == 400
              && send("POST", "/v1/dev/items/coin", forA + "&amount=1.5").status == 400,
          "the other routes refuse a slot that is out of range or empty, a quantity that is too large, a parameter that "
          "is not there, and a coin amount that is above the limit or not a whole number (reply 400)");
    check(send("POST", "/v1/dev/items/give", std::string("character=") + kCharacterNone + "&item=TestOre").status == 404
              && send("GET", "/v1/dev/items", std::string("character=") + kCharacterNone).status == 404
              && send("POST", "/v1/dev/items/give", "item=TestOre").status == 400,
          "a character that the store does not have gives the reply 404, and a request with no character the reply 400");
    check(send("GET", "/v1/dev/items", forA, "gateway.test").status == 403 && send("POST", "/v1/dev/items/give", forA + "&item=TestOre", "gateway.test").status == 403
              && send("POST", "/v1/dev/items/coin", forA + "&amount=1", "gateway.test").status == 403,
          "a request with a service host (a request of the game) gets the reply 403");
    check(rows_text(host.store, kCharacterA) == before && host.store.currency(kCharacterA, state::kCurrencyCoin) == 12345,
          "no request that was refused changed a row");
}

} // namespace

std::vector<Case> inventory_cases() {
    return {
        {"B1", "item catalog: the file of the tool, the index of each item, the lookups, a file that is not correct", catalog_file},
        {"B2", "wire bytes: an item with and with no transmog part, an empty slot, the container state, the journal, the coin state", wire_bytes},
        {"B3", "give, remove, and coin: the rows, what the session of the owner gets, each refusal, a character that is not in the world", give_and_remove},
        {"B4", "move, swap, stack, and split inside the bag, with correct and with wrong arguments", bag_operations},
        {"B5", "client requests: drop (type 1838) and lock (type 2549) with a correct and with a wrong body", client_requests},
        {"B6", "two sessions: an action of A changes nothing for B, and an observer gets no fragment of the bag", two_sessions},
        {"B7", "persistence: the items after a new entry of the character, and in a new world object on the same database", persistence},
        {"B8", "a bag that is larger than one carrier message: more than one bundle, each one below the size limit", large_bag},
        {"B9", "development routes for the items: each command, and each refusal", item_routes},
    };
}

} // namespace soulwarden::backend::tests

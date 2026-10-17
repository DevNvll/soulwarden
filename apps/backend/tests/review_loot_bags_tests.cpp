// The cases of the code review of stage 3.5b.2 (findings LB-R1 to LB-R9) and the open point of 3.5b.2 (an item of a kill that the item
// catalog does not have), corrected in stage 2.2. Each case fails on the tree of the commit of stage 3.5b.2 and passes with the correction.
// The cases come from the proof tests of the review (LBP1 to LBP8, LBT1, LBT2). Each case uses only interfaces of the tree of stage 3.5b.2,
// so that the same file proves the fault on that tree. Made-up accounts, ids, names, and data only.
//
//   RLB1  LB-R1: the item catalog tool exports Nonremovable and NotDroppable (check_item_catalog_tool.py), the loader reads them, and a
//         protected item stays with its character: an inventory discard (1838, with and with no loot container), an equipped discard
//         (826), and a give into an open bag (2801)
//   RLB2  LB-R2: a give (2801) after the fast end of an empty bag is refused, with either order of the request and the due tick
//   RLB3  LB-R3: a walk makes the selection of the nearest 30 storage keys again; an open bag stays in the selected keys
//   RLB4  LB-R4: a give into a part of 500 stacks: a merge into a stack, or a refusal (the source item stays); the same bound when a
//         part is made or loaded
//   RLB5  LB-R5: the GlobalStorage map of 30 bags of 200 items each: the nearest keys, each whole, in one bundle below 256,000 bytes
//   RLB6  LB-R6: the loader refuses a LootDrop slot that a different component of the template uses
//   RLB7  LB-R7: the review cases of the check of the progression tool (an attribute table that does not cover the attribute range)
//   RLB8  LB-R8: the end of a bag finds its rows by an index of the bag key (schema 12 and its migration), and a load case with due ends
//   RLB9  LB-R9: a restart keeps the fast end of an empty bag: an ended bag goes at the start, a later fast end stays the same
//   RLB10 the open point of 3.5b.2: an item of a kill that the item catalog does not have stays in the rows of the bag (the player does
//         not see it; a log line names it); with the item in the catalog a restart gives it to the player

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "support/cases.h"
#include "support/check.h"
#include "support/loot_bag_fixture.h"

namespace soulwarden::backend::tests {
namespace review_loot_bags {

using namespace bags;
using namespace std::chrono_literals;
using nlohmann::json;

/** The made-up items of check_item_catalog_tool.py (the export of the case "protections"). */
constexpr const char* kProtectedHelm = "ReviewProtectedHelm";
constexpr const char* kNoDropOre = "ReviewNoDropOre";
constexpr const char* kFreeOre = "ReviewFreeOre";
/** The client reads 256,000 bytes of one bundle at most (client 0x646C2B0). */
constexpr std::size_t kClientBundleLimit = 256000;

world::InteractionReference own_ref(const world::Entity& entity) {
    world::InteractionReference result;
    result.actor = entity.actor.bytes;
    result.group = entity.id;
    return result;
}

/** A bag of a made-up kill at a place with a part of character A: `items` (TestStone: one stack for each item). @return Its key. */
std::string make_bag(BagShop& shop, const state::WorldPosition& position, const std::vector<world::GrantItem>& items) {
    world::Entity creature;
    creature.position = position;
    world::KillBag kill = shop.bags().begin_kill(creature);
    check(shop.bags().stage_part(kill, *shop.entity(kCharacterA), items, nullptr), "setup: the part of a made-up kill is stored");
    const std::string key = kill.key;
    shop.bags().finish_kill(kill);
    return key;
}

std::string make_bag(BagShop& shop, const state::WorldPosition& position, std::uint32_t stones = 1) {
    return make_bag(shop, position, {world::GrantItem{"TestStone", stones, std::nullopt, {}}});
}

std::array<std::uint8_t, 16> actor_of(BagShop& shop, const std::string& key) {
    const world::LootBag* bag = shop.bags().find(key);
    const world::Entity* entity = bag != nullptr ? shop.host.world.find(bag->entity) : nullptr;
    return entity != nullptr ? entity->actor.bytes : std::array<std::uint8_t, 16>{};
}

bool has_key(const std::vector<world::StorageKey>& map, const std::array<std::uint8_t, 16>& actor) {
    return std::any_of(map.begin(), map.end(), [&](const world::StorageKey& key) { return key.actor == actor; });
}

bool has_entry(const std::vector<StorageEntry>& map, const std::array<std::uint8_t, 16>& actor) {
    return std::any_of(map.begin(), map.end(), [&](const StorageEntry& entry) { return entry.actor == actor; });
}

std::optional<std::uint32_t> slot_of(const world::Bag& bag, const std::string& itemId) {
    for (std::uint32_t slot = 0; slot < bag.slots.size(); ++slot) {
        if (bag.slots[slot].item && bag.slots[slot].item->itemId == itemId) {
            return slot;
        }
    }
    return std::nullopt;
}

Bytes drop_body(std::uint64_t entity, std::uint32_t slot, const world::InstanceId& instance, std::uint32_t quantity) {
    server::hub::Writer body;
    body.u64(0x2FEF73D6DC525917ULL);
    body.u64(entity);
    body.size(1);
    body.u32(slot);
    body.raw(instance);
    body.u32(0xFFFFFFFFU);
    body.u32(quantity);
    return body.data();
}

Bytes paperdoll_drop_body(std::uint64_t entity, std::uint32_t slot, std::uint32_t quantity) {
    server::hub::Writer body;
    body.u64(0x2FEF73D6DC525917ULL);
    body.u64(entity);
    body.u32(slot);
    body.u32(quantity);
    return body.data();
}

/** 1838 of client A for the stack of an item in the bag of A. */
void discard(BagShop& shop, const char* itemId, std::uint32_t quantity) {
    world::Entity& a = *shop.entity(kCharacterA);
    const auto slot = slot_of(a.bag, itemId);
    check(slot.has_value(), std::string("setup: A has ") + itemId);
    if (slot) {
        shop.send(shop.clientA, server::rep::kTypeDropInventoryItems, drop_body(a.id, *slot, a.bag.slots[*slot].item->instance, quantity));
    }
}

/** 2801 of client A: a quantity of the stack of an item in the bag of A into the open bag. */
void give(BagShop& shop, const char* itemId, std::uint32_t quantity) {
    world::Entity& a = *shop.entity(kCharacterA);
    const auto slot = slot_of(a.bag, itemId);
    check(slot.has_value(), std::string("setup: A has ") + itemId + " for the give");
    if (slot) {
        shop.send(shop.clientA, server::rep::kTypeGiveItem, BagShop::bag_body(a, a, true, a.bag.slots[*slot].item->instance, quantity));
    }
}

/** Runs a check script of the tools with arguments. @return Its output lines (each one is also a note). */
std::vector<std::string> run_check(const Options& options, const std::string& script, const std::vector<std::string>& arguments) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    check(std::filesystem::is_regular_file(path), "the check script " + script + " is in the tree");
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-rlb.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + path.lexically_normal().string() + "\"";
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    command += " > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** @return True when a line starts with "ok" and names the case (the text before its first colon), and no line FAILs it. */
bool passed(const std::vector<std::string>& lines, const std::string& name) {
    const bool ok = std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        if (line.rfind("ok", 0) != 0) {
            return false;
        }
        const std::size_t start = line.find_first_not_of(' ', 2);
        return start != std::string::npos && line.compare(start, name.size() + 1, name + ":") == 0;
    });
    const bool failed = std::any_of(lines.begin(), lines.end(),
                                    [&](const std::string& line) { return line.rfind("FAIL", 0) == 0 && line.find(name + ":") != std::string::npos; });
    return ok && !failed;
}

bool run_sql(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* connection = nullptr;
    const bool opened = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK;
    const bool ok = opened && sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    return ok;
}

/** RLB1 (LB-R1): the protections of the item table, from the export of the tool to the three release paths. */
void protected_items(const Options& options) {
    const std::filesystem::path work = options.workDirectory / "review-loot-bags-items";
    const std::vector<std::string> lines = run_check(options, "tools/gamedata/check_item_catalog_tool.py", {work.string(), "protections"});
    check(passed(lines, "protections"), "the item catalog tool exports Nonremovable and NotDroppable of each master item row");
    const std::string file = (work / "item-catalog-protections.json").string();
    // With the loot container: 1838 and 2801. The protected item stays; the request changes nothing.
    {
        BagShop shop;
        check(shop.host.catalog.load(file), "the backend loads the catalog of the tool");
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        check(shop.host.world.inventory().give(kCharacterA, kNoDropOre, 5).ok() && shop.host.world.inventory().give(kCharacterA, kFreeOre, 5).ok()
                  && shop.host.world.inventory().give(kCharacterA, kProtectedHelm, 1).ok(),
              "setup: A has 5 ore with NotDroppable, 5 free ore, and the helm with Nonremovable");
        discard(shop, kNoDropOre, 2);
        discard(shop, kProtectedHelm, 1);
        note("RLB1 1838: no-drop ore " + std::to_string(held(a.bag, kNoDropOre)) + ", helm " + std::to_string(held(a.bag, kProtectedHelm))
             + ", bags " + std::to_string(shop.bags().bag_count()));
        check(held(a.bag, kNoDropOre) == 5 && held(a.bag, kProtectedHelm) == 1 && shop.bags().bag_count() == 0
                  && shop.host.store.list_loot_bags().empty(),
              "1838: an item with NotDroppable and an item with Nonremovable stay in the bag; no Discarded bag, no row");
        // Control: a free item goes into a Discarded bag. The give (2801) of a protected item into the open bag changes nothing.
        discard(shop, kFreeOre, 3);
        const world::LootBag* bag = shop.bag_of(kCharacterA);
        check(bag != nullptr && in_part(*bag, kCharacterA, kFreeOre) == 3 && held(a.bag, kFreeOre) == 2,
              "control: 1838 of 3 free ore makes a Discarded bag with them");
        if (bag == nullptr) {
            return;
        }
        const std::string key = bag->key;
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        give(shop, kNoDropOre, 1);
        give(shop, kProtectedHelm, 1);
        note("RLB1 2801: no-drop ore " + std::to_string(held(a.bag, kNoDropOre)) + " (part " + std::to_string(in_part(*shop.bags().find(key), kCharacterA, kNoDropOre))
             + "), helm " + std::to_string(held(a.bag, kProtectedHelm)));
        check(held(a.bag, kNoDropOre) == 5 && held(a.bag, kProtectedHelm) == 1 && in_part(*shop.bags().find(key), kCharacterA, kNoDropOre) == 0
                  && in_part(*shop.bags().find(key), kCharacterA, kProtectedHelm) == 0,
              "2801: a protected item does not go into the open bag (a bag that ends); it stays in the bag of the character");
        give(shop, kFreeOre, 1);
        check(held(a.bag, kFreeOre) == 1 && in_part(*shop.bags().find(key), kCharacterA, kFreeOre) == 4, "control: 2801 of a free ore goes into the open bag");
        shop.end_block(shop.clientA);
        // 826: the equipped helm with Nonremovable stays in its slot.
        const auto helm = slot_of(a.bag, kProtectedHelm);
        check(helm && shop.host.world.inventory().equip(kCharacterA, *helm).ok() && a.equipment.item(world::paperdoll::kHead) != nullptr,
              "setup: A wears the protected helm");
        const std::size_t before = shop.bags().bag_count();
        shop.send(shop.clientA, server::rep::kTypePaperdollDropItem, paperdoll_drop_body(a.id, world::paperdoll::kHead, 1));
        note("RLB1 826: helm in the head slot " + std::to_string(a.equipment.item(world::paperdoll::kHead) != nullptr) + ", bags "
             + std::to_string(shop.bags().bag_count()));
        check(a.equipment.item(world::paperdoll::kHead) != nullptr && shop.bags().bag_count() == before,
              "826: the equipped item with Nonremovable stays in its slot of the paperdoll; no Discarded bag");
        check(shop.bags().view(kCharacterB).bags.empty() && held(shop.entity(kCharacterB)->bag, kFreeOre) == 0,
              "two sessions: the second character gets no part and no item");
    }
    // With no loot container (1838 removes the items, the rule of stage 1.3): the protected item stays too.
    BagShop old(":memory:", false, false);
    check(old.host.catalog.load(file), "the backend loads the catalog of the tool (no loot container)");
    old.enter();
    world::Entity& a = *old.entity(kCharacterA);
    (void)old.host.world.inventory().give(kCharacterA, kNoDropOre, 5);
    (void)old.host.world.inventory().give(kCharacterA, kFreeOre, 5);
    discard(old, kNoDropOre, 2);
    discard(old, kFreeOre, 2);
    check(held(a.bag, kNoDropOre) == 5 && held(a.bag, kFreeOre) == 3,
          "1838 with no loot container: the item with NotDroppable stays; control: 2 free ore are gone");
}

/** RLB2 (LB-R2): a give after the fast end of an empty bag (the reviewer's probe LBP1), and a give before it (the refill rule). */
void give_after_fast_end(const Options&) {
    for (const bool tickFirst : {false, true}) {
        BagShop shop;
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::string key = make_bag(shop, kKillPlace);
        shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
        check(shop.bags().take_all(kCharacterA, own_ref(a)).ok() && shop.bags().find(key)->emptied.has_value(),
              "setup: the take of the last item; the panel use stays open");
        const auto deadline = *shop.bags().find(key)->emptied;
        shop.host.clock.advance(deadline - shop.host.clock.now() + 1ms);
        if (tickFirst) {
            shop.host.world.tick();
        }
        give(shop, "TestStone", 1);
        const world::LootBag* bag = shop.bags().find(key);
        note("RLB2 tick_first=" + std::to_string(tickFirst) + " bag_alive=" + std::to_string(bag != nullptr) + " held="
             + std::to_string(held(a.bag, "TestStone")));
        check(bag == nullptr && held(a.bag, "TestStone") == 1 && shop.host.store.list_loot_bags().empty(),
              tickFirst ? "the due tick first, then the give: the bag ended, the item stays with the character"
                        : "the give 1 ms after the fast end, before the due tick: refused; the bag ends, the item stays with the character");
        check(held(shop.entity(kCharacterB)->bag, "TestStone") == 0, "two sessions: the other character stays unchanged");
    }
    // Control: a give before the fast end refills the bag (the refill rule): the bag stays after the old fast end.
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const std::string key = make_bag(shop, kKillPlace);
    shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
    check(shop.bags().take_all(kCharacterA, own_ref(a)).ok(), "setup: the take of the last item");
    const auto deadline = *shop.bags().find(key)->emptied;
    shop.host.clock.advance(deadline - shop.host.clock.now() - 100ms);
    give(shop, "TestStone", 1);
    shop.pass(300);
    check(shop.bags().find(key) != nullptr && in_part(*shop.bags().find(key), kCharacterA, "TestStone") == 1 && !shop.bags().find(key)->emptied,
          "control: a give 100 ms before the fast end is accepted; the bag stays after the old fast end");
}

/** RLB3 (LB-R3): a walk to the 31st bag (the reviewer's probe LBP3), and an open bag that is not one of the 30 nearest. */
void storage_after_walk(const Options&) {
    {
        BagShop shop;
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        for (int index = 0; index < 30; ++index) {
            (void)make_bag(shop, {100.0F + static_cast<float>(index) * 0.1F, 203.0F, 30.0F});
        }
        const state::WorldPosition other{160.0F, 203.0F, 30.0F};
        const std::string key = make_bag(shop, other);
        const auto actor = actor_of(shop, key);
        shop.pass(300);
        check(a.storageMap.size() == 30 && !has_key(a.storageMap, actor), "setup: the 31st bag is not one of the 30 nearest");
        const std::size_t from = shop.clientA.got.size();
        const std::uint64_t looked = shop.bags().counts().refreshBags;
        // Accepted position reports at a walk speed (5 m/s): no teleport, no change of an entity value by the test.
        for (int step = 0; step < 120; ++step) {
            shop.placeA.x += 0.5F;
            shop.host.steps(10, {&shop.clientA, &shop.clientB});
            shop.report(shop.clientA, std::nullopt);
        }
        shop.pass(300);
        const auto maps = storage_maps(shop.clientA, from);
        const std::uint64_t walkLooks = shop.bags().counts().refreshBags - looked;
        note("RLB3 x=" + std::to_string(a.position.x) + " near_bag_in_map=" + std::to_string(has_key(a.storageMap, actor))
             + " storage_updates=" + std::to_string(maps.size()) + " bags_looked_at=" + std::to_string(walkLooks));
        check(std::abs(a.position.x - other.x) < 1.0F, "the server accepted the walk to the 31st bag");
        check(has_key(a.storageMap, actor) && a.storageMap.size() == 30 && !maps.empty() && has_entry(maps.back(), actor),
              "the walk makes the selection again: the bag near the character is one of the 30 keys of the world and of the owner map");
        check(walkLooks <= 31U * 40U, "the selections of the walk look only at the 31 bags of the character (a selection after each 2 m at most)");
        check(storage_maps(shop.clientB, from).empty(), "two sessions: the other character gets no storage map");
    }
    // An open bag stays in the selected keys: 30 bags nearer than the open one.
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    for (int index = 0; index < 30; ++index) {
        (void)make_bag(shop, {100.0F, 200.5F, 30.0F});
    }
    const std::string key = make_bag(shop, {100.0F, 204.0F, 30.0F});
    const auto actor = actor_of(shop, key);
    shop.pass(100);
    check(!has_key(a.storageMap, actor), "setup: the bag at 4 m is not one of the 30 nearest");
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
    shop.pass(100);
    const auto maps = storage_maps(shop.clientA, from);
    note("RLB3 open bag in map=" + std::to_string(has_key(a.storageMap, actor)) + " client maps=" + std::to_string(maps.size()));
    check(panels(shop.clientA, from).size() >= 1, "setup: the open of the bag at 4 m is accepted (the panel)");
    check(has_key(a.storageMap, actor) && a.storageMap.size() == 30 && !maps.empty() && has_entry(maps.back(), actor),
          "the open bag is one of the selected keys (it takes the place of the farthest one), also on the wire");
}

/** RLB4 (LB-R4): a part of 500 stacks (the reviewer's probe LBP4), a merge, and the bound when a part is made or loaded. */
void full_part(const Options& options) {
    {
        BagShop shop;
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::string key = make_bag(shop, kKillPlace, world::kMaxContainerSlots);
        shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
        check(shop.host.world.inventory().give(kCharacterA, "TestStone", 1).ok(), "setup: one carried stone for the give");
        give(shop, "TestStone", 1);
        const std::size_t stacks = shop.bags().find(key)->parts.at(kCharacterA).items.size();
        const auto encoded = server::rep::encode_selected_container_state(a, server::rep::View::owner, {0, 100000});
        const WireContainer panel = read_container(std::span<const std::uint8_t>(encoded.front()).subspan(2));
        note("RLB4 ground_stacks=" + std::to_string(stacks) + " carried=" + std::to_string(held(a.bag, "TestStone")) + " full_panel_decodes="
             + std::to_string(panel.ok));
        check(stacks == world::kMaxContainerSlots && held(a.bag, "TestStone") == 1,
              "a give of a new stack into a part of 500 stacks is refused: the part keeps 500 stacks, the source item stays with the character");
        check(panel.ok && panel.elements.size() == world::kMaxContainerSlots, "the full vector of the selected Container fits the reader of the client");
        shop.end_block(shop.clientA);
        check(held(shop.entity(kCharacterB)->bag, "TestStone") == 0, "two sessions: the other character stays unchanged");
    }
    {
        // A part of 500 stacks with an ore stack: a give of ore merges into it (the stack keeps its instance id).
        BagShop shop;
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::string key = make_bag(shop, kKillPlace,
                                         {world::GrantItem{"TestStone", world::kMaxContainerSlots - 1, std::nullopt, {}},
                                          world::GrantItem{"TestOre", 5, std::nullopt, {}}});
        shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
        check(shop.host.world.inventory().give(kCharacterA, "TestOre", 10).ok(), "setup: 10 carried ore");
        give(shop, "TestOre", 3);
        const world::LootBag& bag = *shop.bags().find(key);
        note("RLB4 merge: stacks " + std::to_string(bag.parts.at(kCharacterA).items.size()) + ", ore in the part "
             + std::to_string(in_part(bag, kCharacterA, "TestOre")) + ", carried " + std::to_string(held(a.bag, "TestOre")));
        check(bag.parts.at(kCharacterA).items.size() == world::kMaxContainerSlots && in_part(bag, kCharacterA, "TestOre") == 8
                  && held(a.bag, "TestOre") == 7,
              "a give of ore into a part of 500 stacks merges into its ore stack: still 500 stacks, 8 ore in the part, 7 carried");
        shop.end_block(shop.clientA);
    }
    {
        // The bound when a Discarded part is made: 1838 with 501 entries of 1 ore of one stack (501 new stacks).
        BagShop shop;
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        check(shop.host.world.inventory().give(kCharacterA, "TestOre", 1000).ok(), "setup: a stack of 1000 ore");
        const std::uint32_t slot = *slot_of(a.bag, "TestOre");
        server::hub::Writer body;
        body.u64(0x2FEF73D6DC525917ULL);
        body.u64(a.id);
        body.size(world::kMaxContainerSlots + 1);
        for (std::uint32_t entry = 0; entry <= world::kMaxContainerSlots; ++entry) {
            body.u32(slot);
            body.raw(a.bag.slots[slot].item->instance);
            body.u32(0xFFFFFFFFU);
            body.u32(1);
        }
        shop.send(shop.clientA, server::rep::kTypeDropInventoryItems, body.data());
        const world::LootBag* bag = shop.bag_of(kCharacterA);
        note("RLB4 discard of 501 stacks: bags " + std::to_string(shop.bags().bag_count()) + ", stacks "
             + std::to_string(bag != nullptr ? bag->parts.at(kCharacterA).items.size() : 0) + ", carried ore " + std::to_string(held(a.bag, "TestOre")));
        check(shop.bags().bag_count() == 0 && held(a.bag, "TestOre") == 1000 && shop.host.store.list_loot_bags().empty(),
              "a discard of 501 stacks is refused: no Discarded bag of more than 500 stacks; the ore stays with the character");
    }
    {
        // The bound when a part is made: a roll of 600 stacks.
        BagShop shop;
        shop.enter();
        const std::string key = make_bag(shop, kKillPlace, 600);
        const std::size_t stacks = shop.bags().find(key)->parts.at(kCharacterA).items.size();
        std::uint32_t stored = 0;
        for (const auto& [character, row] : shop.host.store.list_loot_bags()) {
            for (const state::ItemRow& item : row.items) {
                stored += item.itemId == "TestStone" ? item.quantity : 0;
            }
        }
        note("RLB4 made: stacks " + std::to_string(stacks) + ", stones in the rows " + std::to_string(stored));
        check(stacks == world::kMaxContainerSlots && stored == 600,
              "a part made from a roll of 600 stacks has 500 stacks; the rows keep each of the 600 items (no item lost)");
    }
    // The bound when a part is loaded: a stored part with 501 stacks (a row of a backend before the correction).
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "review-loot-bags-501.sqlite3";
    std::filesystem::remove(path);
    std::string key;
    {
        BagShop shop(path.string());
        shop.enter();
        key = make_bag(shop, kKillPlace, world::kMaxContainerSlots);
    }
    check(run_sql(path, "INSERT INTO loot_bag_items(character_id, bag_key, position, instance_id, item_id, quantity) VALUES('"
                            + std::string(kCharacterA) + "', '" + key + "', 500, '00112233445566778899aabbccdd0501', 'TestStone', 1);"),
          "setup: the row of a 501st stack");
    BagShop shop(path.string(), true);
    shop.enter();
    const world::LootBag* bag = shop.bags().find(key);
    const std::size_t stacks = bag != nullptr ? bag->parts.at(kCharacterA).items.size() : 0;
    note("RLB4 loaded: stacks " + std::to_string(stacks));
    check(bag != nullptr && stacks == world::kMaxContainerSlots, "a stored part of 501 stacks loads with 500 stacks");
}

/** RLB5 (LB-R5): 30 bags of 200 items each (the reviewer's probe LBP6), at different distances from the character. */
void large_storage_map(const Options&) {
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    std::vector<std::array<std::uint8_t, 16>> actors;
    for (unsigned index = 0; index < 30; ++index) {
        actors.push_back(actor_of(shop, make_bag(shop, {100.0F + static_cast<float>(index) * 0.2F, 203.0F, 30.0F}, 200)));
    }
    const std::size_t from = shop.clientA.got.size();
    shop.pass(500);
    const auto maps = storage_maps(shop.clientA, from);
    std::size_t largest = 0;
    for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
        if (shop.clientA.got[index].type == 8) {
            largest = std::max(largest, shop.clientA.got[index].body.size());
        }
    }
    const std::size_t keys = maps.empty() ? 0 : maps.back().size();
    bool whole = !maps.empty();
    bool nearest = !maps.empty();
    for (std::size_t index = 0; index < keys; ++index) {
        whole = whole && maps.back()[index].items.size() == 200;
        nearest = nearest && has_entry(maps.back(), actors[index]);
    }
    note("RLB5 world keys=" + std::to_string(a.storageMap.size()) + " delivered maps=" + std::to_string(maps.size()) + " keys="
         + std::to_string(keys) + " largest bundle=" + std::to_string(largest));
    check(!maps.empty() && keys >= 20 && keys < 30, "the owner gets a map of the nearest bags of the 30 (fewer keys within the byte limit)");
    check(whole && nearest, "each key of the map has its whole item vector (200 items), and the keys are the nearest bags");
    check(largest <= kClientBundleLimit, "each bundle of the client is below 256,000 bytes");
    check(storage_maps(shop.clientB, from).empty(), "two sessions: the other character gets no storage map");
}

/** A format 3 interaction file with the bag template and the personal container (the reviewer's probe LBP5). */
json interaction_file(unsigned lootSlot) {
    const auto hex = [](std::uint64_t value) {
        char out[17];
        std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(value));
        return std::string(out);
    };
    json options = json::array();
    for (const auto& [name, action] : {std::pair{"TakeAllOption", 28}, std::pair{"OpenStorageOption", 26}}) {
        options.push_back({{"name", name}, {"id", gamedata::interaction_option_id(name)}, {"action", action}, {"committed", true},
                           {"delay", 0.0}, {"hold", 0.5}, {"instantCancel", false}, {"cooldown", 0}, {"maxUsage", 0.0}, {"inactive", 0.0},
                           {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0}, {"requiredCurrency", 0},
                           {"requiredAchievement", -1}, {"statusRules", false}});
    }
    const json bound = {{"slot", 5}, {"entity", hex(kBagInteractEntity)}, {"component", hex(kBagInteractComponent)}, {"position", {0.0, 0.0, 0.0}},
                        {"rangeSq", 25.0}, {"capacity", -1}, {"lineOfSight", false}, {"arcRequired", false}, {"arc", 180.0}, {"lore", -1},
                        {"requires", json::object()},
                        {"options", json::array({json::array({0, hex(kBagStorageEntity)}), json::array({1, hex(kBagStorageEntity)})})}};
    const json slice = {{"slice", "slices/test/instanced_bag.dynamicslice"}, {"asset", "ba600000000000000000000000000002"}, {"sub", 2},
                        {"kind", "lootDrop"}, {"root", hex(kBagRoot)}, {"slots", {{"position", 1}, {"buildable", 0}, {"ownership", 0}, {"vitals", 0}}},
                        {"buildableState", -1}, {"permissionRule", -1}, {"assembly", nullptr}, {"bounds", json::array({bound})},
                        {"children", json::array()}};
    return {{"format", 3U}, {"options", options}, {"achievements", json::array()}, {"objects", json::array()}, {"templates", json::array({slice})},
            {"spawned", json::array()}, {"fixtures", json::object()},
            {"lootContainers", {{"personal", {{"template", 0}, {"kind", 1}, {"lootDropSlot", lootSlot}, {"lifetime", 180.0}, {"emptyLifetime", 1.25},
                                              {"hideDelay", 1.5}, {"rarityParticles", 5}}}}}};
}

/** RLB6 (LB-R6): the LootDrop slot 1 (PositionInTheWorld) and 5 (Interact) of the template, and the valid slot 6. */
void loot_drop_slot(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const unsigned slot : {6U, 1U, 5U}) {
        const auto path = options.workDirectory / ("review-loot-bags-slot-" + std::to_string(slot) + ".json");
        std::ofstream(path) << interaction_file(slot).dump();
        gamedata::InteractionData data;
        const bool accepted = data.load(path.string());
        // The same check of the set function (the data of a test).
        gamedata::InteractionData set;
        gamedata::LootContainer container = bag_container();
        container.lootDropSlot = slot;
        const bool setAccepted = set_bag_interactions(set, false) && set.set_loot_containers({{"personal", container}});
        note("RLB6 loot_drop_slot=" + std::to_string(slot) + " load accepted=" + std::to_string(accepted) + " set accepted="
             + std::to_string(setAccepted) + (accepted ? "" : " (" + data.error() + ")"));
        check(accepted == (slot == 6) && setAccepted == (slot == 6),
              slot == 6 ? "control: the LootDrop slot 6 of the bag template loads"
                        : "the loader refuses the LootDrop slot " + std::to_string(slot) + ": the slot of a different component of the template");
    }
}

/** RLB7 (LB-R7): the review cases of the check of the progression tool (the reviewer's probe LBT1). */
void progression_attribute_tables(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/gamedata/check_progression_tool.py", {});
    check(passed(lines, "attribute-thresholds"), "control: the complete attribute tables give the Focus threshold of 100");
    check(passed(lines, "short-attribute-table"), "the Focus table with no row of the value 100: the tool stops, the old file stays");
    check(passed(lines, "attribute-gap"), "the Strength table with no row of the value 50: the tool stops, the old file stays");
}

/** @return The query plans and the full scan steps of the two deletions of the end of a bag (Store::delete_loot_bag). */
std::pair<std::string, int> deletion_plans(const std::filesystem::path& path) {
    sqlite3* connection = nullptr;
    sqlite3_open(path.string().c_str(), &connection);
    std::string plans;
    int steps = 0;
    for (const char* table : {"loot_bags", "loot_bag_items"}) {
        // The predicate of Store::delete_loot_bag; a key that is not there separates the look from the delete of rows.
        const std::string deletion = std::string("DELETE FROM ") + table + " WHERE bag_key='review-near'";
        sqlite3_stmt* statement = nullptr;
        sqlite3_prepare_v2(connection, ("EXPLAIN QUERY PLAN " + deletion).c_str(), -1, &statement, nullptr);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            plans += (plans.empty() ? "" : "; ") + std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 3)));
        }
        sqlite3_finalize(statement);
        sqlite3_prepare_v2(connection, deletion.c_str(), -1, &statement, nullptr);
        (void)sqlite3_step(statement);
        steps += sqlite3_stmt_status(statement, SQLITE_STMTSTATUS_FULLSCAN_STEP, 0);
        sqlite3_finalize(statement);
    }
    sqlite3_close(connection);
    return {plans, steps};
}

/** RLB8 (LB-R8): the index of the bag key (the reviewer's probe LBP8), its migration, and a load case with due ends. */
void bag_key_index(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "review-loot-bags-index.sqlite3";
    std::filesystem::remove(path);
    { BagShop schema(path.string()); }
    const std::string populate =
        "BEGIN; WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i<20000) INSERT INTO loot_bags(character_id, bag_key, kind, "
        "source, position_x, position_y, position_z, rarity, created_at, expires_at) SELECT 'review-far', printf('bag-%06d', i), 1, 'review', "
        "5000, 5000, 30, 0, 1, 9000000000000000 FROM n; INSERT INTO loot_bag_items(character_id, bag_key, position, instance_id, item_id, "
        "quantity) SELECT character_id, bag_key, 0, printf('%032d', rowid), 'TestStone', 1 FROM loot_bags; COMMIT;";
    check(run_sql(path, populate), "setup: 20,000 far bags with an item in the store of the tree");
    auto [plans, steps] = deletion_plans(path);
    note("RLB8 store: full_scan_steps=" + std::to_string(steps) + " plan=" + plans);
    check(steps == 0 && plans.find("USING INDEX") != std::string::npos && plans.find("SCAN") == std::string::npos,
          "the end of one bag finds its rows by an index of the bag key: no look at the rows of the 20,000 far bags");
    // A store of schema 10 (no index): the migration adds the indexes and keeps the rows.
    // Schema 10 had no world time (11) and no gathering tables (13; the stage 3.6a fixture).
    std::string downgrade = "DROP INDEX IF EXISTS loot_bags_bag_key; DROP INDEX IF EXISTS loot_bag_items_bag_key; DROP TABLE IF EXISTS world_time; "
                            "DROP TABLE IF EXISTS gathering_nodes; PRAGMA user_version=10;";
    {
        sqlite3* connection = nullptr;
        sqlite3_open(path.string().c_str(), &connection);
        sqlite3_stmt* statement = nullptr;
        sqlite3_prepare_v2(connection, "SELECT COUNT(*) FROM pragma_table_info('loot_bags') WHERE name='emptied_at'", -1, &statement, nullptr);
        const bool column = sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 1;
        sqlite3_finalize(statement);
        sqlite3_close(connection);
        if (column) {
            downgrade = "ALTER TABLE loot_bags DROP COLUMN emptied_at; " + downgrade;
        }
    }
    // 10 bags are enough for the migration (the loader of the rows is not a part of this case).
    downgrade += " DELETE FROM loot_bag_items WHERE bag_key > 'bag-000010'; DELETE FROM loot_bags WHERE bag_key > 'bag-000010';";
    check(run_sql(path, downgrade), "setup: the tables of schema 10");
    std::size_t parts = 0;
    {
        BagShop migrated(path.string(), true);
        parts = migrated.host.store.list_loot_bags().size();
    }
    std::tie(plans, steps) = deletion_plans(path);
    note("RLB8 migrated: parts=" + std::to_string(parts) + " full_scan_steps=" + std::to_string(steps) + " plan=" + plans);
    check(parts == 10 && steps == 0 && plans.find("USING INDEX") != std::string::npos,
          "the migration of a store of schema 10 adds the indexes and keeps each bag row");

    // The load case with due ends (GL7 has none): 100 near bags end in one tick, with 20,000 far bags in the store and in the world.
    // The limit, written before the run: the tick of the 100 ends takes less than 1 s.
    constexpr double kLimitSeconds = 1.0;
    BagShop shop;
    shop.host.world.tick();
    world::LootBags& bags = shop.bags();
    const auto stage = [&](const std::string& owner, const state::WorldPosition& position) {
        world::Entity creature;
        creature.position = position;
        world::KillBag kill = bags.begin_kill(creature);
        world::Entity character;
        character.characterId = owner;
        (void)bags.stage_part(kill, character, {world::GrantItem{"TestOre", 1, std::nullopt, {}}}, nullptr);
        bags.finish_kill(kill);
    };
    for (int index = 0; index < 100; ++index) {
        stage("near-" + std::to_string(index), {static_cast<float>(index), 0.0F, 30.0F});
    }
    shop.host.clock.advance(100s); // The far bags end 100 s after the near bags.
    for (int index = 0; index < 20000; ++index) {
        stage("far-" + std::to_string(index), {5000.0F + static_cast<float>(index % 200) * 20.0F, 5000.0F + static_cast<float>(index / 200) * 20.0F, 30.0F});
    }
    shop.host.clock.advance(80s);
    const world::LootBagCounts before = bags.counts();
    const auto start = std::chrono::steady_clock::now();
    shop.host.world.tick();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const std::uint64_t ended = bags.counts().expired - before.expired;
    const std::uint64_t looks = bags.counts().dueLooks - before.dueLooks;
    note("RLB8 due ends: " + std::to_string(ended) + " bags ended, " + std::to_string(looks) + " due looks, " + std::to_string(seconds) + " s");
    check(ended == 100 && looks == 100 && bags.bag_count() == 20000 && shop.host.store.list_loot_bags().size() == 20000,
          "the tick ends the 100 near bags and looks only at their due entries; the 20,000 far bags stay");
    check(seconds < kLimitSeconds, "the tick of the 100 ends with 20,000 far bags in the store takes less than 1 s");
}

/** RLB9 (LB-R9): a restart after and before the fast end of an empty bag (the reviewer's probe LBP2). */
void fast_end_restart(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const bool afterEnd : {true, false}) {
        const auto path = options.workDirectory / (afterEnd ? "review-loot-bags-fast-after.sqlite3" : "review-loot-bags-fast-before.sqlite3");
        std::filesystem::remove(path);
        std::string key;
        std::chrono::nanoseconds taken{};
        {
            BagShop shop(path.string());
            shop.enter();
            world::Entity& a = *shop.entity(kCharacterA);
            key = make_bag(shop, kKillPlace);
            shop.request(shop.clientA, shop.bag_block(*shop.bags().find(key), kOpenRow));
            check(shop.bags().take_all(kCharacterA, own_ref(a)).ok(), "setup: the take of the last item before the restart");
            taken = std::chrono::duration_cast<std::chrono::nanoseconds>(shop.host.clock.now().time_since_epoch()) - 1'000'000s;
        }
        BagShop shop(path.string(), true);
        // The restart: 10 s after the take (after the fast end of 1.25 s), or 200 ms after it (before the fast end).
        shop.host.clock.advance(taken + (afterEnd ? 10'000ms : 200ms));
        shop.host.world.tick();
        const world::LootBag* bag = shop.bags().find(key);
        if (afterEnd) {
            note("RLB9 restart after the fast end: bag loaded " + std::to_string(bag != nullptr) + ", stored parts "
                 + std::to_string(shop.host.store.list_loot_bags().size()));
            check(bag == nullptr && shop.host.store.list_loot_bags().empty(),
                  "a restart 10 s after the take of the last item: the empty bag does not come back, and its rows go at the start");
            continue;
        }
        const double left = shop.bags().view(kCharacterA).bags.empty() ? 0.0 : shop.bags().view(kCharacterA).bags[0].secondsLeft;
        check(bag != nullptr && bag->emptied.has_value() && left > 170.0, "a restart 200 ms after the take: the bag comes back, empty, with its lifetime");
        // The stored fast end: 1.25 s after the take, not 1.25 s after the restart.
        shop.host.clock.advance(1050ms - 1ms);
        shop.host.world.tick();
        const bool there = shop.bags().find(key) != nullptr;
        shop.host.clock.advance(1ms);
        shop.host.world.tick();
        note("RLB9 restart before the fast end: there 1 ms before the stored end " + std::to_string(there) + ", gone at it "
             + std::to_string(shop.bags().find(key) == nullptr));
        check(there && shop.bags().find(key) == nullptr && shop.host.store.list_loot_bags().empty(),
              "the bag ends at the stored fast end (1.25 s after the take); the restart does not renew it");
    }
}

/** RLB10 (the open point of 3.5b.2): the catalog has no TestSword, which the roll of the wolf gives. */
void unknown_kill_item(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-loot-bags-unknown.sqlite3";
    std::filesystem::remove(path);
    std::string key;
    {
        BagShop shop(path.string());
        std::vector<gamedata::ItemDefinition> items = bag_items();
        std::erase_if(items, [](const gamedata::ItemDefinition& item) { return item.id == "TestSword"; });
        shop.host.catalog.set(std::move(items));
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::size_t from = shop.clientA.got.size();
        const std::uint64_t lost = shop.host.world.rewards().counts().itemsLost;
        shop.kill({kCharacterA});
        shop.pass(300);
        const world::LootBag* bag = shop.bag_of(kCharacterA);
        check(bag != nullptr && in_part(*bag, kCharacterA, "TestOre") == 3, "setup: the kill makes a bag with the 3 ore");
        if (bag == nullptr) {
            return;
        }
        key = bag->key;
        bool storedSword = false;
        for (const auto& [character, row] : shop.host.store.list_loot_bags()) {
            for (const state::ItemRow& item : row.items) {
                storedSword = storedSword || (character == kCharacterA && item.itemId == "TestSword" && item.quantity == 1);
            }
        }
        const auto maps = storage_maps(shop.clientA, from);
        note("RLB10 stored sword=" + std::to_string(storedSword) + " items lost=" + std::to_string(shop.host.world.rewards().counts().itemsLost - lost)
             + " map items=" + std::to_string(maps.empty() || maps.back().empty() ? 0 : maps.back()[0].items.size()));
        check(storedSword && shop.host.world.rewards().counts().itemsLost == lost,
              "the item that the catalog does not have stays in the rows of the part of the bag: no item is lost");
        check(!maps.empty() && maps.back().size() == 1 && maps.back()[0].items.size() == 1 && in_part(*bag, kCharacterA, "TestSword") == 0,
              "the player does not see it: the map key of the bag has only the ore, and the part has no instance of it");
        check(log_count("keeps 1 TestSword") == 1, "a log line names the kept item");
        // Take All: the ore. The kept item keeps the bag (no fast end); the entry of the owner is Looted.
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
        shop.end_block(shop.clientA);
        shop.pass(1500);
        bag = shop.bags().find(key);
        check(held(a.bag, "TestOre") == 3 && bag != nullptr && !bag->emptied,
              "after the take of the ore the bag stays (its rows keep the item): no end after the last visible item");
        check(held(shop.entity(kCharacterB)->bag, "TestOre") == 0 && shop.bags().view(kCharacterB).bags.empty(),
              "two sessions: the other character has no part and no item");
    }
    // A restart with the complete catalog: the kept item gets its instance with the values of the item table.
    BagShop shop(path.string(), true);
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const world::LootBag* bag = shop.bags().find(key);
    const world::ItemInstance* sword = nullptr;
    if (bag != nullptr) {
        for (const world::ItemInstance& item : bag->parts.at(kCharacterA).items) {
            sword = item.itemId == "TestSword" ? &item : sword;
        }
    }
    note("RLB10 after the restart: bag " + std::to_string(bag != nullptr) + ", sword " + std::to_string(sword != nullptr)
         + (sword != nullptr ? ", gear score " + std::to_string(sword->gearScore) + ", durability " + std::to_string(sword->durability) : ""));
    // The roll of an item with no row in the catalog has no gear score (its range comes from the row): the start value of the row (200).
    check(sword != nullptr && sword->index == 1 && sword->gearScore == 200 && sword->durability == 1000 && sword->maxDurability == 1000
              && sword->instance != world::InstanceId{},
          "with the item in the catalog the restart makes the kept item with the start values of the item table (gear score, durability)");
    if (bag != nullptr && sword != nullptr) {
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
        shop.end_block(shop.clientA);
    }
    check(held(a.bag, "TestSword") == 1, "the player takes the kept item");
}

} // namespace review_loot_bags

std::vector<Case> review_loot_bags_cases() {
    using namespace review_loot_bags;
    return {{"RLB1", "review of stage 3.5b.2, LB-R1: the removal protections of the item table, from the export to 1838, 826, and 2801", protected_items},
            {"RLB2", "review of stage 3.5b.2, LB-R2: a give after the fast end of an empty bag is refused in either order", give_after_fast_end},
            {"RLB3", "review of stage 3.5b.2, LB-R3: a walk makes the 30 nearest storage keys again; an open bag stays in them", storage_after_walk},
            {"RLB4", "review of stage 3.5b.2, LB-R4: 500 stacks of a part: a give merges or is refused; the bound at a make and a load", full_part},
            {"RLB5", "review of stage 3.5b.2, LB-R5: the storage map of 30 large bags keeps the bundle below 256,000 bytes", large_storage_map},
            {"RLB6", "review of stage 3.5b.2, LB-R6: the loader refuses a LootDrop slot of a different component", loot_drop_slot},
            {"RLB7", "review of stage 3.5b.2, LB-R7: the progression tool stops at an attribute table that is not complete", progression_attribute_tables},
            {"RLB8", "review of stage 3.5b.2, LB-R8: the end of a bag uses an index of the bag key; a load case with due ends", bag_key_index},
            {"RLB9", "review of stage 3.5b.2, LB-R9: a restart keeps the fast end of an empty bag", fast_end_restart},
            {"RLB10", "open point of stage 3.5b.2: an item of a kill that the catalog does not have stays in the rows of the bag", unknown_kill_item}};
}

} // namespace soulwarden::backend::tests

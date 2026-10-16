// Tests of stage 3.5b.2: the personal loot bags of the kills and the discard of a player (docs/systems/loot-bags.md, protocol notes
// 4.42; research chests-ground-loot.md, the criteria I9 to I15 and the cases GL1 to GL7).
//
//   GL1  I9: an eligible kill makes a personal bag (no item before the pickup; the coin and the experience at the kill); the record, the
//        LootTracker entry, and the storage map of the owner; no data of format 3: the items go into the bag of the character (3.5a)
//   GL2  I10: the panel (OpenStorageOption, slot 76 after the request), a take of a part of a stack and of a whole instance (2942), Take
//        All (2939), Take All of the prompt (TakeAllOption), a full bag takes what fits; each instance keeps its values
//   GL3  I11, MP-10: two sessions: a character that is not eligible gets no record, no entry, no map; a known reference of the bag or of
//        the Container of a different character changes nothing; a bag of two eligible characters: a part for each
//   GL4  I12: the discard (1838 and 826): a Discarded bag with the items (a whole stack keeps its id), the lock, the recovery with its
//        instance values, a give into the open bag (2801)
//   GL5  I13: the lifetime of the data on a synthetic clock at its boundary (180 s, a lifetime of 50 s of a different file), the end after
//        the last item (1.25 s), a request after the end, the end of an open panel
//   GL6  I14: a refused write changes nothing (a take, a discard, a kill); a restart keeps the bag, its remaining time, and its
//        instances; a leave and an entry; an end while the backend was off
//   GL7  I15: the load: 2,000 and 20,000 far bags, 50 characters near their own bags: the same work of the states and of the due list,
//        and a time limit
//
// The accounts, the characters, the creature kinds, the items, the tables, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "support/cases.h"
#include "support/check.h"
#include "support/loot_bag_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace bags;
using namespace std::chrono_literals;

Bytes drop_body(std::uint64_t entity, const std::vector<world::DropEntry>& entries) {
    server::hub::Writer body;
    body.u64(0x2FEF73D6DC525917ULL);
    body.u64(entity);
    body.size(static_cast<std::uint32_t>(entries.size()));
    for (const world::DropEntry& entry : entries) {
        body.u32(entry.slot);
        body.raw(entry.instance);
        body.u32(0xFFFFFFFFU);
        body.u32(entry.quantity);
    }
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

std::optional<std::uint32_t> slot_of(const world::Bag& bag, const std::string& itemId) {
    for (std::uint32_t slot = 0; slot < bag.slots.size(); ++slot) {
        if (bag.slots[slot].item && bag.slots[slot].item->itemId == itemId) {
            return slot;
        }
    }
    return std::nullopt;
}

const world::ItemInstance* part_item(const world::LootBag& bag, const char* character, const std::string& itemId) {
    const auto part = bag.parts.find(character);
    if (part == bag.parts.end()) {
        return nullptr;
    }
    for (const world::ItemInstance& item : part->second.items) {
        if (item.itemId == itemId) {
            return &item;
        }
    }
    return nullptr;
}

std::array<std::uint8_t, 16> actor_of_bag(BagShop& shop, const world::LootBag& bag) {
    const world::Entity* entity = shop.host.world.find(bag.entity);
    return entity != nullptr ? entity->actor.bytes : std::array<std::uint8_t, 16>{};
}

/** The loot notice: the bag journal of the own character has an element of this item with the cause ItemGathered (1). */
bool notice_of(const FakeClient& client, std::uint32_t index, std::size_t from) {
    for (const Seen& seen : fragments_of(client, kBagSlot61, kContainerType, from)) {
        if (seen.interest != 1) {
            continue;
        }
        const WireContainer container = read_container(field_of(seen.fragment, 0, 0));
        for (const auto& [element, item] : container.elements) {
            if (!container.full && item.index == index && item.action == world::sync::kItemGathered) {
                return true;
            }
        }
    }
    return false;
}

constexpr std::uint32_t kSwordIndex = 1;
constexpr std::uint32_t kOreIndex = 2;

/** GL1: the bag of an eligible kill. */
void bag_spawn(const Options&) {
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const std::uint64_t coinBefore = a.wallet.coin;
    const std::uint32_t experienceBefore = a.progression.experience;
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.kill({kCharacterA});
    shop.pass(500);
    const world::LootBag* bag = shop.bag_of(kCharacterA);
    check(bag != nullptr && bag->kind == world::loot_bag_rules::kAiLoot && bag->parts.size() == 1 && in_part(*bag, kCharacterA, "TestSword") == 1
              && in_part(*bag, kCharacterA, "TestOre") == 3 && bag->source == "Test_BagWolf",
          "an eligible kill makes a personal bag (AILoot) with the items of the roll in the part of the character");
    check(held(a.bag, "TestSword") == 0 && held(a.bag, "TestOre") == 0 && a.wallet.coin > coinBefore && a.progression.experience > experienceBefore,
          "no item of the kill is in the bag of the character before the pickup; the coin and the experience come at the kill");
    if (bag == nullptr) {
        return;
    }
    const world::ItemInstance* sword = part_item(*bag, kCharacterA, "TestSword");
    check(sword != nullptr && sword->gearScore >= 210 && sword->gearScore <= 220 && sword->durability == 1000 && sword->maxDurability == 1000
              && sword->instance != world::InstanceId{} && sword->index == kSwordIndex,
          "the instance is made one time at the kill: the gear score of the roll (210 to 220), the durability, an instance id");
    check(std::abs(bag->position.x - kKillPlace.x) < 0.01F && std::abs(bag->position.y - kKillPlace.y) < 0.01F && bag->entity != 0
              && shop.host.world.find(bag->entity) != nullptr && shop.host.world.find(bag->entity)->object.viewers
                                                                    == std::vector<std::string>{kCharacterA},
          "the bag is at the place of the creature: a server child that only the eligible character observes");
    const std::array<std::uint8_t, 16> actor = actor_of_bag(shop, *bag);
    const auto entry = tracker_entry(shop.clientA, actor, fromA);
    check(entry && entry->state == world::loot_bag_rules::kRolled && entry->kind == world::loot_bag_rules::kAiLoot,
          "the LootTracker of the owner has the entry of the bag: Rolled, the kind AILoot, the actor of the bag (live check L5)");
    check(fragments_of_entity_type(shop.clientA, kLootDropType, fromA) >= 1, "the client of the owner gets the record of the bag with its LootDrop state");
    const auto maps = storage_maps(shop.clientA, fromA);
    check(!maps.empty() && maps.back().size() == 1 && maps.back()[0].actor == actor && maps.back()[0].items.size() == 2,
          "the GlobalStorage map of the owner (slot 77) has the key of the bag with its two stacks (the item list under the prompt)");
    const auto rows = shop.host.store.list_loot_bags();
    check(rows.size() == 1 && rows[0].first == kCharacterA && rows[0].second.items.size() == 2 && rows[0].second.kind == 1
              && rows[0].second.expiresAt - rows[0].second.createdAt == 180000,
          "the store has the part of the character with its two stored instances and the end of the lifetime of the data (180 s)");
    check(fragments_of_entity_type(shop.clientB, kLootDropType, fromB) == 0 && !tracker_entry(shop.clientB, actor, fromB)
              && storage_maps(shop.clientB, fromB).empty(),
          "a character that is not eligible gets no record of the bag, no entry, and no storage map (MP-3)");
    // A kill with no item for any character: no bag.
    const std::size_t bagsBefore = shop.bags().bag_count();
    world::Entity creature;
    creature.position = kKillPlace;
    world::KillBag empty = shop.bags().begin_kill(creature);
    (void)shop.bags().stage_part(empty, a, {}, nullptr);
    shop.bags().finish_kill(empty);
    check(shop.bags().bag_count() == bagsBefore && shop.host.store.list_loot_bags().size() == 1, "a kill with no item makes no bag and no row");

    // No loot container (a file of format 2): the items go into the bag of the character (the rule of stage 3.5a).
    BagShop old(":memory:", false, false);
    old.enter();
    old.kill({kCharacterA});
    old.pass(300);
    world::Entity& oldA = *old.entity(kCharacterA);
    check(old.bags().bag_count() == 0 && held(oldA.bag, "TestSword") == 1 && held(oldA.bag, "TestOre") == 3,
          "with no loot container in the interaction data the items of the kill go into the bag of the character (stage 3.5a)");
}

/** GL2: the panel and the transfers. */
void bag_transfers(const Options&) {
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    shop.kill({kCharacterA});
    shop.pass(300);
    const world::LootBag* bag = shop.bag_of(kCharacterA);
    if (bag == nullptr) {
        check(false, "a bag");
        return;
    }
    const std::string key = bag->key;
    const world::ItemInstance swordBefore = *part_item(*bag, kCharacterA, "TestSword");
    const world::InstanceId ore = part_item(*bag, kCharacterA, "TestOre")->instance;
    // The open: the panel (slot 76) after the request (the client clears its selected Container at its request: live check L5).
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    shop.pass(100);
    auto list = panels(shop.clientA, from);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{26U, 1U}} && !list.empty() && list.back().full
              && list.back().elements.size() == 2,
          "T (OpenStorageOption) is accepted (3743 result 1), and the panel gets the full vector of the part (2 stacks)");
    // A part of a stack (2942): 2 of the 3 ore.
    from = shop.clientA.got.size();
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, ore, 2));
    shop.pass(100);
    bag = shop.bags().find(key);
    const world::ItemInstance* rest = part_item(*bag, kCharacterA, "TestOre");
    const auto oreSlot = slot_of(a.bag, "TestOre");
    check(held(a.bag, "TestOre") == 2 && rest != nullptr && rest->quantity == 1 && rest->instance == ore && oreSlot
              && a.bag.slots[*oreSlot].item->instance != ore && notice_of(shop.clientA, kOreIndex, from),
          "a take of 2 of 3 (2942): the bag gets 2 (a new instance, the loot notice cause 1), the part keeps 1 with its instance id");
    list = panels(shop.clientA, from);
    check(!list.empty() && list.back().full && list.back().elements.size() == 2 && list.back().elements[1].second.quantity == 1,
          "the panel gets the part of now (the full vector)");
    // A whole instance (2942): the sword keeps its instance id, its gear score, its durability, and its flags.
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, swordBefore.instance, 1));
    shop.pass(100);
    const world::ItemInstance* sword = stack_of(a.bag, swordBefore.instance);
    check(sword != nullptr && sword->gearScore == swordBefore.gearScore && sword->durability == swordBefore.durability
              && sword->maxDurability == swordBefore.maxDurability && sword->flags == swordBefore.flags && sword->perks == swordBefore.perks,
          "a take of a whole stack moves the stored instance: the same id, gear score, durability, flags, and perks");
    // A take with a wrong quantity or an instance that the part does not have: no change.
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, ore, 5));
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, swordBefore.instance, 1));
    check(held(a.bag, "TestOre") == 2 && in_part(*shop.bags().find(key), kCharacterA, "TestOre") == 1
              && shop.bags().counts().refusals[static_cast<std::size_t>(world::BagRefusal::badQuantity)] == 1
              && shop.bags().counts().refusals[static_cast<std::size_t>(world::BagRefusal::noItem)] == 1,
          "a quantity above the stack and an instance that the part does not have change nothing");
    // Take All of the panel (2939): the rest; the part is empty: Looted, the empty panel, no key in the map.
    from = shop.clientA.got.size();
    shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
    shop.pass(100);
    bag = shop.bags().find(key);
    const std::array<std::uint8_t, 16> actor = actor_of_bag(shop, *bag);
    const auto entry = tracker_entry(shop.clientA, actor, from);
    list = panels(shop.clientA, from);
    const auto maps = storage_maps(shop.clientA, from);
    check(held(a.bag, "TestOre") == 3 && bag->parts.at(kCharacterA).items.empty() && entry && entry->state == world::loot_bag_rules::kLooted
              && !list.empty() && list.back().elements.empty() && !maps.empty() && maps.back().empty() && bag->emptied.has_value(),
          "Take All (2939): the rest goes into the bag; Looted (the client hides the bag), the empty panel, and no key in the map");
    shop.pass(1300);
    check(shop.bags().find(key) == nullptr && shop.host.store.list_loot_bags().empty(),
          "1.25 s after the last item (FastKillSelfDelay of the data) the bag and its rows go away");

    // Take All of the prompt (E: TakeAllOption): each item at once; the client ends the use at Looted.
    shop.kill({kCharacterA});
    shop.pass(300);
    bag = shop.bag_of(kCharacterA);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.bag_block(*bag, kTakeAllRow));
    shop.pass(100);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{28U, 1U}} && held(a.bag, "TestOre") == 6
              && held(a.bag, "TestSword") == 2 && bag->parts.at(kCharacterA).items.empty() && count_type(shop.clientA, kServerCancel, from) == 0,
          "E (TakeAllOption): accepted, each item into the bag at once, and no 3746 (the client ends the use at Looted)");
    shop.end_block(shop.clientA);

    // A full bag takes what fits; the rest stays in the part (no item lost, made again, or rolled again).
    shop.pass(1500);
    shop.kill({kCharacterA});
    shop.pass(300);
    bag = shop.bag_of(kCharacterA);
    const std::string fullKey = bag->key;
    const world::InstanceId keptSword = part_item(*bag, kCharacterA, "TestSword")->instance;
    std::uint32_t used = 0;
    for (const world::BagSlot& slot : a.bag.slots) {
        used += slot.item ? 1 : 0;
    }
    (void)shop.host.world.inventory().give(kCharacterA, "TestStone", world::kMaxContainerSlots - used);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.bag_block(*bag, kTakeAllRow));
    shop.pass(100);
    bag = shop.bags().find(fullKey);
    check(bag != nullptr && held(a.bag, "TestOre") == 9 && in_part(*bag, kCharacterA, "TestSword") == 1 && in_part(*bag, kCharacterA, "TestOre") == 0
              && count_type(shop.clientA, kServerCancel, from) == 1 && part_item(*bag, kCharacterA, "TestSword")->instance == keptSword
              && tracker_entry(shop.clientA, actor_of_bag(shop, *bag))->state == world::loot_bag_rules::kRolled,
          "a full bag: the ore goes into its stack, the sword stays in the part with its instance id (Rolled), and the use ends (3746)");
    shop.end_block(shop.clientA);
    (void)shop.host.world.inventory().remove(kCharacterA, *slot_of(a.bag, "TestStone"));
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
    check(stack_of(a.bag, keptSword) != nullptr && shop.bags().find(fullKey)->parts.at(kCharacterA).items.empty(),
          "with a free slot the kept sword comes into the bag (the same instance)");
}

/** GL3: two sessions; a known reference of a different character. */
void bag_two_sessions(const Options&) {
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    shop.kill({kCharacterA});
    shop.pass(300);
    const world::LootBag* bag = shop.bag_of(kCharacterA);
    const std::string key = bag->key;
    // B uses the reference of the bag of A (a replay of a known actor and target): refused, no change.
    std::size_t from = shop.clientB.got.size();
    shop.request(shop.clientB, shop.bag_block(*bag, kTakeAllRow));
    shop.end_block(shop.clientB);
    shop.request(shop.clientB, shop.bag_block(*bag, kOpenRow));
    check(replies(shop.clientB, from) == std::vector<std::pair<unsigned, unsigned>>{{28U, 2U}, {26U, 2U}}
              && in_part(*shop.bags().find(key), kCharacterA, "TestOre") == 3 && held(b.bag, "TestOre") == 0 && panels(shop.clientB, from).empty(),
          "B with the known reference of the bag of A: Take All and Open are refused (3743 result 2); no item moves, B gets no panel");
    shop.end_block(shop.clientB);
    // B with the Container reference of A (2939): the session refuses it; with its own reference: B has no open bag.
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    shop.send(shop.clientB, server::rep::kTypeTakeAllItems, BagShop::bag_body(b, a));
    shop.send(shop.clientB, server::rep::kTypeTakeAllItems, BagShop::bag_body(b, b));
    shop.send(shop.clientB, server::rep::kTypeTakeItem,
              BagShop::bag_body(b, b, true, part_item(*shop.bags().find(key), kCharacterA, "TestOre")->instance, 1));
    check(held(b.bag, "TestOre") == 0 && in_part(*shop.bags().find(key), kCharacterA, "TestOre") == 3
              && log_count("is not the selected Container of the character of the session") == 1
              && shop.bags().counts().refusals[static_cast<std::size_t>(world::BagRefusal::notOpen)] == 2,
          "B with the Container of A (2939) and with its own Container while A has the bag open: nothing moves");
    shop.end_block(shop.clientA);
    // One kill with the credit of A and B: one bag, one part for each (each its own roll).
    from = shop.clientB.got.size();
    const std::size_t fromA = shop.clientA.got.size();
    shop.kill({kCharacterA, kCharacterB});
    shop.pass(600);
    const world::LootBag* shared = shop.bag_of(kCharacterB);
    check(shared != nullptr && shared->parts.size() == 2 && in_part(*shared, kCharacterA, "TestOre") == 3 && in_part(*shared, kCharacterB, "TestOre") == 3
              && part_item(*shared, kCharacterA, "TestSword")->instance != part_item(*shared, kCharacterB, "TestSword")->instance,
          "a kill of two eligible characters: one bag with a part for each, each with its own instances");
    const std::array<std::uint8_t, 16> actor = actor_of_bag(shop, *shared);
    check(tracker_entry(shop.clientA, actor, fromA) && tracker_entry(shop.clientB, actor, from)
              && fragments_of_entity_type(shop.clientB, kLootDropType, from) >= 1,
          "each eligible character gets the record and its own entry");
    const auto mapA = storage_maps(shop.clientA, fromA);
    const auto mapB = storage_maps(shop.clientB, from);
    bool own = false;
    for (const StorageEntry& entry : mapB.back()) {
        if (entry.actor == actor) {
            own = entry.items.size() == 2 && std::equal(entry.items[1].instance.begin(), entry.items[1].instance.end(),
                                                         shared->parts.at(kCharacterB).items[1].instance.begin());
        }
    }
    check(own, "the storage map of B has the items of the part of B, not of A");
    // A takes its part: A sees Looted; B still sees Rolled; the bag stays until each part is empty.
    const std::string sharedKey = shared->key;
    shop.request(shop.clientA, shop.bag_block(*shared, kTakeAllRow));
    shop.pass(200);
    shop.end_block(shop.clientA);
    shared = shop.bags().find(sharedKey);
    check(tracker_entry(shop.clientA, actor)->state == world::loot_bag_rules::kLooted
              && tracker_entry(shop.clientB, actor)->state == world::loot_bag_rules::kRolled && in_part(*shared, kCharacterB, "TestOre") == 3
              && !shared->emptied,
          "A takes its part: A sees Looted, B sees Rolled, the part of B stays, and the bag stays");
    shop.request(shop.clientB, shop.bag_block(*shared, kTakeAllRow));
    shop.pass(200);
    shop.end_block(shop.clientB);
    check(held(b.bag, "TestOre") == 3 && shop.bags().find(sharedKey)->emptied.has_value(), "B takes its part: the bag ends after its time");
    shop.pass(1400);
    check(shop.bags().find(sharedKey) == nullptr, "the bag of the two characters goes 1.25 s after the last part");
}

/** GL4: the discard and the recovery. */
void bag_discard(const Options&) {
    BagShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    (void)shop.host.world.inventory().give(kCharacterA, "TestOre", 10);
    (void)shop.host.world.inventory().give(kCharacterA, "TestBoundHelm", 1);
    const std::uint32_t oreSlot = *slot_of(a.bag, "TestOre");
    const std::uint32_t helmSlot = *slot_of(a.bag, "TestBoundHelm");
    const world::ItemInstance helm = *a.bag.slots[helmSlot].item;
    const world::InstanceId oreId = a.bag.slots[oreSlot].item->instance;
    std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.send(shop.clientA, server::rep::kTypeDropInventoryItems,
              drop_body(a.id, {{oreSlot, oreId, 3}, {helmSlot, helm.instance, 1}}));
    shop.pass(200);
    const world::LootBag* bag = shop.bag_of(kCharacterA);
    check(bag != nullptr && bag->kind == world::loot_bag_rules::kDiscarded && in_part(*bag, kCharacterA, "TestOre") == 3
              && in_part(*bag, kCharacterA, "TestBoundHelm") == 1 && held(a.bag, "TestOre") == 7 && held(a.bag, "TestBoundHelm") == 0
              && a.bag.slots[oreSlot].item->instance == oreId,
          "a drop of 3 of 10 ore and of the helm (1838): one Discarded bag with them; the rest of the ore keeps its instance id");
    const world::ItemInstance* bagHelm = part_item(*bag, kCharacterA, "TestBoundHelm");
    check(bagHelm != nullptr && bagHelm->instance == helm.instance && (bagHelm->flags & world::kItemBound) != 0
              && part_item(*bag, kCharacterA, "TestOre")->instance != oreId && std::abs(bag->position.x - a.position.x) < 0.01F,
          "the whole helm keeps its instance id and its bind flag; the part of the ore stack is a new instance; the bag is at the character");
    const auto entry = tracker_entry(shop.clientA, actor_of_bag(shop, *bag), fromA);
    check(entry && entry->kind == world::loot_bag_rules::kDiscarded && entry->state == world::loot_bag_rules::kRolled
              && fragments_of_entity_type(shop.clientB, kLootDropType, fromB) == 0,
          "the owner has the entry of the kind Discarded (5); B gets no record of the bag (a private discard)");
    // The lock of the player keeps an item: no bag.
    const std::size_t bagsBefore = shop.bags().bag_count();
    (void)shop.host.world.inventory().set_locked(kCharacterA, oreSlot, nullptr, true);
    shop.send(shop.clientA, server::rep::kTypeDropInventoryItems, drop_body(a.id, {{oreSlot, oreId, 1}}));
    check(shop.bags().bag_count() == bagsBefore && held(a.bag, "TestOre") == 7, "a drop of a locked item: no bag, no change");
    (void)shop.host.world.inventory().set_locked(kCharacterA, oreSlot, nullptr, false);
    // The recovery: Open, then the take of the helm (2942): the same instance in the bag.
    const std::string key = bag->key;
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, helm.instance, 1));
    const world::ItemInstance* back = stack_of(a.bag, helm.instance);
    check(back != nullptr && back->gearScore == helm.gearScore && back->durability == helm.durability && back->flags == helm.flags,
          "the recovery of the helm: the same instance id, gear score, durability, and bind flag");
    // A give into the open bag (2801): 2 ore into the part.
    shop.send(shop.clientA, server::rep::kTypeGiveItem, BagShop::bag_body(a, a, true, oreId, 2));
    check(held(a.bag, "TestOre") == 5 && in_part(*shop.bags().find(key), kCharacterA, "TestOre") == 5,
          "a give into the open bag (2801): 2 ore leave the bag and go into the part");
    shop.end_block(shop.clientA);
    // The drop of an equipped item (826): the slot of the paperdoll is empty, the item is in a new Discarded bag.
    const std::uint32_t slot = *slot_of(a.bag, "TestBoundHelm");
    (void)shop.host.world.inventory().equip(kCharacterA, slot);
    check(a.equipment.item(world::paperdoll::kHead) != nullptr, "the helm is in the head slot");
    shop.send(shop.clientA, server::rep::kTypePaperdollDropItem, paperdoll_drop_body(a.id, world::paperdoll::kHead, 1));
    const world::LootBag* worn = shop.bag_of(kCharacterA);
    check(a.equipment.item(world::paperdoll::kHead) == nullptr && worn != nullptr && worn->key != key
              && part_item(*worn, kCharacterA, "TestBoundHelm") != nullptr && part_item(*worn, kCharacterA, "TestBoundHelm")->instance == helm.instance,
          "the drop of the equipped helm (826): the head slot is empty, and a new Discarded bag has the helm with its instance id");
}

/** GL5: the lifetime and the ends. Characters with no session: the world clock goes on in large steps. */
void bag_lifetime(const Options&) {
    for (const float lifetime : {kLifetime, 50.0F}) {
        BagShop shop(":memory:", false, true, lifetime);
        (void)shop.host.world.spawn_character(kCharacterA, kHere);
        shop.host.world.tick();
        shop.kill({kCharacterA});
        const world::LootBag* bag = shop.bag_of(kCharacterA);
        const std::string key = bag->key;
        const auto life = std::chrono::milliseconds(static_cast<long long>(lifetime * 1000.0F));
        shop.host.clock.advance(life - 1ms);
        shop.host.world.tick();
        const bool before = shop.bags().find(key) != nullptr;
        shop.host.clock.advance(1ms);
        shop.host.world.tick();
        check(before && shop.bags().find(key) == nullptr && shop.host.store.list_loot_bags().empty() && shop.bags().counts().expired == 1
                  && shop.host.world.find_character(kCharacterA)->bagTracker.empty(),
              "the lifetime of the data (" + std::to_string(static_cast<int>(lifetime)) + " s): the bag is there 1 ms before it and gone at it; "
              "its rows and the entry of the owner go");
    }
    // With the clients (a lifetime of 5 s of a different file): the end after the last item at 1.25 s, and the end of an open panel.
    BagShop shop(":memory:", false, true, 5.0F);
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    shop.kill({kCharacterA});
    shop.pass(100);
    const world::LootBag* bag = shop.bag_of(kCharacterA);
    const std::string key = bag->key;
    shop.request(shop.clientA, shop.bag_block(*bag, kTakeAllRow));
    shop.end_block(shop.clientA);
    shop.pass(1150);
    const bool there = shop.bags().find(key) != nullptr;
    shop.pass(100);
    check(there && shop.bags().find(key) == nullptr && shop.bags().counts().emptied == 1,
          "the end after the last item (FastKillSelfDelay, 1.25 s of the data): the bag is there at 1.2 s and gone at 1.3 s");
    shop.kill({kCharacterA});
    shop.pass(100);
    bag = shop.bag_of(kCharacterA);
    const std::string open = bag->key;
    const world::InstanceId ore = part_item(*bag, kCharacterA, "TestOre")->instance;
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    shop.pass(5000);
    check(shop.bags().find(open) == nullptr && count_type(shop.clientA, kServerCancel, from) == 1,
          "the end of the lifetime with the panel open: the bag goes, and the use ends (3746; live check L6: the panel closes)");
    const std::uint32_t oreBefore = held(a.bag, "TestOre");
    shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, ore, 1));
    check(held(a.bag, "TestOre") == oreBefore && shop.bags().counts().refusals[static_cast<std::size_t>(world::BagRefusal::notOpen)] == 1,
          "a take after the end of the bag is refused (no open bag), and nothing changes");
    shop.end_block(shop.clientA);
}

/** GL6: a refused write, a restart, a leave and an entry. */
void bag_store(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "loot-bags.sqlite3";
    std::filesystem::remove(path);
    const auto trigger = [&](const char* sql) {
        sqlite3* database = nullptr;
        sqlite3_open(path.string().c_str(), &database);
        char* error = nullptr;
        const int status = sqlite3_exec(database, sql, nullptr, nullptr, &error);
        sqlite3_free(error);
        sqlite3_close(database);
        return status == SQLITE_OK;
    };
    std::chrono::nanoseconds firstWorld{0};
    std::string key;
    world::InstanceId swordId{};
    {
        BagShop shop(path.string());
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        shop.kill({kCharacterA});
        shop.pass(300);
        const world::LootBag* bag = shop.bag_of(kCharacterA);
        key = bag->key;
        swordId = part_item(*bag, kCharacterA, "TestSword")->instance;
        // A refused write of the part: the take changes nothing (the bag of the character, the part, the rows).
        check(trigger("CREATE TRIGGER refuse_bag BEFORE INSERT ON loot_bags BEGIN SELECT RAISE(ABORT, 'test'); END;"), "a trigger refuses the bag rows");
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
        bag = shop.bags().find(key);
        check(held(a.bag, "TestOre") == 0 && held(a.bag, "TestSword") == 0 && in_part(*bag, kCharacterA, "TestOre") == 3
                  && shop.host.store.list_items(kCharacterA).empty() && shop.host.store.list_loot_bags().size() == 1
                  && shop.bags().counts().storeFailures == 1,
              "a refused write of the part: no item in the bag of the character, the part keeps its items, the rows stay");
        shop.end_block(shop.clientA);
        // A refused discard: the item stays with the character, no bag.
        (void)shop.host.world.inventory().give(kCharacterA, "TestHide", 4);
        const std::uint32_t hideSlot = *slot_of(a.bag, "TestHide");
        const std::size_t bagsBefore = shop.bags().bag_count();
        shop.send(shop.clientA, server::rep::kTypeDropInventoryItems, drop_body(a.id, {{hideSlot, a.bag.slots[hideSlot].item->instance, 4}}));
        check(held(a.bag, "TestHide") == 4 && shop.bags().bag_count() == bagsBefore && shop.host.store.list_items(kCharacterA).size() == 1,
              "a refused write of a discard: the item stays in the bag of the character (its row too), and there is no bag");
        // A refused part at a kill: the whole reward of the character is refused (finding F1 of stage 3.5a).
        const std::uint32_t experience = a.progression.experience;
        const std::uint64_t refused = shop.host.world.rewards().counts().refused;
        shop.kill({kCharacterA});
        shop.pass(100);
        check(shop.host.world.rewards().counts().refused == refused + 1 && a.progression.experience == experience && shop.bags().bag_count() == bagsBefore,
              "a refused part at a kill: no part of the reward (no experience), and no bag");
        check(trigger("DROP TRIGGER refuse_bag;"), "the trigger goes");
        firstWorld = std::chrono::duration_cast<std::chrono::nanoseconds>(shop.host.clock.now().time_since_epoch()) - std::chrono::seconds(1'000'000);
    }
    {
        // A restart 60 s later (the wall clock of the rows): the bag comes back with its remaining time and its instances.
        BagShop shop(path.string(), true);
        shop.host.clock.advance(firstWorld + 60s);
        shop.enter();
        const world::LootBag* bag = shop.bags().find(key);
        world::Entity& a = *shop.entity(kCharacterA);
        check(bag != nullptr && part_item(*bag, kCharacterA, "TestSword") != nullptr && part_item(*bag, kCharacterA, "TestSword")->instance == swordId
                  && shop.bags().counts().loaded == 1,
              "the restart: the bag of the store comes back with the same instances");
        const double left = shop.bags().view(kCharacterA).bags.empty() ? 0.0 : shop.bags().view(kCharacterA).bags[0].secondsLeft;
        note("the time left after the restart: " + std::to_string(left) + " s");
        check(left > 110.0 && left < 121.0, "the restart keeps the remaining time on the wall clock (about 120 s), the entry renews nothing");
        check(tracker_entry(shop.clientA, actor_of_bag(shop, *bag)).has_value(), "the first record of A has the entry of the bag");
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        shop.send(shop.clientA, server::rep::kTypeTakeItem, BagShop::bag_body(a, a, true, swordId, 1));
        check(stack_of(a.bag, swordId) != nullptr, "after the restart the take gives the same instance of the sword");
        shop.end_block(shop.clientA);
        firstWorld = std::chrono::duration_cast<std::chrono::nanoseconds>(shop.host.clock.now().time_since_epoch()) - std::chrono::seconds(1'000'000);
    }
    {
        // A restart after the end of the bag (the wall clock): its rows go, and no bag comes back.
        BagShop shop(path.string(), true);
        shop.host.clock.advance(firstWorld + 200s);
        shop.enter();
        check(shop.bags().find(key) == nullptr && shop.host.store.list_loot_bags().empty() && shop.bags().counts().expired == 1,
              "a bag whose end came while the backend was off: its rows go at the start");
    }
}

/** GL7: the load. */
void bag_load(const Options&) {
    // The limit, written before the run: 100 ticks of the world and 50 state changes near the characters, with 20,000 far bags, below 2 s;
    // and the same work (the bags that the states looked at, the due entries) as with 2,000 far bags.
    constexpr double kLimitSeconds = 2.0;
    struct Work {
        std::uint64_t refreshBags{0};
        std::uint64_t dueLooks{0};
        double seconds{0.0};
    };
    const auto run = [&](std::size_t far) {
        BagShop shop;
        shop.host.world.tick();
        world::LootBags& bags = shop.bags();
        // The far bags: one part of a made-up character that is not in the world, more than 2 km from the characters.
        for (std::size_t index = 0; index < far; ++index) {
            world::Entity creature;
            creature.position = state::WorldPosition{5000.0F + static_cast<float>(index % 200) * 20.0F, 5000.0F + static_cast<float>(index / 200) * 20.0F, 30.0F};
            world::KillBag kill = bags.begin_kill(creature);
            world::Entity owner;
            owner.characterId = "far-" + std::to_string(index);
            (void)bags.stage_part(kill, owner, {world::GrantItem{"TestOre", 1, std::nullopt, {}}}, nullptr);
            bags.finish_kill(kill);
        }
        // 50 characters near the place, each with its own bag.
        std::vector<std::string> characters;
        for (int number = 0; number < 50; ++number) {
            char id[37];
            std::snprintf(id, sizeof(id), "c0c0c0c0-0000-4000-8000-%012d", number);
            add_player(shop.host.store, ("7656120225560" + std::to_string(1000 + number)).c_str(), "Tester", id, ("Load" + std::to_string(number)).c_str(),
                       std::nullopt);
            (void)shop.host.world.spawn_character(id, state::WorldPosition{static_cast<float>(number), 0.0F, 30.0F});
            characters.push_back(id);
        }
        for (const std::string& id : characters) {
            world::Entity creature;
            creature.position = shop.host.world.find_character(id)->position;
            world::KillBag kill = bags.begin_kill(creature);
            (void)bags.stage_part(kill, *shop.host.world.find_character(id), {world::GrantItem{"TestOre", 2, std::nullopt, {}}}, nullptr);
            bags.finish_kill(kill);
        }
        const world::LootBagCounts before = bags.counts();
        const auto start = std::chrono::steady_clock::now();
        for (int tick = 0; tick < 100; ++tick) {
            shop.host.clock.advance(10ms);
            shop.host.world.tick();
        }
        // A state change of each character (a discard of one ore): the states look only at its own bags.
        for (const std::string& id : characters) {
            world::Entity& character = *shop.host.world.find_character(id);
            (void)shop.host.world.inventory().give(id, "TestOre", 1);
            const auto slot = slot_of(character.bag, "TestOre");
            (void)bags.discard(id, {{*slot, character.bag.slots[*slot].item->instance, 1}});
        }
        Work work;
        work.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        work.refreshBags = bags.counts().refreshBags - before.refreshBags;
        work.dueLooks = bags.counts().dueLooks - before.dueLooks;
        return work;
    };
    const Work small = run(2000);
    const Work large = run(20000);
    note("2,000 far bags: " + std::to_string(small.refreshBags) + " bags looked at, " + std::to_string(small.dueLooks) + " due looks, "
         + std::to_string(small.seconds) + " s; 20,000: " + std::to_string(large.refreshBags) + ", " + std::to_string(large.dueLooks) + ", "
         + std::to_string(large.seconds) + " s");
    check(small.refreshBags == large.refreshBags && small.dueLooks == large.dueLooks && large.refreshBags == 100,
          "the same work with 2,000 and 20,000 far bags: each change looks only at the bags of its character (2 for each of 50), no due entry");
    check(large.seconds < kLimitSeconds, "100 ticks and 50 discards near the characters with 20,000 far bags take less than 2 s");
}

} // namespace

std::vector<Case> loot_bag_cases() {
    return {{"GL1", "loot bags: an eligible kill makes a personal bag; the record, the entry, the map of the owner; no data of format 3", bag_spawn},
            {"GL2", "loot bags: the panel, a take of a part and of a whole stack, Take All, Take All of the prompt, a full bag", bag_transfers},
            {"GL3", "loot bags: two sessions, a known reference of a different character, a bag of two eligible characters", bag_two_sessions},
            {"GL4", "loot bags: the discard (1838, 826), the lock, the recovery, a give into the open bag (2801)", bag_discard},
            {"GL5", "loot bags: the lifetime of the data at its boundary (180 s, 50 s), a request with no open bag", bag_lifetime},
            {"GL6", "loot bags: a refused write (a take, a discard, a kill), a restart, an end while the backend was off", bag_store},
            {"GL7", "loot bags: the load (2,000 and 20,000 far bags, 50 characters; a time limit)", bag_load}};
}

} // namespace soulwarden::backend::tests

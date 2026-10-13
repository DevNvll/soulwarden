#include "world/inventory.h"

#include <algorithm>
#include <mutex>
#include <random>
#include <set>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** @return A new instance id: 16 random bytes, not all zero (the client reads zero as "no instance"). */
InstanceId new_instance() {
    static std::mutex mutex;
    static std::mt19937_64 generator{std::random_device{}()};
    InstanceId instance{};
    std::lock_guard lock(mutex);
    do {
        for (std::size_t offset = 0; offset < instance.size(); offset += 8) {
            const std::uint64_t value = generator();
            for (std::size_t byte = 0; byte < 8; ++byte) {
                instance[offset + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
            }
        }
    } while (instance == InstanceId{});
    return instance;
}

/** @return The first empty slot from `first`, or the number of a new slot, or no value when the bag is full. */
std::optional<std::uint32_t> free_slot(const Bag& bag, std::uint32_t first = 0) {
    for (std::uint32_t index = first; index < bag.slots.size(); ++index) {
        if (!bag.slots[index].item) {
            return index;
        }
    }
    if (bag.slots.size() < kMaxContainerSlots && first <= bag.slots.size()) {
        return static_cast<std::uint32_t>(bag.slots.size());
    }
    return std::nullopt;
}

/** @return The slot with this number. The vector gets empty slots up to it when it is shorter. */
BagSlot& slot_at(Bag& bag, std::uint32_t index) {
    if (index >= bag.slots.size()) {
        bag.slots.resize(index + 1);
    }
    return bag.slots[index];
}

state::ItemRow row_of(const ItemInstance& item, int container, std::uint32_t slot) {
    state::ItemRow row;
    row.instanceId = instance_text(item.instance);
    row.container = container;
    row.slot = slot;
    row.itemId = item.itemId;
    row.quantity = item.quantity;
    row.gearScore = item.gearScore;
    row.durability = item.durability;
    row.maxDurability = item.maxDurability;
    row.flags = item.flags;
    row.chrono = item.chrono;
    return row;
}

std::string slot_list(const std::set<std::uint32_t>& slots) {
    std::string text;
    for (const std::uint32_t slot : slots) {
        text += (text.empty() ? "" : ", ") + std::to_string(slot);
    }
    return text.empty() ? "none" : text;
}

/** @return A new item instance of an item of the catalog, with the start values of the item table. */
ItemInstance new_item(const gamedata::ItemDefinition& definition,
                      std::uint32_t quantity,
                      std::optional<std::uint32_t> gearScore,
                      std::uint32_t chrono) {
    ItemInstance item;
    item.instance = new_instance();
    item.itemId = definition.id;
    item.index = definition.index;
    item.quantity = quantity;
    if (definition.maxGearScore != 0) {
        const std::uint32_t low = definition.minGearScore;
        const std::uint32_t high = std::max(definition.maxGearScore, low);
        const std::uint32_t standard = definition.gearScoreOverride != 0 ? definition.gearScoreOverride : low;
        item.gearScore = std::clamp(gearScore.value_or(standard), low, high);
    }
    item.durability = std::min(definition.durability, kMaxItemDurability);
    item.maxDurability = item.durability;
    item.chrono = chrono;
    item.flags = definition.bindOnPickup ? kItemBound : 0;
    return item;
}

/**
 * @return The slot of the paperdoll for an item of this class when a request names none: the
 *         one slot of the class, or for a weapon the first weapon slot that `free` accepts
 *         (the first weapon slot when no one is free). No value for a class with no slot of
 *         its own (a consumable, a bag, ammunition).
 */
template <class Free>
std::optional<std::uint32_t> own_slot(std::string_view itemClass, Free&& free) {
    constexpr std::uint32_t kSingleSlots[] = {paperdoll::kHead,   paperdoll::kChest, paperdoll::kHands,
                                              paperdoll::kLegs,   paperdoll::kFeet,  paperdoll::kAmulet,
                                              paperdoll::kToken,  paperdoll::kRing,  33, 34, 35, 36, 37, 38, 39,
                                              40,                 41,                42, 43, 45};
    for (const std::uint32_t slot : kSingleSlots) {
        if (paperdoll::slot_accepts(slot, itemClass)) {
            return slot;
        }
    }
    // The third weapon slot is not open for a new character: the first two are the choice.
    for (const std::uint32_t first : {paperdoll::kMainHand1, paperdoll::kOffHand1}) {
        if (!paperdoll::slot_accepts(first, itemClass)) {
            continue;
        }
        for (std::uint32_t slot = first; slot < first + 2; ++slot) {
            if (free(slot)) {
                return slot;
            }
        }
        return first;
    }
    return std::nullopt;
}

} // namespace

const char* item_error_text(ItemError error) {
    switch (error) {
    case ItemError::none:
        return "no error";
    case ItemError::unknownCharacter:
        return "the store has no character with this id";
    case ItemError::noCatalog:
        return "the backend has no item catalog (make it with tools/gamedata/items.sh)";
    case ItemError::unknownItem:
        return "the item catalog has no item with this id";
    case ItemError::noItemType:
        return "the item has no item type: the client cannot make it";
    case ItemError::badQuantity:
        return "the quantity is not in the permitted range";
    case ItemError::bagFull:
        return "the bag has no free slot";
    case ItemError::badSlot:
        return "the slot number is not a slot of the bag";
    case ItemError::emptySlot:
        return "the slot has no item";
    case ItemError::wrongInstance:
        return "the slot has a different item instance";
    case ItemError::sameSlot:
        return "the source and the target are the same slot";
    case ItemError::targetInUse:
        return "the target slot has an item";
    case ItemError::emptyRequest:
        return "the request has no entry";
    case ItemError::badEquipSlot:
        return "the slot number is not a slot of the paperdoll";
    case ItemError::slotRefusesItem:
        return "the slot of the paperdoll does not take this item";
    case ItemError::noEquipSlot:
        return "the item has no slot of its own in the paperdoll: give the slot";
    case ItemError::itemLocked:
        return "the item is locked";
    case ItemError::notEnoughCoin:
        return "the character has less coin than the cost";
    case ItemError::storeFailed:
        return "the database did not accept the change";
    }
    return "unknown error";
}

Inventory::Inventory(World& world, state::Store& store, const gamedata::ItemCatalog* catalog)
    : m_world(world), m_store(store), m_catalog(catalog) {}

Inventory::Carried Inventory::read_items(const std::string& characterId) const {
    Carried items;
    Bag& bag = items.bag;
    unsigned unknown = 0;
    for (const state::ItemRow& row : m_store.list_items(characterId)) {
        const auto instance = read_instance(row.instanceId);
        const bool inBag = row.container == state::kContainerBag && row.slot < kMaxContainerSlots;
        const bool equipped = row.container == state::kContainerPaperdoll && paperdoll::valid_slot(row.slot);
        if ((!inBag && !equipped) || !instance || row.quantity == 0) {
            log::write("inventory: the item row %s of character %s is not a valid item of the bag or of the paperdoll: "
                       "not loaded",
                       row.instanceId.c_str(),
                       characterId.c_str());
            continue;
        }
        ItemInstance item;
        item.instance = *instance;
        item.itemId = row.itemId;
        item.quantity = std::min(row.quantity, kMaxItemQuantity);
        item.gearScore = row.gearScore;
        item.durability = std::min(row.durability, kMaxItemDurability);
        item.maxDurability = std::min(row.maxDurability, kMaxItemDurability);
        item.chrono = row.chrono;
        item.flags = static_cast<std::uint8_t>(row.flags);
        // The index on the wire comes from the catalog at each load: the database has the id text.
        const gamedata::ItemDefinition* definition = m_catalog != nullptr ? m_catalog->find(row.itemId) : nullptr;
        if (definition != nullptr) {
            item.index = definition->index;
        } else {
            ++unknown;
        }
        bag.nextChrono = std::max(bag.nextChrono, item.chrono + 1);
        if (inBag) {
            slot_at(bag, row.slot).item = std::move(item);
        } else {
            items.equipment.slots[row.slot].item = std::move(item);
        }
    }
    if (unknown != 0) {
        log::write("inventory: %u item(s) of character %s are not in the item catalog: their slots are empty for the "
                   "client, and the rows stay",
                   unknown,
                   characterId.c_str());
    }
    return items;
}

void Inventory::load(Entity& entity) {
    Carried carried = read_items(entity.characterId);
    entity.bag = std::move(carried.bag);
    entity.equipment = std::move(carried.equipment);
    entity.wallet.coin = std::min(m_store.currency(entity.characterId, state::kCurrencyCoin), kWalletLimit);
    unsigned items = 0;
    for (const BagSlot& slot : entity.bag.slots) {
        items += slot.item ? 1 : 0;
    }
    log::write("inventory: character %s has %u item(s) in %zu bag slot(s), %zu item(s) in its paperdoll, and "
               "%llu.%02llu coin",
               entity.characterId.c_str(),
               items,
               entity.bag.slots.size(),
               entity.equipment.slots.size(),
               static_cast<unsigned long long>(entity.wallet.coin / 100),
               static_cast<unsigned long long>(entity.wallet.coin % 100));
}

std::optional<Holdings> Inventory::holdings(const std::string& characterId) {
    Holdings holdings;
    if (const Entity* entity = m_world.find_character(characterId)) {
        holdings.bag = entity->bag;
        holdings.equipment = entity->equipment;
        holdings.wallet = entity->wallet;
        holdings.inWorld = true;
        return holdings;
    }
    if (!m_store.find_character(characterId)) {
        return std::nullopt;
    }
    Carried carried = read_items(characterId);
    holdings.bag = std::move(carried.bag);
    holdings.equipment = std::move(carried.equipment);
    holdings.wallet.coin = m_store.currency(characterId, state::kCurrencyCoin);
    return holdings;
}

template <class Change>
ItemResult Inventory::change(const std::string& characterId, const char* what, Change&& apply) {
    ItemResult result;
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    Carried items;
    if (entity != nullptr) {
        items.bag = entity->bag;
        items.equipment = entity->equipment;
        result.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        items = read_items(characterId);
    } else {
        result.error = ItemError::unknownCharacter;
    }
    // The change is made on a copy: the items of the entity change only after the rows.
    const Carried before = items;
    Touched touched;
    if (result.ok()) {
        result.error = apply(items, touched);
    }
    if (!result.ok()) {
        log::write("inventory: %s of character %s refused: %s", what, characterId.c_str(), item_error_text(result.error));
        return result;
    }
    // A slot that the vector did not have is a new element for the client too.
    for (std::size_t index = before.bag.slots.size(); index < items.bag.slots.size(); ++index) {
        touched.bag.insert(static_cast<std::uint32_t>(index));
    }
    // The rows of the change. An item that left its place has its row removed, also when the
    // change puts it in a different place: the store removes each row of the change first,
    // and then writes the new rows. Thus an item that moves between the bag and the paperdoll
    // is one row at all times.
    std::vector<std::string> removed;
    std::vector<state::ItemRow> rows;
    const auto place = [&](const ItemInstance* old, const ItemInstance* now, int container, std::uint32_t slot) {
        if (old != nullptr && (now == nullptr || now->instance != old->instance)) {
            removed.push_back(instance_text(old->instance));
        }
        if (now != nullptr) {
            rows.push_back(row_of(*now, container, slot));
        }
    };
    for (const std::uint32_t slot : touched.bag) {
        place(slot < before.bag.slots.size() && before.bag.slots[slot].item ? &*before.bag.slots[slot].item : nullptr,
              items.bag.slots[slot].item ? &*items.bag.slots[slot].item : nullptr,
              state::kContainerBag,
              slot);
    }
    for (const std::uint32_t slot : touched.equipment) {
        place(before.equipment.item(slot), items.equipment.item(slot), state::kContainerPaperdoll, slot);
    }
    // All rows of the change are one transaction.
    if ((!removed.empty() || !rows.empty()) && !m_store.write_items(characterId, removed, rows)) {
        result.error = ItemError::storeFailed;
        log::write("inventory: %s of character %s refused: %s", what, characterId.c_str(), item_error_text(result.error));
        return result;
    }
    if (entity != nullptr) {
        // One mark for each state that changed: each session that observes the entity gets
        // the slots that are newer than its last record.
        if (!touched.bag.empty()) {
            const Revision revision = m_world.touch(entity->id, ComponentKey::container);
            for (const std::uint32_t slot : touched.bag) {
                items.bag.slots[slot].changed = revision;
            }
        }
        if (!touched.equipment.empty()) {
            const Revision revision = m_world.touch(entity->id, ComponentKey::paperdoll);
            for (const std::uint32_t slot : touched.equipment) {
                items.equipment.slots[slot].changed = revision;
                if (paperdoll::weapon_slot(slot)) {
                    items.equipment.weaponsChanged = revision;
                }
            }
        }
        entity->bag = std::move(items.bag);
        entity->equipment = std::move(items.equipment);
    }
    result.slots.assign(touched.bag.begin(), touched.bag.end());
    result.equipmentSlots.assign(touched.equipment.begin(), touched.equipment.end());
    if (touched.equipment.empty()) {
        log::write("inventory: %s of character %s: changed slot(s) %s (%s)",
                   what,
                   characterId.c_str(),
                   slot_list(touched.bag).c_str(),
                   entity != nullptr ? "the character is in the world" : "the rows only");
    } else {
        log::write("inventory: %s of character %s: changed slot(s) %s of the bag and slot(s) %s of the paperdoll (%s)",
                   what,
                   characterId.c_str(),
                   slot_list(touched.bag).c_str(),
                   slot_list(touched.equipment).c_str(),
                   entity != nullptr ? "the character is in the world" : "the rows only");
    }
    return result;
}

template <class Change>
ItemResult Inventory::change_bag(const std::string& characterId, const char* what, Change&& apply) {
    return change(characterId, what, [&](Carried& items, Touched& touched) { return apply(items.bag, touched.bag); });
}

ItemResult Inventory::give(const std::string& characterId,
                           std::string_view itemId,
                           std::uint32_t quantity,
                           const GiveOptions& options) {
    const std::string what = "give of " + std::to_string(quantity) + " " + std::string(itemId);
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (m_catalog == nullptr || !m_catalog->loaded()) {
            return ItemError::noCatalog;
        }
        const gamedata::ItemDefinition* definition = m_catalog->find(itemId);
        if (definition == nullptr) {
            return ItemError::unknownItem;
        }
        if (definition->type.empty()) {
            return ItemError::noItemType;
        }
        if (quantity == 0 || quantity > kGiveLimit) {
            return ItemError::badQuantity;
        }
        const std::uint32_t maxStack = std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity);
        const std::uint8_t flags = definition->bindOnPickup ? kItemBound : 0;
        const std::uint8_t action = std::min(options.action, sync::kLargest);
        std::uint32_t left = quantity;
        // First the stacks of the item that are not full.
        for (std::uint32_t index = 0; index < bag.slots.size() && left > 0; ++index) {
            BagSlot& slot = bag.slots[index];
            if (!slot.item || slot.item->itemId != definition->id || slot.item->quantity >= maxStack
                || (slot.item->flags & kItemBound) != flags) {
                continue;
            }
            const std::uint32_t moved = std::min(left, maxStack - slot.item->quantity);
            slot.item->quantity += moved;
            slot.action = action;
            changed.insert(index);
            left -= moved;
        }
        // Then new stacks in the first empty slots.
        std::uint32_t search = 0;
        while (left > 0) {
            const auto target = free_slot(bag, search);
            if (!target) {
                return ItemError::bagFull;
            }
            ItemInstance item = new_item(*definition, std::min(left, maxStack), options.gearScore, bag.nextChrono++);
            left -= item.quantity;
            BagSlot& slot = slot_at(bag, *target);
            slot.item = std::move(item);
            slot.action = action;
            changed.insert(*target);
            search = *target + 1;
        }
        return ItemError::none;
    });
}

ItemResult Inventory::remove(const std::string& characterId, std::uint32_t slot, std::optional<std::uint32_t> quantity) {
    const std::string what = "removal from slot " + std::to_string(slot);
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (slot >= bag.slots.size()) {
            return ItemError::badSlot;
        }
        BagSlot& source = bag.slots[slot];
        if (!source.item) {
            return ItemError::emptySlot;
        }
        const std::uint32_t taken = quantity.value_or(source.item->quantity);
        if (taken == 0 || taken > source.item->quantity) {
            return ItemError::badQuantity;
        }
        source.item->quantity -= taken;
        if (source.item->quantity == 0) {
            source.item.reset();
        }
        source.action = sync::kItemRemoved;
        changed.insert(slot);
        return ItemError::none;
    });
}

ItemResult Inventory::move(const std::string& characterId, std::uint32_t from, std::uint32_t to) {
    const std::string what = "move from slot " + std::to_string(from) + " to slot " + std::to_string(to);
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (from >= bag.slots.size() || to >= kMaxContainerSlots) {
            return ItemError::badSlot;
        }
        if (from == to) {
            return ItemError::sameSlot;
        }
        if (!bag.slots[from].item) {
            return ItemError::emptySlot;
        }
        // The reference to the target first: it can make the vector longer.
        BagSlot& target = slot_at(bag, to);
        BagSlot& source = bag.slots[from];
        std::uint32_t maxStack = 1;
        if (m_catalog != nullptr) {
            if (const gamedata::ItemDefinition* definition = m_catalog->find(source.item->itemId)) {
                maxStack = std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity);
            }
        }
        if (!target.item) {
            target.item = std::move(source.item);
            source.item.reset();
        } else if (target.item->itemId == source.item->itemId && target.item->quantity < maxStack
                   && (target.item->flags & kItemBound) == (source.item->flags & kItemBound)) {
            // A stack of the same item that is not full: it gets as much as it can hold.
            const std::uint32_t moved = std::min(source.item->quantity, maxStack - target.item->quantity);
            target.item->quantity += moved;
            source.item->quantity -= moved;
            if (source.item->quantity == 0) {
                source.item.reset();
            }
        } else {
            std::swap(source.item, target.item);
        }
        source.action = sync::kContainerTransfer;
        target.action = sync::kContainerTransfer;
        changed.insert(from);
        changed.insert(to);
        return ItemError::none;
    });
}

ItemResult Inventory::split(const std::string& characterId,
                            std::uint32_t slot,
                            std::uint32_t quantity,
                            std::optional<std::uint32_t> to) {
    const std::string what = "split of " + std::to_string(quantity) + " from slot " + std::to_string(slot);
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (slot >= bag.slots.size() || (to && *to >= kMaxContainerSlots)) {
            return ItemError::badSlot;
        }
        if (!bag.slots[slot].item) {
            return ItemError::emptySlot;
        }
        if (quantity == 0 || quantity >= bag.slots[slot].item->quantity) {
            return ItemError::badQuantity;
        }
        if (to && *to == slot) {
            return ItemError::sameSlot;
        }
        if (to && *to < bag.slots.size() && bag.slots[*to].item) {
            return ItemError::targetInUse;
        }
        const auto place = to ? to : free_slot(bag);
        if (!place) {
            return ItemError::bagFull;
        }
        BagSlot& target = slot_at(bag, *place);
        BagSlot& source = bag.slots[slot];
        // The new stack is a new item instance with the properties of the first one.
        ItemInstance part = *source.item;
        part.instance = new_instance();
        part.quantity = quantity;
        part.chrono = bag.nextChrono++;
        part.flags = static_cast<std::uint8_t>(part.flags & ~kItemLocked);
        source.item->quantity -= quantity;
        target.item = std::move(part);
        source.action = sync::kContainerTransfer;
        target.action = sync::kContainerTransfer;
        changed.insert(slot);
        changed.insert(*place);
        return ItemError::none;
    });
}

ItemResult Inventory::set_locked(const std::string& characterId,
                                 std::uint32_t slot,
                                 const InstanceId* instance,
                                 bool locked) {
    const std::string what = std::string(locked ? "lock" : "unlock") + " of slot " + std::to_string(slot);
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (slot >= bag.slots.size()) {
            return ItemError::badSlot;
        }
        BagSlot& target = bag.slots[slot];
        if (!target.item) {
            return ItemError::emptySlot;
        }
        if (instance != nullptr && target.item->instance != *instance) {
            return ItemError::wrongInstance;
        }
        const std::uint8_t flags = static_cast<std::uint8_t>(locked ? target.item->flags | kItemLocked
                                                                    : target.item->flags & ~kItemLocked);
        if (flags != target.item->flags) {
            target.item->flags = flags;
            target.action = sync::kLockStateChanged;
            changed.insert(slot);
        }
        return ItemError::none;
    });
}

ItemResult Inventory::drop(const std::string& characterId, const std::vector<DropEntry>& entries) {
    const std::string what = "drop of " + std::to_string(entries.size()) + " entr" + (entries.size() == 1 ? "y" : "ies");
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        if (entries.empty()) {
            return ItemError::emptyRequest;
        }
        // Each entry is checked against the bag with the entries before it applied: two
        // entries for one slot cannot take more than the slot has. A fault in one entry
        // refuses the full request, because the change is made on a copy of the bag.
        for (const DropEntry& entry : entries) {
            if (entry.slot >= bag.slots.size()) {
                return ItemError::badSlot;
            }
            BagSlot& source = bag.slots[entry.slot];
            if (!source.item) {
                return ItemError::emptySlot;
            }
            if (source.item->instance != entry.instance) {
                return ItemError::wrongInstance;
            }
            if ((source.item->flags & kItemLocked) != 0) {
                return ItemError::itemLocked; // The lock of the player keeps the item.
            }
            if (entry.quantity == 0 || entry.quantity > source.item->quantity) {
                return ItemError::badQuantity;
            }
            source.item->quantity -= entry.quantity;
            if (source.item->quantity == 0) {
                source.item.reset();
            }
            source.action = sync::kItemRemoved;
            changed.insert(entry.slot);
        }
        return ItemError::none;
    });
}

ItemResult Inventory::equip(const std::string& characterId,
                            std::uint32_t bagSlot,
                            std::optional<std::uint32_t> paperdollSlot,
                            const InstanceId* instance,
                            std::optional<std::uint32_t> quantity) {
    const std::string what = "equip of bag slot " + std::to_string(bagSlot)
                             + (paperdollSlot ? " in paperdoll slot " + std::to_string(*paperdollSlot) : std::string{});
    return change(characterId, what.c_str(), [&](Carried& items, Touched& touched) {
        if (m_catalog == nullptr || !m_catalog->loaded()) {
            return ItemError::noCatalog; // The class of an item comes from the catalog.
        }
        if (paperdollSlot && !paperdoll::valid_slot(*paperdollSlot)) {
            return ItemError::badEquipSlot;
        }
        if (bagSlot >= items.bag.slots.size()) {
            return ItemError::badSlot;
        }
        BagSlot& source = items.bag.slots[bagSlot];
        if (!source.item) {
            return ItemError::emptySlot;
        }
        if (instance != nullptr && source.item->instance != *instance) {
            return ItemError::wrongInstance;
        }
        // The full stack moves: a part of a stack has no rule in the backend yet.
        if (quantity && *quantity != source.item->quantity) {
            return ItemError::badQuantity;
        }
        const gamedata::ItemDefinition* definition = m_catalog->find(source.item->itemId);
        if (definition == nullptr) {
            return ItemError::unknownItem;
        }
        const auto slot = paperdollSlot ? paperdollSlot
                                        : own_slot(definition->itemClass, [&](std::uint32_t candidate) {
                                              return items.equipment.item(candidate) == nullptr;
                                          });
        if (!slot) {
            return ItemError::noEquipSlot;
        }
        if (!paperdoll::slot_accepts(*slot, definition->itemClass)) {
            return ItemError::slotRefusesItem;
        }
        // The item that is in the slot goes into the place of the new one.
        EquipmentSlot& target = items.equipment.slots[*slot];
        std::swap(source.item, target.item);
        target.action = sync::kItemEquipped;
        source.action = sync::kItemUnequipped;
        touched.bag.insert(bagSlot);
        touched.equipment.insert(*slot);
        return ItemError::none;
    });
}

ItemResult Inventory::unequip(const std::string& characterId,
                              std::uint32_t paperdollSlot,
                              std::optional<std::uint32_t> bagSlot,
                              std::optional<std::uint32_t> quantity) {
    const std::string what = "unequip of paperdoll slot " + std::to_string(paperdollSlot);
    return change(characterId, what.c_str(), [&](Carried& items, Touched& touched) {
        if (!paperdoll::valid_slot(paperdollSlot)) {
            return ItemError::badEquipSlot;
        }
        const ItemInstance* equipped = items.equipment.item(paperdollSlot);
        if (equipped == nullptr) {
            return ItemError::emptySlot;
        }
        if (quantity && *quantity != equipped->quantity) {
            return ItemError::badQuantity;
        }
        if (bagSlot && *bagSlot >= kMaxContainerSlots) {
            return ItemError::badSlot;
        }
        if (bagSlot && *bagSlot < items.bag.slots.size() && items.bag.slots[*bagSlot].item) {
            return ItemError::targetInUse;
        }
        const auto place = bagSlot ? bagSlot : free_slot(items.bag);
        if (!place) {
            return ItemError::bagFull;
        }
        EquipmentSlot& source = items.equipment.slots[paperdollSlot];
        BagSlot& target = slot_at(items.bag, *place);
        target.item = std::move(source.item);
        source.item.reset();
        target.action = sync::kItemUnequipped;
        source.action = sync::kItemUnequipped;
        touched.bag.insert(*place);
        touched.equipment.insert(paperdollSlot);
        return ItemError::none;
    });
}

ItemResult Inventory::swap_equipped(const std::string& characterId,
                                    std::uint32_t first,
                                    std::uint32_t second,
                                    const InstanceId* firstInstance,
                                    const InstanceId* secondInstance) {
    const std::string what = "swap of the paperdoll slots " + std::to_string(first) + " and " + std::to_string(second);
    return change(characterId, what.c_str(), [&](Carried& items, Touched& touched) {
        if (m_catalog == nullptr || !m_catalog->loaded()) {
            return ItemError::noCatalog;
        }
        if (!paperdoll::valid_slot(first) || !paperdoll::valid_slot(second)) {
            return ItemError::badEquipSlot;
        }
        if (first == second) {
            return ItemError::sameSlot;
        }
        const ItemInstance* one = items.equipment.item(first);
        const ItemInstance* two = items.equipment.item(second);
        if (one == nullptr && two == nullptr) {
            return ItemError::emptySlot;
        }
        // A request names the instance of each slot, and no instance for an empty slot.
        const auto named = [](const InstanceId* instance, const ItemInstance* item) {
            return instance == nullptr || *instance == (item != nullptr ? item->instance : InstanceId{});
        };
        if (!named(firstInstance, one) || !named(secondInstance, two)) {
            return ItemError::wrongInstance;
        }
        const auto fits = [&](const ItemInstance* item, std::uint32_t slot) {
            if (item == nullptr) {
                return true;
            }
            const gamedata::ItemDefinition* definition = m_catalog->find(item->itemId);
            return definition != nullptr && paperdoll::slot_accepts(slot, definition->itemClass);
        };
        if (!fits(one, second) || !fits(two, first)) {
            return ItemError::slotRefusesItem;
        }
        EquipmentSlot& left = items.equipment.slots[first];
        EquipmentSlot& right = items.equipment.slots[second];
        std::swap(left.item, right.item);
        left.action = left.item ? sync::kItemEquipped : sync::kItemUnequipped;
        right.action = right.item ? sync::kItemEquipped : sync::kItemUnequipped;
        touched.equipment.insert(first);
        touched.equipment.insert(second);
        return ItemError::none;
    });
}

std::vector<state::ItemRow> start_items(const gamedata::ItemCatalog& catalog, std::string_view backstory) {
    std::vector<state::ItemRow> rows;
    const std::vector<gamedata::BackstoryItem>* items = catalog.backstory_items(backstory);
    if (items == nullptr) {
        return rows;
    }
    std::set<std::uint32_t> equipped;
    std::uint32_t bagSlot = 0;
    std::uint32_t chrono = 1;
    for (const gamedata::BackstoryItem& entry : *items) {
        const gamedata::ItemDefinition* definition = catalog.find(entry.itemId);
        // An entry with the quantity 0 is an item of the character creation screen only.
        if (definition == nullptr || definition->type.empty() || entry.quantity == 0) {
            continue;
        }
        const std::uint32_t maxStack = std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity);
        std::uint32_t left = std::min(entry.quantity, kGiveLimit);
        while (left > 0 && bagSlot < kMaxContainerSlots) {
            const std::uint32_t quantity = std::min(left, maxStack);
            left -= quantity;
            const ItemInstance item =
                new_item(*definition, quantity, entry.gearScore != 0 ? std::optional(entry.gearScore) : std::nullopt, chrono++);
            const auto slot = own_slot(definition->itemClass, [&](std::uint32_t candidate) { return equipped.count(candidate) == 0; });
            if (slot && equipped.insert(*slot).second) {
                rows.push_back(row_of(item, state::kContainerPaperdoll, *slot));
            } else {
                rows.push_back(row_of(item, state::kContainerBag, bagSlot++));
            }
        }
    }
    return rows;
}

ItemResult Inventory::pay(const std::string& characterId,
                          std::uint64_t cost,
                          const std::function<bool(std::uint64_t coin)>& write) {
    ItemResult result;
    Entity* entity = m_world.find_character(characterId);
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    result.inWorld = entity != nullptr;
    std::uint64_t coin = 0;
    if (entity != nullptr) {
        coin = entity->wallet.coin;
    } else if (m_store.find_character(characterId)) {
        coin = m_store.currency(characterId, state::kCurrencyCoin);
    } else {
        result.error = ItemError::unknownCharacter;
    }
    if (result.ok() && coin < cost) {
        result.error = ItemError::notEnoughCoin;
    }
    if (result.ok() && !write(coin - cost)) {
        result.error = ItemError::storeFailed;
    }
    if (!result.ok()) {
        log::write("inventory: cost of %llu.%02llu coin of character %s not taken: %s",
                   static_cast<unsigned long long>(cost / 100),
                   static_cast<unsigned long long>(cost % 100),
                   characterId.c_str(),
                   item_error_text(result.error));
        return result;
    }
    if (entity != nullptr) {
        entity->wallet.coin = coin - cost;
        m_world.touch(entity->id, ComponentKey::currency);
    }
    log::write("inventory: cost of %llu.%02llu coin taken from character %s: %llu.%02llu coin now (%s)",
               static_cast<unsigned long long>(cost / 100),
               static_cast<unsigned long long>(cost % 100),
               characterId.c_str(),
               static_cast<unsigned long long>((coin - cost) / 100),
               static_cast<unsigned long long>((coin - cost) % 100),
               entity != nullptr ? "the character is in the world" : "the rows only");
    return result;
}

ItemResult Inventory::set_coin(const std::string& characterId, std::uint64_t amount) {
    ItemResult result;
    Entity* entity = m_world.find_character(characterId);
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    result.inWorld = entity != nullptr;
    if (entity == nullptr && !m_store.find_character(characterId)) {
        result.error = ItemError::unknownCharacter;
    } else if (amount > kWalletLimit) {
        result.error = ItemError::badQuantity;
    } else if (!m_store.set_currency(characterId, state::kCurrencyCoin, amount)) {
        result.error = ItemError::storeFailed;
    }
    if (!result.ok()) {
        log::write("inventory: coin of character %s not set: %s", characterId.c_str(), item_error_text(result.error));
        return result;
    }
    if (entity != nullptr) {
        entity->wallet.coin = amount;
        m_world.touch(entity->id, ComponentKey::currency);
    }
    log::write("inventory: coin of character %s set to %llu.%02llu (%s)",
               characterId.c_str(),
               static_cast<unsigned long long>(amount / 100),
               static_cast<unsigned long long>(amount % 100),
               entity != nullptr ? "the character is in the world" : "the row only");
    return result;
}

} // namespace soulwarden::backend::world

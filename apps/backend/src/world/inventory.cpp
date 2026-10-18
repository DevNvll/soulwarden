#include "world/inventory.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <random>
#include <set>

#include "core/logging/log.h"
#include "gamedata/perk_data.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/**
 * @return True when the row of the item table protects the item from a removal by a request of a client (Nonremovable, NotDroppable; code
 *         review of stage 3.5b.2, finding LB-R1). An item that the catalog does not have has no protection.
 */
bool removal_protected(const gamedata::ItemCatalog* catalog, const ItemInstance& item) {
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
    return definition != nullptr && definition->removal_protected();
}

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
    row.perks = item.perks;
    return row;
}

std::string slot_list(const std::set<std::uint32_t>& slots) {
    std::string text;
    for (const std::uint32_t slot : slots) {
        text += (text.empty() ? "" : ", ") + std::to_string(slot);
    }
    return text.empty() ? "none" : text;
}

/**
 * @return A new item instance of an item of the catalog, with the start values of the item table.
 * @param loot True: a gear score of a loot roll (world/rewards.h). It is not below 1 and not above
 *        the largest gear score of the item; the smallest gear score of the item table is not a
 *        lower limit for it (the loot generation of the client, loot.md 2.1).
 * @param limit The largest gear score (stage B0: the limit of a give, Inventory::give_limit). 0: the
 *        largest gear score of the item table.
 */
ItemInstance new_item(const gamedata::ItemDefinition& definition,
                      std::uint32_t quantity,
                      std::optional<std::uint32_t> gearScore,
                      std::uint32_t chrono,
                      bool loot = false,
                      const PerkIds& perks = {},
                      std::uint32_t limit = 0) {
    ItemInstance item;
    item.instance = new_instance();
    item.itemId = definition.id;
    item.index = definition.index;
    item.quantity = quantity;
    if (definition.maxGearScore != 0) {
        const std::uint32_t low = loot ? 1U : definition.minGearScore;
        const std::uint32_t high = std::max(std::max(definition.maxGearScore, limit), low);
        const std::uint32_t standard = definition.gearScoreOverride != 0 ? definition.gearScoreOverride : definition.minGearScore;
        item.gearScore = std::clamp(gearScore.value_or(standard), low, high);
    }
    item.durability = std::min(definition.durability, kMaxItemDurability);
    item.maxDurability = item.durability;
    item.chrono = chrono;
    item.flags = definition.bindOnPickup ? kItemBound : 0;
    item.perks = perks;
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

/**
 * Puts a quantity of an item into a bag: first into the stacks of the item that are not full, then
 * into new stacks in the first empty slots.
 * @return The quantity that the bag took: less than `quantity` when the bag has no free slot for the rest.
 */
std::uint32_t put(Bag& bag,
                  const gamedata::ItemDefinition& definition,
                  std::uint32_t quantity,
                  std::optional<std::uint32_t> gearScore,
                  bool loot,
                  std::uint8_t action,
                  std::set<std::uint32_t>& changed,
                  const PerkIds& perks = {},
                  std::uint32_t limit = 0) {
    const std::uint32_t maxStack = std::clamp<std::uint32_t>(definition.maxStack, 1, kMaxItemQuantity);
    const std::uint8_t flags = definition.bindOnPickup ? kItemBound : 0;
    std::uint32_t left = quantity;
    // First the stacks of the item that are not full, with the same perks (an equal ItemID alone is not enough).
    for (std::uint32_t index = 0; index < bag.slots.size() && left > 0; ++index) {
        BagSlot& slot = bag.slots[index];
        if (!slot.item || slot.item->itemId != definition.id || slot.item->quantity >= maxStack
            || (slot.item->flags & kItemBound) != flags || slot.item->perks != perks) {
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
            break;
        }
        ItemInstance item = new_item(definition, std::min(left, maxStack), gearScore, bag.nextChrono++, loot, perks, limit);
        left -= item.quantity;
        BagSlot& slot = slot_at(bag, *target);
        slot.item = std::move(item);
        slot.action = action;
        changed.insert(*target);
        search = *target + 1;
    }
    return quantity - left;
}

} // namespace

InstanceId make_instance_id() {
    return new_instance();
}

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
    case ItemError::badPerk:
        return "a perk is not in the perk data, is in two places, is a second gem, or is not a perk for the item";
    case ItemError::notUsable:
        return "the item cannot be used now";
    case ItemError::gearScoreAboveLimit:
        return "the gear score of the item is above the largest gear score of an equipped item (the limit of the expansion)";
    case ItemError::itemProtected:
        return "the item table protects the item (Nonremovable or NotDroppable): it stays with the character";
    case ItemError::missingMaterials:
        return "the unlocked stacks of the bag and the quick slots do not have the quantity of an ingredient";
    }
    return "unknown error";
}

Inventory::Inventory(World& world, state::Store& store, const gamedata::ItemCatalog* catalog)
    : m_world(world), m_store(store), m_catalog(catalog) {}

std::uint32_t Inventory::give_limit(const gamedata::ItemDefinition& definition) const {
    // The upgrade limit of the item table (MaxCraftUpgradeGS: 800 for an artifact) counts up to the largest gear score of an equipped
    // item of the owned expansion (MaxEquipGS); with no expansion data it has no such limit. The gear score range of the item table
    // stays the lower part: an item of the table keeps its range also when the expansion limit is lower.
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const std::uint32_t equip = tables != nullptr && tables->loaded() ? tables->max_equip_gear_score() : 0;
    const std::uint32_t upgrade = equip != 0 ? std::min(definition.maxCraftUpgradeGS, equip) : definition.maxCraftUpgradeGS;
    return std::max(definition.maxGearScore, upgrade);
}

Inventory::Carried Inventory::read_items(const std::string& characterId) const {
    Carried items;
    Bag& bag = items.bag;
    unsigned unknown = 0;
    unsigned unknownPerks = 0;
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
        item.perks = row.perks;
        unknownPerks += resolve_perks(item);
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
    if (unknownPerks != 0) {
        log::write("inventory: %u perk(s) of the items of character %s are not in the perk data: their places are empty for "
                   "the client, and the rows keep them",
                   unknownPerks,
                   characterId.c_str());
    }
    return items;
}

unsigned Inventory::resolve_perks(ItemInstance& item) const {
    const gamedata::PerkData* perks = m_world.perk_data();
    unsigned unknown = 0;
    // The swap table of the game: the instance keeps the perks that the client shows and uses, so the passes of the
    // client change nothing (research of stage 1.3b, perk-swap). The next write of the row keeps them.
    const gamedata::ItemDefinition* definition = m_catalog != nullptr ? m_catalog->find(item.itemId) : nullptr;
    if (perks != nullptr && definition != nullptr && item.has_perks()) {
        perks->apply_swaps(item.perks, item.itemId, definition->itemClass);
    }
    for (std::size_t place = 0; place < kPerkPlaces; ++place) {
        item.perkIndex[place] = 0;
        if (item.perks[place].empty()) {
            continue;
        }
        const gamedata::PerkRow* perk = perks != nullptr ? perks->find(item.perks[place]) : nullptr;
        if (perk != nullptr) {
            item.perkIndex[place] = perk->index;
        } else {
            ++unknown;
        }
    }
    return unknown;
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

Encumbrance Inventory::encumbrance(const Entity& entity) const {
    Encumbrance result;
    const gamedata::PerkData* perks = m_world.perk_data();
    result.max = perks != nullptr && perks->loaded() ? perks->tables().equipLoad.encumbranceMax : gamedata::EquipLoadData{}.encumbranceMax;
    if (m_catalog == nullptr) {
        return result;
    }
    for (const BagSlot& slot : entity.bag.slots) {
        if (!slot.item) {
            continue;
        }
        const gamedata::ItemDefinition* definition = m_catalog->find(slot.item->itemId);
        if (definition != nullptr) {
            result.current += static_cast<std::uint64_t>(definition->weight) * slot.item->quantity;
        }
    }
    const double current = static_cast<double>(result.current);
    result.encumbered = result.max >= 0.0F && current > static_cast<double>(result.max);
    result.overencumbered = result.max >= 0.0F && current > static_cast<double>(result.max) * kEncumbranceImmobilized;
    return result;
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
Inventory::Plan Inventory::stage(const std::string& characterId, Change&& apply) {
    Plan plan;
    plan.characterId = characterId;
    Entity* entity = m_world.find_character(characterId);
    if (entity != nullptr) {
        plan.items.bag = entity->bag;
        plan.items.equipment = entity->equipment;
        plan.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        plan.items = read_items(characterId);
    } else {
        plan.error = ItemError::unknownCharacter;
        return plan;
    }
    // The change is made on a copy: the items of the entity change only after the rows.
    plan.before = plan.items;
    plan.error = apply(plan.items, plan.touched);
    if (!plan.ok()) {
        return plan;
    }
    // A slot that the vector did not have is a new element for the client too.
    for (std::size_t index = plan.before.bag.slots.size(); index < plan.items.bag.slots.size(); ++index) {
        plan.touched.bag.insert(static_cast<std::uint32_t>(index));
    }
    // The rows of the change. An item that left its place has its row removed, also when the
    // change puts it in a different place: the store removes each row of the change first,
    // and then writes the new rows. Thus an item that moves between the bag and the paperdoll
    // is one row at all times.
    const auto place = [&](const ItemInstance* old, const ItemInstance* now, int container, std::uint32_t slot) {
        if (old != nullptr && (now == nullptr || now->instance != old->instance)) {
            plan.removed.push_back(instance_text(old->instance));
        }
        if (now != nullptr) {
            plan.rows.push_back(row_of(*now, container, slot));
        }
    };
    for (const std::uint32_t slot : plan.touched.bag) {
        place(slot < plan.before.bag.slots.size() && plan.before.bag.slots[slot].item ? &*plan.before.bag.slots[slot].item : nullptr,
              plan.items.bag.slots[slot].item ? &*plan.items.bag.slots[slot].item : nullptr,
              state::kContainerBag,
              slot);
    }
    for (const std::uint32_t slot : plan.touched.equipment) {
        place(plan.before.equipment.item(slot), plan.items.equipment.item(slot), state::kContainerPaperdoll, slot);
    }
    return plan;
}

Inventory::Plan Inventory::plan_craft(const std::string& characterId, const std::vector<CraftCost>& costs, const CraftOutput& output,
                                      std::uint32_t jobs) {
    return stage(characterId, [&](Carried& items, Touched& touched) {
        if (m_catalog == nullptr || !m_catalog->loaded()) {
            return ItemError::noCatalog;
        }
        if (jobs == 0) {
            return ItemError::badQuantity;
        }
        const auto same_item = [](const ItemInstance& item, const std::string& id) {
            return item.itemId.size() == id.size()
                   && std::equal(item.itemId.begin(), item.itemId.end(), id.begin(), [](char one, char two) {
                          return std::tolower(static_cast<unsigned char>(one)) == std::tolower(static_cast<unsigned char>(two));
                      });
        };
        const auto usable = [&](const BagSlot& slot, const std::string& id) {
            return slot.item && (slot.item->flags & kItemLocked) == 0 && same_item(*slot.item, id);
        };
        // The materials of the whole batch first, in one count: a batch with too few of them is refused before the plan of its jobs.
        for (const CraftCost& cost : costs) {
            std::uint64_t have = 0;
            for (const BagSlot& slot : items.bag.slots) {
                have += usable(slot, cost.itemId) ? slot.item->quantity : 0;
            }
            for (std::uint32_t slot = paperdoll::kQuickFirst; slot <= paperdoll::kQuickLast; ++slot) {
                const auto found = items.equipment.slots.find(slot);
                have += found != items.equipment.slots.end() && usable(found->second, cost.itemId) ? found->second.item->quantity : 0;
            }
            if (have < static_cast<std::uint64_t>(cost.quantity) * jobs) {
                return ItemError::missingMaterials;
            }
        }
        // The output of all jobs, whole or not at all.
        const std::uint64_t total = static_cast<std::uint64_t>(output.quantity) * jobs;
        if (total == 0 || total > kGiveLimit) {
            return ItemError::badQuantity;
        }
        // Code review of stage 3.7a, finding CR7A-R1: the jobs of a batch run one after the other (Crafting::run plans one job at a time:
        // the costs of the job, then its output). The plan of the batch makes the same steps in the same order on the copy: a batch whose
        // bag has no place for the output of a job before its last job is refused at the request (bagFull), not accepted and stopped at
        // that job. Before: the costs of all jobs left first, and the slots that they freed took the whole output.
        // The cost: each job looks at the bag once for its output; the stacks of a cost are consumed in the order of the slots, thus the
        // search of a cost goes on from the slot of the job before (not when the output is the item of a cost: its stacks can grow).
        const bool resume = std::none_of(costs.begin(), costs.end(), [&](const CraftCost& cost) {
            return cost.itemId.size() == output.itemId.size()
                   && std::equal(cost.itemId.begin(), cost.itemId.end(), output.itemId.begin(), [](char one, char two) {
                          return std::tolower(static_cast<unsigned char>(one)) == std::tolower(static_cast<unsigned char>(two));
                      });
        });
        std::vector<std::uint32_t> from(costs.size(), 0);
        GiveOptions options;
        options.action = output.action;
        options.gearScore = output.gearScore;
        options.exactGearScore = output.gearScore.has_value();
        options.perks = output.perks;
        std::set<std::uint32_t> outputSlots;
        for (std::uint32_t job = 0; job < jobs; ++job) {
            // The costs of one job: the unlocked stacks of the bag first, then the quick slots (the sources of the cache of the client).
            for (std::size_t index = 0; index < costs.size(); ++index) {
                const CraftCost& cost = costs[index];
                std::uint64_t left = cost.quantity;
                const auto take = [&](BagSlot& slot) {
                    if (left == 0 || !usable(slot, cost.itemId)) {
                        return false;
                    }
                    const std::uint32_t taken = static_cast<std::uint32_t>(std::min<std::uint64_t>(left, slot.item->quantity));
                    slot.item->quantity -= taken;
                    if (slot.item->quantity == 0) {
                        slot.item.reset();
                    }
                    slot.action = sync::kItemRemoved;
                    left -= taken;
                    return true;
                };
                std::uint32_t& slot = from[index];
                if (!resume) {
                    slot = 0;
                }
                while (slot < items.bag.slots.size() && left > 0) {
                    if (take(items.bag.slots[slot])) {
                        touched.bag.insert(slot);
                    }
                    // A slot that gave all its units (or had none) has no unit for a later job.
                    if (left > 0) {
                        ++slot;
                    }
                }
                for (std::uint32_t quick = paperdoll::kQuickFirst; quick <= paperdoll::kQuickLast && left > 0; ++quick) {
                    const auto found = items.equipment.slots.find(quick);
                    if (found != items.equipment.slots.end() && take(found->second)) {
                        touched.equipment.insert(quick);
                    }
                }
                if (left > 0) {
                    return ItemError::missingMaterials;
                }
            }
            // The output of the job: stacks of the item first, then empty slots, also a slot that a cost freed.
            outputSlots.clear();
            const ItemError error = give_into(items.bag, outputSlots, output.itemId, output.quantity, options);
            if (error != ItemError::none) {
                return error;
            }
            touched.bag.insert(outputSlots.begin(), outputSlots.end());
        }
        return ItemError::none;
    });
}

void Inventory::coin_written(const std::string& characterId, std::uint64_t coin) {
    Entity* entity = m_world.find_character(characterId);
    if (entity != nullptr && entity->wallet.coin != coin) {
        entity->wallet.coin = coin;
        m_world.touch(entity->id, ComponentKey::currency);
    }
}

bool Inventory::write(const Plan& plan, std::optional<std::uint64_t> coin) {
    // All rows of the change are one transaction, with the coin of a grant.
    return (plan.removed.empty() && plan.rows.empty() && !coin) || m_store.write_items(plan.characterId, plan.removed, plan.rows, coin);
}

ItemResult Inventory::finish(Plan& plan, const char* what) {
    ItemResult result;
    result.inWorld = plan.inWorld;
    const std::string& characterId = plan.characterId;
    Entity* entity = plan.inWorld ? m_world.find_character(characterId) : nullptr;
    Carried& items = plan.items;
    const Touched& touched = plan.touched;
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
        // The weapon in the hand before the change (its slot and its instance: Combat::weapon_key), and its weapon row (stage B3: its tags).
        const std::string heldBefore = Combat::weapon_key(*entity);
        const ItemInstance* held = Combat::weapon_in_hand(*entity);
        const gamedata::CombatTables* combatTables = m_world.combat().tables();
        const gamedata::WeaponRow* before = held != nullptr && combatTables != nullptr ? combatTables->weapon_of(held->itemId) : nullptr;
        entity->bag = std::move(items.bag);
        entity->equipment = std::move(items.equipment);
        // The perks, the attributes, the armor, and the equip load of the new equipment (stage 1.3b).
        if (!touched.equipment.empty()) {
            m_world.gear().refresh(*entity);
            // A committed change of the weapon in the hand (an equip, a preset that replaces it) is a change of the weapon for the abilities
            // (code review of stage B0, finding B0-R2): the window of a counter of a cast with the old weapon ends before the next hit (the
            // rule of Combat::select_weapon). An unchanged weapon in the hand gets no notification.
            if (Combat::weapon_key(*entity) != heldBefore) {
                m_world.abilities().weapon_changed(*entity);
                // Stage B3: the tag removals of the data (the haste of Dodge and Weave ends when the bow leaves the hand). An equip is not a
                // weapon swap: no rule with OnWeaponSwap (an agent decision, research bow-build-2.md K2).
                if (Entity* own = m_world.find_character(characterId)) {
                    m_world.passives().weapon_changed(*own, before, false);
                }
            }
        }
        // The countdown of each item of a consumable cooldown that runs, also for a new item (finding F8 of the review of
        // stage 1.3b).
        if (!touched.bag.empty() || !touched.equipment.empty()) {
            m_world.consumables().items_changed(*entity);
        }
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
ItemResult Inventory::change(const std::string& characterId, const char* what, Change&& apply, std::optional<std::uint64_t> coin) {
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    Plan plan = stage(characterId, std::forward<Change>(apply));
    if (plan.ok() && !write(plan, coin)) {
        plan.error = ItemError::storeFailed;
    }
    if (!plan.ok()) {
        ItemResult result;
        result.error = plan.error;
        result.inWorld = plan.inWorld;
        log::write("inventory: %s of character %s refused: %s", what, characterId.c_str(), item_error_text(result.error));
        return result;
    }
    if (entity != nullptr && coin && *coin != entity->wallet.coin) {
        entity->wallet.coin = *coin;
        m_world.touch(entity->id, ComponentKey::currency);
    }
    return finish(plan, what);
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
        return give_into(bag, changed, itemId, quantity, options);
    });
}

ItemError Inventory::give_into(Bag& bag,
                               std::set<std::uint32_t>& changed,
                               std::string_view itemId,
                               std::uint32_t quantity,
                               const GiveOptions& options) const {
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
    PerkIds perks{};
    if (options.perks) {
        const std::optional<PerkIds> checked = checked_perks(*options.perks, *definition);
        if (!checked) {
            return ItemError::badPerk;
        }
        perks = *checked;
    }
    const std::uint8_t action = std::min(options.action, sync::kLargest);
    const std::set<std::uint32_t> before = changed;
    // The gear score of a give (stage B0): the range of the item table, to the upgrade limit of the item (give_limit). A value
    // outside the range gets the nearest value of the range, as before stage B0 (a decision of an agent: the command makes a
    // valid item, and the log line names the change).
    // Stage 3.7c: the craft gear score has its own cap (client 0x2E57A70): the new instance gets it as it is.
    const std::uint32_t limit = options.exactGearScore && options.gearScore ? std::max(give_limit(*definition), *options.gearScore)
                                                                            : give_limit(*definition);
    if (options.gearScore && definition->maxGearScore != 0
        && (*options.gearScore > limit || *options.gearScore < definition->minGearScore)) {
        log::write("inventory: the gear score %u of %s is outside its range %u to %u: the new item gets %u",
                   *options.gearScore,
                   definition->id.c_str(),
                   definition->minGearScore,
                   limit,
                   std::clamp(*options.gearScore, definition->minGearScore, std::max(limit, definition->minGearScore)));
    }
    // All or nothing: the change is not written when the bag has no place for all of it.
    if (put(bag, *definition, quantity, options.gearScore, false, action, changed, perks, limit) != quantity) {
        return ItemError::bagFull;
    }
    for (const std::uint32_t slot : changed) {
        if (before.count(slot) == 0 && bag.slots[slot].item) {
            resolve_perks(*bag.slots[slot].item);
        }
    }
    return ItemError::none;
}

std::optional<PerkIds> Inventory::checked_perks(const PerkIds& requested, const gamedata::ItemDefinition& definition) const {
    // Each perk is a perk of the data, in one place, and one gem at most (stage 1.3b).
    const gamedata::PerkData* data = m_world.perk_data();
    PerkIds perks{};
    unsigned gems = 0;
    for (std::size_t place = 0; place < kPerkPlaces; ++place) {
        const std::string& name = requested[place];
        if (name.empty()) {
            continue;
        }
        const gamedata::PerkRow* perk = data != nullptr ? data->find(name) : nullptr;
        if (perk == nullptr) {
            return std::nullopt;
        }
        gems += perk->gem() ? 1 : 0;
        // The client removes a perk that the item cannot have (client code at image RVA 0x5CF5750).
        if (perk->deprecated || !gamedata::PerkData::allowed(*perk, definition.itemClass)) {
            return std::nullopt;
        }
        for (std::size_t other = 0; other < place; ++other) {
            if (data->find(requested[other]) == perk) {
                return std::nullopt;
            }
        }
        perks[place] = perk->id;
    }
    if (gems > 1) {
        return std::nullopt;
    }
    if (data != nullptr) {
        data->apply_swaps(perks, definition.id, definition.itemClass);
    }
    return perks;
}

ItemResult Inventory::set_perks(const std::string& characterId, bool paperdoll, std::uint32_t slot, const PerkIds& requested) {
    const std::string what = std::string("perks of the item in slot ") + std::to_string(slot) + (paperdoll ? " of the paperdoll" : " of the bag");
    return change(characterId, what.c_str(), [&](Carried& carried, Touched& touched) {
        std::optional<ItemInstance>* place = nullptr;
        if (paperdoll) {
            if (!paperdoll::valid_slot(slot)) {
                return ItemError::badEquipSlot;
            }
            place = &carried.equipment.slots[slot].item;
        } else {
            if (slot >= carried.bag.slots.size()) {
                return ItemError::badSlot;
            }
            place = &carried.bag.slots[slot].item;
        }
        if (!*place || (*place)->quantity == 0) {
            return ItemError::emptySlot;
        }
        const gamedata::ItemDefinition* definition = m_catalog != nullptr ? m_catalog->find((*place)->itemId) : nullptr;
        if (definition == nullptr) {
            return ItemError::unknownItem;
        }
        const std::optional<PerkIds> perks = checked_perks(requested, *definition);
        if (!perks) {
            return ItemError::badPerk;
        }
        (*place)->perks = *perks;
        resolve_perks(**place);
        (paperdoll ? touched.equipment : touched.bag).insert(slot);
        return ItemError::none;
    });
}

ItemResult Inventory::take_one(const std::string& characterId,
                               bool paperdoll,
                               std::uint32_t slot,
                               const std::function<ItemError(const ItemInstance&)>& check) {
    const std::string what = std::string("use of the item in slot ") + std::to_string(slot) + (paperdoll ? " of the paperdoll" : " of the bag");
    return change(characterId, what.c_str(), [&](Carried& carried, Touched& touched) {
        std::optional<ItemInstance>* place = nullptr;
        if (paperdoll) {
            if (!paperdoll::valid_slot(slot)) {
                return ItemError::badEquipSlot;
            }
            place = &carried.equipment.slots[slot].item;
        } else {
            if (slot >= carried.bag.slots.size()) {
                return ItemError::badSlot;
            }
            place = &carried.bag.slots[slot].item;
        }
        if (!*place || (*place)->quantity == 0) {
            return ItemError::emptySlot;
        }
        if (const ItemError error = check(**place); error != ItemError::none) {
            return error;
        }
        if ((*place)->quantity == 1) {
            place->reset();
        } else {
            --(*place)->quantity;
        }
        (paperdoll ? touched.equipment : touched.bag).insert(slot);
        return ItemError::none;
    });
}

ItemResult Inventory::grant(const std::string& characterId,
                            const std::vector<GrantItem>& items,
                            std::uint64_t coin,
                            std::vector<GrantItem>* lost,
                            bool whole) {
    const std::string what = "grant of " + std::to_string(items.size()) + " item(s) and " + std::to_string(coin / 100) + "."
                             + (coin % 100 < 10 ? "0" : "") + std::to_string(coin % 100) + " coin";
    // The coin of the character after the grant: the wallet limit of the client is the largest value.
    std::optional<std::uint64_t> wallet;
    if (coin != 0) {
        std::uint64_t now = 0;
        if (const Entity* entity = m_world.find_character(characterId)) {
            now = entity->wallet.coin;
        } else {
            now = m_store.currency(characterId, state::kCurrencyCoin);
        }
        wallet = std::min(now + coin, kWalletLimit);
    }
    return change(
        characterId,
        what.c_str(),
        [&](Carried& carried, Touched& touched) {
            if (whole) {
                // The whole result or nothing (code review of stage 3.5b.1, finding CH-R3): an item that the catalog does not have (or with
                // no item type) refuses the whole grant before a change. The caller keeps the complete roll.
                for (const GrantItem& grant : items) {
                    const gamedata::ItemDefinition* definition =
                        m_catalog != nullptr && m_catalog->loaded() ? m_catalog->find(grant.itemId) : nullptr;
                    if (grant.quantity != 0 && (definition == nullptr || definition->type.empty())) {
                        return definition == nullptr ? ItemError::unknownItem : ItemError::noItemType;
                    }
                }
            }
            for (const GrantItem& grant : items) {
                const gamedata::ItemDefinition* definition =
                    m_catalog != nullptr && m_catalog->loaded() ? m_catalog->find(grant.itemId) : nullptr;
                std::uint32_t placed = 0;
                if (definition != nullptr && !definition->type.empty() && grant.quantity != 0) {
                    // The quantity of the drop, not the limit of the give command: `put` stops at the
                    // size of a stack and at the slots of the bag (code review of stage 3.5a, finding F11).
                    placed = put(carried.bag, *definition, grant.quantity, grant.gearScore, true, sync::kItemGathered, touched.bag,
                                 grant.perks);
                    if (whole && placed < grant.quantity) {
                        return ItemError::bagFull; // The whole result or nothing (stage 3.5b.1): the copy is not used.
                    }
                }
                // An item that the catalog does not have, and the part that the bag has no slot for: the
                // character does not get it (the world has no item on the ground).
                if (placed < grant.quantity && lost != nullptr) {
                    lost->push_back(GrantItem{grant.itemId, grant.quantity - placed, grant.gearScore, grant.perks});
                }
            }
            for (const std::uint32_t slot : touched.bag) {
                if (slot < carried.bag.slots.size() && carried.bag.slots[slot].item) {
                    resolve_perks(*carried.bag.slots[slot].item);
                }
            }
            return ItemError::none;
        },
        wallet);
}

std::vector<ItemInstance> Inventory::make_instances(const std::vector<GrantItem>& items, std::vector<GrantItem>* unknown) const {
    std::vector<ItemInstance> made;
    std::uint32_t chrono = 1;
    for (const GrantItem& grant : items) {
        const gamedata::ItemDefinition* definition =
            m_catalog != nullptr && m_catalog->loaded() ? m_catalog->find(grant.itemId) : nullptr;
        if (definition == nullptr || definition->type.empty() || grant.quantity == 0) {
            if (unknown != nullptr && grant.quantity != 0) {
                unknown->push_back(grant);
            }
            continue;
        }
        const std::uint32_t maxStack = std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity);
        for (std::uint32_t left = grant.quantity; left > 0;) {
            ItemInstance item = new_item(*definition, std::min(left, maxStack), grant.gearScore, chrono++, true, grant.perks);
            left -= item.quantity;
            resolve_perks(item);
            made.push_back(std::move(item));
        }
    }
    return made;
}

ItemResult Inventory::receive(const std::string& characterId, const std::vector<ItemInstance>& items, std::vector<std::uint32_t>& taken) {
    taken.assign(items.size(), 0);
    const std::string what = "pickup of " + std::to_string(items.size()) + " stack(s)";
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            const ItemInstance& source = items[index];
            const gamedata::ItemDefinition* definition =
                m_catalog != nullptr && m_catalog->loaded() ? m_catalog->find(source.itemId) : nullptr;
            if (definition == nullptr || source.quantity == 0) {
                continue; // An item that the catalog does not have stays with its source.
            }
            const std::uint32_t maxStack = std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity);
            std::uint32_t left = source.quantity;
            // First the stacks of the same item that are not full (an equal ItemID alone is not enough: same_stack).
            for (std::uint32_t slot = 0; slot < bag.slots.size() && left > 0; ++slot) {
                BagSlot& target = bag.slots[slot];
                if (!target.item || !same_stack(*target.item, source) || target.item->quantity >= maxStack) {
                    continue;
                }
                const std::uint32_t moved = std::min(left, maxStack - target.item->quantity);
                target.item->quantity += moved;
                target.action = sync::kItemGathered;
                changed.insert(slot);
                left -= moved;
            }
            // Then empty slots: the stored instance, its values unchanged.
            std::vector<std::uint32_t> placed;
            std::uint32_t search = 0;
            while (left > 0) {
                const auto slot = free_slot(bag, search);
                if (!slot) {
                    break;
                }
                ItemInstance stack = source;
                stack.instance = new_instance();
                stack.quantity = std::min(left, maxStack);
                stack.chrono = bag.nextChrono++;
                left -= stack.quantity;
                BagSlot& target = slot_at(bag, *slot);
                target.item = std::move(stack);
                target.action = sync::kItemGathered;
                changed.insert(*slot);
                placed.push_back(*slot);
                search = *slot + 1;
            }
            // The source keeps nothing of the instance: its last new stack keeps the instance id (the instance moved).
            if (left == 0 && !placed.empty()) {
                bag.slots[placed.back()].item->instance = source.instance;
            }
            taken[index] = source.quantity - left;
        }
        for (const std::uint32_t slot : changed) {
            if (slot < bag.slots.size() && bag.slots[slot].item) {
                resolve_perks(*bag.slots[slot].item);
            }
        }
        return ItemError::none;
    });
}

ItemResult Inventory::release(const std::string& characterId, const std::vector<DropEntry>& entries, std::vector<ItemInstance>& out) {
    out.clear();
    const std::string what = "discard of " + std::to_string(entries.size()) + " entr" + (entries.size() == 1 ? "y" : "ies");
    return change_bag(characterId, what.c_str(), [&](Bag& bag, std::set<std::uint32_t>& changed) {
        out.clear();
        if (entries.empty()) {
            return ItemError::emptyRequest;
        }
        // The rules of drop(): each entry against the bag with the entries before it applied; one wrong entry refuses the request.
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
                return ItemError::itemLocked;
            }
            if (removal_protected(m_catalog, *source.item)) {
                return ItemError::itemProtected; // Nonremovable or NotDroppable of the item table (finding LB-R1): before any write.
            }
            if (entry.quantity == 0 || entry.quantity > source.item->quantity) {
                return ItemError::badQuantity;
            }
            ItemInstance left = *source.item;
            left.quantity = entry.quantity;
            if (entry.quantity == source.item->quantity) {
                source.item.reset();
            } else {
                left.instance = new_instance();
                source.item->quantity -= entry.quantity;
            }
            out.push_back(std::move(left));
            source.action = sync::kItemRemoved;
            changed.insert(entry.slot);
        }
        return ItemError::none;
    });
}

ItemResult Inventory::release_instance(const std::string& characterId, const InstanceId& instance, std::uint32_t quantity,
                                       std::vector<ItemInstance>& out) {
    std::optional<std::uint32_t> slot;
    if (const Entity* entity = m_world.find_character(characterId)) {
        for (std::uint32_t index = 0; index < entity->bag.slots.size(); ++index) {
            if (entity->bag.slots[index].item && entity->bag.slots[index].item->instance == instance) {
                slot = index;
                break;
            }
        }
    }
    if (!slot) {
        out.clear();
        ItemResult result;
        result.error = ItemError::wrongInstance;
        log::write("inventory: give of an instance of character %s refused: the bag has no such instance", characterId.c_str());
        return result;
    }
    return release(characterId, {DropEntry{*slot, instance, quantity}}, out);
}

ItemResult Inventory::release_equipped(const std::string& characterId, std::uint32_t slot, std::uint32_t quantity,
                                       std::vector<ItemInstance>& out) {
    out.clear();
    const std::string what = "discard of the item of slot " + std::to_string(slot) + " of the paperdoll";
    return change(characterId, what.c_str(), [&](Carried& carried, Touched& touched) {
        out.clear();
        if (!paperdoll::valid_slot(slot)) {
            return ItemError::badEquipSlot;
        }
        const auto found = carried.equipment.slots.find(slot);
        if (found == carried.equipment.slots.end() || !found->second.item) {
            return ItemError::emptySlot;
        }
        ItemInstance& item = *found->second.item;
        if ((item.flags & kItemLocked) != 0) {
            return ItemError::itemLocked;
        }
        if (removal_protected(m_catalog, item)) {
            return ItemError::itemProtected; // Nonremovable or NotDroppable of the item table (finding LB-R1): before any write.
        }
        // The request names the quantity of the slot (the client sends 1 for a piece of equipment).
        if (quantity == 0 || quantity > item.quantity) {
            return ItemError::badQuantity;
        }
        ItemInstance left = item;
        left.quantity = quantity;
        if (quantity == item.quantity) {
            found->second.item.reset();
        } else {
            left.instance = new_instance();
            item.quantity -= quantity;
        }
        found->second.action = sync::kItemUnequipped;
        out.push_back(std::move(left));
        touched.equipment.insert(slot);
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
        } else if (same_stack(*target.item, *source.item) && target.item->quantity < maxStack) {
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
            if (removal_protected(m_catalog, *source.item)) {
                return ItemError::itemProtected; // The item table keeps the item (finding LB-R1).
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
        return equip_into(items, touched, bagSlot, paperdollSlot, instance, quantity);
    });
}

ItemError Inventory::equip_into(Carried& items,
                                Touched& touched,
                                std::uint32_t bagSlot,
                                std::optional<std::uint32_t> paperdollSlot,
                                const InstanceId* instance,
                                std::optional<std::uint32_t> quantity) const {
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
    // The largest gear score of an equipped item: MaxEquipGS of the owned expansion (stage 1.1b; the rule of the client,
    // equipmentcommon ShowCannotEquipNotification: an item above it is not equipped). No expansion data: no limit.
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const std::uint32_t limit = tables != nullptr && tables->loaded() ? tables->max_equip_gear_score() : 0;
    if (limit != 0 && source.item->gearScore > limit) {
        return ItemError::gearScoreAboveLimit;
    }
    // The item that is in the slot goes into the place of the new one.
    EquipmentSlot& target = items.equipment.slots[*slot];
    std::swap(source.item, target.item);
    target.action = sync::kItemEquipped;
    source.action = sync::kItemUnequipped;
    touched.bag.insert(bagSlot);
    touched.equipment.insert(*slot);
    return ItemError::none;
}

Inventory::Plan Inventory::plan_build(const std::string& characterId, const std::vector<BuildItem>& build) {
    return stage(characterId, [&](Carried& items, Touched& touched) { return build_into(items, touched, build); });
}

ItemError Inventory::check_build(const std::vector<BuildItem>& build, std::string& detail) const {
    Carried items;
    Touched touched;
    if (const ItemError error = build_into(items, touched, build); error != ItemError::none) {
        detail = "the items: " + std::string(item_error_text(error));
        return error;
    }
    for (const BuildItem& entry : build) {
        if (!entry.slot) {
            continue;
        }
        const ItemInstance* item = items.equipment.item(*entry.slot);
        if (item == nullptr || item->itemId != entry.itemId) {
            detail = "the item " + entry.itemId + " is not in slot " + std::to_string(*entry.slot);
            return ItemError::slotRefusesItem;
        }
        if (entry.gearScore && item->gearScore != *entry.gearScore) {
            detail = "the gear score " + std::to_string(*entry.gearScore) + " of " + entry.itemId + " is outside its range (a give makes "
                     + std::to_string(item->gearScore) + ")";
            return ItemError::badQuantity;
        }
        if (item->perks != entry.perks) {
            detail = "a perk of " + entry.itemId + " is not the stored perk (the swap of the game changes it)";
            return ItemError::badPerk;
        }
    }
    return ItemError::none;
}

ItemError Inventory::build_into(Carried& items, Touched& touched, const std::vector<BuildItem>& build) const {
    {
        for (const BuildItem& entry : build) {
            GiveOptions options;
            options.gearScore = entry.gearScore;
            options.perks = entry.perks;
            if (!entry.slot) {
                // A stack of the bag: the give of a command (the stacks of the item that are not full first), up to the quantity of the
                // build: the items of the bag with no perk count (a second apply gives no second stack; a decision of an agent).
                std::uint32_t held = 0;
                for (const BagSlot& slot : items.bag.slots) {
                    if (slot.item && slot.item->itemId == entry.itemId && slot.item->perks == PerkIds{}) {
                        held += slot.item->quantity;
                    }
                }
                if (held >= entry.quantity) {
                    continue;
                }
                if (const ItemError error = give_into(items.bag, touched.bag, entry.itemId, entry.quantity - held, options);
                    error != ItemError::none) {
                    return error;
                }
                continue;
            }
            // The slot has the item of the build already (the same item, gear score, and perks): it stays (a second apply, or a different
            // variant, changes no item; a decision of an agent). A stack with fewer items than the build (the potions of a quick slot) gets
            // the quantity of the build.
            const auto held = items.equipment.slots.find(*entry.slot);
            ItemInstance* there = held != items.equipment.slots.end() && held->second.item ? &*held->second.item : nullptr;
            if (there != nullptr && there->itemId == entry.itemId && (!entry.gearScore || there->gearScore == *entry.gearScore)
                && there->perks == entry.perks) {
                if (there->quantity < entry.quantity) {
                    const gamedata::ItemDefinition* definition = m_catalog != nullptr ? m_catalog->find(entry.itemId) : nullptr;
                    if (definition == nullptr || entry.quantity > std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity)) {
                        return ItemError::badQuantity;
                    }
                    there->quantity = entry.quantity;
                    held->second.action = sync::kItemAdded;
                    touched.equipment.insert(*entry.slot);
                }
                continue;
            }
            // An item of the paperdoll: a new instance in a free slot of the bag (the give, not into a stack: the equip moves the
            // whole stack of its slot), then the equip of that slot, which puts the old item of the paperdoll into it.
            const std::optional<std::uint32_t> free = free_slot(items.bag);
            if (!free) {
                return ItemError::bagFull;
            }
            Bag single;
            std::set<std::uint32_t> made;
            single.nextChrono = items.bag.nextChrono;
            if (const ItemError error = give_into(single, made, entry.itemId, entry.quantity, options); error != ItemError::none) {
                return error;
            }
            if (made.size() != 1 || !single.slots[0].item) {
                return ItemError::badQuantity; // The quantity of a slot of the paperdoll is one stack.
            }
            items.bag.nextChrono = single.nextChrono;
            BagSlot& target = slot_at(items.bag, *free);
            target.item = std::move(single.slots[0].item);
            target.action = sync::kItemAdded;
            touched.bag.insert(*free);
            if (const ItemError error = equip_into(items, touched, *free, entry.slot, nullptr, std::nullopt); error != ItemError::none) {
                return error;
            }
        }
        return ItemError::none;
    }
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

std::uint32_t menu_appearance(const gamedata::ItemDefinition& item, bool female) {
    if (!gamedata::class_has_tags(item.itemClass, "Armor")) {
        return 0;
    }
    return female ? item.armorAppearanceF : item.armorAppearanceM;
}

std::vector<state::MenuEquipment> menu_equipment(const gamedata::ItemCatalog& catalog,
                                                 const std::vector<state::ItemRow>& rows,
                                                 bool female) {
    std::vector<state::MenuEquipment> entries;
    for (const state::ItemRow& row : rows) {
        if (row.container != state::kContainerPaperdoll || row.slot >= state::kMenuSlotKeys.size()) {
            continue;
        }
        const gamedata::ItemDefinition* definition = catalog.find(row.itemId);
        const std::uint32_t appearance = definition != nullptr ? menu_appearance(*definition, female) : 0;
        if (appearance != 0) {
            entries.push_back({row.slot, appearance});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const state::MenuEquipment& left, const state::MenuEquipment& right) {
        return left.slot < right.slot;
    });
    return entries;
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

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata/item_catalog.h"
#include "state/store.h"
#include "world/entity.h"
#include "world/item.h"

// The inventory system: the items, the equipment, and the coin of each character.
//
// Each function names a character by its id (roadmap, rule MP-9). The system finds the bag and
// the equipment of the character: the entity of the character when it is in the world, or the
// rows of the store when it is not. It checks the request, computes the result itself (rule
// MP-5), writes all rows of the change in one transaction (rule MP-7: a move between the bag
// and the paperdoll is one transaction), and then changes the entity and marks the component
// states: the encoders send the result to each session that observes the entity (rule MP-2).
// A request that is not correct changes nothing.
//
// World thread only, but for start_items, which reads only the item catalog.

namespace soulwarden::backend::world {

class World;

enum class ItemError {
    none,
    /** The store has no character with this id. */
    unknownCharacter,
    /** The backend has no item catalog (tools/gamedata/items.sh makes it). */
    noCatalog,
    /** The catalog has no item with this id. */
    unknownItem,
    /** The item has no item type: the client has no item class for it. */
    noItemType,
    /** The quantity is 0, or it is larger than the quantity that the request can have. */
    badQuantity,
    /** The bag has no free slot for the result. */
    bagFull,
    /** The slot number is not a slot of the bag. */
    badSlot,
    /** The slot has no item. */
    emptySlot,
    /** The slot has a different item instance than the request names. */
    wrongInstance,
    /** The source and the target are the same slot. */
    sameSlot,
    /** The target slot of a split has an item. */
    targetInUse,
    /** The request has no entry. */
    emptyRequest,
    /** The slot number is not a slot of the paperdoll. */
    badEquipSlot,
    /** The slot of the paperdoll does not take an item of this class. */
    slotRefusesItem,
    /** The item has no slot of its own in the paperdoll: the request must name a slot. */
    noEquipSlot,
    /** The item has the lock of the player: a request of a client does not remove it. */
    itemLocked,
    /** The character has less coin than the cost. */
    notEnoughCoin,
    /** The database did not accept the change. */
    storeFailed,
    /** A perk of the request is not in the perk data, is in two places, is a second gem, or is not a perk for the item. */
    badPerk,
    /** The consumable system refused the use (world/consumables.h gives the cause). */
    notUsable,
    /**
     * The gear score of the item is above the largest gear score of an equipped item (MaxEquipGS of the owned expansion, stage
     * 1.1b: 800 with Rise of the Angry Earth, 625 with no expansion).
     */
    gearScoreAboveLimit,
};

/** @return A short text for a log line and for the reply of a command. */
[[nodiscard]] const char* item_error_text(ItemError error);

struct ItemResult {
    ItemError error{ItemError::none};
    /** True: the character is in the world, and its session gets the change. False: only the rows changed. */
    bool inWorld{false};
    /** The slots of the bag that changed. */
    std::vector<std::uint32_t> slots;
    /** The slots of the paperdoll that changed. */
    std::vector<std::uint32_t> equipmentSlots;

    [[nodiscard]] bool ok() const {
        return error == ItemError::none;
    }
};

struct GiveOptions {
    /** Gear score of a new instance. No value: the value of the item table. */
    std::optional<std::uint32_t> gearScore;
    /**
     * The perks of each new instance (stage 1.3b): the PerkID text of each place (a gem is one of
     * them). No value: no perk. Each perk must be in the perk data, a perk is in one place at most,
     * and an instance has one gem at most.
     */
    std::optional<PerkIds> perks;
    /** The cause that the client gets for the changed slots (namespace sync). */
    std::uint8_t action{sync::kItemAdded};
};

/** One item of a grant (Inventory::grant): the loot of a kill. */
struct GrantItem {
    /** The ItemID text of the item catalog. */
    std::string itemId;
    std::uint32_t quantity{0};
    /** The gear score of a new instance. No value: the value of the item table. */
    std::optional<std::uint32_t> gearScore;
    /** The perks of a new instance (stage 1.3b: the roll of world/perk_roll.h). */
    PerkIds perks{};
};

/** One entry of a drop request: a quantity of the item instance in a slot. */
struct DropEntry {
    std::uint32_t slot{0};
    InstanceId instance{};
    std::uint32_t quantity{0};
};

/** The bag, the equipment, and the coin of a character, for a reader. */
struct Holdings {
    Bag bag;
    Equipment equipment;
    Wallet wallet;
    /** True: the values are the values of the entity of the character. False: of the rows. */
    bool inWorld{false};
};

/** @return A new item instance id: 16 random bytes, not all zero (stage 3.5b.2: the part of a stack that a pickup moves). */
[[nodiscard]] InstanceId make_instance_id();

/** Largest coin amount (hundredths): the wallet limit of the client (`javelin.wallet-cap-player`). */
inline constexpr std::uint64_t kWalletLimit = 100000000;
/** Largest quantity of one give command. */
inline constexpr std::uint32_t kGiveLimit = 100000;

/**
 * @return The rows of the items that a new character has: the items of its backstory (the
 *         archetype of the character creation) that have a quantity. An item with a slot of its
 *         own in the paperdoll that is free goes into that slot (the clothing, the first
 *         weapon); each other item goes into the bag. No row when the catalog does not have
 *         the backstory. The store writes the rows in the transaction that inserts the
 *         character. Any thread: the function reads only the catalog.
 */
[[nodiscard]] std::vector<state::ItemRow> start_items(const gamedata::ItemCatalog& catalog, std::string_view backstory);

class Inventory {
public:
    /** @param catalog The item catalog, or null: then the system gives no item. */
    Inventory(World& world, state::Store& store, const gamedata::ItemCatalog* catalog);

    /** Reads the bag, the equipment, and the coin of the character of a new entity from the store. */
    void load(Entity& entity);

    /**
     * Sets the index on the wire of each perk of an instance from the perk data (0 for a perk that the
     * data does not have: the place is empty for the client, and the row keeps the id).
     * @return The number of perks that the data does not have.
     */
    unsigned resolve_perks(ItemInstance& item) const;

    /** @return The bag, the equipment, and the coin of a character, or no value when the store does not have the character. */
    [[nodiscard]] std::optional<Holdings> holdings(const std::string& characterId);

    /**
     * Gives a quantity of an item to a character. The quantity goes to the stacks of the item
     * that are not full first, then to new stacks in the first empty slots. When the bag has
     * no place for all of it, nothing changes.
     */
    ItemResult give(const std::string& characterId,
                    std::string_view itemId,
                    std::uint32_t quantity,
                    const GiveOptions& options = {});

    /**
     * Gives the loot of a kill to a character (world/rewards.h): the items and the coin in one
     * transaction. Each slot that changes has the cause "ItemGathered" (sync::kItemGathered): the
     * client shows its loot notice for it. An item goes into the stacks of the item that are not
     * full first, then into new stacks; a part that the bag has no free slot for, and an item that
     * the catalog does not have, is not given (it goes into `lost`), and the other items and the
     * coin are given. The coin is not more than the wallet limit. A gear score of an item is the
     * gear score of the roll (not below 1, and not above the largest gear score of the item).
     * @param whole True (the open of a chest, stage 3.5b.1): when the bag has no place for the whole quantity of an item of the
     *        catalog, nothing changes (ItemError::bagFull). An item that the catalog does not have (or with no item type) also changes
     *        nothing (ItemError::unknownItem or noItemType; code review of stage 3.5b.1, finding CH-R3): the caller keeps the roll.
     */
    ItemResult grant(const std::string& characterId,
                     const std::vector<GrantItem>& items,
                     std::uint64_t coin,
                     std::vector<GrantItem>* lost = nullptr,
                     bool whole = false);

    /**
     * @return The item instances of the items of a loot roll (stage 3.5b.2: the part of a personal loot bag; docs/systems/loot-bags.md):
     *         for each item of the catalog stacks of its stack size, each a new instance with the values of a loot item of a grant (the
     *         instance id, the gear score of the roll, the durability, the bind flag of BindOnPickup, the perks). The instance is made one
     *         time: a pickup moves it. An item that the catalog does not have (or with no item type) is not made: it goes into `unknown`.
     */
    [[nodiscard]] std::vector<ItemInstance> make_instances(const std::vector<GrantItem>& items, std::vector<GrantItem>* unknown = nullptr) const;

    /**
     * Puts item instances of a source (a loot bag, stage 3.5b.2) into the bag of a character: each one goes first into the stacks of the
     * same item that are not full (same_stack: the item, the bind flag, the perks), then into empty slots. A new stack keeps the instance
     * id when the source keeps nothing of the instance, else it gets a new one; the gear score, the durability, the flags, and the perks
     * stay. The part that has no place stays with the source (a full bag takes what fits). Each changed slot has the cause ItemGathered
     * (the loot notice). One transaction of the rows (in a batch: a savepoint).
     * @param taken Gets, for each instance of `items`, the quantity that the bag took (0 to its quantity).
     */
    ItemResult receive(const std::string& characterId, const std::vector<ItemInstance>& items, std::vector<std::uint32_t>& taken);

    /**
     * Takes the quantities of a drop request out of the bag for a discard (stage 3.5b.2): the rules of `drop` (each entry names the
     * instance of its slot, a locked item stays, one wrong entry refuses the whole request). A whole stack leaves with its instance id; a
     * part of a stack leaves as a new instance (the rest keeps the id). One transaction of the rows (in a batch: a savepoint).
     * @param out Gets the instances that left the bag.
     */
    ItemResult release(const std::string& characterId, const std::vector<DropEntry>& entries, std::vector<ItemInstance>& out);
    /** The same for the stack with this instance id, wherever it is in the bag (a give to a loot bag, request 2801). */
    ItemResult release_instance(const std::string& characterId, const InstanceId& instance, std::uint32_t quantity,
                                std::vector<ItemInstance>& out);
    /** The same for the item of a slot of the paperdoll (the drop of an equipped item, request 826): the full quantity of the slot. */
    ItemResult release_equipped(const std::string& characterId, std::uint32_t slot, std::uint32_t quantity, std::vector<ItemInstance>& out);

    /** Removes a quantity from the item of a slot. No quantity: the full stack. */
    ItemResult remove(const std::string& characterId, std::uint32_t slot, std::optional<std::uint32_t> quantity = {});

    /**
     * Moves the item of a slot to a different slot. An empty target gets the item. A target
     * with a stack of the same item that is not full gets as much as it can hold. A target
     * with a different item changes places with the source.
     */
    ItemResult move(const std::string& characterId, std::uint32_t from, std::uint32_t to);

    /**
     * Takes a quantity out of a stack and makes a new stack of it, in the slot `to`, or in the
     * first empty slot. The quantity must be smaller than the stack.
     */
    ItemResult split(const std::string& characterId,
                     std::uint32_t slot,
                     std::uint32_t quantity,
                     std::optional<std::uint32_t> to = {});

    /**
     * Sets or clears the lock of the item of a slot.
     * @param instance The item instance that the request names, or null for no check.
     */
    ItemResult set_locked(const std::string& characterId, std::uint32_t slot, const InstanceId* instance, bool locked);

    /**
     * Removes the quantities of a drop request from the bag. Each entry must name the item
     * instance of its slot, and an item with the lock of the player does not go. When one
     * entry is not correct, nothing changes. The world has no entity for an item on the
     * ground yet: the items are gone.
     *
     * The lock protects an item from the requests of a client that remove it from the
     * character (the drop now; later the salvage, the sale, and the trade). It does not stop
     * an equip: the equip check of the client has no lock test (image RVA 0x679C7A0), and the
     * item stays an item of the character. A development command (remove) has no lock rule.
     */
    ItemResult drop(const std::string& characterId, const std::vector<DropEntry>& entries);

    /** Sets the coin of a character (hundredths of a coin, kWalletLimit at most). */
    ItemResult set_coin(const std::string& characterId, std::uint64_t amount);

    /**
     * Takes a cost in coin from a character, for a different system (the cost of an attribute
     * reset). The other system gives the function that writes its own rows and the new coin
     * in one transaction (rule MP-7); the wallet of the entity changes after that write.
     * @param write Gets the new coin of the character. False: the database did not accept the change.
     * @return The error `notEnoughCoin` when the character has less coin than the cost: then
     *         `write` did not run, and nothing changed.
     */
    ItemResult pay(const std::string& characterId, std::uint64_t cost, const std::function<bool(std::uint64_t coin)>& write);

    /**
     * Moves the item of a slot of the bag into a slot of the paperdoll. The slot must take the
     * class of the item. An item that is in the slot of the paperdoll goes into the slot of
     * the bag. The full stack moves.
     * @param paperdollSlot The slot, or no value: the slot of the item class (the first free
     *        weapon slot for a weapon).
     * @param instance The item instance that the request names, or null for no check.
     * @param quantity The quantity that the request names, or no value. A quantity that is
     *        not the quantity of the stack is refused.
     */
    ItemResult equip(const std::string& characterId,
                     std::uint32_t bagSlot,
                     std::optional<std::uint32_t> paperdollSlot = {},
                     const InstanceId* instance = nullptr,
                     std::optional<std::uint32_t> quantity = {});

    /**
     * Moves the item of a slot of the paperdoll into the bag: into the slot `bagSlot`, which
     * must be empty, or into the first empty slot. The full stack moves.
     * @param quantity The quantity that the request names, or no value. A quantity that is
     *        not the quantity of the stack is refused.
     */
    ItemResult unequip(const std::string& characterId,
                       std::uint32_t paperdollSlot,
                       std::optional<std::uint32_t> bagSlot = {},
                       std::optional<std::uint32_t> quantity = {});

    /**
     * The items of two slots of the paperdoll change places. One of the slots can be empty.
     * Each item must be an item for its new slot.
     * @param firstInstance, secondInstance The item instance that the request names for each
     *        slot (all zero for an empty slot), or null for no check.
     */
    /**
     * Sets the perks of the item of a slot of the bag or of the paperdoll (a command; stage 1.3b, the change of a
     * character of the user, I11): the checks and the swap of a give (GiveOptions::perks). The instance, the gear
     * score, and the durability stay; the row changes in one transaction.
     */
    ItemResult set_perks(const std::string& characterId, bool paperdoll, std::uint32_t slot, const PerkIds& perks);

    /**
     * Takes one item from a slot of the bag or of the paperdoll (a quick slot): the use of a
     * consumable (world/consumables.h). The row changes in one transaction; an item with a
     * quantity of 1 goes away.
     * @param check Gets the item before the change: an error other than `none` stops the change.
     */
    ItemResult take_one(const std::string& characterId,
                        bool paperdoll,
                        std::uint32_t slot,
                        const std::function<ItemError(const ItemInstance&)>& check);

    ItemResult swap_equipped(const std::string& characterId,
                             std::uint32_t first,
                             std::uint32_t second,
                             const InstanceId* firstInstance = nullptr,
                             const InstanceId* secondInstance = nullptr);

private:
    /** The items of a character: the copy that a change works on. */
    struct Carried {
        Bag bag;
        Equipment equipment;
    };
    /** The slots that a change touched. */
    struct Touched {
        std::set<std::uint32_t> bag;
        std::set<std::uint32_t> equipment;
    };

    /**
     * A change of the bag and of the equipment: `apply(Carried&, Touched&)` gives an ItemError.
     * @param coin When it has a value: the new coin of the character, in the transaction of the rows.
     */
    template <class Change>
    ItemResult change(const std::string& characterId, const char* what, Change&& apply, std::optional<std::uint64_t> coin = {});
    /** A change of the bag only: `apply(Bag&, std::set<std::uint32_t>&)` gives an ItemError. */
    template <class Change>
    ItemResult change_bag(const std::string& characterId, const char* what, Change&& apply);
    [[nodiscard]] Carried read_items(const std::string& characterId) const;
    /** @return The perks of a request after the checks and the swap of the game, or no value for a perk that is not correct. */
    [[nodiscard]] std::optional<PerkIds> checked_perks(const PerkIds& requested, const gamedata::ItemDefinition& definition) const;

    World& m_world;
    state::Store& m_store;
    const gamedata::ItemCatalog* m_catalog;
};

} // namespace soulwarden::backend::world

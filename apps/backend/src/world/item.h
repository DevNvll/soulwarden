#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "world/ids.h"

// The items, the equipment, and the coin of a character: plain data of its entity (roadmap,
// rule MP-1). The inventory system (world/inventory.h) changes them, and the encoders of the
// container state, of the paperdoll state, and of the currency state make the wire form
// (server/rep/states).

namespace soulwarden::backend::world {

/** The 16 bytes that name one item instance (one stack) on the wire. All zero: no instance. */
using InstanceId = std::array<std::uint8_t, 16>;

/**
 * Why a slot changed: the value `ActionCausingSync` of the client (protocol notes 4.10). The
 * client shows a line on the HUD for some values when the quantity of a slot changes.
 */
namespace sync {
inline constexpr std::uint8_t kItemAdded = 0;
inline constexpr std::uint8_t kItemGathered = 1;
inline constexpr std::uint8_t kItemRemoved = 2;
/** The item came into a slot of the paperdoll: the client shows its equip notice. */
inline constexpr std::uint8_t kItemEquipped = 4;
/** The item left a slot of the paperdoll. The client does the entries with this cause first. */
inline constexpr std::uint8_t kItemUnequipped = 5;
inline constexpr std::uint8_t kContainerTransfer = 6;
/** Start value of a slot in the client: the load of a container. It gives no line on the HUD. */
inline constexpr std::uint8_t kLoad = 0x11;
inline constexpr std::uint8_t kLockStateChanged = 28;
/** Largest value that the wire form can hold (6 bits). */
inline constexpr std::uint8_t kLargest = 63;
} // namespace sync

/** Flags of an item instance on the wire. */
inline constexpr std::uint8_t kItemBound = 0x01;
inline constexpr std::uint8_t kItemLocked = 0x02;

/** Limits of the wire form of one item (the packed number, protocol notes 4.10). */
inline constexpr std::uint32_t kMaxItemQuantity = 10000;
inline constexpr std::uint32_t kMaxItemDurability = 8000;
/** Largest number of slots of one container: the client reads 500 elements at most. */
inline constexpr std::uint32_t kMaxContainerSlots = 500;

/** One item instance: one stack in one slot. */
struct ItemInstance {
    InstanceId instance{};
    /** The ItemID text of the item table: the key of the item in the database. */
    std::string itemId;
    /**
     * Index of the item in the list of the client (gamedata/item_catalog.h). 0: the catalog
     * does not have the item, and the slot is empty on the wire.
     */
    std::uint32_t index{0};
    std::uint32_t quantity{0};
    std::uint32_t gearScore{0};
    std::uint32_t durability{0};
    std::uint32_t maxDurability{0};
    /** Order counter of the item in its container. */
    std::uint32_t chrono{0};
    /** kItemBound, kItemLocked. */
    std::uint8_t flags{0};
};

/** One slot of a container. */
struct BagSlot {
    /** No value: the slot is empty. */
    std::optional<ItemInstance> item;
    /** The cause of the last change of the slot (namespace sync). */
    std::uint8_t action{sync::kLoad};
    /** World revision of the last change of the slot. */
    Revision changed{0};
};

/**
 * The bag of a character: one element for each slot, as the replicated vector of the client.
 * The vector does not become shorter while the character is in the world: a slot that loses
 * its item stays as an empty slot, and a new item gets the first empty slot.
 */
struct Bag {
    std::vector<BagSlot> slots;
    /** Order counter for the next new stack. */
    std::uint32_t nextChrono{1};
    /**
     * A session whose last record of the entity is older than this revision gets the full
     * vector again, not only the slots that changed.
     */
    Revision reset{0};
};

/**
 * The slots of the paperdoll: the equipment of a character. The numbers are the values
 * `Javelin::PaperdollSlotTypes` of the client (protocol notes 4.11).
 */
namespace paperdoll {
inline constexpr std::uint32_t kHead = 0;
inline constexpr std::uint32_t kChest = 1;
inline constexpr std::uint32_t kHands = 2;
inline constexpr std::uint32_t kLegs = 3;
inline constexpr std::uint32_t kFeet = 4;
inline constexpr std::uint32_t kAmulet = 5;
/** The earring: the item class of the client is "EquippableToken". */
inline constexpr std::uint32_t kToken = 6;
inline constexpr std::uint32_t kRing = 7;
/** The three weapons, and the off hand item (a shield) of each one. */
inline constexpr std::uint32_t kMainHand1 = 24;
inline constexpr std::uint32_t kMainHand3 = 26;
inline constexpr std::uint32_t kOffHand1 = 27;
inline constexpr std::uint32_t kOffHand3 = 29;
/** Number of slot values: a slot number is below it. */
inline constexpr std::uint32_t kSlotCount = 50;

/**
 * @return True for a slot of the slot database of the player (`paperdoll.paperdolldb`): 0 to
 *         20 and 24 to 49. The client accepts no item for a different number.
 */
[[nodiscard]] bool valid_slot(std::uint32_t slot);
/**
 * @return True when the slot takes an item with this class text (the tags of the column
 *         ItemClass with "+" between them). The rule is the rule of the client (image RVA
 *         0x679C060). A slot whose rule the backend does not have takes no item.
 */
[[nodiscard]] bool slot_accepts(std::uint32_t slot, std::string_view itemClass);
/**
 * @return True for a slot whose item shows on the body: the armor (0 to 4) and the weapons
 *         (24 to 29). Each observer gets the item of such a slot.
 */
[[nodiscard]] bool visible_slot(std::uint32_t slot);
/** @return True for a slot of a weapon or of its off hand item (24 to 29). */
[[nodiscard]] bool weapon_slot(std::uint32_t slot);
} // namespace paperdoll

/** One slot of the paperdoll: the same data as a slot of the bag. */
using EquipmentSlot = BagSlot;

/**
 * The equipment of a character: the items in the slots of its paperdoll. A slot that loses
 * its item stays in the map as an empty slot while the character is in the world: the client
 * removes an item only when it gets the slot with no item.
 */
struct Equipment {
    /** Slot number of the paperdoll to the slot. */
    std::map<std::uint32_t, EquipmentSlot> slots;
    /** World revision of the last change of a weapon slot: the sheathe bits and the active bits changed. */
    Revision weaponsChanged{0};
    /** A session whose last record of the entity is older than this revision gets each slot again. */
    Revision reset{0};

    /** @return The item of a slot, or null. */
    [[nodiscard]] const ItemInstance* item(std::uint32_t slot) const {
        const auto found = slots.find(slot);
        return found != slots.end() && found->second.item ? &*found->second.item : nullptr;
    }
    /**
     * @return The weapon slot that is active, or no value when the character has no weapon:
     *         the first of the three weapon slots that has an item. The backend has no weapon
     *         change yet (the action system, roadmap 3.1).
     */
    [[nodiscard]] std::optional<std::uint32_t> active_weapon() const {
        for (std::uint32_t slot = paperdoll::kMainHand1; slot <= paperdoll::kMainHand3; ++slot) {
            if (item(slot) != nullptr) {
                return slot;
            }
        }
        return std::nullopt;
    }
};

/** The coin of a character, in hundredths of a coin. */
struct Wallet {
    std::uint64_t coin{0};
};

/** @return The instance id as 32 hexadecimal digits (the key of the row in the database). */
[[nodiscard]] std::string instance_text(const InstanceId& instance);
/** @return The instance id of a text of 32 hexadecimal digits, or no value. */
[[nodiscard]] std::optional<InstanceId> read_instance(const std::string& text);

} // namespace soulwarden::backend::world

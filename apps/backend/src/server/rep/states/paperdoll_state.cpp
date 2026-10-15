#include <algorithm>
#include <array>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::PaperdollComponentReplicatedState (constructor at image RVA 0x670D610), slot 62 of the
// player slice and of the remote player slice (component Paperdoll of the entity "Inventory":
// the equipment). Three groups, group 0 has no field (protocol notes 4.11).
//
//   group 1 (the owner):   0 visibleDurability       list of u32
//                          1 visibleFullItemData     list of item
//                          2 nonVisibleFullItemData  list of item
//                          3 nonVisiblePaperdollSlots list of u32
//                          4 nonVisibleDurability    list of u32
//                          6 isLocalPlayer           1 byte
//   group 2 (each client): 0 sheatheMap              7 bytes, bit N is for slot N
//                          2 activeMap               7 bytes
//                          3 visiblePaperdollSlots   list of u32
//                          4 visibleItemVisualData   list of item look
//
//   list      := 00 version count:size element*count           the full form (reader 0x2F9BC20)
//   version   := has:u8 [sequence:size64 when has is 1]         the backend sends has = 0
//   item      := the element of a container (states/container_state.cpp, reader 0x5CE8C40)
//   item look := itemIndex:size flags:u8 [size when bit 0] [dye:4 bytes when bit 1]
//                [appearance:u32 skinDye:4 bytes]               (reader 0x5CE9070)
//
// The lists of a fragment are parallel: entry N is one slot with its item. The client handles
// each entry as a change of that slot, and a slot with no entry keeps its item (client code
// at image RVA 0x68382D0 for the owner and 0x6839160 for a different player). Thus a fragment
// has only the slots that changed, and a slot that lost its item has an entry with no item.
// The client of the owner reads the item lists only when it has `isLocalPlayer` = 1; the
// client of an observer has no such value and reads the item looks.
//
// View     Fields
// owner    isLocalPlayer. The slots with a look (0 to 4, 24 to 29) in the visible lists, each
//          other slot in the lists that are not visible. The two maps when a weapon slot
//          changed.
// observer The slots with a look in visiblePaperdollSlots and visibleItemVisualData, and the
//          two maps. No fragment when the entity has no such item and had none.
//
// The backend has no draw and no sheathe action yet: each weapon is in its sheath, and the
// active weapon is the first weapon slot that has an item.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 3;
constexpr unsigned kOwnerGroup = 1;
constexpr unsigned kVisibleDurability = 0;
constexpr unsigned kVisibleItems = 1;
constexpr unsigned kOtherItems = 2;
constexpr unsigned kOtherSlots = 3;
constexpr unsigned kOtherDurability = 4;
constexpr unsigned kIsLocalPlayer = 6;
constexpr unsigned kCommonGroup = 2;
constexpr unsigned kSheatheMap = 0;
constexpr unsigned kActiveMap = 2;
constexpr unsigned kVisibleSlots = 3;
constexpr unsigned kVisibleLooks = 4;
/** One bit for each slot of the paperdoll, in 7 bytes. */
constexpr std::size_t kSlotMapBytes = 7;
/** Slot code of an item that is in no container: an item of the paperdoll has it. */
constexpr std::uint32_t kNoContainerSlot = 501;
/** Bytes of a state around its lists: the group mask, the field masks, and the head of each list. */
constexpr std::size_t kStateOverhead = 48;

using SlotMap = std::array<std::uint8_t, kSlotMapBytes>;

/** One slot of a fragment. */
struct Entry {
    std::uint32_t slot;
    /** The item for the owner, or the item look for an observer. */
    hub::Bytes element;
    std::uint32_t durability;
};

void set_bit(SlotMap& map, std::uint32_t slot) {
    map[slot / 8] = static_cast<std::uint8_t>(map[slot / 8] | (1U << (slot % 8)));
}

void write_slots(hub::Writer& value, const std::vector<Entry>& entries) {
    value.size(0); // No changes: the full form.
    value.u8(0);   // No sequence.
    value.size(static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries) {
        value.u32(entry.slot);
    }
}

void write_elements(hub::Writer& value, const std::vector<Entry>& entries) {
    value.size(0);
    value.u8(0);
    value.size(static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries) {
        value.raw(entry.element);
    }
}

void write_durability(hub::Writer& value, const std::vector<Entry>& entries) {
    value.size(0);
    value.u8(0);
    value.size(static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries) {
        value.u32(entry.durability);
    }
}

} // namespace

hub::Bytes encode_item_look(const world::ItemInstance* item, ItemWire wire) {
    // An item that the catalog does not have has no index: the client then has no item in the slot.
    const bool empty = item == nullptr || item->index == 0 || item->quantity == 0;
    hub::Writer out;
    out.size(empty ? 0 : item->index);
    out.u8(0); // Flags: no value of bit 0 (its meaning is not known), no dye.
    if (wire.transmog) {
        out.u32(0);   // Appearance id.
        out.zeros(4); // Skin dye settings.
    }
    return out.data();
}

std::vector<hub::Bytes> encode_paperdoll_state(const world::Entity& entity, View view, const PartsRequest& request) {
    std::vector<hub::Bytes> parts;
    const world::Equipment& equipment = entity.equipment;
    const bool owner = view == View::owner;
    // A client with no record of the entity has no item of it. A client that must get each
    // slot again has items: it also needs each slot that lost its item.
    const bool first = request.since == 0;
    const bool again = !first && request.since < equipment.reset;
    std::vector<Entry> entries;
    for (const auto& [slot, content] : equipment.slots) {
        if (!owner && !world::paperdoll::visible_slot(slot)) {
            continue;
        }
        const bool wanted = first ? content.item.has_value() : again || content.changed > request.since;
        if (!wanted) {
            continue;
        }
        const world::ItemInstance* item = content.item ? &*content.item : nullptr;
        Entry entry;
        entry.slot = slot;
        if (owner) {
            // The client does the entries with the cause "unequipped" first: an entry with no
            // item has this cause, so that an item leaves its old slot before it is in a new one.
            ItemWire wire;
            wire.emptyAction = world::sync::kItemUnequipped;
            entry.element = encode_item(item, kNoContainerSlot, first || again ? world::sync::kLoad : content.action, wire);
        } else {
            entry.element = encode_item_look(item);
        }
        entry.durability = item != nullptr && item->index != 0 ? item->durability : 0;
        entries.push_back(std::move(entry));
    }

    SlotMap sheathe{};
    SlotMap active{};
    bool weapons = false;
    for (std::uint32_t slot = world::paperdoll::kMainHand1; slot <= world::paperdoll::kOffHand3; ++slot) {
        if (equipment.item(slot) != nullptr) {
            set_bit(sheathe, slot);
            weapons = true;
        }
    }
    if (const auto weapon = equipment.active_weapon()) {
        set_bit(active, *weapon);
        // The off hand item of the active weapon is active with it.
        const std::uint32_t offHand = *weapon + (world::paperdoll::kOffHand1 - world::paperdoll::kMainHand1);
        if (equipment.item(offHand) != nullptr) {
            set_bit(active, offHand);
        }
    }
    // A new client has maps with no bit: it needs the maps only when the entity has a weapon.
    // A client that gets each slot again needs them when the entity has or had a weapon.
    const bool maps = first   ? weapons
                      : again ? weapons || equipment.weaponsChanged != 0
                              : equipment.weaponsChanged > request.since;

    // The owner gets isLocalPlayer with each fragment group: its client reads the item lists
    // only with that value. A session that has each change gets no fragment.
    if (entries.empty() && !maps && !(owner && (first || again))) {
        return parts;
    }
    const std::size_t budget = std::max<std::size_t>(request.budget, kStateOverhead + 1);
    // The parts: the entries of the slots that do not show on the body first, then the others. A group of lists stays in
    // one part when it fits the budget: in a first record each entry has the cause "load", and a later part with the same
    // group replaced the items of that group of the part before it (live check I12 of stage 1.3b: a second part with only
    // the heart rune took the jewelry and the quick slot items out of their slots).
    std::stable_partition(entries.begin(), entries.end(), [](const Entry& entry) { return !world::paperdoll::visible_slot(entry.slot); });
    const auto entry_size = [](const Entry& entry) { return entry.element.size() + 8; }; // The slot number and the durability.
    const auto otherCount = static_cast<std::size_t>(std::count_if(
        entries.begin(), entries.end(), [](const Entry& entry) { return !world::paperdoll::visible_slot(entry.slot); }));
    // A first record (or each slot again): each group of lists stays whole in one part, also when it is larger than the
    // budget (code review of stage 1.3b, finding F12: a group of 31 items in three parts lost the items of the first two
    // parts). Such a part goes in a bundle above one carrier message, which the carrier sends in chunks; the largest
    // group (each slot of the paperdoll) is far below the limit of a bundle of the client.
    const bool wholeGroups = first || again;
    std::size_t groupSize = kStateOverhead;
    for (const Entry& entry : entries) {
        groupSize += entry_size(entry);
    }
    std::size_t next = 0;
    bool head = true;
    while (head || next < entries.size()) {
        // The entries of this fragment: as many as the budget permits, and one at least. The visible group starts a new
        // part when it does not fit as a whole after the other group.
        std::vector<Entry> visible;
        std::vector<Entry> other;
        std::size_t size = kStateOverhead;
        if (wholeGroups) {
            // One part with the two groups when they fit the budget together; else one part for each group.
            const bool together = groupSize <= budget;
            while (next < entries.size()) {
                const bool isVisible = world::paperdoll::visible_slot(entries[next].slot);
                if (!together && isVisible && !other.empty()) {
                    break;
                }
                (isVisible ? visible : other).push_back(entries[next]);
                ++next;
            }
        }
        while (!wholeGroups && next < entries.size()) {
            const Entry& entry = entries[next];
            const std::size_t entrySize = entry_size(entry);
            const bool isVisible = world::paperdoll::visible_slot(entry.slot);
            if ((!visible.empty() || !other.empty()) && size + entrySize > budget) {
                break;
            }
            if (isVisible && !other.empty() && next == otherCount) {
                std::size_t visibleSize = 0;
                for (std::size_t index = otherCount; index < entries.size(); ++index) {
                    visibleSize += entry_size(entries[index]);
                }
                if (size + visibleSize > budget && kStateOverhead + visibleSize <= budget) {
                    break;
                }
            }
            size += entrySize;
            (isVisible ? visible : other).push_back(entry);
            ++next;
        }
        StateWriter state(kGroups);
        if (owner && !visible.empty()) {
            write_durability(state.field(kOwnerGroup, kVisibleDurability), visible);
            write_elements(state.field(kOwnerGroup, kVisibleItems), visible);
            write_slots(state.field(kCommonGroup, kVisibleSlots), visible);
        }
        if (owner && !other.empty()) {
            write_elements(state.field(kOwnerGroup, kOtherItems), other);
            write_slots(state.field(kOwnerGroup, kOtherSlots), other);
            write_durability(state.field(kOwnerGroup, kOtherDurability), other);
        }
        if (!owner && !visible.empty()) {
            write_slots(state.field(kCommonGroup, kVisibleSlots), visible);
            write_elements(state.field(kCommonGroup, kVisibleLooks), visible);
        }
        // Each part of the owner has isLocalPlayer: the client reads the item lists of a part only with it (live check
        // I12 of stage 1.3b: a second part with no isLocalPlayer took the items of the first part out of the slots that
        // do not show on the body).
        if (owner) {
            state.field(kOwnerGroup, kIsLocalPlayer).u8(1);
        }
        if (head && maps) {
            state.field(kCommonGroup, kSheatheMap).raw(sheathe);
            state.field(kCommonGroup, kActiveMap).raw(active);
        }
        head = false;
        parts.push_back(state.finish());
    }
    return parts;
}

} // namespace soulwarden::backend::server::rep

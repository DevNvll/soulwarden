#include <algorithm>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::ContainerComponentReplicatedState (constructor at image RVA 0x48EAB40), slot 61 of the
// player slice (component Container of the entity "Inventory": the bag). One group. The
// backend sends field 0, `Container`: a replicated vector with one element for each slot of
// the bag (client reader at image RVA 0x40E8F30, 500 elements at most).
//
//   Container := changes:size
//                changes == 0:  version count:size item*count        the full vector
//                changes  > 0:  group*                                the journal
//   group     := mask:u8 entry*        one group for each 8 entries; bit N is for entry N
//   entry     := index:size64 version [item when the mask bit is 1]
//   version   := has:u8 [sequence:size64 when has is 1]               the backend sends has = 0
//
// An entry with the mask bit 1 sets the element at `index`. When `index` is the length of the
// vector, the client adds the element at the end. The backend does not use the entry with the
// mask bit 0 (erase): it moves the elements after it. A slot with no item is the empty element.
//
//   item      := packed:u64 durability:size chrono:size gearScore:size perk:size x5 dye:4 bytes
//                instance:16 bytes flags:u8 [appearance:u32 skinDye:4 bytes]
//                                                     (client reader at image RVA 0x5CE8C40)
//   packed    := ((((quantity * 64 + action) * 502 + slot) << 20) + itemIndex) * 8001 + maxDurability
//
// The two values at the end are there when the client setting `javelin.enable-transmog` is
// true (ItemWire). The item index is the index of the item catalog. A perk is the index of the perk
// in the list of the client (gamedata/perk_data.h, stage 1.3b; 0: no perk). The dye bytes are zero:
// the backend has no dyes yet.
//
// View     Fragments
// owner    A session with no record of the entity: the full vector. When the vector is larger
//          than one fragment can be, the first fragment has the first elements, and each next
//          fragment is a journal that adds elements at the end. After that: one journal entry
//          for each slot that changed after the last record of the session.
// observer no fragment: the remote player slice has no facet for the component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kContainerField = 0;
/** Slot code of the packed number for "no slot": the empty element has it. */
constexpr std::uint64_t kNoSlot = 501;
constexpr std::uint64_t kSlotCodes = 502;
constexpr std::uint64_t kDurabilityCodes = 8001;
constexpr unsigned kItemIndexBits = 20;
constexpr unsigned kPerkCount = 5;
/** Entries of one journal group: one mask byte has 8 bits. */
constexpr std::size_t kGroupEntries = 8;
/** Bytes of a state before and around the Container value: group mask, field mask, and the counts. */
constexpr std::size_t kStateOverhead = 8;

/** @return The state with the Container field. */
hub::Bytes container_state(const hub::Writer& value) {
    StateWriter state(kGroups);
    state.field(0, kContainerField).raw(value.data());
    return state.finish();
}

/** The elements of the vector from `first`, as journals that set them: each journal is one fragment. */
void append_journals(std::vector<hub::Bytes>& parts,
                     const std::vector<std::pair<std::uint32_t, hub::Bytes>>& entries,
                     std::size_t budget) {
    std::size_t next = 0;
    while (next < entries.size()) {
        // The entries of this journal: as many as the budget permits, and one at least.
        std::size_t count = 0;
        std::size_t size = kStateOverhead;
        while (next + count < entries.size()) {
            // The index (5 bytes at most), the version byte, and a part of a mask byte.
            const std::size_t entrySize = entries[next + count].second.size() + 7;
            if (count > 0 && size + entrySize > budget) {
                break;
            }
            size += entrySize;
            ++count;
        }
        hub::Writer value;
        value.size(static_cast<std::uint32_t>(count));
        for (std::size_t first = 0; first < count; first += kGroupEntries) {
            const std::size_t group = std::min(kGroupEntries, count - first);
            // Each entry sets its element: all mask bits of the group are 1.
            value.u8(static_cast<std::uint8_t>((1U << group) - 1));
            for (std::size_t entry = 0; entry < group; ++entry) {
                const auto& [index, element] = entries[next + first + entry];
                value.size64(index);
                value.u8(0); // No sequence.
                value.raw(element);
            }
        }
        parts.push_back(container_state(value));
        next += count;
    }
}

} // namespace

hub::Bytes encode_item(const world::ItemInstance* item, std::uint32_t slot, std::uint8_t action, ItemWire wire) {
    // An item that the catalog does not have has no index: the client cannot make it.
    const bool empty = item == nullptr || item->index == 0 || item->quantity == 0;
    const std::uint64_t quantity = empty ? 0 : std::min(item->quantity, world::kMaxItemQuantity);
    const std::uint64_t cause = std::min(empty ? wire.emptyAction : action, world::sync::kLargest);
    const std::uint64_t slotCode = empty ? kNoSlot : std::min<std::uint64_t>(slot, kNoSlot);
    const std::uint64_t index = empty ? 0 : item->index & ((1U << kItemIndexBits) - 1);
    const std::uint64_t maxDurability = empty ? 0 : std::min(item->maxDurability, world::kMaxItemDurability);
    hub::Writer out;
    out.u64(((((quantity * 64 + cause) * kSlotCodes + slotCode) << kItemIndexBits) + index) * kDurabilityCodes
            + maxDurability);
    out.size(empty ? 0 : std::min(item->durability, world::kMaxItemDurability));
    out.size(empty ? 0 : item->chrono);
    out.size(empty ? 0 : item->gearScore);
    // The five perk places (stage 1.3b): the index of each perk in the list of the client, 0 for no perk.
    for (unsigned perk = 0; perk < kPerkCount; ++perk) {
        out.size(empty ? 0 : item->perkIndex[perk]);
    }
    out.zeros(4); // Dye settings.
    if (empty) {
        out.zeros(16);
    } else {
        out.raw(item->instance);
    }
    out.u8(empty ? 0 : item->flags);
    if (wire.transmog) {
        out.u32(0);   // Appearance id.
        out.zeros(4); // Skin dye settings.
    }
    return out.data();
}

std::vector<hub::Bytes> encode_container_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner) {
        return {};
    }
    return encode_bag_parts(entity.bag, request, false);
}

std::vector<hub::Bytes> encode_selected_container_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner) {
        return {};
    }
    return encode_bag_parts(entity.selectedContainer, request, true);
}

std::vector<hub::Bytes> encode_bag_parts(const world::Bag& bag, const PartsRequest& request, bool skipEmptyFirst) {
    std::vector<hub::Bytes> parts;
    if (skipEmptyFirst && request.since == 0 && bag.slots.empty()) {
        return parts;
    }
    const std::size_t budget = std::max<std::size_t>(request.budget, kStateOverhead + 1);
    std::vector<std::pair<std::uint32_t, hub::Bytes>> entries;
    if (request.since == 0 || request.since < bag.reset) {
        // The client has no vector, or it must get all of it again. The load of a bag gives no
        // line on the HUD: each element has the cause "load".
        hub::Writer elements;
        std::uint32_t count = 0;
        std::size_t index = 0;
        for (; index < bag.slots.size(); ++index) {
            const world::BagSlot& slot = bag.slots[index];
            const hub::Bytes element =
                encode_item(slot.item ? &*slot.item : nullptr, static_cast<std::uint32_t>(index), world::sync::kLoad);
            if (kStateOverhead + elements.data().size() + element.size() > budget) {
                break;
            }
            elements.raw(element);
            ++count;
        }
        hub::Writer value;
        value.size(0); // No changes: the full form.
        value.u8(0);   // No sequence.
        value.size(count);
        value.raw(elements.data());
        parts.push_back(container_state(value));
        // The elements that did not fit: each journal adds some at the end of the vector.
        for (; index < bag.slots.size(); ++index) {
            const world::BagSlot& slot = bag.slots[index];
            entries.emplace_back(static_cast<std::uint32_t>(index),
                                 encode_item(slot.item ? &*slot.item : nullptr,
                                             static_cast<std::uint32_t>(index),
                                             world::sync::kLoad));
        }
    } else {
        // The slots that changed after the last record, in the sequence of their numbers: a
        // new slot at the end of the vector is then an element that the client adds.
        for (std::size_t index = 0; index < bag.slots.size(); ++index) {
            const world::BagSlot& slot = bag.slots[index];
            if (slot.changed > request.since) {
                entries.emplace_back(static_cast<std::uint32_t>(index),
                                     encode_item(slot.item ? &*slot.item : nullptr,
                                                 static_cast<std::uint32_t>(index),
                                                 slot.action));
            }
        }
    }
    append_journals(parts, entries, budget);
    return parts;
}

} // namespace soulwarden::backend::server::rep

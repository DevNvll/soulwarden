#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "server/hub/wire.h"
#include "world/inventory.h"

// The requests of a client for the items of its bag (protocol notes 4.10) and for its
// equipment (protocol notes 4.11). Each one is a call of a component of the player entity:
// its body starts with the facet target and the entity group id. The client has no reply
// message for a request that the server did: the new container state and the new paperdoll
// state are the answer. A request for the equipment that the server refuses gets the message
// "equip item fail".
//
// The client has no request to move, to swap, to stack, or to split an item inside one
// container: its bag window sorts the items itself.

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeDropInventoryItems = 1838; // ItemDropComponentServerFacet_DropInventoryItems
inline constexpr std::uint32_t kTypeSetLockState = 2549;       // ItemRepairComponentServerFacet_RequestSetLockState
inline constexpr std::uint32_t kTypeUseItem = 2917;            // ContainerComponentServerFacet_ServerUseItem (stage 1.3b)
inline constexpr std::uint32_t kTypeEquipItem = 3895;          // PaperdollComponentServerFacet_ServerEquipItem
inline constexpr std::uint32_t kTypeUnequipItem = 3896;        // PaperdollComponentServerFacet_ServerUnequipItem
inline constexpr std::uint32_t kTypeSwapItems = 3900;          // PaperdollComponentServerFacet_ServerSwapItems
/** Server to client: the server refused a request for the equipment. */
inline constexpr std::uint32_t kTypeEquipItemFail = 3626;      // PaperdollComponentClientFacet_OnEquipItemFail

/** Container types of an item location (`eItemContainerType` of the client). */
inline constexpr std::uint32_t kItemContainerBag = 1;
/** Largest number of entries of a drop request (client writer at image RVA 0x65AF090). */
inline constexpr std::uint32_t kMaxDropEntries = 501;

/** The start of the body of each component call. */
struct ComponentCall {
    /** Facet target of the component (protocol notes 4.2). */
    std::uint64_t target{0};
    /** Entity group id of the entity of the component. */
    std::uint64_t entity{0};
};

/**
 * ItemDropComponentServerFacet_DropInventoryItems (client writer at image RVA 0x65AF090):
 *   target:u64 entity:u64 count:size, then for each entry: slot:u32 instance:16 bytes u32 quantity:u32
 * The second number of an entry is a slot value whose meaning is not known.
 */
struct DropRequest {
    ComponentCall call;
    std::vector<world::DropEntry> entries;
};

/**
 * ItemRepairComponentServerFacet_RequestSetLockState (client writer at image RVA 0x2E8E120):
 *   target:u64 entity:u64 containerType:u32 slot:u32 instance:16 bytes quantity:u32 locked:u8
 */
struct LockRequest {
    ComponentCall call;
    std::uint32_t containerType{0};
    std::uint32_t slot{0};
    world::InstanceId instance{};
    std::uint32_t quantity{0};
    bool locked{false};
};

/**
 * PaperdollComponentServerFacet_ServerEquipItem (client writer at image RVA 0x65AA180):
 *   target:u64 entity:u64 bagSlot:u32 paperdollSlot:u32 quantity:u32 instance:16 bytes flag:u8
 * The client reads the bag slot as a signed number from -1 to 501, the slot of the paperdoll
 * from 0 to 51, and the quantity from 0 to 10,000. The meaning of the flag is not known (the
 * client sent 0).
 */
struct EquipRequest {
    ComponentCall call;
    std::int32_t bagSlot{0};
    std::uint32_t paperdollSlot{0};
    std::uint32_t quantity{0};
    world::InstanceId instance{};
    bool flag{false};
};

/**
 * PaperdollComponentServerFacet_ServerUnequipItem (client writer at image RVA 0x6562810):
 *   target:u64 entity:u64 paperdollSlot:u32 bagSlot:u32 quantity:u32 flag:u8
 * The bag slot -1 is "the first free slot". The client sent the flag 1.
 */
struct UnequipRequest {
    ComponentCall call;
    std::uint32_t paperdollSlot{0};
    std::int32_t bagSlot{0};
    std::uint32_t quantity{0};
    bool flag{false};
};

/**
 * PaperdollComponentServerFacet_ServerSwapItems (client writer at image RVA 0x65875F0):
 *   target:u64 entity:u64 slot:u32 slot:u32 instance:16 bytes instance:16 bytes count:u8
 * The first slot is the slot that the user moved the item to, the second one is the slot that
 * the item came from. Each instance is the item of its slot, all zero for a slot with no item.
 * The meaning of the last byte is not known (0 to 30; the client sent 0).
 */
struct SwapRequest {
    ComponentCall call;
    std::uint32_t first{0};
    std::uint32_t second{0};
    world::InstanceId firstInstance{};
    world::InstanceId secondInstance{};
    std::uint8_t count{0};
};

/** @return The request, or no value when the body does not have the form of the request. */
[[nodiscard]] std::optional<DropRequest> read_drop_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<LockRequest> read_lock_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<EquipRequest> read_equip_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<UnequipRequest> read_unequip_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<SwapRequest> read_swap_request(std::span<const std::uint8_t> body);
/** @return The start of a component call, or no value when the body is shorter than it. */
[[nodiscard]] std::optional<ComponentCall> read_component_call(std::span<const std::uint8_t> body);

/**
 * @return The body of PaperdollComponentClientFacet_OnEquipItemFail (client reader at image
 *         RVA 0x26D52A0): target:u64 entity:u64 reason:u32. The target of a client facet is
 *         the target of the server facet of the same component: the value of the request. The
 *         handler of the client does not read the reason: it shows its slots again.
 */
[[nodiscard]] hub::Bytes equip_fail_body(const ComponentCall& call, std::uint32_t reason = 0);

} // namespace soulwarden::backend::server::rep

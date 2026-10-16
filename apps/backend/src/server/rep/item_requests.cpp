#include "server/rep/item_requests.h"

#include <algorithm>

#include "server/hub/wire.h"

namespace soulwarden::backend::server::rep {
namespace {

ComponentCall read_call(hub::Reader& reader) {
    ComponentCall call;
    call.target = reader.u64();
    call.entity = reader.u64();
    return call;
}

// Ranges of the field readers of the client (image RVA 0x2E6AB10, 0x2D93D60, 0x2D93E30, 0x692CF20).
constexpr std::int32_t kLowestBagSlot = -1;
constexpr std::int32_t kHighestBagSlot = 501;
constexpr std::uint32_t kHighestPaperdollSlot = 51;
constexpr std::uint32_t kHighestQuantity = 10000;
constexpr std::uint8_t kHighestSwapCount = 30;

bool bag_slot_in_range(std::int32_t slot) {
    return slot >= kLowestBagSlot && slot <= kHighestBagSlot;
}

world::InstanceId read_instance(hub::Reader& reader) {
    world::InstanceId instance{};
    const auto bytes = reader.raw(instance.size());
    if (bytes.size() == instance.size()) {
        std::copy(bytes.begin(), bytes.end(), instance.begin());
    }
    return instance;
}

} // namespace

std::optional<DropRequest> read_drop_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    DropRequest request;
    request.call = read_call(reader);
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > kMaxDropEntries) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count && reader.ok(); ++index) {
        world::DropEntry entry;
        entry.slot = reader.u32();
        entry.instance = read_instance(reader);
        reader.u32(); // A slot value whose meaning is not known.
        entry.quantity = reader.u32();
        request.entries.push_back(entry);
    }
    // A body with bytes after the last entry is not this request.
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return request;
}

std::optional<BagRequest> read_bag_request(std::span<const std::uint8_t> body, bool item) {
    hub::Reader reader(body);
    BagRequest request;
    request.call = read_call(reader);
    const auto actor = reader.raw(request.container.actor.size());
    if (actor.size() == request.container.actor.size()) {
        std::copy(actor.begin(), actor.end(), request.container.actor.begin());
    }
    request.container.group = reader.u64();
    request.container.target = reader.u64();
    if (item) {
        request.instance = read_instance(reader);
        request.quantity = reader.u32();
    }
    // The client sends a quantity of 1 to 10,000 (its check 0x2D93E30); a body with more or fewer bytes is not the request.
    if (!reader.ok() || reader.left() != 0 || (item && request.quantity > kHighestQuantity)) {
        return std::nullopt;
    }
    return request;
}

std::optional<PaperdollDropRequest> read_paperdoll_drop_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    PaperdollDropRequest request;
    request.call = read_call(reader);
    request.slot = reader.u32();
    request.quantity = reader.u32();
    if (!reader.ok() || reader.left() != 0 || request.slot > kHighestPaperdollSlot || request.quantity > kHighestQuantity) {
        return std::nullopt;
    }
    return request;
}

std::optional<ComponentCall> read_component_call(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    const ComponentCall call = read_call(reader);
    return reader.ok() ? std::optional(call) : std::nullopt;
}

std::optional<EquipRequest> read_equip_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    EquipRequest request;
    request.call = read_call(reader);
    request.bagSlot = static_cast<std::int32_t>(reader.u32());
    request.paperdollSlot = reader.u32();
    request.quantity = reader.u32();
    request.instance = read_instance(reader);
    const std::uint8_t flag = reader.u8();
    if (!reader.ok() || reader.left() != 0 || flag > 1 || !bag_slot_in_range(request.bagSlot)
        || request.paperdollSlot > kHighestPaperdollSlot || request.quantity > kHighestQuantity) {
        return std::nullopt;
    }
    request.flag = flag == 1;
    return request;
}

std::optional<UnequipRequest> read_unequip_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    UnequipRequest request;
    request.call = read_call(reader);
    request.paperdollSlot = reader.u32();
    request.bagSlot = static_cast<std::int32_t>(reader.u32());
    request.quantity = reader.u32();
    const std::uint8_t flag = reader.u8();
    if (!reader.ok() || reader.left() != 0 || flag > 1 || !bag_slot_in_range(request.bagSlot)
        || request.paperdollSlot > kHighestPaperdollSlot || request.quantity > kHighestQuantity) {
        return std::nullopt;
    }
    request.flag = flag == 1;
    return request;
}

std::optional<SwapRequest> read_swap_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    SwapRequest request;
    request.call = read_call(reader);
    request.first = reader.u32();
    request.second = reader.u32();
    request.firstInstance = read_instance(reader);
    request.secondInstance = read_instance(reader);
    request.count = reader.u8();
    if (!reader.ok() || reader.left() != 0 || request.first > kHighestPaperdollSlot
        || request.second > kHighestPaperdollSlot || request.count > kHighestSwapCount) {
        return std::nullopt;
    }
    return request;
}

hub::Bytes equip_fail_body(const ComponentCall& call, std::uint32_t reason) {
    hub::Writer body;
    body.u64(call.target);
    body.u64(call.entity);
    body.u32(reason);
    return body.data();
}

std::optional<LockRequest> read_lock_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    LockRequest request;
    request.call = read_call(reader);
    request.containerType = reader.u32();
    request.slot = reader.u32();
    request.instance = read_instance(reader);
    request.quantity = reader.u32();
    const std::uint8_t locked = reader.u8();
    // The quantity of an item location is 0 to 10,000 in the client. The client sends 0.
    if (!reader.ok() || reader.left() != 0 || locked > 1 || request.quantity > kHighestQuantity) {
        return std::nullopt;
    }
    request.locked = locked == 1;
    return request;
}

} // namespace soulwarden::backend::server::rep

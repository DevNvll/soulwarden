#include "server/rep/progress_requests.h"

#include "gamedata/progression_tables.h"

namespace soulwarden::backend::server::rep {
namespace {

ComponentCall read_call(hub::Reader& reader) {
    ComponentCall call;
    call.target = reader.u64();
    call.entity = reader.u64();
    return call;
}

// Limits of the field readers of the client (image RVA 0x274F8D0, 0x17876F0, 0x279ABA0,
// 0x3CFE360, 0x2DE7290).
constexpr std::uint32_t kMaxSpendEntries = 5;
constexpr std::uint32_t kHighestAttributeType = 4;
constexpr std::uint32_t kHighestAssignCount = 4096;
constexpr std::uint32_t kHighestBarSlot = 2;

} // namespace

std::optional<SpendRequest> read_spend_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    SpendRequest request;
    request.call = read_call(reader);
    // The first list has the attribute types, the second one the points of each type.
    const std::uint32_t types = reader.size();
    if (!reader.ok() || types > kMaxSpendEntries) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < types; ++index) {
        world::AttributeSpend entry;
        entry.type = reader.u32();
        request.entries.push_back(entry);
    }
    const std::uint32_t points = reader.size();
    if (!reader.ok() || points != types) {
        return std::nullopt;
    }
    for (world::AttributeSpend& entry : request.entries) {
        entry.points = reader.u32();
        if (entry.type > kHighestAttributeType || entry.points > world::kSpendLimit) {
            return std::nullopt;
        }
    }
    // A body with bytes after the last number is not this request.
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return request;
}

std::optional<AssignRequest> read_assign_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    AssignRequest request;
    request.call = read_call(reader);
    request.pool = reader.u32();
    request.node = reader.u32();
    request.count = reader.u32();
    if (!reader.ok() || reader.left() != 0 || request.count > kHighestAssignCount) {
        return std::nullopt;
    }
    return request;
}

std::optional<BarRequest> read_bar_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    BarRequest request;
    request.call = read_call(reader);
    request.tableIndex = reader.u16();
    request.slot = reader.u32();
    request.ability = reader.u32();
    if (!reader.ok() || reader.left() != 0 || request.slot > kHighestBarSlot) {
        return std::nullopt;
    }
    return request;
}

std::optional<TreeResetRequest> read_tree_reset_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    TreeResetRequest request;
    request.call = read_call(reader);
    request.tableIndex = reader.u16();
    const std::uint8_t flag = reader.u8();
    if (!reader.ok() || reader.left() != 0 || flag > 1) {
        return std::nullopt;
    }
    request.flag = flag == 1;
    return request;
}

std::optional<ComponentCall> read_plain_call(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    const ComponentCall call = read_call(reader);
    return reader.ok() && reader.left() == 0 ? std::optional(call) : std::nullopt;
}

std::optional<std::uint32_t> ability_table(std::uint32_t tableIndex) {
    return tableIndex < gamedata::kAbilityTableOrder.size() ? std::optional(gamedata::kAbilityTableOrder[tableIndex])
                                                            : std::nullopt;
}

hub::Bytes tree_reset_reply_body(const ComponentCall& call, bool success, std::uint32_t tableIndex) {
    hub::Writer body;
    body.u64(call.target);
    body.u64(call.entity);
    body.u8(success ? 1 : 0);
    body.u32(tableIndex);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

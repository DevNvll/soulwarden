#include "server/rep/instance_messages.h"

#include <algorithm>

#include "server/hub/wire.h"

namespace soulwarden::backend::server::rep {

std::vector<std::uint8_t> dungeon_result_body(std::uint64_t target, std::uint64_t entity, std::uint32_t result) {
    hub::Writer out;
    out.u64(target);
    out.u64(entity);
    out.u32(result);
    return out.data();
}

namespace {

std::uint64_t read_u64(std::span<const std::uint8_t> body, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8) | body[at + index];
    }
    return value;
}

} // namespace

std::optional<LeaveGameModeRequest> read_leave_game_mode(std::span<const std::uint8_t> body) {
    if (body.size() != 48) {
        return std::nullopt;
    }
    const auto u64 = [&body](std::size_t at) { return read_u64(body, at); };
    LeaveGameModeRequest request;
    request.target = u64(0);
    request.entity = u64(8);
    std::copy_n(body.begin() + 16, 16, request.id.begin());
    request.first = u64(32);
    request.second = u64(40);
    return request;
}

std::optional<ForfeitArenaRequest> read_forfeit_arena(std::span<const std::uint8_t> body) {
    if (body.size() != 17) {
        return std::nullopt;
    }
    ForfeitArenaRequest request;
    request.target = read_u64(body, 0);
    request.entity = read_u64(body, 8);
    request.leaveDungeon = body[16] != 0;
    return request;
}

} // namespace soulwarden::backend::server::rep

#include "server/rep/health_messages.h"

#include "server/rep/travel_messages.h"

namespace soulwarden::backend::server::rep {

hub::Bytes damage_taken_body(world::EntityId character, const world::DamageTaken& damage) {
    hub::Writer body;
    body.u64(player_vitals_target(character));
    body.u64(character);
    body.u64(damage.source);
    // The flags are two raw bytes, the low byte first.
    body.u8(static_cast<std::uint8_t>(kDamageHasPlace));
    body.u8(static_cast<std::uint8_t>(kDamageHasPlace >> 8));
    body.f32(damage.part);
    body.f32(damage.from.x);
    body.f32(damage.from.y);
    body.f32(damage.from.z);
    body.size(1);
    body.u8(damage.type);
    body.f32(damage.amount);
    body.f32(1.0F);
    return body.data();
}

hub::Bytes player_death_body(world::EntityId character, float respawnSeconds) {
    hub::Writer body;
    body.u64(character + kPlayerComponent);
    body.u64(character);
    body.f32(respawnSeconds);
    return body.data();
}

std::optional<RespawnRequest> read_respawn_request(std::span<const std::uint8_t> body) {
    const auto call = read_component_call(body);
    if (!call) {
        return std::nullopt;
    }
    hub::Reader reader(body);
    reader.raw(16);
    RespawnRequest request;
    request.call = *call;
    request.random = reader.u8() != 0;
    if (!reader.ok()) {
        return std::nullopt;
    }
    // The id text is not necessary for the answer: the backend has one respawn place for a character.
    const std::string id = reader.string();
    if (reader.ok()) {
        request.id = id;
    }
    return request;
}

} // namespace soulwarden::backend::server::rep

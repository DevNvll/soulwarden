#include "server/rep/combat_messages.h"

namespace soulwarden::backend::server::rep {

hub::Bytes damage_dealt_body(world::EntityId attacker, const world::DamageDealt& damage) {
    hub::Writer body;
    body.u64(attacker + kPlayerDamageReceiver);
    body.u64(attacker);
    body.raw(damage.targetActor.bytes);
    body.u64(damage.target);
    body.u32(0);
    body.u32(0);
    // The flags are two raw bytes, the low byte first (client reader at image RVA 0x694A7F0).
    const std::uint16_t flags = damage.killed ? kDamageCausedDeath : 0;
    body.u8(static_cast<std::uint8_t>(flags));
    body.u8(static_cast<std::uint8_t>(flags >> 8));
    // A converted part (a gem of the weapon, stage 1.3b) is a second entry with its own damage type.
    const bool second = damage.secondAmount > 0.0F;
    body.size(second ? 2 : 1);
    body.u8(damage.type);
    body.f32(damage.amount);
    body.f32(1.0F);
    if (second) {
        body.u8(damage.secondType);
        body.f32(damage.secondAmount);
        body.f32(1.0F);
    }
    return body.data();
}

} // namespace soulwarden::backend::server::rep

#pragma once

#include <cstdint>

#include "server/hub/wire.h"
#include "world/combat.h"

// The messages of the server for the combat of a character (protocol notes 4.15).
//
// The damage number. The client of an attacker shows a number at the entity that its
// character did hurt when it gets the message "on damage dealt" for the damage receiver
// component of its own character:
//
//   DamageReceiverComponentClientFacet_OnDamageDealt (client reader at image RVA 0x65B4B50,
//   handler 0x686CC50):
//     target:u64 entity:u64                       the damage receiver facet of the attacker
//     actor id:16 bytes  G:u64                    the entity that took the damage
//     u32 u32                                     two keys (feedback, sound): 0
//     flags:2 bytes                               low byte first: bit 0 "ranged", bit 3 "the
//                                                 damage was the death", bit 5 "can crit", bit 6
//                                                 "critical hit", bit 8 "headshot" (stage 3.3b-S5:
//                                                 the client shows the special number for bit 6, or
//                                                 for bits 0, 5, and 8 together: live check L6)
//     count:size, then for each entry:
//       damage type:u8 amount:f32 part:f32        `part` is the part of the damage before the
//                                                 defense that the target took: 1
//
// The message has the damage that the world computed. The client computes none.

namespace soulwarden::backend::server::rep {

/** Type index from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeDamageDealt = 2071; // DamageReceiverComponentClientFacet_OnDamageDealt

/**
 * Id of the component DamageReceiver of the entity RootPlayer in slices/player.dynamicslice of
 * the supported client build. The facet target of the component for a character is this id
 * plus the entity group id of the character: the component is in the root entity of the slice
 * (protocol notes 4.2).
 */
inline constexpr std::uint64_t kPlayerDamageReceiver = 0xCCC47F92DAF6BFC1ULL;

/** Flag of the message: the damage was the death of the target (bit 3, "DamageCausedDeath"). */
inline constexpr std::uint16_t kDamageCausedDeath = 1U << 3;

/**
 * @return The body of the message "on damage dealt" for the client of an attacker.
 * @param attacker Entity group id of the character of that client.
 */
[[nodiscard]] hub::Bytes damage_dealt_body(world::EntityId attacker, const world::DamageDealt& damage);

} // namespace soulwarden::backend::server::rep

#pragma once

#include <cstdint>

#include "server/hub/wire.h"
#include "world/entity.h"
#include "world/world_clock.h"

// The mana of the prediction of the client (open point of stage 3.5b.2, solved in stage 2.2; protocol notes 4.5; the static analysis
// of the evidence folder mana/MANA-ANALYSIS.md).
//
// The Mana component of the client has two copies of its four values: the predicted copy A (component +0xB0) and the copy of the
// server B (+0xC8). The mana state (slot 47, type 1652, apply 0x683B740) writes only B, and the meter one time. The regeneration of
// the client (0x676B8E0, in each movement step through 0x676B310) changes only A, and the meter follows A only when A changes. The
// client copies B into A only at a movement state of the server whose id is the number of an input of the client (0x486D660), which
// the id rule of the backend never sends. Thus with the mana state alone the meter does not move during the regeneration.
//
//   ManaComponentClientFacet_ClientForceSyncServerState (type 4289, reader 0x656DF80, handler 0x656DE10 to 0x67A6940):
//     target:u64 entity:u64                 the Mana facet: entity "Stats" of the player slice, component A4CF5C98 (slice-ids.json)
//     current:f32 maximum:f32 delay:f32 rate:f32   (big-endian)
//
// The handler copies the four values into A. The client then gains GetStatRegenRate(2) x stat 0x30 x rate in each second after the
// delay (seconds). Live check of stage 2.2: with rate 1.0 the meter filled at 2.0 a second (the base rate of the data), with rate
// 1.2 at 2.37 a second and hid at the full value after 25 s (the rate of the server with Focus 100: 2 x 1.2). Thus `rate` is the
// factor of the server (1 + the ManaRate parts, Vitals::manaRateFactor), and `maximum` is the base maximum (the client multiplies
// it with its MaxMana stat 0x31). Message 4291 (ClientSyncServerState) does nothing in this build.

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeManaForceSync = 4289;
/** The name of the Mana component of the player slice in the table of the slice ids (entity "Stats", type MB::ManaComponent). */
inline constexpr const char* kManaComponentName = "Stats/a4cf5c98-487e-4db9-ac48-9698f965688d";

/** @return The body of a 4289 for the own entity: the facet target, the entity group id, and the four mana values of the server. */
[[nodiscard]] hub::Bytes encode_mana_sync(std::uint64_t target, const world::Entity& entity, const world::WorldClock& clock);

} // namespace soulwarden::backend::server::rep

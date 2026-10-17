#pragma once

#include <cstdint>

#include "server/hub/wire.h"
#include "world/world_time.h"

// The messages of the world time (stage 2.2, protocol notes 4.43; research world-time.md 2.1 to 2.4, world-time-stage.md 7).
// All numbers of a body are big-endian. None of them is a component state: the Time component of the player slice has no slot.
//
// The request of the client (its local Time facet is ready; client code 0x4087CE0, 0x408C2B0, sender 0x407A3C0):
//
//   TimeComponentServerFacet_SyncToClient (type 4248):
//     target:u64 entity:u64                 the Time facet of the own player: G + D41EB32C0F154004, and G
//
// The client sends it one time for each player entity of the client: after the mission load of the world level at each world entry
// (live check L0 of stage 2.2). A travel and a respawn keep the entity and its record: no new request (live check L2).
//
// The reply (client reader 0x406F740, record reader 0x69F2530, handler 0x408C520; live check L1 of stage 2.2):
//
//   TimeComponentClientFacet_SyncTimeAndPhase (type 7036):
//     target:u64 entity:u64 record:60 phase:u32
//     record := a:f32 b:f32 offset:f32 dawnTime:u32 dawnDuration:u32 duskTime:u32 duskDuration:u32 dayLength:u32 nightLength:u32
//               anchor:u64 dawnStart:f32 dayStart:f32 duskStart:f32 nightStart:f32
//
// The client keeps the record (component +0x98; game +0x730 through 0x644ECC0), sets the hour of the engine at once, and calculates
// the hour again in each frame from the anchor and its copy of the server clock. It stores the phase (component +0xE0).
//
//   TimeComponentClientFacet_SyncDayPhase (type 4247):   target:u64 entity:u64 phase:u32   (handler 0x408C490: the stored phase)
//
// The clock correction (client dispatcher 0x6B6F190 to 0x6AFEDB0; the server sender of the image, 0x6A3D9B0, sends it each 3,000 ms:
// research world-time-stage.md 6 item 6; live check L8 of stage 2.2):
//
//   REPClient::TimeSynchMsg (type 335):   serverTime:u64   (nanoseconds of the server clock: WorldClock::server_time_ns)
//
// A difference of 1 s or less moves the clock of the client in steps of 2 ms; a larger one sets it at once.

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeClockSync = 335;
inline constexpr std::uint32_t kTypeDayPhase = 4247;
inline constexpr std::uint32_t kTypeTimeRequest = 4248;
inline constexpr std::uint32_t kTypeTimeAndPhase = 7036;

/** The id of the Time component in the root entity of the player slice (the target of its facet is G plus this id). */
inline constexpr std::uint64_t kTimeComponent = 0xD41EB32C0F154004ULL;
/** The body of a request 4248: the target and the entity. */
inline constexpr std::size_t kTimeRequestSize = 16;

/** @return The facet target of the Time component of an own player entity (G plus the component id, modulo 2^64). */
[[nodiscard]] constexpr std::uint64_t time_target(std::uint64_t group) {
    return group + kTimeComponent;
}

/** @return The body of a 7036 for the own entity G: the record and the phase. */
[[nodiscard]] hub::Bytes encode_time_and_phase(std::uint64_t group, const world::TimeRecord& record, world::DayPhase phase);
/** @return The body of a 4247 for the own entity G. */
[[nodiscard]] hub::Bytes encode_day_phase(std::uint64_t group, world::DayPhase phase);
/** @return The body of a 335: the server clock in nanoseconds. */
[[nodiscard]] hub::Bytes encode_clock_sync(std::uint64_t serverNs);

} // namespace soulwarden::backend::server::rep

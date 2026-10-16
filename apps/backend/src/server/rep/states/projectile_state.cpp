#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "server/rep/replication.h"
#include "server/rep/states/half_float.h"
#include "server/rep/states/state_codecs.h"

// The Projectile state of a projectile of a creature (stage 3.4f-A, protocol notes 4.37; research creature-ranged.md Q2 and R0,
// live checks L1 and L2 of the stage).
//
// MB::ProjectileReplicatedState (type 16, constructor 0x6715370, apply 0x683D750 -> 0x68D5B60, then 0x68D66F0 at the first application
// and 0x68D6CF0 at a later one), at the slot of the Projectile component of the projectile slice (game data: 1 for basic_ai_arrow, 2
// for arcane_ai_arrow). One group, twenty fields; the field masks are chained (each mask byte comes before the values of its 7 fields).
// Fixed numbers are big-endian; the fields 11 and 13 are the size code of hub::Writer::size (reader 0x87B5C0: each tail little-endian,
// also the five-byte form).
//
//   0  positionAnchor                    x:f32 y:f32 z:u16 over -100 to 2000 (reader 0x5CEA190, range 0x87ADF0)
//   2  spawnVelocity                     three half floats (m/s)
//   4  timeAnchorMicroseconds            u64: the server time of the anchor in microseconds (the time of the world clock)
//   5  timeOffsetStartAccelMilliseconds  u16: 0, the acceleration from the anchor (the delay distance of the attack is a rule of the
//                                        flight of the client)
//   7  collidedWithGDEId                 u64: the entity that the projectile hit; the backend sends 00000000FFFFFFFF (none)
//   8  collidedWithHitVolumeIndex        u8: 0
//   9  flags                             u8: bit 0 collided (0 in the flight; 1: the client holds the projectile at the anchor)
//   10 owner                             u64: the entity id of the creature that shot it
//   11 rangedAttackIndex                 size32: the place of the attack in the attacks of all profiles of the ranged attack data
//   13 ammoIdCRC                         size32: the CRC-32 of the lower-case AmmoID (0: none)
//   14 damageTableId                     u32: the CRC-32 of the lower-case unique name of the damage table
//   15 damageTableRowIndex               u16: the place of the row among the rows of the table with a DamageID
// The other fields (1, 3, 6, 12, 16 to 19) are not sent: their constructor values (0, no override of the gravity, no bounce, no weapon
// effect, no pierce, no magnet) are what the shot needs (live check L2e: the reduced state of 51 bytes flies as the full one).
//
// The forms of the live checks:
//   launch     the fields 0, 2, 4, 5, 9, 10, 11, 13, 14, 15 (51 bytes for the archer; L2e: one arrow, its flight from the anchor);
//   collision  the fields 0, 4, 7, 8, 9 with the contact place, its time, and the flags 1 (L2c: the arrow stops at the contact).
// A first record has the launch state; a session that had the launch gets the collision state at the end. The stop of the record ends
// the presentation (L2g). A projectile has only the observer view: each session gets the same bytes.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kPosition = 0;
constexpr unsigned kVelocity = 2;
constexpr unsigned kTimeAnchor = 4;
constexpr unsigned kAccelOffset = 5;
constexpr unsigned kCollidedWith = 7;
constexpr unsigned kHitVolume = 8;
constexpr unsigned kFlags = 9;
constexpr unsigned kOwner = 10;
constexpr unsigned kAttackIndex = 11;
constexpr unsigned kAmmo = 13;
constexpr unsigned kTable = 14;
constexpr unsigned kRow = 15;
/** The flags bit of a collided projectile (0x6781820 holds the anchor; research R0). */
constexpr std::uint8_t kCollided = 0x01;
/** The id of no entity in field 7 (the constructor value of an id of a game entity). */
constexpr std::uint64_t kNoEntity = 0x00000000FFFFFFFFULL;
/** The height range of the position anchor (reader 0x5CEA190), as for the Spell state. */
constexpr float kHeightMin = -100.0F;
constexpr float kHeightMax = 2000.0F;

void write_position(hub::Writer& writer, const state::WorldPosition& position) {
    writer.u32(std::bit_cast<std::uint32_t>(position.x));
    writer.u32(std::bit_cast<std::uint32_t>(position.y));
    const float clamped = std::clamp(position.z, kHeightMin, kHeightMax);
    writer.u16(static_cast<std::uint16_t>(std::lround((clamped - kHeightMin) / (kHeightMax - kHeightMin) * 65535.0F)));
}

std::uint64_t microseconds(world::WorldClock::time_point time) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count());
}

hub::Bytes launch_state(const world::ProjectileBody& shot) {
    StateWriter state(kGroups);
    write_position(state.field(0, kPosition), shot.origin);
    hub::Writer& velocity = state.field(0, kVelocity);
    for (const float part : shot.velocity) {
        velocity.u16(half_of(part));
    }
    state.field(0, kTimeAnchor).u64(microseconds(shot.launched));
    state.field(0, kAccelOffset).u16(0);
    state.field(0, kFlags).u8(0);
    state.field(0, kOwner).u64(shot.source);
    state.field(0, kAttackIndex).size(shot.attackIndex);
    state.field(0, kAmmo).size(shot.ammoKey);
    state.field(0, kTable).u32(shot.tableKey);
    state.field(0, kRow).u16(shot.rowIndex);
    return state.finish();
}

hub::Bytes collision_state(const world::ProjectileBody& shot) {
    StateWriter state(kGroups);
    write_position(state.field(0, kPosition), *shot.contact);
    state.field(0, kTimeAnchor).u64(microseconds(shot.contactAt));
    state.field(0, kCollidedWith).u64(kNoEntity);
    state.field(0, kHitVolume).u8(0);
    state.field(0, kFlags).u8(kCollided);
    return state.finish();
}

} // namespace

std::vector<hub::Bytes> encode_projectile_parts(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    std::vector<hub::Bytes> parts;
    if (entity.kind != world::EntityKind::projectile || entity.shot.slice == nullptr) {
        return parts;
    }
    // A session with no record of the projectile gets the launch first (also when the flight ended in the same tick), and a session
    // that has the launch gets only the collision.
    if (request.since == 0 || !entity.shot.contact) {
        parts.push_back(launch_state(entity.shot));
    }
    if (entity.shot.contact) {
        parts.push_back(collision_state(entity.shot));
    }
    return parts;
}

std::uint32_t projectile_slot(const world::Entity& entity) {
    return entity.kind == world::EntityKind::projectile && entity.shot.slice != nullptr ? entity.shot.slice->slot : 0;
}

} // namespace soulwarden::backend::server::rep

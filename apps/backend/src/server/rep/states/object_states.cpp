#include <algorithm>
#include <bit>
#include <cmath>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The states of a server child (stage 2.5b, world/interactions.h, protocol notes 4.36): a station of a settlement, a camp, a fixture.
// Live check L4 of stage 2.5b: the metadata state and the PositionInTheWorld state are the smallest record that shows the forge of a
// station (its template child), the prompt of the station, and its crafting screen.
//
// MB::PositionInTheWorldReplicatedState (type 13; constructor 0x5BB27D0), the slot of the PositionInTheWorld component of the slice
// (game data: 2 for a station, 1 for its forge, 6 for the powder keg). One group:
//   0 position  x:f32 y:f32 height:u16 over -100 to 2000 (reader 0x5CEA190, 0x87ADF0)
//   1 rotation  header:u8, then u16 for each of x, y, z that has no header bit (reader 0x5CEA140, 0x87B250): the bits 0x01, 0x02,
//               0x04 give 0, the bits 0x08, 0x10, 0x20 give 1, the bit 0x40 makes w negative; a u16 value v is v / 32767.5 - 1, and
//               w is the square root of the rest. `07` is (0, 0, 0, 1). The writer of the client truncates (c + 1) * 32767.5.
//   2 scale     kind:u8 (0: 1, 1, 1; reader 0x87B0B0): the backend sends no value (the default of the client is 1)
// The spell entities with no Spell component (the trap of the musket: states/spell_state.cpp) use the same encoder.
//
// MB::BuildableControllerReplicatedState (type 1594; constructor 0x47B7D20), group 0 field 0 currentState:u8 (reader 0x279E210):
// m_initialState of the slice (game data: 5, Complete, for a station and the keg). Live check L4: the station shows with no such state
// too; the backend sends the authored value. The other fields (the resources, the upgrade time, the station CRC of field 6) have no
// value: the backend sends none.
//
// MB::OwnershipComponentReplicatedState (type 3217; constructor 0x360B3C0), group 0 field 4 currentPermissionRule:u8 (reader 0x279E210):
// m_initialPermissionRule of the slice (game data: 1 for a camp, 4 for the keg), or the rule of a development command. Live checks L5 and
// L6: with no such state the camp shows no option; with the rule 1 the camp shows "Set Camp Recall Point" and the keg "Ignite Fuse";
// with the rule 4 the keg shows "No public access", and with 0 the camp shows no option.
//
// Each has only the observer view (an object has no owner).

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kPlacePosition = 0;
constexpr unsigned kPlaceRotation = 1;
constexpr float kHeightMin = -100.0F;
constexpr float kHeightMax = 2000.0F;
constexpr unsigned kCurrentState = 0;
constexpr unsigned kPermissionRule = 4;

} // namespace

void write_place_rotation(hub::Writer& writer, std::array<float, 4> q) {
    // A quaternion and its negative are one rotation: the backend sends the form with w not negative (no bit 0x40).
    if (q[3] < 0.0F) {
        for (float& value : q) {
            value = -value;
        }
    }
    std::uint8_t header = 0;
    std::array<std::uint16_t, 3> values{};
    std::array<bool, 3> present{};
    constexpr std::uint8_t kZero[] = {0x01, 0x02, 0x04};
    constexpr std::uint8_t kOne[] = {0x08, 0x10, 0x20};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float value = std::clamp(q[axis], -1.0F, 1.0F);
        if (std::abs(value) < 1.0e-6F) {
            header |= kZero[axis];
        } else if (value >= 1.0F) {
            header |= kOne[axis];
        } else {
            values[axis] = static_cast<std::uint16_t>(std::min((value + 1.0F) * 32767.5F, 65535.0F));
            present[axis] = true;
        }
    }
    writer.u8(header);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (present[axis]) {
            writer.u16(values[axis]);
        }
    }
}

std::array<float, 4> yaw_rotation(float yawDegrees) {
    const float half = yawDegrees * 3.14159265358979F / 360.0F;
    return {0.0F, 0.0F, std::sin(half), std::cos(half)};
}

hub::Bytes position_in_the_world_state(const state::WorldPosition& position, const std::array<float, 4>& rotation) {
    StateWriter state(kGroups);
    hub::Writer& place = state.field(0, kPlacePosition);
    place.u32(std::bit_cast<std::uint32_t>(position.x));
    place.u32(std::bit_cast<std::uint32_t>(position.y));
    const float clamped = std::clamp(position.z, kHeightMin, kHeightMax);
    place.u16(static_cast<std::uint16_t>(std::lround((clamped - kHeightMin) / (kHeightMax - kHeightMin) * 65535.0F)));
    write_place_rotation(state.field(0, kPlaceRotation), rotation);
    return state.finish();
}

hub::Bytes encode_object_place_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::object || entity.object.positionSlot == 0) {
        return {};
    }
    return position_in_the_world_state(entity.position, entity.object.rotation);
}

hub::Bytes encode_target_place_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::creature || entity.creature.body == nullptr || !entity.creature.body->stationary()) {
        return {};
    }
    // A stationary target (stage QG5c1): the place of its spawn point and the rotation of its yaw (the tilt of a placement is not in the
    // creature data).
    return position_in_the_world_state(entity.position, yaw_rotation(entity.creature.yaw));
}

hub::Bytes encode_buildable_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::object || entity.object.buildableSlot == 0) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, kCurrentState).u8(entity.object.buildableState);
    return state.finish();
}

hub::Bytes encode_ownership_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::object || entity.object.ownershipSlot == 0) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, kPermissionRule).u8(entity.object.permissionRule);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

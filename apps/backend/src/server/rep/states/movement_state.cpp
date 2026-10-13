#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::ALCReplicatedState, the movement state of a character (tools/st ALCReplicated), slot 16
// of the player slice. It has its own reader with a wide field mask (client reader at image
// RVA 0x2A436B0).
//
// View     Fields (all in group 0)
// owner    worldPosAbs, rotation, lookDir, teleportAndMigrationId, timeAnchor, dataBits,
//          moreDataBits. After this full state the owner gets a state with a new timeAnchor
//          and the id of the state (idRel, idAbs) at a constant interval: the client of the
//          owner moves its character itself.
// observer worldPosAbs, rotation, lookDir, teleportAndMigrationId, timeAnchor, and the action
//          of layer 0: slayerStateId, slayerStateIdStarted, slayerSequenceId, and
//          slayerScriptLayers. The client of the owner makes the action of its character
//          itself. The client of an observer plays what the server sends, and it shows no
//          body and no name tag for a player with no action sequence (live runs: the body
//          shows with slayerSequenceId and slayerScriptLayers, and not with one of the two).
//          The two bit fields are the movement permissions for the input of the owner: an
//          observer has no input for the character, and the client sets the two values to 0
//          when the server does not send them (constructor at image RVA 0x2A35DB0).
//
// creature worldPosAbs (the exact position: the client does not move a creature to the
//          ground), rotation (the direction of the creature), lookDir, teleportAndMigrationId,
//          timeAnchor, dataBits, and moreDataBits (both 0). No later time anchor. A creature
//          with no action of the server plays the idle action that its client selects. For a
//          creature with an action of the server (the death action), the state also has the
//          scope fields: scopeData, scopeInfoBlob, scopeTimeBlobEx, globalFragTags, and the
//          four bytes of the time of scope 0 (protocol notes 4.14).
//
// The id of a state (idAbs and idRel) is, for the original server, the number of the last
// input of the owner that the server applied: the client looks for its predicted state with
// that number and corrects itself to the position of the server state. The backend has no
// movement simulation, thus its state must never have the number of a predicted state of the
// client. The heartbeat state has an id that is far from each input number that the client
// can have in its history (movement_state_id). An observer gets no id.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 2;
// The id of the state: idRel has its low 8 bits and idAbs its high 8 bits (client readers at
// image RVA 0x2A42D40 and 0x2A42DA0; the client makes the id at 0x2A2ACD0).
constexpr unsigned kIdRel = 0;
constexpr unsigned kIdAbs = 25;
constexpr unsigned kRotation = 11;
constexpr unsigned kLookDirection = 12;
// The action of script layer 0 (the layers 1 to 3 have the fields 16 to 24). A state id and a
// sequence id go out as the value plus 1 (client reader at image RVA 0x2A42E60, copy at
// 0x2A2ACD0): 0 on the wire is "no state".
constexpr unsigned kActionStateId = 13;
constexpr unsigned kActionStateStarted = 14;
constexpr unsigned kActionSequenceId = 15;
constexpr unsigned kActionLayers = 47;
constexpr unsigned kWorldPosition = 27;
constexpr unsigned kTeleportId = 31;
constexpr unsigned kTimeAnchor = 40;
constexpr unsigned kDataBits = 41;
constexpr unsigned kMoreDataBits = 42;
// The action of a creature. A creature has no script layers: its action list has scopes
// (client copy at image RVA 0x2A2B23D, reader of a scope record at 0x2A75F70). Scope 0 is the
// scope "FullBody" of each creature controller.
//   scopeData        mask of the scopes that have a record (size code)
//   scopeInfoBlob    byte count (size code), then one record for each scope of the mask:
//                    start count:u16, fragment index + 1 (size code), action index + 1 (size
//                    code), playback speed (16-bit float), option:u8
//   scopeTimeBlobEx  byte count (size code) and the times of the scopes 2 and more. The client
//                    reads the scope fields only when the state has this field too.
//   globalFragTags   12 bytes: the tag state of the fragments
//   scope 0 time     4 fields of one byte, the low byte first: the time of the action in ticks
//                    of 1/30 s
constexpr unsigned kScopeTimeLow = 28;
constexpr unsigned kScopeTimeBlob = 29;
constexpr unsigned kScopeData = 32;
constexpr unsigned kScopeInfoBlob = 33;
constexpr unsigned kGlobalFragTags = 34;
constexpr unsigned kScopeTimeByte1 = 38;
constexpr unsigned kScopeTimeByte2 = 49;
constexpr unsigned kScopeTimeByte3 = 50;
constexpr std::uint32_t kScopeFullBody = 0x01;
/** The playback speed 1.0 as a 16-bit float. */
constexpr std::uint16_t kPlaybackNormal = 0x3C00;
constexpr std::size_t kFragTagBytes = 12;
/** Ticks of the action time for one second. */
constexpr std::uint64_t kActionTicksPerSecond = 30;
// The rotation and the look direction are compressed unit quaternions (client reader at image
// RVA 0x87A8D0): one header byte, then one byte for each component that is not the largest one
// and not zero. Header: bit 0 is the sign of the largest component, bits 1 to 4 mark the
// components x, y, z, w that are zero, bits 5 and 6 are the index of the largest component.
// The two values below need no component bytes. The client does not initialize these two
// fields: without them the character has a random rotation, and the camera of the client can
// get a direction that is not a number (endless loop at image RVA 0x104E790).
constexpr std::uint8_t kRotationIdentity = 0x6E;   // (0, 0, 0, 1): the character looks north.
constexpr std::uint8_t kLookDirectionNorth = 0x3A; // (0, 1, 0, 0)
// Movement permissions of the server in the two bit fields of the movement state. The client
// copies them into each input (client code at image RVA 0x2A2D200), and the actions of the
// character test them: without "can navigate" the character does not leave its idle action
// (condition "IfCanNav", client code at image RVA 0x2BFB780).
constexpr std::uint8_t kDataBitsCanSprint = 0x40;
constexpr std::uint8_t kDataBitsCanRun = 0x80;
constexpr std::uint8_t kMoreDataBitsCanNavigate = 0x01;
constexpr std::uint8_t kMoreDataBitsCanDodge = 0x02;
/** Height range of an absolute world position on the wire (client reader at image RVA 0x2A433D0). */
constexpr float kWorldHeightMin = -100.0F;
constexpr float kWorldHeightMax = 1000.0F;

/** Absolute world position: x and y as 32-bit floats, the height as 16 bits of its range. */
void write_world_position(hub::Writer& writer, const state::WorldPosition& position) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &position.x, sizeof(bits));
    writer.u32(bits);
    std::memcpy(&bits, &position.y, sizeof(bits));
    writer.u32(bits);
    const float clamped = std::min(std::max(position.z, kWorldHeightMin), kWorldHeightMax);
    const auto height = static_cast<std::uint16_t>(
        std::lround((clamped - kWorldHeightMin) / (kWorldHeightMax - kWorldHeightMin) * 65535.0F));
    writer.u8(static_cast<std::uint8_t>(height >> 8));
    writer.u8(static_cast<std::uint8_t>(height));
}

/**
 * Writes the rotation of an entity that turns only about the vertical axis, as a compressed
 * unit quaternion (0, 0, sin(yaw / 2), cos(yaw / 2)). The largest component is not negative:
 * a quaternion and its negative are the same rotation.
 * @param yawDegrees The angle about the vertical axis. 0: the entity looks north.
 */
void write_yaw_rotation(hub::Writer& writer, float yawDegrees) {
    constexpr float kStep = 0.0055459356F;
    constexpr float kLowest = 0.70710677F;
    constexpr std::uint8_t kZeroX = 0x02;
    constexpr std::uint8_t kZeroY = 0x04;
    constexpr std::uint8_t kZeroZ = 0x08;
    constexpr std::uint8_t kZeroW = 0x10;
    constexpr std::uint8_t kLargestZ = 2U << 5;
    constexpr std::uint8_t kLargestW = 3U << 5;
    const float half = yawDegrees * 3.14159265358979F / 360.0F;
    float z = std::sin(half);
    float w = std::cos(half);
    const bool largestW = std::abs(w) >= std::abs(z);
    if ((largestW ? w : z) < 0.0F) {
        z = -z;
        w = -w;
    }
    const float other = largestW ? z : w;
    const long byte = std::lround((other + kLowest) / kStep);
    const bool zero = std::abs(other) < kStep / 2.0F;
    std::uint8_t header = kZeroX | kZeroY | (largestW ? kLargestW : kLargestZ);
    if (zero) {
        header |= largestW ? kZeroZ : kZeroW;
    }
    writer.u8(header);
    if (!zero) {
        writer.u8(static_cast<std::uint8_t>(std::min(std::max(byte, 0L), 255L)));
    }
}

/** The client ignores a movement state that has no time anchor (client code at image RVA 0x485A550). */
void write_time_anchor(StateWriter& state, const world::WorldClock& clock) {
    state.field(0, kTimeAnchor).u64(clock.server_time_ns() / 1000);
}

} // namespace

hub::Bytes encode_movement_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    // Without a world position the player stays at the map origin and the world streaming
    // of the client does not start.
    StateWriter state(kGroups, FieldMask::wide);
    write_world_position(state.field(0, kWorldPosition), world::spawn_position(entity));
    state.field(0, kRotation).u8(kRotationIdentity);
    state.field(0, kLookDirection).u8(kLookDirectionNorth);
    // The low 4 bits of the teleport field are the teleport number of the entity. The high 4
    // bits are the migration number of the client, which stays 0. The client of the owner
    // takes the position of this state only when the number is not the number of its own
    // state (client code at image RVA 0x486D89B): the first state of a session (the number of
    // a new client is 0), and the state after a travel of the character (World::travel).
    state.field(0, kTeleportId).u8(static_cast<std::uint8_t>(entity.teleport & 0x0F));
    if (view == View::owner) {
        const world::MovementPermissions& permissions = entity.movement;
        state.field(0, kDataBits)
            .u8(static_cast<std::uint8_t>((permissions.sprint ? kDataBitsCanSprint : 0)
                                          | (permissions.run ? kDataBitsCanRun : 0)));
        state.field(0, kMoreDataBits)
            .u8(static_cast<std::uint8_t>((permissions.navigate ? kMoreDataBitsCanNavigate : 0)
                                          | (permissions.dodge ? kMoreDataBitsCanDodge : 0)));
    } else {
        const world::ActionState& action = entity.action;
        state.field(0, kActionStateId).size(static_cast<std::uint32_t>(action.state + 1));
        state.field(0, kActionStateStarted).u16(action.started);
        state.field(0, kActionSequenceId).size(static_cast<std::uint32_t>(action.sequence + 1));
        state.field(0, kActionLayers).u8(action.layers);
    }
    write_time_anchor(state, clock);
    return state.finish();
}

hub::Bytes encode_creature_movement_state(const world::Entity& entity, View, const world::WorldClock& clock) {
    StateWriter state(kGroups, FieldMask::wide);
    // The position of a creature is exact: the client does not move a creature to the ground.
    write_world_position(state.field(0, kWorldPosition), entity.position);
    write_yaw_rotation(state.field(0, kRotation), entity.creature.yaw);
    state.field(0, kLookDirection).u8(kLookDirectionNorth);
    state.field(0, kTeleportId).u8(1);
    state.field(0, kDataBits).u8(0);
    state.field(0, kMoreDataBits).u8(0);
    if (entity.creature.action) {
        // The action that the server started, in scope 0. The time of the action is the time
        // from its start to this state: a client that gets the state later shows the pose of
        // that time, not the start of the action.
        const world::CreatureAction& action = *entity.creature.action;
        hub::Writer record;
        record.u16(action.generation);
        record.size(action.fragment + 1);
        record.size(action.action + 1);
        record.u16(kPlaybackNormal);
        record.u8(0); // The first option of the fragment.
        state.field(0, kScopeData).size(kScopeFullBody);
        hub::Writer& blob = state.field(0, kScopeInfoBlob);
        blob.size(static_cast<std::uint32_t>(record.data().size()));
        blob.raw(record.data());
        state.field(0, kScopeTimeBlob).size(0);
        state.field(0, kGlobalFragTags).zeros(kFragTagBytes);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - action.started).count();
        const std::uint64_t ticks =
            std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max<long long>(elapsed, 0)) * kActionTicksPerSecond / 1000,
                                    0xFFFFFFFFULL);
        state.field(0, kScopeTimeLow).u8(static_cast<std::uint8_t>(ticks));
        state.field(0, kScopeTimeByte1).u8(static_cast<std::uint8_t>(ticks >> 8));
        state.field(0, kScopeTimeByte2).u8(static_cast<std::uint8_t>(ticks >> 16));
        state.field(0, kScopeTimeByte3).u8(static_cast<std::uint8_t>(ticks >> 24));
    }
    write_time_anchor(state, clock);
    return state.finish();
}

std::uint16_t movement_state_id(std::uint16_t newestInput) {
    // The input number of the client has 16 bits and adds one for each simulation step: it is
    // 0 again after 36.4 minutes. The client keeps its last 60 inputs and predicted states,
    // and it finds the predicted state of a server state with an exact compare of the id
    // (client code at image RVA 0x485A460). A state with no id has the id 0: after the wrap
    // of the input number the client finds a predicted state for it and pulls its character
    // to the position of the server state, which is the spawn position of the session.
    // Thus the id is one of two values, the one that is 0x4000 or more from the newest input
    // number: 0x8000 for a number in 0xC000 to 0xFFFF or in 0x0000 to 0x3FFF, and 0x0000 for
    // a number in 0x4000 to 0xBFFF.
    return static_cast<std::uint16_t>(((newestInput + 0x4000U) & 0x8000U) ^ 0x8000U);
}

hub::Bytes encode_movement_heartbeat(const world::WorldClock& clock, std::uint16_t newestInput) {
    // The client stops its own input when the last movement state from the server is older
    // than javelin.alc-server-timeout-for-input (2000 ms; client tick at image RVA 0x486C740).
    // A movement state with a new time anchor keeps the input on. Its id is not the number of
    // a predicted state of the client, thus the client does not correct its position (client
    // code at image RVA 0x486D660).
    const std::uint16_t id = movement_state_id(newestInput);
    StateWriter state(kGroups, FieldMask::wide);
    state.field(0, kIdRel).u8(static_cast<std::uint8_t>(id));
    state.field(0, kIdAbs).u8(static_cast<std::uint8_t>(id >> 8));
    write_time_anchor(state, clock);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

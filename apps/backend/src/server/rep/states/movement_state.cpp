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
//          timeAnchor, dataBits (the two target bits below), moreDataBits (0), globalFragTags (the tags of the weapons
//          of the creature, which select the variant of a fragment), and the scope fields
//          scopeData, scopeInfoBlob, and scopeTimeBlobEx (protocol notes 4.14 and 4.17). No
//          later time anchor. For an action of the server (the death action, an attack, a
//          reaction) the scope fields have its record, and the four bytes of the time of scope
//          0 the time of the action (a timed action in each state while it plays, user test 2).
//          After the end of an action, a walk, or a stun the state has the idle record of the
//          slice (user test 2). Each other state has the scope fields with no record: the
//          client keeps the fields of a state that a later state does not have, and it starts
//          the action of a kept record again each time that it applies a state after the end
//          of that action (live proof, user test 1: a Reaver that walks plays its spawn action
//          again and again, a wolf that runs after a bite plays the bite). With no record the
//          client plays the action that it selects: the idle action, and the walk for a
//          sequence of states with a new position each 200 ms. The first state of a session
//          for a creature whose slice needs its spawn action has that action from its start.
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
// The target bits of an entity (stage 3.3b-M2b, protocol notes 4.34). The client copies dataBits and moreDataBits into one
// 16-bit value of its action list state ((moreDataBits << 8) | dataBits, client code at image RVA 0x2A2AF07). At each state
// that it applies (0x2A2BC20) it gives bit 0 to the camera lock target of the entity (getter 0x2A279B0, setter of the actor
// 0x4848AD0: CameraLockTargetComponent) and bit 1 to the homing target (getter 0x2A27A90, setter 0x48496E0: the
// HomingTargetComponent writes the targetable flag of its client facet, 0x6928AB0). With bit 1 at 0 the Homing clips of an
// ability (Wrecking Ball, Mighty Gavel) find no target: the candidate handler 0x67A8A10 refuses the creature (live trace of
// stage 3.3b-M2b). The server of the game sets the homing bit at the start of the entity from m_notTargetableOnInit of the
// server facet of the slice (0x68689D0: the bit is its negation; false for the wolf slices).
constexpr std::uint8_t kDataBitsCameraLockTarget = 0x01;
constexpr std::uint8_t kDataBitsHomingTarget = 0x02;
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
    if (view == View::observer && entity.facing) {
        // The direction of a dash for an observer (stage 3.3b-S4). The owner keeps the direction of its own client.
        write_yaw_rotation(state.field(0, kRotation), *entity.facing);
    } else {
        state.field(0, kRotation).u8(kRotationIdentity);
    }
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

hub::Bytes encode_observed_motion_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    // The movement of a character for its observers (stage 3.3b-S4: the dash of an ability): the observer view of the
    // movement state with the new position, the direction, and the action of layer 0. The owner gets no fragment: its
    // client moves its character itself, and a position of the server would be a second displacement.
    if (view == View::owner) {
        return {};
    }
    return encode_movement_state(entity, View::observer, clock);
}

namespace {

/** Writes one scope record: start count, fragment index + 1, action index + 1, playback speed 1.0, option 0 (client reader 0x2A75F70). */
void write_scope_record(hub::Writer& record, const world::CreatureAction& action) {
    record.u16(action.generation);
    record.size(action.fragment + 1);
    record.size(action.action + 1);
    record.u16(kPlaybackNormal);
    record.u8(0); // The first option of the fragment.
}

/** Writes the four time fields of scope 0: the time of the action in ticks of 1/30 s, the low byte first. */
void write_scope_time(StateWriter& state, std::chrono::steady_clock::duration elapsedTime) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(elapsedTime).count();
    const std::uint64_t ticks =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max<long long>(elapsed, 0)) * kActionTicksPerSecond / 1000, 0xFFFFFFFFULL);
    state.field(0, kScopeTimeLow).u8(static_cast<std::uint8_t>(ticks));
    state.field(0, kScopeTimeByte1).u8(static_cast<std::uint8_t>(ticks >> 8));
    state.field(0, kScopeTimeByte2).u8(static_cast<std::uint8_t>(ticks >> 16));
    state.field(0, kScopeTimeByte3).u8(static_cast<std::uint8_t>(ticks >> 24));
}

/** @return A float as a 16-bit float (IEEE half, rounded to the nearest; the client reader 0x14087AE90 makes a float of it). */
std::uint16_t half_of(float value) {
    if (!(value > 0.0F)) {
        return 0;
    }
    if (value >= 65504.0F) {
        return 0x7BFF;
    }
    int exponent = 0;
    const float mantissa = std::frexp(value, &exponent); // value = mantissa x 2^exponent, mantissa in [0.5, 1)
    const int biased = exponent - 1 + 15;
    if (biased <= 0) {
        // A subnormal half: value / 2^-24.
        return static_cast<std::uint16_t>(std::lround(std::ldexp(value, 24)));
    }
    std::uint32_t fraction = static_cast<std::uint32_t>(std::lround((mantissa * 2.0F - 1.0F) * 1024.0F));
    std::uint32_t bits = static_cast<std::uint32_t>(biased) << 10;
    bits += fraction; // A fraction of 1024 carries into the exponent.
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(bits, 0x7BFFU));
}

} // namespace

CreatureScope creature_scope(const world::Creature& creature, bool first) {
    CreatureScope scope;
    if (first && creature.spawnAction && !(creature.dead && creature.action)) {
        // From its start for each session: the client shows the body only when it plays the
        // action from the start. A spawn action with the time from the creation of a creature
        // that a session sees later leaves the body hidden, with only the light of the action
        // (live proof, user test 1: an Ancient Guardian Spearman 80 s after its creation).
        scope.action = &*creature.spawnAction;
        scope.fromStart = true;
    } else if (creature.action) {
        scope.action = &*creature.action;
        // Stage QG9c: the first record of a body that the world made again (Creature::renewal) has its death action from the start: the
        // new client entity plays the death of the creature.
        scope.fromStart = first && creature.dead && creature.renewal > 0;
    } else if (creature.moving) {
        scope.action = &*creature.moving;
    } else if (creature.idle && !creature.dead) {
        // The idle record of the end of an action, a walk, or a stun (user test 2, UT2-2; Creatures::rest).
        scope.action = &*creature.idle;
    }
    scope.key = scope.action != nullptr ? 0x10000U | scope.action->generation : kScopeNone;
    // The additive reaction (stage U3): a record of its own scope over the action of scope 0 (a walk, an attack, the idle). Not in the first
    // record of the spawn action, and not for a dead creature.
    if (creature.additive && !creature.dead && !scope.fromStart) {
        scope.additive = &*creature.additive;
        scope.key |= 0x80000000U | (static_cast<std::uint32_t>(creature.additive->record.generation & 0x3FFFU) << 17);
    }
    return scope;
}

hub::Bytes encode_creature_movement_state(const world::Entity& entity, const world::WorldClock& clock, world::Revision since,
                                          std::uint32_t sent, bool keepScope) {
    StateWriter state(kGroups, FieldMask::wide);
    // The position of a creature is exact: the client does not move a creature to the ground.
    write_world_position(state.field(0, kWorldPosition), entity.position);
    write_yaw_rotation(state.field(0, kRotation), entity.creature.yaw);
    state.field(0, kLookDirection).u8(kLookDirectionNorth);
    state.field(0, kTeleportId).u8(1);
    // A creature is a camera lock target and a homing target of the players (the target bits above). The client refuses a dead
    // creature as a homing target itself (m_isNotTargetableWhenDead of the client facet of the slice).
    state.field(0, kDataBits).u8(kDataBitsCameraLockTarget | kDataBitsHomingTarget);
    state.field(0, kMoreDataBits).u8(0);
    // The action of the state (creature_scope). The scope fields go out only when the action
    // changed for the session: the client keeps its action and plays it with no jump.
    const world::Creature& creature = entity.creature;
    const CreatureScope scope = creature_scope(creature, since == 0);
    // A one-shot action of the server with an end (the reaction to a hit, a knockdown, an attack, an area cast, a status action, a timed
    // task) goes out again in each movement state while it plays: the same start count and the time of the state (user test 2, UT2-3). The
    // client builds the target state of a remote creature from the buffered states and interpolates the time of a record between them
    // (client code 0x48709A0, 0x2A222E0); a state with no scope fields keeps the record with its old time (copy 0x2A2ACD0), and at each apply
    // the client starts a one-shot fragment again when the time of its record is more than half the length of the fragment behind its own
    // time (0x2A2BC20, the test at 0x2A2C28D with the constant -0.5 at 0x7F4016C). A push sends a state each 50 ms: one arrow gave three
    // reactions. The locomotion, the idle record, and the actions with no end keep one record (user test 1: a looping fragment is not
    // started again).
    // An additive reaction is a one-shot record too (stage U3): while it plays, each state has both records.
    const bool timed = (scope.action != nullptr && !scope.fromStart && creature.action && scope.action == &*creature.action
                        && creature.action->ends.has_value())
                       || scope.additive != nullptr;
    if (since != 0 && (keepScope || (sent != kScopeUnknown && scope.key == sent && !timed))) {
        write_time_anchor(state, clock);
        return state.finish();
    }
    const world::CreatureAction* selected = scope.action;
    const bool fromStart = scope.fromStart;
    if (scope.additive != nullptr) {
        // Stage U3 (research Q5, live check LC-Q5-2; client code 0x2A2ACD0, 0x2A75F70, 0x14087AE90): the mask has the bit of scope 0 (the
        // action of the server, when one plays: else the client plays its idle) and the bit of the scope of the additive reaction; the
        // blob has one record for each bit, in the order of the scopes; the times of scope 0 are its four time fields, and the time of a
        // scope of 2 or more is one 16-bit float (seconds, the high byte first) in scopeTimeBlobEx, one for each such scope in the mask.
        const world::CreatureAdditive& additive = *scope.additive;
        hub::Writer records;
        if (selected != nullptr) {
            write_scope_record(records, *selected);
        }
        write_scope_record(records, additive.record);
        state.field(0, kScopeData).size((selected != nullptr ? kScopeFullBody : 0U) | (1U << additive.scope));
        hub::Writer& blob = state.field(0, kScopeInfoBlob);
        blob.size(static_cast<std::uint32_t>(records.data().size()));
        blob.raw(records.data());
        hub::Writer& times = state.field(0, kScopeTimeBlob);
        times.size(2);
        times.u16(half_of(std::max(std::chrono::duration<float>(clock.now() - additive.record.started).count(), 0.0F)));
        state.field(0, kGlobalFragTags).raw(selected != nullptr ? selected->tags : additive.record.tags);
        write_scope_time(state, selected != nullptr && !fromStart ? clock.now() - selected->started : std::chrono::steady_clock::duration{});
        write_time_anchor(state, clock);
        return state.finish();
    }
    if (selected == nullptr) {
        // No action of the server: the scope fields with no record. The client ends the action
        // of a record that it kept from an earlier state, and it plays its idle action. Without
        // these fields it starts the kept action again.
        state.field(0, kScopeData).size(0);
        state.field(0, kScopeInfoBlob).size(0);
        state.field(0, kScopeTimeBlob).size(0);
        state.field(0, kGlobalFragTags).raw(creature.body->tags);
    } else {
        // The action that the server started, in scope 0. The time of the action is the time
        // from its start to this state: a client that gets the state later shows the pose of
        // that time, not the start of the action. The spawn action of a first record starts at 0.
        const world::CreatureAction& action = *selected;
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
        state.field(0, kGlobalFragTags).raw(action.tags);
        const auto elapsed =
            fromStart ? 0LL : std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - action.started).count();
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

std::vector<hub::Bytes> encode_creature_movement_parts(const world::Entity& entity, View, const PartsRequest& request) {
    std::vector<hub::Bytes> parts;
    if (request.clock != nullptr) {
        parts.push_back(encode_creature_movement_state(entity, *request.clock, request.since, request.scope, request.keepScope));
    }
    return parts;
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

#include "server/rep/rep_session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "middleware/steam/local_ticket.h"
#include "server/hub/wire.h"
#include "server/rep/combat_messages.h"
#include "server/rep/item_requests.h"
#include "server/rep/lock_messages.h"
#include "server/rep/replication.h"
#include "server/rep/travel_messages.h"
#include "state/identity.h"

namespace soulwarden::backend::server::rep {
namespace {

constexpr std::size_t kRouteSize = 16;
/** Main open-world level of the supported build. */
constexpr const char* kLevelName = "newworld_vitaeeterna";
/**
 * Terrain project of the level (game data folder sharedassets/coatlicue/<name>). The client
 * starts its terrain system only when this text is not empty (client code at image RVA
 * 0x6448CD0). Without the terrain the loading screen does not end.
 */
constexpr const char* kTerrainProject = "coatlicue/newworld_vitaeeterna";
/**
 * Largest client context instance: the level information and the state bundle have one byte
 * for it, and the client uses 0 for "no instance".
 */
constexpr std::uint32_t kContextInstanceCount = 255;
constexpr std::uint8_t kEnvelopeTarget = 0x01;
constexpr std::uint8_t kEnvelopeSource = 0x02;
constexpr std::uint8_t kEnvelopeExtension = 0x04;
/**
 * Interval of the movement state of the own character. The client disconnects
 * ("connection_ingame_timeout") when it gets no game message for 30 seconds (client check at
 * image RVA 0x6445E10), and it stops its input when the last movement state is older than 2 s.
 */
constexpr std::chrono::milliseconds kAnchorInterval{200};
/** Interval at which a session compares the entities near its character with its interest table. */
constexpr std::chrono::milliseconds kInterestInterval{500};
/**
 * Longest wait of the new creatures after a travel for the client to confirm the new place
 * (RepSession::m_creaturesAfterTravel): a client with no position report and no camera near
 * the place gets its creatures after this time. A far travel needs 3 s to 8 s.
 */
constexpr std::chrono::seconds kCreaturesAfterTravel{20};
/**
 * The first record of a session for a creature whose slice needs its spawn action has that action
 * from its start, also while a different action of the server plays. The action that plays goes
 * to the session this time later: the client shows the light of the spawn action for 1 s to 4 s
 * and then the body (stage 3.4c), and an action in the next state can hide the body (code review
 * of the corrections after user test 1, finding F3). A rule of this project.
 */
constexpr std::chrono::milliseconds kSpawnLead = world::behavior_rules::kSpawnLead;
/** Largest number of interest numbers in the control block of one bundle (client reader at image RVA 0x6A6DDB0). */
constexpr std::size_t kMaxStopsInBundle = 100;
/** Shortest time between two log lines for a camera that moves. */
constexpr std::chrono::milliseconds kInputLogInterval{1000};
/** Key in the set of logged message types for an input report that did not parse. */
constexpr std::uint32_t kUnreadableInputMark = 0xFFFFFFFFU;
/** The same for the first input report with target data, and for the first one whose reader stopped. */
constexpr std::uint32_t kTargetDataMark = 0xFFFFFFFEU;
constexpr std::uint32_t kInputTailMark = 0xFFFFFFFDU;
/**
 * A session whose client sends its position uses the camera estimate when no input report had
 * the position field for this time. A report with no field in a shorter time keeps the last
 * position: the client sends no field for a simulation step with no physics step.
 */
constexpr std::chrono::milliseconds kReportTimeout{1000};
/** Largest number of log lines of a session for a change of its position source. */
constexpr unsigned kLoggedSourceChanges = 16;
/** Shortest time between two log lines with the counts of the refused position reports. */
constexpr std::chrono::milliseconds kRefusalLogInterval{5000};
/** Number of log lines for client messages that do not agree with the id rule. */
constexpr std::uint64_t kLoggedIdFaults = 16;
/**
 * Largest payload of one state bundle that the backend makes from more than one fragment. The
 * carrier puts 1,024 data bytes in one message and sends a larger message in chunks, and no
 * live run has a state bundle in chunks. The frame, the envelope, and the header of a bundle
 * are 21 bytes at most. Thus each bundle with this payload is one carrier message.
 */
constexpr std::size_t kBundleBudget = 960;
/** Largest state of one fragment of a state with more than one fragment (the bag): it fits in kBundleBudget. */
constexpr std::size_t kFragmentBudget = 900;
/** Bytes of a record before its fragments: the interest number and the fragment count. */
constexpr std::size_t kRecordHeader = 6;
/**
 * Largest payload of a bundle that the client reads (client code at image RVA 0x646C2B0). The
 * backend sends no bundle that is larger.
 */
constexpr std::size_t kClientBundleLimit = 256000;
/** The fragment count of a record is one byte. */
constexpr unsigned kMaxFragmentsInRecord = 255;
/**
 * Largest number of creatures that come into the interest table in one scan, the nearest ones
 * first. Many creatures can be near a character at its world entry: the client then makes
 * some slices in each scan interval and not all in one frame. A decision of this project.
 */
constexpr std::size_t kMaxNewCreaturesInScan = 16;
/**
 * A travel of the character to a place that is farther than this gets a loading screen in its
 * client (meters). The client has the world near its character: at a place that is far it
 * must load the world first, and with no loading screen the user sees a character that does
 * not move in a world that comes in. A decision of this project (protocol notes 4.16).
 */
constexpr float kTravelScreenDistance = 100.0F;
/**
 * After a travel, a session keeps the place of the travel until its client confirms it: an
 * accepted position report, or a camera estimate that is near the place. A camera estimate is
 * near when it is this distance or less from the place in the horizontal plane, and this
 * distance or less above and below it (meters: an estimate can be some meters from the
 * character). The session does not follow an estimate that is not near: the client made such
 * a report before it got the new position, or it did not take the position. The time alone
 * does not end the rule. It adds only the distance of a movement with the limits of the
 * position check (world/position_report.h: the window speed, the rise speed, and the fall
 * speed), from the first camera after the travel: as with a position report, a client gets a
 * different place only after the time of a walk to it.
 */
constexpr float kTravelCameraRange = 15.0F;

// Hub numbers and type ids of the actor references of the self identification. The client
// sends only the actor id of a reference (the 16-byte route of a client frame), not the hub
// number and not the type id. The type ids are the ids of Aoi::PlayerManagerActor (type 565)
// and Amazon::Hub::ClientProxy (type 551), in the sequence of their text.
constexpr std::uint32_t kWorldHub = 1;
constexpr std::uint32_t kConnectionHub = 2;
constexpr std::array<std::uint8_t, 16> kPlayerManagerType{0x38, 0xA7, 0x37, 0xFF, 0x6C, 0xAD, 0x47, 0x92,
                                                          0xA2, 0xF9, 0x32, 0x47, 0x36, 0xE0, 0xEE, 0x02};
constexpr std::array<std::uint8_t, 16> kClientProxyType{0x29, 0x70, 0xE2, 0x83, 0xFC, 0xF3, 0x4B, 0x6F,
                                                        0x8F, 0x07, 0x86, 0xD6, 0x8B, 0xF3, 0xAE, 0x45};

// The position of a character has two sources. A client with the hook change of the game hook
// sends the exact position of its character in each input report (protocol notes 4.7): the
// session gives it to the world, which checks it (World::report_position). A client with no
// hook change sends no position: the backend then estimates it from the camera, as below.
//
// The camera estimate. The input report has the position
// and the direction of the camera. The camera is behind the character in its own direction,
// 0.5 m to the right of the character in the horizontal plane, and 1.65 m above the feet of
// the character on the vertical axis of the world (live runs: memory read of the character
// position against the camera of the input entry, with the camera level and with the camera
// turned down and up). The distance behind is 4.0 m at the start of a session. It is not a
// constant: an obstacle behind the camera makes it smaller (0.3 m to 3.2 m in live runs), and
// the user changes it with the pointer wheel (2.0 m to 8.0 m in live runs, 1.0 m for one
// step). Thus the backend knows only a line that has the character on it, the camera axis,
// and its estimate can be some meters from the character. The estimate is the position of the
// entity in the world.
//
// The backend follows the wheel only while the character is at rest: the change of the
// camera distance is then the movement of the camera along its axis. The estimate uses the
// distance of the wheel when the camera is nearer than 4.0 m, and 4.0 m when the camera is
// farther: an obstacle makes the camera nearer, never farther, and an estimate with a
// distance that is too large is a position in front of the character, which can be in a wall.
constexpr float kCameraBehind = 4.0F;
constexpr float kCameraRight = 0.5F;
constexpr float kCameraAbove = 1.65F;
/** Limits for the camera distance that the wheel gives. */
constexpr float kMinCameraDistance = 1.0F;
constexpr float kMaxCameraDistance = 10.0F;
/**
 * The client moves a camera that is nearer than this to this distance when the character
 * moves, and the camera stays there (2.0 m from the wheel became 2.2 m in each of 6 walks).
 */
constexpr float kMovingCameraDistance = 2.2F;
/** Time from the last wheel input to a camera that is at its new distance (0.4 s in captures). */
constexpr std::chrono::milliseconds kWheelSettleTime{700};
/**
 * The character moved to the rear when the camera is this much farther behind the known
 * position than its distance with no obstacle.
 */
constexpr float kRearWalkDistance = 3.0F;
/** No obstacle is behind the camera when it is not more than this nearer than its distance. */
constexpr float kFreeCameraTolerance = 0.5F;
/** A position is on the camera axis when its horizontal distance from the axis is not more than this. */
constexpr float kAxisTolerance = 0.75F;
/** Below this horizontal length, the direction of the camera is up or down and gives no axis. */
constexpr float kMinAxisLength = 0.05F;
/** The camera is at rest when it stays in this distance for this time. */
constexpr float kCameraRestDistance = 0.1F;
constexpr std::chrono::milliseconds kCameraRestTime{500};

/** Directions of the camera of an input report in the world. */
struct CameraFrame {
    std::array<float, 3> forward;
    /** To the right of the camera, in the horizontal plane. */
    std::array<float, 3> right;
};

CameraFrame camera_frame(const ClientInput& input) {
    // The yaw turns the camera to the left from north. A positive pitch on the wire is down.
    const float pitch = -input.lookPitch;
    const float sinYaw = std::sin(input.lookYaw);
    const float cosYaw = std::cos(input.lookYaw);
    const float sinPitch = std::sin(pitch);
    const float cosPitch = std::cos(pitch);
    return CameraFrame{{-sinYaw * cosPitch, cosYaw * cosPitch, sinPitch}, {cosYaw, sinYaw, 0.0F}};
}

/**
 * @return The point of the camera axis that is `behind` meters in front of the camera: the
 *         position of a character that has the camera this far behind it.
 */
state::WorldPosition axis_point(const ClientInput& input, const CameraFrame& frame, float behind) {
    return state::WorldPosition{input.cameraX + frame.forward[0] * behind - frame.right[0] * kCameraRight,
                                input.cameraY + frame.forward[1] * behind - frame.right[1] * kCameraRight,
                                input.cameraZ + frame.forward[2] * behind - kCameraAbove};
}

/** Place of a position in relation to the camera axis. The values are horizontal, thus a jump changes nothing. */
struct AxisOffset {
    /** Distance of the position in front of the camera, along the axis. */
    float along;
    /** Distance of the position from the axis. */
    float aside;
    /**
     * Distance of the camera behind the position in the direction of the camera (the value
     * that axis_point takes). No value when the camera points up or down and gives no axis.
     */
    std::optional<float> behind;
};

AxisOffset axis_offset(const ClientInput& input, const CameraFrame& frame, const state::WorldPosition& position) {
    const state::WorldPosition origin = axis_point(input, frame, 0.0F);
    const float deltaX = position.x - origin.x;
    const float deltaY = position.y - origin.y;
    const float length = std::hypot(frame.forward[0], frame.forward[1]);
    if (length < kMinAxisLength) {
        return AxisOffset{0.0F, std::hypot(deltaX, deltaY), std::nullopt};
    }
    const float directionX = frame.forward[0] / length;
    const float directionY = frame.forward[1] / length;
    const float along = deltaX * directionX + deltaY * directionY;
    return AxisOffset{along, std::abs(deltaX * directionY - deltaY * directionX), along / length};
}

/** Actor reference: hub number, actor id, actor type id (client writer at image RVA 0x14FD380). */
void write_actor_reference(hub::Writer& writer,
                           std::uint32_t hubNumber,
                           const world::ActorId& actor,
                           std::span<const std::uint8_t> type) {
    writer.u32(hubNumber);
    writer.raw(actor.bytes);
    writer.raw(type);
}

/** Identity with a 16-byte id: flags with bit 0, then the id. */
void write_identity(hub::Writer& writer, const world::ActorId& actor) {
    writer.u8(1);
    writer.raw(actor.bytes);
}

std::uint64_t read_u64(std::span<const std::uint8_t> data) {
    std::uint64_t value = 0;
    for (const std::uint8_t byte : data) {
        value = (value << 8) | byte;
    }
    return value;
}

/** @return The number of bytes of a value in the variable-length form. */
std::size_t size_length(std::uint32_t value) {
    hub::Writer writer;
    writer.size(value);
    return writer.data().size();
}

} // namespace

RepSession::RepSession(std::uint32_t id, Send send, world::World& world, SessionOptions options)
    : m_id(id),
      m_logMark(log::session_mark(id)),
      // Session 1 has instance 1, the value of each session before the sessions had an id.
      m_contextInstance(static_cast<std::uint8_t>((id + kContextInstanceCount - 1) % kContextInstanceCount + 1)),
      m_options(std::move(options)),
      m_send(std::move(send)),
      m_world(world),
      m_clock(world.clock()),
      m_cameraDistance(kCameraBehind),
      m_knownBehind(kCameraBehind) {}

void RepSession::on_message(std::uint8_t channel, std::span<const std::uint8_t> data, std::optional<std::uint16_t> datagram) {
    m_messageDatagram = datagram;
    std::span<const std::uint8_t> payload;
    if (!hub::decode_frame(data, payload)) {
        log::write("rep: frame on channel %u (%zu bytes) has a bad size or checksum", channel, data.size());
        return;
    }
    hub::Reader reader(payload);
    const auto route = reader.raw(kRouteSize);
    const std::uint8_t flags = reader.u8();
    if ((flags & kEnvelopeTarget) != 0) {
        reader.raw(8);
    }
    if ((flags & kEnvelopeSource) != 0) {
        reader.raw(8);
    }
    if ((flags & kEnvelopeExtension) != 0) {
        log::write("rep: envelope with an extension (flags 0x%02x) is not supported", flags);
        return;
    }
    const std::uint8_t present = reader.u8();
    if (!reader.ok() || present != 1) {
        log::write("rep: envelope without a message (flags 0x%02x)", flags);
        return;
    }
    const std::uint32_t type = reader.size();
    if (!reader.ok()) {
        return;
    }
    if (m_closing) {
        return; // The session sent its client away: a message of the client changes nothing.
    }
    const auto body = payload.subspan(reader.position());
    // The client sends some types 30 times each second. Only the first of each type is logged.
    const bool firstOfType = m_seenTypes.insert(type).second;
    switch (type) {
    case kTypeRegistrationRequest:
    case kTypeRegistrationRequestV2:
    case kTypeRegistrationRequestV3:
        handle_registration(type, body);
        break;
    case kTypeClientInput:
        check_route(route, type, body, firstOfType);
        handle_client_input(body);
        break;
    case kTypeDropInventoryItems:
        check_route(route, type, body, firstOfType);
        handle_drop_items(body);
        break;
    case kTypeSetLockState:
        check_route(route, type, body, firstOfType);
        handle_set_lock_state(body);
        break;
    case kTypeEquipItem:
        check_route(route, type, body, firstOfType);
        handle_equip_item(body);
        break;
    case kTypeUnequipItem:
        check_route(route, type, body, firstOfType);
        handle_unequip_item(body);
        break;
    case kTypeSwapItems:
        check_route(route, type, body, firstOfType);
        handle_swap_items(body);
        break;
    case kTypeSpendAttributePoints:
        check_route(route, type, body, firstOfType);
        handle_spend_attribute_points(body);
        break;
    case kTypeRefundAttributePoints:
        check_route(route, type, body, firstOfType);
        handle_refund_attribute_points(body);
        break;
    case kTypeAssignPoolPoints:
        check_route(route, type, body, firstOfType);
        handle_assign_pool_points(body);
        break;
    case kTypeApplyAbilityPoints:
        check_route(route, type, body, firstOfType);
        handle_apply_ability_points(body);
        break;
    case kTypeChangeMappedAbility:
        check_route(route, type, body, firstOfType);
        handle_change_mapped_ability(body);
        break;
    case kTypeMasteryTreeRespec:
        check_route(route, type, body, firstOfType);
        handle_mastery_tree_respec(body);
        break;
    case kTypeRequestRespawn:
        check_route(route, type, body, firstOfType);
        handle_request_respawn(body);
        break;
    case kTypeCutsceneEnded:
        check_route(route, type, body, firstOfType);
        handle_cutscene_ended(body);
        break;
    case kTypeCancelCommittedInteraction:
        check_route(route, type, body, firstOfType);
        handle_interaction_cancel(body);
        break;
    case kTypeUseItem:
        check_route(route, type, body, firstOfType);
        handle_use_item(body);
        break;
    default:
        check_route(route, type, body, firstOfType);
        if (firstOfType) {
            log::write("rep: message type %u (%zu bytes) has no handler", type, body.size());
        }
        break;
    }
}

void RepSession::check_route(std::span<const std::uint8_t> route,
                             std::uint32_t type,
                             std::span<const std::uint8_t> body,
                             bool firstOfType) {
    // The route of a client frame is the actor id of the receiver: the player manager, or an
    // entity for a component call. The body of a component call starts with the facet target
    // and the entity group id, each one as a big-endian number.
    const auto fault = [&](const char* text) {
        if (m_counts.unknownRoutes + m_counts.unknownTargets + m_counts.wrongEntity <= kLoggedIdFaults) {
            std::string routeText;
            static const char* digits = "0123456789abcdef";
            for (const std::uint8_t byte : route) {
                routeText += digits[byte >> 4];
                routeText += digits[byte & 15];
            }
            log::write("rep: message type %u: %s (route %s)", type, text, routeText.c_str());
        }
    };
    if (route.size() != kRouteSize) {
        return;
    }
    if (std::equal(route.begin(), route.end(), m_world.player_manager().bytes.begin())) {
        ++m_counts.playerManager;
        return;
    }
    const world::Entity* entity = nullptr;
    for (const auto& [id, known] : m_observer.known()) {
        const world::Entity* candidate = m_world.find(id);
        if (candidate != nullptr && std::equal(route.begin(), route.end(), candidate->actor.bytes.begin())) {
            entity = candidate;
            break;
        }
    }
    if (entity == nullptr) {
        ++m_counts.unknownRoutes;
        fault("the route is not the player manager and not an entity of this session");
        return;
    }
    ++m_counts.componentCalls;
    if (body.size() < 16) {
        ++m_counts.unknownTargets;
        fault("the body has no target and no entity");
        return;
    }
    const std::uint64_t target = read_u64(body.subspan(0, 8));
    const std::uint64_t group = read_u64(body.subspan(8, 8));
    if (group != entity->id) {
        ++m_counts.wrongEntity;
        fault("the entity field is not the entity group id of the route");
        return;
    }
    if (entity->kind == world::EntityKind::probe || entity->kind == world::EntityKind::spell
        || entity->kind == world::EntityKind::projectile) {
        ++m_counts.unknownTargets;
        fault("the target is a component of a spell, a probe, or a projectile entity (no message of the client for it is known)");
        return;
    }
    if (entity->kind == world::EntityKind::creature) {
        // A call for a component of a creature that the client observes: the hit of an attack,
        // an interaction. The slice of the creature (game data) names the component.
        const gamedata::CreatureSlice::Component* component = entity->creature.body->find(entity->id, target);
        ++(component != nullptr ? m_counts.creatureTargets : m_counts.unknownTargets);
        if (component == nullptr) {
            fault("the target is not a facet of the slice of this creature");
        } else if (firstOfType) {
            log::write("rep: message type %u: target %016llx is the facet of component %016llx (%s) of creature %016llx "
                       "(%s)",
                       type,
                       static_cast<unsigned long long>(target),
                       static_cast<unsigned long long>(component->component),
                       component->name.c_str(),
                       static_cast<unsigned long long>(entity->id),
                       entity->creature.kind->id.c_str());
        }
        return;
    }
    const gamedata::SliceIds* slice = m_options.playerSlice;
    if (slice == nullptr || !slice->loaded()) {
        ++m_counts.uncheckedTargets;
        return;
    }
    const gamedata::SliceIds::Component* component = slice->find(entity->id, target);
    if (component == nullptr) {
        ++m_counts.unknownTargets;
        fault("the target is not a facet of the player slice for this entity");
        return;
    }
    ++m_counts.knownTargets;
    if (firstOfType) {
        log::write("rep: message type %u: target %016llx is the facet of component %016llx (%s) of entity %016llx",
                   type,
                   static_cast<unsigned long long>(target),
                   static_cast<unsigned long long>(component->component),
                   component->name.empty() ? "no name" : component->name.c_str(),
                   static_cast<unsigned long long>(entity->id));
    }
}

void RepSession::log_message_counts() const {
    if (!m_registered) {
        return;
    }
    if (m_spellRecords != 0) {
        log::write("rep: spells of the session: %llu first record(s) of a spell or a probe", static_cast<unsigned long long>(m_spellRecords));
    }
    if (m_creatureRecords != 0) {
        log::write("rep: creatures of the session: %llu first record(s), %zu in the interest table at one time at most, "
                   "%llu client message(s) for a component of a creature",
                   static_cast<unsigned long long>(m_creatureRecords),
                   m_creaturesObserved,
                   static_cast<unsigned long long>(m_counts.creatureTargets));
    }
    log::write("rep: client messages of the session: %llu component calls (%llu with a target of the id rule, %llu "
               "with a target that is not known, %llu with a wrong entity, %llu with no check), %llu for the player "
               "manager, %llu with a route that is not known",
               static_cast<unsigned long long>(m_counts.componentCalls),
               static_cast<unsigned long long>(m_counts.knownTargets + m_counts.creatureTargets),
               static_cast<unsigned long long>(m_counts.unknownTargets),
               static_cast<unsigned long long>(m_counts.wrongEntity),
               static_cast<unsigned long long>(m_counts.uncheckedTargets),
               static_cast<unsigned long long>(m_counts.playerManager),
               static_cast<unsigned long long>(m_counts.unknownRoutes));
    if (m_contacts + m_projectileItems + m_attack.presses() != 0) {
        using world::HitVerdict;
        log::write("rep: hit claims of the session: %u attack input(s), %llu melee contact(s): %llu accepted, %llu refused "
                   "(%llu not a creature, %llu not a component of the creature, %llu not observed, %llu dead, %llu no "
                   "melee weapon, %llu no attack, %llu the same attack, %llu too far, %llu bad point, %llu too many "
                   "claims, %llu no combat tables, %llu immune, %llu no strike); %llu item(s) of the projectile lists",
                   m_attack.presses(),
                   static_cast<unsigned long long>(m_contacts),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::accepted)),
                   static_cast<unsigned long long>(m_attack.refused()),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::notCreature)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::badReference)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::notObserved)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::dead)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::noWeapon)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::noAttack)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::sameAttack)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::tooFar)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::badPoint)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::tooMany)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::noTables)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::immune)),
                   static_cast<unsigned long long>(m_attack.count(HitVerdict::noStrike)),
                   static_cast<unsigned long long>(m_projectileItems));
    }
    if (std::any_of(m_shots.begin(), m_shots.end(), [](std::uint64_t count) { return count != 0; })) {
        std::string causes;
        for (std::size_t verdict = 1; verdict < m_shots.size(); ++verdict) {
            if (m_shots[verdict] != 0) {
                causes += (causes.empty() ? "" : ", ") + std::to_string(m_shots[verdict]) + " "
                          + world::shot_verdict_text(static_cast<world::ShotVerdict>(verdict));
            }
        }
        log::write("rep: shots of the session (stage 3.3b-S3): %llu accepted%s%s",
                   static_cast<unsigned long long>(m_shots[0]),
                   causes.empty() ? "" : ", refused: ",
                   causes.c_str());
    }
    if (m_abilityPresses != 0) {
        log::write("rep: ability presses of the session: %llu (the casts and the refusals are in the line abilities of tools/world list)",
                   static_cast<unsigned long long>(m_abilityPresses));
    }
    const std::uint64_t accepted = m_track.count(world::PositionVerdict::accepted);
    const std::uint64_t outOfSequence = m_track.count(world::PositionVerdict::outOfSequence);
    if (accepted + outOfSequence + m_track.refused() == 0) {
        log::write("rep: position reports of the session: none (the client sent no position field)");
        return;
    }
    const world::PositionTrack::Largest& largest = m_track.largest();
    if (m_travels != 0) {
        log::write("rep: travels of the session: %u; %llu position report(s) from a place before a travel were not used",
                   m_travels,
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::beforeTravel)));
    }
    if (m_cutsceneEnds != 0 || m_interactionEnds != 0 || m_interactionRequests != 0) {
        log::write("rep: locks of the input of the client in the session: %u end(s) of a cutscene answered, %u end(s) of an "
                   "interaction, %u interaction request(s)",
                   m_cutsceneEnds,
                   m_interactionEnds,
                   m_interactionRequests);
    }
    if (m_damageMarks != 0 || m_deaths != 0 || m_respawns != 0 || m_healNotices != 0) {
        log::write("rep: health of the character in the session: %llu damage mark(s) sent, %u death(s), %u respawn(s), %llu notice(s) of a "
                   "heal of a different character",
                   static_cast<unsigned long long>(m_damageMarks),
                   m_deaths,
                   m_respawns,
                   static_cast<unsigned long long>(m_healNotices));
    }
    log::write("rep: position reports of the session: %llu accepted, %llu refused (%llu too fast, %llu not finite, %llu "
               "at a height limit, %llu outside the world), %llu not in sequence, %u new input sequence(s); largest "
               "accepted distance for one tick %.3f m (input %u), largest speed between two reports %.2f m/s, largest "
               "distance for one window %.3f m in %.3f s, largest rise %.3f m and largest fall %.3f m for one tick "
               "(%.2f m/s up, %.2f m/s down)",
               static_cast<unsigned long long>(accepted),
               static_cast<unsigned long long>(m_track.refused()),
               static_cast<unsigned long long>(m_track.count(world::PositionVerdict::tooFast)),
               static_cast<unsigned long long>(m_track.count(world::PositionVerdict::notFinite)),
               static_cast<unsigned long long>(m_track.count(world::PositionVerdict::heightLimit)),
               static_cast<unsigned long long>(m_track.count(world::PositionVerdict::outsideWorld)),
               static_cast<unsigned long long>(outOfSequence),
               m_track.restarts(),
               static_cast<double>(largest.tick),
               static_cast<unsigned>(largest.tickInput),
               static_cast<double>(largest.stepSpeed),
               static_cast<double>(largest.window),
               static_cast<double>(largest.windowSeconds),
               static_cast<double>(largest.rise),
               static_cast<double>(largest.fall),
               static_cast<double>(largest.riseSpeed),
               static_cast<double>(largest.fallSpeed));
}

void RepSession::tick(carrier::Clock::time_point now) {
    replicate(now);
    if (m_researchKickAt) {
        send_research_kick(now);
    }
}

void RepSession::replicate(carrier::Clock::time_point now) {
    if (!m_inWorld) {
        return;
    }
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    update_interest(now, *own);
    if (!m_pendingStops.empty()) {
        send_stops();
    }
    // The record of the own character goes first: the client must have its local player
    // before it gets a different player (client code at image RVA 0x1747C60).
    send_record(*own, *m_observer.find(own->id), now);
    // The records of the creatures go together in common bundles: many creatures can come into
    // the interest table in one tick.
    RecordBatch batch;
    for (const auto& [id, known] : m_observer.known()) {
        if (id == own->id) {
            continue;
        }
        const world::Entity* entity = m_world.find(id);
        if (entity == nullptr) {
            continue;
        }
        if (entity->kind == world::EntityKind::creature) {
            add_creature_record(*entity, *m_observer.find(id), batch);
        } else if (entity->kind == world::EntityKind::spell || entity->kind == world::EntityKind::probe
                   || entity->kind == world::EntityKind::object || entity->kind == world::EntityKind::projectile) {
            add_observer_record(*entity, *m_observer.find(id), batch);
        } else {
            send_record(*entity, *m_observer.find(id), now);
        }
    }
    send_batch(batch);
}

void RepSession::add_creature_record(const world::Entity& entity, Known& known, RecordBatch& batch) {
    // A creature has only the observer view, and each of its states is one small fragment.
    std::vector<PendingFragment> fragments;
    const world::WorldClock::time_point now = m_clock.now();
    // The action that plays after a first record with the spawn action (kSpawnLead). Until the
    // end of the lead a movement state of the creature (a step of its walk) has no scope fields:
    // the client plays the spawn action from its start (code review of stage 3.4e, finding F3).
    bool scopeDue = false;
    if (known.scopeDue && now >= *known.scopeDue) {
        known.scopeDue.reset();
        // Each scope that is not the scope of the client, also the fields with no record: a walk
        // that ended in the lead must not leave the spawn action in the client (code review of
        // stage 3.5a, finding F5).
        const std::uint32_t playing = creature_scope(entity.creature, false).key;
        scopeDue = playing != known.scope;
    }
    const bool leading = known.scopeDue.has_value();
    for (const StateCodec& codec : creature_codecs()) {
        const auto changed = entity.changed.find(codec.key);
        const bool due = scopeDue && codec.key == world::ComponentKey::movement;
        if (!due && (changed == entity.changed.end() || changed->second <= known.sent)) {
            continue;
        }
        const std::uint32_t slot = slot_of(codec, entity);
        if (slot == 0 && codec.key != world::ComponentKey::metadata) {
            continue; // The slice of the creature has no such component.
        }
        if (codec.parts != nullptr) {
            for (hub::Bytes& state :
                 codec.parts(entity, View::observer,
                             PartsRequest{known.sent, kFragmentBudget, &m_clock, known.scope, leading && known.sent != 0})) {
                fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
            }
            if (codec.key == world::ComponentKey::movement && !(leading && known.sent != 0)) {
                // The first record of a creature that needs the spawn action: the creature keeps its time, and its shot waits to the end of
                // the lead of this session (code review of stage 3.4f-B, finding CS-R4).
                if (known.sent == 0 && entity.creature.spawnAction) {
                    m_world.creatures().first_record(entity.id);
                }
                // The action that the client of this session plays now.
                known.scope = creature_scope(entity.creature, known.sent == 0).key;
                // A first record with the spawn action, while a different action plays: that
                // action follows at the end of the lead (kSpawnLead), also when the creature
                // moves in that time.
                const std::uint32_t playing = creature_scope(entity.creature, false).key;
                if (known.sent == 0 && playing != kScopeNone && playing != known.scope) {
                    known.scopeDue = now + kSpawnLead;
                }
            }
            continue;
        }
        hub::Bytes state = codec.encode(entity, View::observer, m_clock);
        if (!state.empty()) {
            fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
        }
    }
    // Development aid: the fragments of a command (tools/world fragment).
    for (const world::RawFragment& raw : entity.rawFragments) {
        if (raw.changed > known.sent) {
            fragments.push_back(PendingFragment{raw.slot, raw.type, raw.state, true});
        }
    }
    add_records(entity, known, batch, fragments, true);
}

void RepSession::add_observer_record(const world::Entity& entity, Known& known, RecordBatch& batch) {
    std::vector<PendingFragment> fragments;
    const PartsRequest request{known.sent, kFragmentBudget, &m_clock};
    for (const StateCodec& codec : codecs_of(entity)) {
        const auto changed = entity.changed.find(codec.key);
        if (changed == entity.changed.end() || changed->second <= known.sent) {
            continue;
        }
        if (codec.slotParts != nullptr) {
            for (SlotState& part : codec.slotParts(entity, View::observer, request)) {
                fragments.push_back(PendingFragment{part.slot, codec.type, std::move(part.state), false});
            }
            continue;
        }
        const std::uint32_t slot = slot_of(codec, entity);
        if (slot == 0 && codec.key != world::ComponentKey::metadata) {
            continue; // The slice has no such component.
        }
        if (codec.parts != nullptr) {
            for (hub::Bytes& state : codec.parts(entity, View::observer, request)) {
                fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
            }
            continue;
        }
        hub::Bytes state = codec.encode(entity, View::observer, m_clock);
        if (!state.empty()) {
            fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
        }
    }
    // Development aid: the fragments of a command (tools/world probe, tools/world probe fragment).
    for (const world::RawFragment& raw : entity.rawFragments) {
        if (raw.changed > known.sent) {
            fragments.push_back(PendingFragment{raw.slot, raw.type, raw.state, true});
        }
    }
    add_records(entity, known, batch, fragments, false);
}

void RepSession::add_records(const world::Entity& entity, Known& known, RecordBatch& batch, const std::vector<PendingFragment>& fragments,
                             bool creature) {
    if (fragments.empty()) {
        return;
    }
    // The fragments go into records by the rule of send_record: a record ends before a fragment
    // that makes it larger than one carrier message, and before a second fragment for one slot
    // (the client sorts the fragments of a record by their slot). The usual fragments of a
    // creature are one record. A later record of the same creature starts a new bundle: a
    // bundle has one record of an entity at most, as for each other entity.
    (void)entity;
    RecordWriter record(known.interest);
    std::vector<std::uint32_t> slots;
    std::size_t size = kRecordHeader;
    unsigned count = 0;
    /** True from the second record of the creature on. */
    bool later = false;
    const auto add = [&] {
        if (count == 0) {
            return;
        }
        const hub::Bytes payload = record.finish();
        // A bundle stays below the size of one carrier message: a record that does not fit
        // starts the next bundle.
        if (batch.records != 0 && (later || batch.payload.size() + payload.size() > kBundleBudget)) {
            send_batch(batch);
        }
        batch.payload.insert(batch.payload.end(), payload.begin(), payload.end());
        ++batch.records;
        if (known.sent == 0 && !later) {
            ++(creature ? batch.fresh : batch.freshOther);
        }
        later = true;
        record = RecordWriter(known.interest);
        slots.clear();
        size = kRecordHeader;
        count = 0;
    };
    for (const PendingFragment& fragment : fragments) {
        const std::size_t fragmentSize = size_length(fragment.slot) + size_length(fragment.type) + fragment.state.size();
        if (kRecordHeader + fragmentSize > kClientBundleLimit) {
            // The client cannot read a bundle of this size. The fragment does not go out, and
            // the other fragments of the creature do.
            log::write("rep: the fragment for slot %u, type %u (%zu bytes) is larger than the client permits for one "
                       "bundle (%zu bytes): not sent",
                       fragment.slot,
                       fragment.type,
                       fragment.state.size(),
                       kClientBundleLimit);
            continue;
        }
        const bool sameSlot = std::find(slots.begin(), slots.end(), fragment.slot) != slots.end();
        if (count > 0 && (size + fragmentSize > kBundleBudget || sameSlot || count == kMaxFragmentsInRecord)) {
            add();
        }
        record.fragment(fragment.slot, fragment.type, fragment.state);
        slots.push_back(fragment.slot);
        size += fragmentSize;
        ++count;
        if (fragment.development) {
            log::write("rep: DEVELOPMENT COMMAND: fragment of the command for slot %u, type %u (%zu bytes) is in the "
                       "record of interest %u",
                       fragment.slot,
                       fragment.type,
                       fragment.state.size(),
                       known.interest);
        }
    }
    add();
    // The session has the creature at this revision: each of its records is in a bundle or in the batch.
    known.sent = m_world.revision();
}

void RepSession::send_batch(RecordBatch& batch) {
    if (batch.records == 0) {
        return;
    }
    send_state_bundle(batch.payload);
    log::write("rep: state bundle %llu sent (%zu bytes): %u record(s) of creatures and spells, %u first record(s) of a creature, %u of a "
               "spell or a probe",
               static_cast<unsigned long long>(m_bundleSequence),
               batch.payload.size(),
               batch.records,
               batch.fresh,
               batch.freshOther);
    m_creatureRecords += batch.fresh;
    m_spellRecords += batch.freshOther;
    batch = RecordBatch{};
}

void RepSession::update_interest(carrier::Clock::time_point now, const world::Entity& own) {
    // An entity that is not in the world any more: the client must remove it at once. The
    // distance of an entity that left by its distance is for the log line.
    std::vector<world::EntityId> gone;
    std::map<world::EntityId, float> distances;
    for (const auto& [id, known] : m_observer.known()) {
        if (id != own.id && m_world.find(id) == nullptr) {
            gone.push_back(id);
        }
    }
    // A session observes the creatures near its character in each start. It observes the
    // characters of other players only with the option.
    // A travel of a character changes the distances at once: the session does not wait for
    // its interval. Thus an observer gets the stop of a character that went far away, and not
    // a record with its new place.
    // After a travel the new creatures wait for the client (m_creaturesAfterTravel): its
    // confirmation of the new place starts a scan at once.
    const bool creaturesWait = m_travelAnchor && m_creaturesAfterTravel && now < *m_creaturesAfterTravel;
    // The latest time of the wait after a travel holds also for a client that sent no camera
    // report before the travel (code review of the corrections after user test 1, finding F4).
    const bool waitEnded = m_travelAnchor && m_creaturesAfterTravel && now >= *m_creaturesAfterTravel;
    // A new spell entity (stage 3.3b-S2a) starts a scan at once: a spell lives some seconds, and its record must come with
    // no wait for the interval.
    const bool scan = (m_cameraSeen || waitEnded)
                      && (now - m_lastInterestScan >= kInterestInterval || m_world.last_travel() > m_interestTravel
                          || m_world.last_spell() > m_interestSpell || m_world.last_projectile() > m_interestProjectile
                          || (m_creaturesWaited && !creaturesWait));
    if (scan) {
        m_lastInterestScan = now;
        m_interestTravel = m_world.last_travel();
        m_interestSpell = m_world.last_spell();
        m_interestProjectile = m_world.last_projectile();
        for (const auto& [id, known] : m_observer.known()) {
            const world::Entity* entity = m_world.find(id);
            if (id == own.id || entity == nullptr) {
                continue;
            }
            // An object: the distance of its nearest bound (code review of stage 2.5a, finding INT-R3).
            const float distance = entity->kind == world::EntityKind::object ? m_world.interactions().distance_to(*entity, own.position)
                                                                            : world::horizontal_distance(own.position, entity->position);
            if (distance > m_world.interest_leave(*entity)) {
                gone.push_back(id);
                distances[id] = distance;
            }
        }
    }
    // A spell that follows an entity (an aura) names that entity in its record: the session observes it only while it observes
    // that entity (code review of stage 3.3b-S2b, finding F7). Its attachment goes out: the spell goes out too.
    for (const auto& [id, known] : m_observer.known()) {
        const world::Entity* entity = m_world.find(id);
        if (entity == nullptr || entity->kind != world::EntityKind::spell || entity->spell.attached == 0 || entity->spell.attached == own.id
            || std::find(gone.begin(), gone.end(), id) != gone.end()) {
            continue;
        }
        if (m_observer.find(entity->spell.attached) == nullptr
            || std::find(gone.begin(), gone.end(), entity->spell.attached) != gone.end()) {
            gone.push_back(id);
        }
    }
    for (const world::EntityId id : gone) {
        const std::uint32_t interest = m_observer.remove(id);
        if (interest == 0) {
            continue; // The entity was in the list two times: it is out already.
        }
        m_pendingStops.push_back(interest);
        const auto distance = distances.find(id);
        if (distance != distances.end()) {
            log::write("rep: entity %016llx goes out of the interest table (interest %u, %.1f m)",
                       static_cast<unsigned long long>(id),
                       interest,
                       static_cast<double>(distance->second));
        } else {
            log::write("rep: entity %016llx goes out of the interest table (interest %u): it left the world",
                       static_cast<unsigned long long>(id),
                       interest);
        }
    }
    if (!scan) {
        return;
    }
    // After a travel the new creatures wait for the client: its loading screen hides the start
    // of their first records (the spawn action of a slice that needs it plays while the client
    // loads, and the body of the creature stays hidden).
    m_creaturesWaited = creaturesWait;
    // The largest interest distance of an entity is the distance of a character.
    std::vector<std::pair<float, const world::Entity*>> creatures;
    m_world.for_each_near(own.position, m_world.options().interestEnter, [&](world::Entity& entity) {
        if (entity.id == own.id || m_observer.find(entity.id) != nullptr) {
            return;
        }
        const float distance = world::horizontal_distance(own.position, entity.position);
        if (entity.kind == world::EntityKind::creature) {
            if (!creaturesWait && distance <= m_world.interest_enter(entity)) {
                creatures.emplace_back(distance, &entity);
            }
            return;
        }
        if (entity.kind == world::EntityKind::spell) {
            // A spell entity (stage 3.3b-S2a) at the m_sliceSpawnRadius of its slice; a spell with no slice data has no record. An
            // aura that follows an entity waits until the session observes that entity (finding F7: its record names the actor).
            const bool attachment = entity.spell.attached == 0 || entity.spell.attached == own.id
                                    || m_observer.find(entity.spell.attached) != nullptr;
            if (entity.spell.slice != nullptr && attachment && distance <= m_world.interest_enter(entity)) {
                const std::uint32_t interest = m_observer.add(entity.id);
                if (interest != 0) {
                    log::write("rep: spell %016llx (%s) comes into the interest table (interest %u, %.1f m, age %lld ms)",
                               static_cast<unsigned long long>(entity.id),
                               entity.spell.spellId.c_str(),
                               interest,
                               static_cast<double>(distance),
                               static_cast<long long>(
                                   std::chrono::duration_cast<std::chrono::milliseconds>(m_clock.now() - entity.spell.started).count()));
                }
            }
            return;
        }
        if (entity.kind == world::EntityKind::object) {
            return; // The objects come from the grid of the interaction system (below): the distance of their nearest bound.
        }
        if (entity.kind == world::EntityKind::projectile) {
            // A projectile of a creature in the air (stage 3.4f-A): its record has the launch with its time anchor, thus a session that
            // comes near in the flight gets it at its place of now (the client flies it from the anchor). An ended one gets no record.
            if (!entity.shot.ended && distance <= m_world.interest_enter(entity)) {
                const std::uint32_t interest = m_observer.add(entity.id);
                if (interest != 0) {
                    log::write("rep: projectile %016llx (flight %llu of creature %016llx) comes into the interest table (interest %u, "
                               "%.1f m, age %lld ms)",
                               static_cast<unsigned long long>(entity.id),
                               static_cast<unsigned long long>(entity.shot.number),
                               static_cast<unsigned long long>(entity.shot.source),
                               interest,
                               static_cast<double>(distance),
                               static_cast<long long>(
                                   std::chrono::duration_cast<std::chrono::milliseconds>(m_clock.now() - entity.shot.launched).count()));
                }
            }
            return;
        }
        if (entity.kind == world::EntityKind::probe) {
            // Development aid (tools/world probe): an entity of a slice with the fragments of a command.
            if (distance <= m_world.interest_enter(entity)) {
                const std::uint32_t interest = m_observer.add(entity.id);
                if (interest != 0) {
                    log::write("rep: DEVELOPMENT COMMAND: probe entity %016llx comes into the interest table (interest %u, "
                               "%.1f m)",
                               static_cast<unsigned long long>(entity.id),
                               interest,
                               static_cast<double>(distance));
                }
            }
            return;
        }
        if (!m_options.observePlayers) {
            return;
        }
        const std::uint32_t interest = m_observer.add(entity.id);
        if (interest == 0) {
            return; // The table has no free number.
        }
        log::write("rep: entity %016llx (character %s) comes into the interest table (interest %u, %.1f m)",
                   static_cast<unsigned long long>(entity.id),
                   entity.characterId.c_str(),
                   interest,
                   distance);
    });
    // The objects (stage 2.5a; server children: stage 2.5b): a level object with a state of the server, a station, a camp, a
    // fixture, each at the distance of a character from its nearest bound (code review of stage 2.5a, finding INT-R3: a bound can be
    // more than 100 m from the root of its placement). Its record has the state of now, also when the client has an older one (the
    // client keeps the last state of a stopped record of a level object: live check L2 of stage 2.5a).
    m_world.interactions().for_each_entity_near(own.position, m_world.options().interestEnter, [&](world::Entity& entity) {
        if (m_observer.find(entity.id) != nullptr) {
            return;
        }
        const float distance = m_world.interactions().distance_to(entity, own.position);
        if (distance > m_world.interest_enter(entity)) {
            return;
        }
        const std::uint32_t interest = m_observer.add(entity.id);
        if (interest != 0) {
            log::write("rep: object %016llx comes into the interest table (interest %u, %.1f m)", static_cast<unsigned long long>(entity.id),
                       interest, static_cast<double>(distance));
        }
    });
    // The nearest creatures first, and not more than some of them in one scan.
    std::sort(creatures.begin(), creatures.end(), [](const auto& left, const auto& right) {
        return left.first != right.first ? left.first < right.first : left.second->id < right.second->id;
    });
    if (creatures.size() > kMaxNewCreaturesInScan) {
        creatures.resize(kMaxNewCreaturesInScan);
    }
    for (const auto& [distance, entity] : creatures) {
        const std::uint32_t interest = m_observer.add(entity->id);
        if (interest == 0) {
            break; // The table has no free number.
        }
        log::write("rep: creature %016llx (%s, level %u) comes into the interest table (interest %u, %.1f m)",
                   static_cast<unsigned long long>(entity->id),
                   entity->creature.kind->id.c_str(),
                   entity->creature.level,
                   interest,
                   distance);
    }
    m_creaturesObserved = std::max(m_creaturesObserved, creature_count());
}

std::size_t RepSession::creature_count() const {
    std::size_t count = 0;
    for (const auto& [id, known] : m_observer.known()) {
        const world::Entity* entity = m_world.find(id);
        count += entity != nullptr && entity->kind == world::EntityKind::creature ? 1 : 0;
    }
    return count;
}

void RepSession::send_stops() {
    // A stop list goes in the control block of a bundle with no record. The client removes
    // the entity group of each number and clears its table place (client code at image RVA
    // 0x1733A20). After that, a new entity can get the number.
    for (std::size_t first = 0; first < m_pendingStops.size(); first += kMaxStopsInBundle) {
        const std::size_t count = std::min(kMaxStopsInBundle, m_pendingStops.size() - first);
        const std::span<const std::uint32_t> stops(m_pendingStops.data() + first, count);
        send_state_bundle({}, stops);
        for (const std::uint32_t interest : stops) {
            m_observer.release(interest);
        }
        log::write("rep: state bundle %llu sent: stop of %zu interest number(s), first %u",
                   static_cast<unsigned long long>(m_bundleSequence),
                   count,
                   stops.front());
    }
    m_pendingStops.clear();
}

void RepSession::send_record(const world::Entity& entity, Known& known, carrier::Clock::time_point now) {
    const bool owner = entity.id == m_observer.own();
    const View view = owner ? View::owner : View::observer;
    const bool first = known.sent == 0;
    std::vector<PendingFragment> fragments;
    bool movement = false;
    for (const StateCodec& codec : codecs_of(entity)) {
        if (codec.ownerOnly && !owner) {
            continue; // The client of an observer has no facet for this state.
        }
        if (owner && !first && codec.key == world::ComponentKey::metadata) {
            // The owner gets the metadata state of its entity one time. A second one makes the
            // client close its connection (protocol notes 4.2).
            continue;
        }
        const auto changed = entity.changed.find(codec.key);
        if (changed == entity.changed.end() || changed->second <= known.sent) {
            continue;
        }
        const std::uint32_t slot = slot_of(codec, entity);
        if (codec.parts != nullptr) {
            PartsRequest request{known.sent, kFragmentBudget, &m_clock};
            request.ownerHealth = owner ? known.ownerHealth : -1.0F;
            for (hub::Bytes& state : codec.parts(entity, view, request)) {
                fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
            }
            if (owner && codec.key == world::ComponentKey::vitals) {
                known.ownerHealth = entity.vitals.health;
            }
            continue;
        }
        hub::Bytes state = codec.encode(entity, view, m_clock);
        if (state.empty()) {
            continue; // The view has no fragment of this state.
        }
        fragments.push_back(PendingFragment{slot, codec.type, std::move(state), false});
        movement = movement || codec.key == world::ComponentKey::movement;
    }
    // Development aid: the fragments of a command (tools/world fragment), for each view.
    for (const world::RawFragment& raw : entity.rawFragments) {
        if (raw.changed > known.sent) {
            fragments.push_back(PendingFragment{raw.slot, raw.type, raw.state, true});
        }
    }
    /** True when the record has the full movement state of the entity (its position), not only a time anchor. */
    const bool fullMovement = movement;
    bool anchorOnly = false;
    if (owner && !movement && now - m_lastAnchor >= kAnchorInterval) {
        // The owner gets its full movement state one time. After that it gets only a new time
        // anchor, which keeps its input on, with an id that is not the number of an input of
        // its client. The client of the owner moves its character itself: it must not get the
        // position of the entity as a new position.
        anchorOnly = fragments.empty();
        fragments.push_back(PendingFragment{kMovementSlot, kTypeMovementState,
                                            encode_movement_heartbeat(m_clock, m_input.sequence), false});
        movement = true;
    }
    if (fragments.empty()) {
        return;
    }
    // The fragments go into records, one record in one bundle, in their sequence. A record
    // ends before a fragment that makes it larger than one carrier message, and before a
    // second fragment for one slot: the client sorts the fragments of a record by their slot,
    // thus two fragments of one slot have no sure sequence in one record.
    RecordWriter record(known.interest);
    std::vector<std::uint32_t> slots;
    std::size_t size = kRecordHeader;
    unsigned count = 0;
    const auto send = [&] {
        if (count == 0) {
            return;
        }
        const hub::Bytes payload = record.finish();
        send_state_bundle(payload);
        if (!anchorOnly) {
            log::write("rep: state bundle %llu sent (%zu bytes): %s view of entity %016llx, interest %u, %u fragment(s)",
                       static_cast<unsigned long long>(m_bundleSequence),
                       payload.size(),
                       owner ? "owner" : "observer",
                       static_cast<unsigned long long>(entity.id),
                       known.interest,
                       count);
        }
        record = RecordWriter(known.interest);
        slots.clear();
        size = kRecordHeader;
        count = 0;
    };
    for (const PendingFragment& fragment : fragments) {
        const std::size_t fragmentSize = size_length(fragment.slot) + size_length(fragment.type) + fragment.state.size();
        if (kRecordHeader + fragmentSize > kClientBundleLimit) {
            // The client cannot read a bundle of this size. The fragment does not go out, and
            // the other fragments of the entity do.
            log::write("rep: the fragment for slot %u, type %u (%zu bytes) is larger than the client permits for one "
                       "bundle (%zu bytes): not sent",
                       fragment.slot,
                       fragment.type,
                       fragment.state.size(),
                       kClientBundleLimit);
            continue;
        }
        const bool sameSlot = std::find(slots.begin(), slots.end(), fragment.slot) != slots.end();
        if (count > 0 && (size + fragmentSize > kBundleBudget || sameSlot || count == kMaxFragmentsInRecord)) {
            send();
        }
        record.fragment(fragment.slot, fragment.type, fragment.state);
        slots.push_back(fragment.slot);
        size += fragmentSize;
        ++count;
        if (fragment.development) {
            log::write("rep: DEVELOPMENT COMMAND: fragment of the command for slot %u, type %u (%zu bytes) is in the "
                       "record of interest %u",
                       fragment.slot,
                       fragment.type,
                       fragment.state.size(),
                       known.interest);
        }
    }
    send();
    // The session has the entity at this revision only now: each bundle of the record is out.
    known.sent = m_world.revision();
    if (owner && std::any_of(fragments.begin(), fragments.end(),
                             [](const PendingFragment& fragment) { return fragment.slot == kStatMultiplierSlot && fragment.type == kTypeStatMultiplierState; })) {
        // The client has this speed table now: the check of its position reports takes its multiplier (code review of
        // stage 3.3b-S1, finding F1).
        m_track.set_client_speed(std::max(entity.moveMultiplier, entity.sprintMultiplier), m_clock.now());
    }
    if (owner && movement) {
        m_lastAnchor = now;
    }
    if (owner && fullMovement) {
        // The client has the teleport number of this state: the next travel counts from it.
        m_clientTeleport = entity.teleport;
    }
    if (owner && m_travelNotice && fullMovement) {
        // The movement state with the new place of a travel is out. This message ends the mark
        // "teleport pending" of the client: its loading screen can end.
        m_travelNotice = false;
        send_message(kTypePlayerTeleport, player_teleport_body(entity.id));
        log::write("rep: travel: the movement state with the new position is out (teleport number %u): message \"on "
                   "player teleport\" (type %u) sent",
                   static_cast<unsigned>(entity.teleport),
                   kTypePlayerTeleport);
    }
    if (owner && first) {
        log::write("rep: local player record sent");
    }
}

void RepSession::character_lost(world::LostCause cause) {
    // The call comes from the world, in the handler of a different session or of a task.
    const log::Context context(m_logMark);
    m_inWorld = false;
    m_observer.clear();
    m_pendingStops.clear();
    m_researchKickAt.reset();
    const bool deleted = cause == world::LostCause::deleted;
    send_kick(deleted ? kKickCharacterDeactivated : kKickDuplicateSession);
    m_closing = Closing{deleted ? CloseCause::characterDeleted : CloseCause::replaced, m_clock.now()};
    log::write("rep: the session lost its character (%s): message \"kicked\" (type %u) sent",
               deleted ? "the character was deleted" : "a newer session has the character",
               kTypeKickClient);
}

void RepSession::send_kick(std::uint32_t reason) {
    // KickClientMsg, client reader at image RVA 0x7CB980: reason:u32 u64 text.
    hub::Writer kick;
    kick.u32(reason);
    kick.u64(0);
    kick.string("");
    send_message(kTypeKickClient, kick.data());
}

void RepSession::handle_client_input(std::span<const std::uint8_t> body) {
    const auto input = read_client_input(body);
    if (!input) {
        if (m_seenTypes.insert(kUnreadableInputMark).second) {
            log::write("rep: input report (%zu bytes) did not parse", body.size());
        }
        return;
    }
    // The input of a session changes only the entity that the session owns.
    world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto now = m_clock.now();
    if (input->targetKind != kInputTargetNone && m_seenTypes.insert(kTargetDataMark).second) {
        // The layout of the target data has static proof only (client reader at image RVA
        // 0x416EB00). The log shows the first report of a session that has it.
        log::write("rep: input %u (%zu bytes) has target data of the kind %u: the first report of the session with "
                   "target data; the reader %s",
                   static_cast<unsigned>(input->sequence),
                   body.size(),
                   static_cast<unsigned>(input->targetKind),
                   input->tail == InputTail::read ? "got to the position of the character" : "stopped after it");
    }
    if (input->tail != InputTail::read && m_seenTypes.insert(kInputTailMark).second) {
        log::write("rep: input %u (%zu bytes, flags 0x%llx): %s before the end of the entry: the report gives no "
                   "position and no attack data (the first report of the session with this result)",
                   static_cast<unsigned>(input->sequence),
                   body.size(),
                   static_cast<unsigned long long>(input->flags),
                   input->tail == InputTail::badValue ? "the entry has a value that the client does not send"
                                                      : "the body ends");
    }
    const state::WorldPosition before = own->position;
    if (input->hasCharacter) {
        report_position(*input, now, *own);
    }
    // After the position: the check of a hit claim uses the position of the character.
    handle_attacks(*input);
    if (!std::isfinite(input->cameraX) || !std::isfinite(input->cameraY) || !std::isfinite(input->cameraZ)
        || !std::isfinite(input->lookYaw) || !std::isfinite(input->lookPitch)) {
        // A number that is not finite is not a camera: it must not go into the estimate of
        // the position.
        if (m_seenTypes.insert(kUnreadableInputMark).second) {
            log::write("rep: input report (%zu bytes) has a camera number that is not finite: not used", body.size());
        }
        return;
    }
    const bool axesChanged = input->axes != m_input.axes;
    const bool cameraMoved = input->hasCamera
                             && (input->cameraX != m_input.cameraX || input->cameraY != m_input.cameraY
                                 || input->cameraZ != m_input.cameraZ);
    if (input->hasCamera) {
        m_cameraSeen = true;
        if (m_options.research.kickReason && !m_researchKickArmed) {
            m_researchKickArmed = true;
            m_researchKickAt = now + m_options.research.kickDelay;
        }
        if (!input->hasCharacter && use_camera_estimate(*input, now, *own) && !camera_before_travel(*input, now, *own)) {
            track_position(*input, now, *own);
        }
        m_input = *input;
    } else {
        m_input.sequence = input->sequence;
        m_input.axes = input->axes;
    }
    const bool positionMoved =
        own->position.x != before.x || own->position.y != before.y || own->position.z != before.z;
    // The log has one line for each change of the axes, and one line each second while the
    // camera or the character moves. The first line shows that the loading screen of the
    // client is gone: the client sends no camera position before that.
    if (m_input.hasCamera
        && (axesChanged || ((cameraMoved || positionMoved) && now - m_lastInputLog >= kInputLogInterval))) {
        log::write("rep: input %u camera %.2f %.2f %.2f look %.3f %.3f position %.3f %.3f %.3f (%s)",
                   static_cast<unsigned>(m_input.sequence),
                   m_input.cameraX,
                   m_input.cameraY,
                   m_input.cameraZ,
                   m_input.lookYaw,
                   m_input.lookPitch,
                   own->position.x,
                   own->position.y,
                   own->position.z,
                   m_positionSource == PositionSource::report   ? "client report"
                   : m_positionSource == PositionSource::camera ? "camera estimate"
                                                                : "no source");
        m_lastInputLog = now;
    }
}

void RepSession::handle_attacks(const ClientInput& input) {
    if (input.steps.empty()) {
        return;
    }
    const auto now = m_clock.now();
    const auto take = [&](const InputStep& step) {
        if (step.weaponKey != 0) {
            m_world.combat().select_weapon(m_id, step.weaponKey);
        }
        // The weapon in the hand at each step (code review of stage 3.3b-S3, finding F3): a press fits only a shot of its weapon.
        const world::Entity* own = m_world.entity_of(m_id);
        m_attack.step(step.sequence, step.attackPress, step.attackHold, step.attackRelease, now,
                      own != nullptr ? world::Combat::weapon_key(*own) : std::string());
        // The sequence of the basic attacks of the character (stage 3.3b-M1): before the contacts of the step.
        world::AttackBits bits;
        bits.press = step.attackPress;
        bits.hold = step.attackHold;
        bits.release = step.attackRelease;
        bits.dodge = step.dodge;
        // The lock of a travel (stage 3.4f-B): a dodge in the loading screen of a travel is refused, as the press of an ability.
        bits.locked = m_travelAnchor.has_value();
        bits.block = step.blockPress;
        m_world.combat().step(m_id, m_attack.sequence(), step.sequence, bits);
        m_projectileItems += step.projectileHits + step.shots;
        if (step.abilityKey != 0) {
            press_ability(input, step);
        }
        // The confirm of the aim of a heal of an ability (stage 3.3b-S6): a click (on the target) or the self cast input. The world
        // starts the cast only for an aim of this session; each other click is an attack as before.
        if (step.attackPress || step.selfTarget) {
            // The lock of the session now (code review of stage 3.3b-S6, finding S6-R8): a confirm in the loading screen of a travel is refused.
            if (m_world.abilities().confirm(m_id, step.sequence, step.selfTarget, input.hasLook ? input.lookYaw : m_input.lookYaw,
                                            m_travelAnchor.has_value())) {
                log::write("rep: the aim of the ability is confirmed (%s, input %u)", step.selfTarget ? "self cast" : "click",
                           static_cast<unsigned>(step.sequence));
            }
        }
        // A quick slot key and the heart rune key (stage 1.3b): the world finds the character from the session.
        if (step.quickKey != 0) {
            const world::UseVerdict verdict = m_world.consumables().use_quick(m_id, step.quickKey);
            log::write("rep: quick slot key %u (input %u): %s",
                       static_cast<unsigned>(step.quickKey),
                       static_cast<unsigned>(step.sequence),
                       world::use_verdict_text(verdict));
        }
        if (step.runeKey) {
            const world::RuneVerdict verdict = m_world.runes().activate(m_id);
            log::write("rep: heart rune key (input %u): %s", static_cast<unsigned>(step.sequence), world::rune_verdict_text(verdict));
        }
        // A shot of the client: the projectile of a ranged or magic attack (stage 3.3b-S3), or of Gravity Well.
        for (std::uint32_t ordinal = 0; ordinal < step.shotItems.size(); ++ordinal) {
            shoot(step, step.shotItems[ordinal], ordinal);
        }
        for (const InputContact& contact : step.contacts) {
            claim_hit(step, contact);
        }
        observe_interaction(step);
    };
    // The steps of a report are newest first.
    const InputStep& newest = input.steps.front();
    if (!m_attack.newer(newest.sequence)) {
        // An old or a second datagram. Only when the input numbers of the client started
        // again, the newest step is the first step of a new sequence.
        if (m_attack.restarted(newest.sequence, now)) {
            take(newest);
        }
        return;
    }
    for (auto step = input.steps.rbegin(); step != input.steps.rend(); ++step) {
        // An older step of the report that the session got in a report before it is not new,
        // also when the reports stopped for some time: a contact counts one time.
        if (m_attack.newer(step->sequence)) {
            take(*step);
        }
    }
}

void RepSession::press_ability(const ClientInput& input, const InputStep& step) {
    // The world finds the character from the session and the ability from the bar of the weapon
    // in its hand (world/abilities.h). A press that it refuses changes nothing and gets no
    // message: the client has none for it (live check L2 of stage 3.3a).
    world::AbilityPress press;
    press.place = static_cast<unsigned>(step.abilityKey - 1);
    press.input = step.sequence;
    press.sequence = m_attack.sequence();
    press.yaw = input.hasLook ? input.lookYaw : m_input.lookYaw;
    // The lock of the input: the client did not confirm the place of a travel (its loading screen).
    press.locked = m_travelAnchor.has_value();
    // The key with the self cast input held (stage 3.3b-S6): an aimed heal casts at once on the caster, with no aim.
    press.self = step.selfTarget;
    press.confirmed = step.selfTarget;
    const world::CastVerdict verdict = m_world.abilities().press(m_id, press);
    ++m_abilityPresses;
    const auto cause = static_cast<std::size_t>(verdict);
    if (verdict != world::CastVerdict::accepted && !m_castVerdictLogged[cause]) {
        m_castVerdictLogged[cause] = true;
        log::write("rep: ability press refused (%s): key %u, input %u (the first press of the session with this cause)",
                   world::cast_verdict_text(verdict),
                   static_cast<unsigned>(step.abilityKey),
                   static_cast<unsigned>(step.sequence));
    }
}

void RepSession::observe_interaction(const InputStep& step) {
    if (step.interaction == m_interactionBlock) {
        return; // The client sends its request in each input: the same request is no new request.
    }
    m_interactionBlock = step.interaction;
    if (!step.interaction) {
        // The client ended its request (the close of the journal of a lore page: live check L1).
        m_world.interactions().request_ended(m_id);
        return;
    }
    const InputInteraction& block = *step.interaction;
    // A press before an early cancel of its reference (code review of stage 2.5b, finding ST-R2; stage 3.4f-A, findings CR-R3 and CR-R4):
    // the client released it before the server got it. No use, and no reply: the request is old. The press is old when its datagram is
    // older than the datagram of the cancel, or when its input step is not later than the steps of the cancelled attempt (a newer
    // datagram can repeat the old entry). Each reference has its own mark until its time ends; a real new press (a later step) ends it.
    const world::WorldClock::time_point now = m_clock.now();
    std::erase_if(m_earlyCancels, [&](const EarlyCancel& mark) { return now >= mark.ends; });
    const world::InteractionReference reference{block.actor, block.group, block.target};
    const auto mark = std::find_if(m_earlyCancels.begin(), m_earlyCancels.end(),
                                   [&](const EarlyCancel& candidate) { return candidate.reference == reference; });
    if (mark != m_earlyCancels.end()) {
        const bool olderDatagram =
            m_messageDatagram && static_cast<std::int16_t>(static_cast<std::uint16_t>(*m_messageDatagram - mark->datagram)) < 0;
        const bool olderStep = mark->hasStep && static_cast<std::int16_t>(static_cast<std::uint16_t>(step.sequence - mark->step)) <= 0;
        if (olderDatagram || olderStep) {
            log::write("rep: interaction request of input %u is older than the cancel of its client (datagram %u, cancel datagram %u, "
                       "steps of the attempt to %u): no use",
                       static_cast<unsigned>(step.sequence), m_messageDatagram ? static_cast<unsigned>(*m_messageDatagram) : 0U,
                       static_cast<unsigned>(mark->datagram), static_cast<unsigned>(mark->step));
            // The attempt ended with the cancel: the same block in a later step is a new press (as after a matched cancel).
            m_interactionBlock.reset();
            return;
        }
        m_earlyCancels.erase(mark);
    }
    // The session took a request of this reference: a later cancel of it with no use is a late cancel (no mark).
    std::erase_if(m_seenRequests, [&](const auto& seen) { return seen.first == reference || now >= seen.second; });
    if (m_seenRequests.size() >= kEarlyCancelMost) {
        m_seenRequests.erase(m_seenRequests.begin());
    }
    m_seenRequests.emplace_back(reference, now + kEarlyCancelKeep);
    world::InteractionRequest request;
    request.object.actor = block.actor;
    request.object.group = block.group;
    request.object.target = block.target;
    request.action = block.action;
    request.baseAction = block.baseAction;
    request.optionEntity = block.optionEntity;
    request.optionId = block.optionId;
    request.committed = block.committed;
    request.input = step.sequence;
    // The world finds the character from the session and checks the request (world/interactions.h).
    const world::InteractionVerdict verdict = m_world.interactions().request(m_id, request);
    ++m_interactionRequests;
    if (verdict == world::InteractionVerdict::same || verdict == world::InteractionVerdict::notInWorld) {
        return;
    }
    const bool accepted = verdict == world::InteractionVerdict::accepted;
    send_message(kTypeInteractionResult, interaction_result_body(m_observer.own(), request.object, block.action, accepted));
    log::write("rep: interaction request (input %u): object %016llx target %016llx action %u base %u option %08x: %s, reply %u "
               "(result %u)",
               static_cast<unsigned>(step.sequence), static_cast<unsigned long long>(block.group),
               static_cast<unsigned long long>(block.target), static_cast<unsigned>(block.action),
               static_cast<unsigned>(block.baseAction), block.optionId, world::interaction_verdict_text(verdict),
               kTypeInteractionResult, accepted ? 1U : 2U);
}

void RepSession::interaction_cancelled() {
    // The call comes from the world (a command, the death of the character).
    if (!m_inWorld || m_closing) {
        return;
    }
    send_message(kTypeCancelCurrentInteraction, cancel_interaction_body(m_observer.own()));
    log::write("rep: the world ended the interaction of the character: message \"cancel current interaction\" (type %u) sent",
               kTypeCancelCurrentInteraction);
}

void RepSession::shoot(const InputStep& step, const InputShot& item, std::uint32_t ordinal) {
    if (m_world.abilities().takes_shot(m_id, item.attack)) {
        // The projectile of an ability of its character (Gravity Well, stage 3.3a; the trap and the Beacon, stage 3.3b-S2b): the
        // abilities system. The cast waits for a shot item with the attack of its projectile.
        world::AbilityShot shot;
        shot.input = step.sequence;
        shot.sequence = m_attack.sequence();
        shot.attack = item.attack;
        shot.origin = state::WorldPosition{item.origin[0], item.origin[1], item.origin[2]};
        shot.direction = item.direction;
        m_world.abilities().shot(m_id, shot);
        return;
    }
    // The world finds the character from the session, checks the shot, and follows its projectile (world/projectiles.h). A
    // shot that it refuses changes nothing and gets no message.
    world::ShotClaim claim;
    claim.input = step.sequence;
    claim.sequence = m_attack.sequence();
    claim.ordinal = ordinal;
    claim.origin = state::WorldPosition{item.origin[0], item.origin[1], item.origin[2]};
    claim.direction = item.direction;
    claim.charge = item.charge;
    claim.ammo = item.ammo;
    claim.attack = item.attack;
    claim.row = item.row;
    claim.hasReceiver = item.hasReceiver;
    claim.nearActor.bytes = item.receiverActor;
    claim.nearId = item.receiver;
    claim.nearFacet = item.facet;
    claim.nearPoint = state::WorldPosition{item.point[0], item.point[1], item.point[2]};
    claim.observed = item.hasReceiver && item.receiver != m_observer.own() && m_observer.find(item.receiver) != nullptr;
    claim.locked = m_travelAnchor.has_value();
    const world::ShotResult result = m_world.projectiles().launch(m_id, claim, m_attack);
    const auto cause = static_cast<std::size_t>(result.verdict);
    ++m_shots[cause];
    if (result.verdict != world::ShotVerdict::accepted && !m_shotVerdictLogged[cause]) {
        m_shotVerdictLogged[cause] = true;
        log::write("rep: shot refused (%s): input %u, attack %08x, row %08x, charge %.0f, ammunition %08x%s (the first shot of the "
                   "session with this cause)",
                   world::shot_verdict_text(result.verdict),
                   static_cast<unsigned>(step.sequence),
                   item.attack,
                   item.row,
                   item.charge,
                   item.ammo,
                   item.hasReceiver ? ", with a receiver near the muzzle" : "");
    }
}

void RepSession::claim_hit(const InputStep& step, const InputContact& contact) {
    ++m_contacts;
    world::HitClaim claim;
    claim.input = step.sequence;
    claim.target.bytes = contact.actor;
    claim.targetId = contact.entity;
    claim.facet = contact.facet;
    claim.group.bytes = contact.groupActor;
    claim.groupId = contact.group;
    claim.point = state::WorldPosition{contact.point[0], contact.point[1], contact.point[2]};
    // The session is an observer: only it knows which entities its client has.
    claim.observed = contact.entity != m_observer.own() && m_observer.find(contact.entity) != nullptr;
    // The world finds the attacker from the session, checks the claim, and computes the
    // damage. A claim that it refuses changes nothing.
    const world::HitResult result = m_world.combat().claim(m_id, claim, m_attack);
    if (result.verdict == world::HitVerdict::accepted) {
        return;
    }
    const auto cause = static_cast<std::size_t>(result.verdict);
    if (!m_hitVerdictLogged[cause]) {
        m_hitVerdictLogged[cause] = true;
        const world::AttackTrack::Press* attack = m_attack.newest();
        log::write("rep: hit claim refused (%s): input %u, target %016llx, facet %016llx, point %.3f %.3f %.3f; %s (the "
                   "first claim of the session with this cause)",
                   world::hit_verdict_text(result.verdict),
                   static_cast<unsigned>(step.sequence),
                   static_cast<unsigned long long>(contact.entity),
                   static_cast<unsigned long long>(contact.facet),
                   contact.point[0],
                   contact.point[1],
                   contact.point[2],
                   attack == nullptr ? "the client sent no attack input"
                                     : ("the newest attack input started at input " + std::to_string(attack->input) + " and was on for "
                                        + std::to_string(attack->holdSteps) + " step(s)")
                                           .c_str());
    }
}

void RepSession::traveled(const world::Travel& travel) {
    // The call comes from the world, in a task of a command.
    const log::Context context(m_logMark);
    ++m_travels;
    // The reports that the client made before it gets the new position must not put the entity
    // back: the check starts again from the new place.
    m_track.traveled();
    m_followCamera = false;
    forget_camera_distance();
    m_knownBehind = kCameraBehind;
    // The same for the camera estimate: the session keeps the place until the client confirms it.
    m_travelAnchor = TravelAnchor{travel.to, std::nullopt, false};
    // The creatures of the new place come into the interest table when the loading screen of
    // the client is gone: the client confirms the place.
    m_creaturesAfterTravel = m_clock.now() + kCreaturesAfterTravel;
    if (!m_inWorld || m_closing) {
        return;
    }
    const bool screen = travel.screen == world::TravelScreen::on
                        || (travel.screen == world::TravelScreen::automatic && travel.distance > kTravelScreenDistance);
    // A travel and a respawn end each lock of the input: a character that the client keeps in
    // place (an open conversation, a cutscene with no answer) moves again at the new place.
    end_input_lock("a travel");
    if (screen) {
        send_message(kTypeTeleportWithLoadingScreen, teleport_screen_body(m_observer.own()));
    }
    m_travelNotice = true;
    log::write("rep: travel of the character: %.1f m; %s", static_cast<double>(travel.distance),
               screen ? "message \"on teleport with loading screen\" (type 3951) sent" : "no loading screen");
}

bool RepSession::camera_before_travel(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own) {
    if (!m_travelAnchor) {
        return false;
    }
    using namespace world::position_limits;
    TravelAnchor& anchor = *m_travelAnchor;
    // The position that the estimate gives for this camera: the camera has its start distance
    // after a travel (RepSession::traveled).
    const state::WorldPosition estimate = axis_point(input, camera_frame(input), kCameraBehind);
    // The time of a movement starts at the first camera after the travel: the loading screen
    // of the client is not a time in which the character moves.
    if (!anchor.firstCamera) {
        anchor.firstCamera = now;
    }
    const float waited = std::chrono::duration<float>(now - *anchor.firstCamera).count();
    const float distance = world::horizontal_distance(estimate, anchor.place);
    const float rise = estimate.z - anchor.place.z;
    if (distance > kTravelCameraRange + kWindowSpeed * waited || rise > kTravelCameraRange + kRiseSpeed * waited
        || -rise > kTravelCameraRange + kFallSpeed * waited) {
        if (!anchor.logged) {
            anchor.logged = true;
            log::write("rep: the camera estimate is not near the place of the travel: input %u gives %.3f %.3f %.3f, the "
                       "place is %.3f %.3f %.3f; the entity keeps the place until the client confirms it (the first report "
                       "of this travel with this result)",
                       static_cast<unsigned>(input.sequence),
                       estimate.x,
                       estimate.y,
                       estimate.z,
                       anchor.place.x,
                       anchor.place.y,
                       anchor.place.z);
        }
        return true;
    }
    // A camera at the new place: the client has the position, and the estimate starts again there.
    m_travelAnchor.reset();
    resume_camera_estimate(input, now, own);
    return false;
}

void RepSession::damage_dealt(const world::DamageDealt& damage) {
    if (!m_inWorld || m_closing) {
        return;
    }
    // The number at the target, for the client of the attacker only. Each observer of the
    // target gets its new health with the vitals state.
    send_message(kTypeDamageDealt, damage_dealt_body(m_observer.own(), damage));
}

void RepSession::damage_taken(const world::DamageTaken& damage) {
    // The call comes from the world, in its tick (a contact of an attack of a creature).
    if (!m_inWorld || m_closing) {
        return;
    }
    // The mark with the direction of the damage, for the client of the character only. The
    // owner and each observer of the character get its new health with the vitals state.
    send_message(kTypeDamageTaken, damage_taken_body(m_observer.own(), damage));
    ++m_damageMarks;
}

void RepSession::healed_player(world::EntityId target, float amount) {
    // The call comes from the world, in its tick (a heal of an ability or of a status of the character of this session).
    if (!m_inWorld || m_closing) {
        return;
    }
    // The green number at the healed character, for the client of the healer only (live check L3).
    send_message(kTypeHealedPlayer, healed_player_body(m_observer.own(), target, amount));
    ++m_healNotices;
}

void RepSession::died(float respawnSeconds) {
    // The call comes from the world, in its tick or in a task of a command.
    const log::Context context(m_logMark);
    if (!m_inWorld || m_closing) {
        return;
    }
    // The dead mark is in the vitals state of the entity, which goes out with the next record.
    // The client shows its death screen when it has the two.
    send_message(kTypePlayerDeath, player_death_body(m_observer.own(), respawnSeconds));
    ++m_deaths;
    log::write("rep: the character is dead: message \"on player death\" (type %u) sent, %.1f s to the respawn screen",
               kTypePlayerDeath,
               static_cast<double>(respawnSeconds));
}

void RepSession::handle_request_respawn(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    const auto request = read_respawn_request(body);
    if (own == nullptr) {
        return;
    }
    if (!request) {
        log::write("rep: respawn request (type %u, %zu bytes) is too short: no change", kTypeRequestRespawn, body.size());
        return;
    }
    if (!own_call(*own, request->call, kTypeRequestRespawn, "respawn request")) {
        return;
    }
    // The world finds the character from the session. A dead character lives again at its
    // respawn place: the vitals state and the movement state with a new teleport number go out
    // with the next record, and the message "on player teleport" after it (the travel path).
    const world::HealthResult result = m_world.health().respawn(m_id);
    if (result.error == world::HealthError::none) {
        ++m_respawns;
        log::write("rep: respawn request (random %d, id \"%s\"): the character lives again", request->random ? 1 : 0,
                   request->id.c_str());
        if (!result.travelled) {
            // No travel was possible: the character lives again at its place, and this message
            // ends the loading screen that the client showed for its request. A travel ends each
            // lock of the input (RepSession::traveled): here the session does it.
            end_input_lock("a respawn");
            send_message(kTypePlayerTeleport, player_teleport_body(m_observer.own()));
        }
        return;
    }
    // The client showed its loading screen before it sent the request, and only this message
    // ends the mark "teleport pending" of the client when no travel comes.
    send_message(kTypePlayerTeleport, player_teleport_body(m_observer.own()));
    log::write("rep: respawn request refused (%s): message \"on player teleport\" (type %u) sent, no change",
               result.error == world::HealthError::alive ? "the character is not dead" : "the session has no character",
               kTypePlayerTeleport);
}

void RepSession::handle_cutscene_ended(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    const auto call = read_component_call(body);
    if (own == nullptr) {
        return;
    }
    if (!call) {
        log::write("rep: end of a cutscene (type %u, %zu bytes) is too short: no answer", kTypeCutsceneEnded, body.size());
        return;
    }
    if (!own_call(*own, *call, kTypeCutsceneEnded, "end of a cutscene")) {
        return;
    }
    // The client keeps the input of its character blocked until this answer: without it the
    // character never moves again (user test 1, a conversation with a person of the settlement).
    send_message(kTypeCutsceneEndFinalized, cutscene_finalized_body(own->id));
    ++m_cutsceneEnds;
    log::write("rep: the client ended a cutscene (type %u): message \"cutscene end request finalized\" (type %u) sent, "
               "the input of the character is free again",
               kTypeCutsceneEnded,
               kTypeCutsceneEndFinalized);
}

void RepSession::handle_interaction_cancel(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    // The full body: the component call and the 32 bytes of the reference (research K2). A short body is not the message.
    const auto cancel = read_interaction_cancel(body);
    if (!cancel) {
        log::write("rep: end of an interaction (type %u, %zu bytes) is not 48 bytes: no change", kTypeCancelCommittedInteraction,
                   body.size());
        return;
    }
    if (!own_call(*own, ComponentCall{cancel->target, cancel->entity}, kTypeCancelCommittedInteraction, "end of an interaction")) {
        return;
    }
    ++m_interactionEnds;
    // The world ends the committed interaction of the character when the reference is its reference: a late cancel, or the
    // answer of the client to 3746, changes nothing (research K7).
    const bool ended = m_world.interactions().client_cancel(m_id, cancel->reference);
    const world::WorldClock::time_point now = m_clock.now();
    std::erase_if(m_seenRequests, [&](const auto& seen) { return now >= seen.second; });
    const bool late = !ended && std::any_of(m_seenRequests.begin(), m_seenRequests.end(),
                                            [&](const auto& seen) { return seen.first == cancel->reference; });
    // One cancel for each attempt (code review of stage 3.4f-B, finding CS-R3): the cancel that ended the use, or the late cancel (the
    // echo of a use that the server ended), takes the request of the attempt. A cancel of a next attempt of the same reference then finds
    // no request: it is early, and its mark stops the delayed press of that attempt.
    if (ended || late) {
        std::erase_if(m_seenRequests, [&](const auto& seen) { return seen.first == cancel->reference; });
    }
    if (!ended && !late && m_messageDatagram) {
        // A cancel with no use, and no request of its reference in the last kEarlyCancelKeep: its press can still come in an earlier
        // datagram of a different channel (finding ST-R2), or in a later report that repeats the old entry (finding CR-R4). One mark for
        // each reference (finding CR-R3). A cancel of a reference that the session took is late (an echo of a use that ended): no mark.
        EarlyCancel mark{cancel->reference, *m_messageDatagram, 0, false, now + kEarlyCancelKeep};
        if (const std::optional<std::uint16_t> client = m_attack.estimate(now)) {
            mark.step = static_cast<std::uint16_t>(*client + kEarlyCancelSteps);
            mark.hasStep = true;
        }
        std::erase_if(m_earlyCancels, [&](const EarlyCancel& old) { return old.reference == cancel->reference || now >= old.ends; });
        if (m_earlyCancels.size() >= kEarlyCancelMost) {
            m_earlyCancels.erase(m_earlyCancels.begin());
        }
        m_earlyCancels.push_back(mark);
    }
    if (ended) {
        // The attempt of this block ended: a later input step with the same block is a new request (code review of stage 2.5a,
        // finding INT-R2: a new press of the same object with no report of an absent block between). The input numbers of the
        // steps that the session processed stay: an old report changes nothing.
        m_interactionBlock.reset();
    }
    log::write("rep: the client ended its interaction (type %u) with the object %016llx target %016llx: %s",
               kTypeCancelCommittedInteraction, static_cast<unsigned long long>(cancel->reference.group),
               static_cast<unsigned long long>(cancel->reference.target),
               ended ? "the interaction ended" : "not the interaction of the character, no change");
}

void RepSession::end_input_lock(const char* cause) {
    // The order of the client: the cancel ends a conversation, whose cutscene then sends its
    // end (the session answers it), and the answer of the cutscene frees the input at once.
    send_message(kTypeCancelCurrentInteraction, cancel_interaction_body(m_observer.own()));
    send_message(kTypeCutsceneEndFinalized, cutscene_finalized_body(m_observer.own()));
    log::write("rep: %s ends each lock of the input of the client: messages \"cancel current interaction\" (type %u) and "
               "\"cutscene end request finalized\" (type %u) sent",
               cause,
               kTypeCancelCurrentInteraction,
               kTypeCutsceneEndFinalized);
}

void RepSession::development_message(std::uint32_t type, const std::vector<std::uint8_t>& body) {
    // The call comes from the world, in a task of a command.
    const log::Context context(m_logMark);
    if (!m_inWorld || m_closing) {
        return;
    }
    send_message(type, body);
    log::write("rep: DEVELOPMENT COMMAND: message of the command sent: type %u, %zu bytes", type, body.size());
}

void RepSession::report_position(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own) {
    const world::PositionReport report{input.sequence,
                                       state::WorldPosition{input.characterX, input.characterY, input.characterZ},
                                       input.characterHeight, input.hasLook ? input.lookYaw : m_input.lookYaw};
    // The world finds the character from the session, checks the report, and keeps the
    // position in the entity. A report that it refuses changes nothing.
    const world::PositionVerdict verdict = m_world.report_position(m_id, report, m_track);
    m_reportSign = now;
    if (m_positionSource != PositionSource::report) {
        set_position_source(PositionSource::report, input);
    }
    if (verdict == world::PositionVerdict::accepted) {
        // The first accepted report after a travel is near the new place: the client confirmed it.
        m_travelAnchor.reset();
    }
    if (verdict == world::PositionVerdict::accepted || verdict == world::PositionVerdict::noEntity) {
        return;
    }
    const auto cause = static_cast<std::size_t>(verdict);
    if (verdict == world::PositionVerdict::beforeTravel) {
        // The client made the report before it got the new position of a travel: not a fault.
        if (!m_verdictLogged[cause]) {
            m_verdictLogged[cause] = true;
            log::write("rep: input %u has the position %.3f %.3f %.3f, which is not near the place of the travel %.3f "
                       "%.3f %.3f: not used (the first report of the session with this cause)",
                       static_cast<unsigned>(input.sequence),
                       report.position.x,
                       report.position.y,
                       report.position.z,
                       own.position.x,
                       own.position.y,
                       own.position.z);
        }
        return;
    }
    if (verdict == world::PositionVerdict::outOfSequence) {
        if (!m_verdictLogged[cause]) {
            m_verdictLogged[cause] = true;
            log::write("rep: input %u is not newer than the input before it: its position is not used (the first report "
                       "of the session with this cause)",
                       static_cast<unsigned>(input.sequence));
        }
        return;
    }
    if (!m_verdictLogged[cause]) {
        m_verdictLogged[cause] = true;
        m_lastRefusalLog = now;
        log::write("rep: position report refused (%s): input %u, position %.3f %.3f %.3f, height value 0x%04x; the "
                   "entity keeps its position %.3f %.3f %.3f (the first report of the session with this cause)",
                   world::position_verdict_text(verdict),
                   static_cast<unsigned>(input.sequence),
                   report.position.x,
                   report.position.y,
                   report.position.z,
                   static_cast<unsigned>(report.height),
                   own.position.x,
                   own.position.y,
                   own.position.z);
    } else if (now - m_lastRefusalLog >= kRefusalLogInterval) {
        m_lastRefusalLog = now;
        log::write("rep: position reports refused: %llu (%llu too fast, %llu not finite, %llu at a height limit, %llu "
                   "outside the world); %llu accepted",
                   static_cast<unsigned long long>(m_track.refused()),
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::tooFast)),
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::notFinite)),
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::heightLimit)),
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::outsideWorld)),
                   static_cast<unsigned long long>(m_track.count(world::PositionVerdict::accepted)));
    }
}

bool RepSession::use_camera_estimate(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own) {
    if (!m_reportSign && (input.flags & kInputFlagPosition) != 0) {
        // The client sends its position, and this entry has none: wait for the field.
        m_reportSign = now;
    }
    if (m_reportSign && now - *m_reportSign < kReportTimeout) {
        return false; // The entity keeps its last position.
    }
    if (m_positionSource != PositionSource::camera) {
        const bool exact = m_positionSource == PositionSource::report;
        set_position_source(PositionSource::camera, input);
        if (exact) {
            resume_camera_estimate(input, now, own);
        }
    }
    return true;
}

void RepSession::set_position_source(PositionSource source, const ClientInput& input) {
    const bool first = m_positionSource == PositionSource::none;
    m_positionSource = source;
    if (++m_sourceChanges > kLoggedSourceChanges) {
        return;
    }
    const char* cause = "the input report has the position of the character";
    if (source == PositionSource::camera) {
        cause = (input.flags & kInputFlagPosition) == 0
                    ? "the input report has no position flag: a client with no position report of the game hook"
                    : input.tail != InputTail::read ? "the reader of the input report stops before the position field"
                                                    : "the input reports have the position flag and no position field";
    }
    log::write("rep: position source%s: %s (input %u, flags 0x%llx: %s)%s",
               first ? "" : " changed",
               source == PositionSource::report ? "the position field of the input reports, the exact position"
                                                : "the camera estimate",
               static_cast<unsigned>(input.sequence),
               static_cast<unsigned long long>(input.flags),
               cause,
               m_sourceChanges == kLoggedSourceChanges ? "; no more lines for a change of the source in this session" : "");
}

void RepSession::resume_camera_estimate(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own) {
    // The position of the entity is the last position that the client sent. The camera is at
    // some distance behind it: that distance is the start value of the estimate.
    m_followCamera = false;
    m_wheelStartBehind.reset();
    m_restCamera = state::WorldPosition{input.cameraX, input.cameraY, input.cameraZ};
    m_cameraRestStart = now;
    m_cameraDistance = kCameraBehind;
    const AxisOffset offset = axis_offset(input, camera_frame(input), own.position);
    m_knownBehind = offset.behind && offset.aside <= kAxisTolerance ? std::clamp(*offset.behind, 0.0F, kMaxCameraDistance)
                                                                    : kCameraBehind;
}

void RepSession::handle_drop_items(std::span<const std::uint8_t> body) {
    // The character of a request is the character of the session, not a value of the body.
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_drop_request(body);
    if (!request) {
        log::write("rep: drop request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeDropInventoryItems,
                   body.size());
        return;
    }
    if (request->call.entity != own->id) {
        log::write("rep: drop request (type %u) names the entity %016llx, which is not the entity of the session: no "
                   "change",
                   kTypeDropInventoryItems,
                   static_cast<unsigned long long>(request->call.entity));
        return;
    }
    const world::ItemResult result = m_world.inventory().drop(own->characterId, request->entries);
    log::write("rep: drop request (type %u, %zu entr%s): %s",
               kTypeDropInventoryItems,
               request->entries.size(),
               request->entries.size() == 1 ? "y" : "ies",
               result.ok() ? "done" : world::item_error_text(result.error));
}

void RepSession::handle_set_lock_state(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_lock_request(body);
    if (!request) {
        log::write("rep: lock request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeSetLockState,
                   body.size());
        return;
    }
    if (request->call.entity != own->id) {
        log::write("rep: lock request (type %u) names the entity %016llx, which is not the entity of the session: no "
                   "change",
                   kTypeSetLockState,
                   static_cast<unsigned long long>(request->call.entity));
        return;
    }
    if (request->containerType != kItemContainerBag) {
        // The bag is the only container that the backend has.
        log::write("rep: lock request (type %u) for container type %u: the backend has only the bag: no change",
                   kTypeSetLockState,
                   request->containerType);
        return;
    }
    const world::ItemResult result =
        m_world.inventory().set_locked(own->characterId, request->slot, &request->instance, request->locked);
    log::write("rep: lock request (type %u, slot %u, %s): %s",
               kTypeSetLockState,
               request->slot,
               request->locked ? "lock" : "unlock",
               result.ok() ? "done" : world::item_error_text(result.error));
}

std::optional<ComponentCall> RepSession::equipment_call(const world::Entity& own,
                                                        std::uint32_t type,
                                                        const char* name,
                                                        std::span<const std::uint8_t> body) {
    const auto call = read_component_call(body);
    if (!call) {
        log::write("rep: %s request (type %u, %zu bytes) does not have the form of the request: no change", name, type, body.size());
        return std::nullopt;
    }
    // The character of a request is the character of the session, not a value of the body.
    // A reply goes to the facet of the own entity only.
    if (call->entity != own.id) {
        log::write("rep: %s request (type %u) names the entity %016llx, which is not the entity of the session: no "
                   "change",
                   name,
                   type,
                   static_cast<unsigned long long>(call->entity));
        return std::nullopt;
    }
    if (m_options.research.refuseEquipment) {
        log::write("rep: RESEARCH SWITCH: %s request (type %u) refused with no check: no change, message \"equip item "
                   "fail\" (type %u) sent",
                   name,
                   type,
                   kTypeEquipItemFail);
        send_equip_fail(*call);
        return std::nullopt;
    }
    return call;
}

void RepSession::send_equip_fail(const ComponentCall& call) {
    send_message(kTypeEquipItemFail, equip_fail_body(call));
}

void RepSession::handle_use_item(std::span<const std::uint8_t> body) {
    // ContainerComponentServerFacet_ServerUseItem (type 2917, stage 1.3b): the target, the entity, and the bag slot (u32;
    // live check I7: 20 bytes from the entry "Use" of the action menu of an item in the bag).
    const world::Entity* own = m_world.find(m_observer.own());
    const auto call = read_component_call(body);
    hub::Reader reader(body);
    reader.u64();
    reader.u64();
    const std::uint32_t slot = reader.u32();
    if (!reader.ok() || !call) {
        log::write("rep: use item request (type %u, %zu bytes) does not have the form of the request: no change", kTypeUseItem, body.size());
        return;
    }
    // The character of a request is the character of the session (code review of stage 1.3b, finding F6): a request that
    // names a different entity changes nothing, as the other item requests.
    if (own == nullptr || call->entity != own->id) {
        log::write("rep: use item request (type %u) names the entity %016llx, which is not the entity of the session: no change",
                   kTypeUseItem,
                   static_cast<unsigned long long>(call->entity));
        return;
    }
    const world::UseVerdict verdict = m_world.consumables().use_bag(m_id, slot);
    log::write("rep: use item request (type %u, bag slot %u): %s", kTypeUseItem, slot, world::use_verdict_text(verdict));
}

void RepSession::handle_equip_item(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto call = equipment_call(*own, kTypeEquipItem, "equip", body);
    if (!call) {
        return;
    }
    const auto request = read_equip_request(body);
    if (!request) {
        log::write("rep: equip request (type %u, %zu bytes) does not have the form of the request: no change, message "
                   "\"equip item fail\" (type %u) sent",
                   kTypeEquipItem,
                   body.size(),
                   kTypeEquipItemFail);
        send_equip_fail(*call);
        return;
    }
    world::ItemResult result;
    if (request->bagSlot < 0) {
        result.error = world::ItemError::badSlot; // "No slot" is not the place of an item.
    } else {
        result = m_world.inventory().equip(own->characterId,
                                           static_cast<std::uint32_t>(request->bagSlot),
                                           request->paperdollSlot,
                                           &request->instance,
                                           request->quantity);
    }
    log::write("rep: equip request (type %u, bag slot %d, paperdoll slot %u): %s%s",
               kTypeEquipItem,
               request->bagSlot,
               request->paperdollSlot,
               result.ok() ? "done" : world::item_error_text(result.error),
               result.ok() ? "" : ": message \"equip item fail\" sent");
    if (!result.ok()) {
        send_equip_fail(*call);
    }
}

void RepSession::handle_unequip_item(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto call = equipment_call(*own, kTypeUnequipItem, "unequip", body);
    if (!call) {
        return;
    }
    const auto request = read_unequip_request(body);
    if (!request) {
        log::write("rep: unequip request (type %u, %zu bytes) does not have the form of the request: no change, "
                   "message \"equip item fail\" (type %u) sent",
                   kTypeUnequipItem,
                   body.size(),
                   kTypeEquipItemFail);
        send_equip_fail(*call);
        return;
    }
    // The bag slot -1 is "the first free slot".
    const std::optional<std::uint32_t> bagSlot =
        request->bagSlot < 0 ? std::nullopt : std::optional(static_cast<std::uint32_t>(request->bagSlot));
    const world::ItemResult result =
        m_world.inventory().unequip(own->characterId, request->paperdollSlot, bagSlot, request->quantity);
    log::write("rep: unequip request (type %u, paperdoll slot %u, bag slot %d): %s%s",
               kTypeUnequipItem,
               request->paperdollSlot,
               request->bagSlot,
               result.ok() ? "done" : world::item_error_text(result.error),
               result.ok() ? "" : ": message \"equip item fail\" sent");
    if (!result.ok()) {
        send_equip_fail(*call);
    }
}

void RepSession::handle_swap_items(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto call = equipment_call(*own, kTypeSwapItems, "swap", body);
    if (!call) {
        return;
    }
    const auto request = read_swap_request(body);
    if (!request) {
        log::write("rep: swap request (type %u, %zu bytes) does not have the form of the request: no change, message "
                   "\"equip item fail\" (type %u) sent",
                   kTypeSwapItems,
                   body.size(),
                   kTypeEquipItemFail);
        send_equip_fail(*call);
        return;
    }
    const world::ItemResult result = m_world.inventory().swap_equipped(own->characterId,
                                                                       request->first,
                                                                       request->second,
                                                                       &request->firstInstance,
                                                                       &request->secondInstance);
    log::write("rep: swap request (type %u, paperdoll slots %u and %u): %s%s",
               kTypeSwapItems,
               request->first,
               request->second,
               result.ok() ? "done" : world::item_error_text(result.error),
               result.ok() ? "" : ": message \"equip item fail\" sent");
    if (!result.ok()) {
        send_equip_fail(*call);
    }
}

bool RepSession::own_call(const world::Entity& own, const ComponentCall& call, std::uint32_t type, const char* name) {
    // The character of a request is the character of the session, not a value of the body.
    if (call.entity == own.id) {
        return true;
    }
    log::write("rep: %s request (type %u) names the entity %016llx, which is not the entity of the session: no change",
               name,
               type,
               static_cast<unsigned long long>(call.entity));
    return false;
}

void RepSession::handle_spend_attribute_points(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_spend_request(body);
    if (!request) {
        log::write("rep: attribute spend request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeSpendAttributePoints,
                   body.size());
        return;
    }
    if (!own_call(*own, request->call, kTypeSpendAttributePoints, "attribute spend")) {
        return;
    }
    static constexpr const char* kNames[] = {"constitution", "focus", "strength", "dexterity", "intelligence"};
    std::string text;
    for (const world::AttributeSpend& entry : request->entries) {
        text += (text.empty() ? "" : ", ") + std::string(kNames[entry.type]) + " " + std::to_string(entry.points);
    }
    const world::ProgressResult result = m_world.progress().spend(own->characterId, request->entries);
    log::write("rep: attribute spend request (type %u, %s): %s, %u free point(s)",
               kTypeSpendAttributePoints,
               text.empty() ? "no entry" : text.c_str(),
               result.ok() ? "done" : world::progress_error_text(result.error),
               result.unspent);
}

void RepSession::handle_refund_attribute_points(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto call = read_plain_call(body);
    if (!call) {
        log::write("rep: attribute reset request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeRefundAttributePoints,
                   body.size());
        return;
    }
    if (!own_call(*own, *call, kTypeRefundAttributePoints, "attribute reset")) {
        return;
    }
    const world::ProgressResult result = m_world.progress().reset_attributes(own->characterId);
    log::write("rep: attribute reset request (type %u): %s, %u free point(s)",
               kTypeRefundAttributePoints,
               result.ok() ? "done" : world::progress_error_text(result.error),
               result.unspent);
}

void RepSession::handle_assign_pool_points(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_assign_request(body);
    if (!request) {
        log::write("rep: mastery point request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeAssignPoolPoints,
                   body.size());
        return;
    }
    if (!own_call(*own, request->call, kTypeAssignPoolPoints, "mastery point")) {
        return;
    }
    const world::SkillResult result =
        m_world.skills().assign(own->characterId, request->pool, request->node, request->count);
    log::write("rep: mastery point request (type %u, pool %08X, node %08X, %u point(s)): %s, %u free point(s)",
               kTypeAssignPoolPoints,
               request->pool,
               request->node,
               request->count,
               result.ok() ? "done" : world::progress_error_text(result.error),
               own->skills.free_points(request->pool));
}

void RepSession::handle_apply_ability_points(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto call = read_plain_call(body);
    if (!call) {
        log::write("rep: apply ability points request (type %u, %zu bytes) does not have the form of the request",
                   kTypeApplyAbilityPoints,
                   body.size());
        return;
    }
    if (!own_call(*own, *call, kTypeApplyAbilityPoints, "apply ability points")) {
        return;
    }
    // The end of a selection in a weapon tree. Each point of the selection came in its own
    // request: the backend has nothing to change here.
    log::write("rep: apply ability points request (type %u): the points of the selection are in the tree",
               kTypeApplyAbilityPoints);
}

void RepSession::handle_change_mapped_ability(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_bar_request(body);
    if (!request) {
        log::write("rep: ability bar request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeChangeMappedAbility,
                   body.size());
        return;
    }
    if (!own_call(*own, request->call, kTypeChangeMappedAbility, "ability bar")) {
        return;
    }
    // The table index is the place of the weapon in the list of the client.
    const auto table = ability_table(request->tableIndex);
    world::SkillResult result;
    if (!table) {
        result.error = world::ProgressError::unknownProgression;
    } else {
        result = m_world.skills().set_bar(own->characterId, *table, request->slot, request->ability);
    }
    log::write("rep: ability bar request (type %u, table index %u, place %u, ability %08X): %s",
               kTypeChangeMappedAbility,
               request->tableIndex,
               request->slot,
               request->ability,
               result.ok() ? "done" : world::progress_error_text(result.error));
}

void RepSession::handle_mastery_tree_respec(std::span<const std::uint8_t> body) {
    const world::Entity* own = m_world.find(m_observer.own());
    if (own == nullptr) {
        return;
    }
    const auto request = read_tree_reset_request(body);
    if (!request) {
        log::write("rep: tree reset request (type %u, %zu bytes) does not have the form of the request: no change",
                   kTypeMasteryTreeRespec,
                   body.size());
        return;
    }
    if (!own_call(*own, request->call, kTypeMasteryTreeRespec, "tree reset")) {
        return;
    }
    const auto table = ability_table(request->tableIndex);
    world::SkillResult result;
    if (!table) {
        result.error = world::ProgressError::unknownProgression;
    } else {
        result = m_world.skills().reset_tree(own->characterId, *table);
    }
    // The client waits for this answer: without it, it sends no second reset. The states of
    // the tree and of the bar follow in the next bundle.
    send_message(kTypeMasteryTreeRespecResponse, tree_reset_reply_body(request->call, result.ok(), request->tableIndex));
    log::write("rep: tree reset request (type %u, table index %u): %s: message \"mastery tree respec response\" (type "
               "%u, success %u) sent",
               kTypeMasteryTreeRespec,
               request->tableIndex,
               result.ok() ? "done" : world::progress_error_text(result.error),
               kTypeMasteryTreeRespecResponse,
               result.ok() ? 1U : 0U);
}

void RepSession::track_position(const ClientInput& input, carrier::Clock::time_point now, world::Entity& own) {
    const state::WorldPosition camera{input.cameraX, input.cameraY, input.cameraZ};
    const bool cameraMoves = std::hypot(camera.x - m_restCamera.x, camera.y - m_restCamera.y, camera.z - m_restCamera.z)
                             > kCameraRestDistance;
    if (cameraMoves) {
        m_restCamera = camera;
        m_cameraRestStart = now;
    }
    const CameraFrame frame = camera_frame(input);
    const AxisOffset offset = axis_offset(input, frame, own.position);
    if (input.wheel) {
        m_lastWheel = now;
        if (m_followCamera || !offset.behind) {
            // The change of the distance does not show while the character moves.
            forget_camera_distance();
        } else if (!m_wheelStartBehind) {
            m_wheelStartBehind = offset.behind;
        }
    }
    if (!m_followCamera) {
        // The spawn position is exact. While the camera axis goes through the known position,
        // the character did not move: only the camera turned, or its distance changed. The
        // position then stays. When the axis leaves the position, the character moved. A walk
        // straight forward keeps the position on the axis, thus the forward key with a camera
        // that moves is also a movement, but only when no obstacle is behind the camera: an
        // obstacle holds the camera while the character goes away from it.
        const bool wheelActive = m_wheelStartBehind.has_value();
        const bool onAxis = offset.aside <= kAxisTolerance && offset.along >= -kAxisTolerance
                            && (wheelActive || offset.along <= m_knownBehind + kRearWalkDistance);
        const bool freeCamera = offset.behind && *offset.behind >= m_knownBehind - kFreeCameraTolerance;
        if (!onAxis || (input.forward && cameraMoves && (wheelActive || freeCamera))) {
            m_followCamera = true;
            if (wheelActive) {
                forget_camera_distance();
            }
        } else if (wheelActive && now - m_lastWheel >= kWheelSettleTime) {
            if (offset.behind) {
                apply_wheel(*offset.behind);
            } else {
                forget_camera_distance();
            }
        }
    }
    if (m_followCamera) {
        // The position follows the estimate until the camera is at rest again.
        m_cameraDistance = std::max(m_cameraDistance, kMovingCameraDistance);
        const float behind = std::min(m_cameraDistance, kCameraBehind);
        m_knownBehind = behind;
        m_followCamera = now - m_cameraRestStart < kCameraRestTime;
        // The position is world state: the world keeps it, puts it in its position index, and
        // saves it.
        m_world.set_position(own.id, axis_point(input, frame, behind), m_followCamera);
    }
}

void RepSession::apply_wheel(float behind) {
    const float change = behind - *m_wheelStartBehind;
    m_wheelStartBehind.reset();
    m_cameraDistance = std::clamp(m_cameraDistance + change, kMinCameraDistance, kMaxCameraDistance);
    m_knownBehind = std::max(m_knownBehind + change, 0.0F);
    log::write("rep: camera distance %.2f after a wheel input (change %+.2f)", m_cameraDistance, change);
}

void RepSession::forget_camera_distance() {
    m_wheelStartBehind.reset();
    m_cameraDistance = kCameraBehind;
}

void RepSession::send_message(std::uint32_t type, std::span<const std::uint8_t> body) {
    // A frame to the client is a size and the envelope. It has no checksum and no route
    // (client frame parser at image RVA 0x6AE44F0).
    hub::Writer envelope;
    envelope.u8(0); // Envelope flags: no target, no source, no extension.
    envelope.u8(1); // A message is present.
    envelope.size(type);
    envelope.raw(body);
    hub::Writer frame;
    frame.size(static_cast<std::uint32_t>(envelope.data().size()));
    frame.raw(envelope.data());
    m_send(0, frame.data(), carrier::Reliability::reliable);
}

void RepSession::send_registration_reply(std::int32_t result) {
    // RegistrationResponseMsg, client reader at image RVA 0x7CD040:
    //   result:u32 serverTime:u64 text text flag flag flag flag
    hub::Writer reply;
    reply.u32(static_cast<std::uint32_t>(result));
    reply.u64(m_clock.server_time_ns());
    reply.string(m_options.serverVersion);
    reply.string("");
    reply.boolean(false);
    reply.boolean(false);
    reply.boolean(false);
    reply.boolean(false); // The local server does not require the anti-cheat service.
    send_message(kTypeRegistrationResponse, reply.data());
}

void RepSession::handle_registration(std::uint32_t type, std::span<const std::uint8_t> body) {
    if (m_registered) {
        return;
    }
    m_registered = true;
    const Registration request = read_registration(body);
    if (!request.characterId.empty()) {
        m_characterId = request.characterId;
        m_logMark = log::session_mark(m_id, m_characterId);
    }
    const ResearchSwitch& research = m_options.research;
    if (research.registrationResult && *research.registrationResult != 0) {
        // A message after a reply with a result that is not 0 is not correct: the client ends
        // the connection itself (client code at image RVA 0x6B6F190).
        send_registration_reply(*research.registrationResult);
        log::write("rep: RESEARCH SWITCH: registration request (type %u, %zu bytes), reply with result %d and no "
                   "other message",
                   type,
                   body.size(),
                   *research.registrationResult);
        return;
    }
    if (request.parsed) {
        check_identity(request);
    }
    // The world has the character of a session. It refuses a character id that the store
    // does not have, and a character of a different account.
    const world::EnterResult entered = m_world.enter(m_id, *this, request.characterId, request.personaId);
    if (entered.error != world::EnterError::none) {
        // A refusal is the reply alone: a different message before a reply with result 0
        // ends the connection of the client with a different reason. The client shows a text
        // and closes its connection.
        send_registration_reply(kRegistrationRefused);
        m_closing = Closing{CloseCause::refused, m_clock.now()};
        log::write("rep: registration request (type %u, %zu bytes) refused (%s): reply with result %d and no other "
                   "message",
                   type,
                   body.size(),
                   entered.error == world::EnterError::unknownCharacter ? "the store does not have the character"
                                                                        : "the character is of a different account",
                   kRegistrationRefused);
        return;
    }
    const world::Entity& entity = *entered.entity;
    m_observer.set_own(entity.id);
    m_sessionActor = m_world.next_session_actor();
    const state::WorldPosition spawn = world::spawn_position(entity);
    log::write("rep: character %s (%s), spawn %.2f %.2f %.2f, actor %s",
               entity.characterId.c_str(),
               entity.name.c_str(),
               spawn.x,
               spawn.y,
               spawn.z,
               world::actor_text(entity.actor).c_str());
    send_registration_reply(0);
    log::write("rep: registration request (type %u, %zu bytes) accepted", type, body.size());
    if (research.rejectReason) {
        // PlayerManagerRejectedMsg, client reader at image RVA 0x14FF630: reason:u32 text.
        hub::Writer rejected;
        rejected.u32(*research.rejectReason);
        rejected.string("");
        send_message(kTypePlayerManagerRejected, rejected.data());
        log::write("rep: RESEARCH SWITCH: message \"rejected\" (type %u) with reason %u sent in place of the self "
                   "identification",
                   kTypePlayerManagerRejected,
                   *research.rejectReason);
        return;
    }
    send_self_identification();
    send_level_information();
    // ClientViewListenerTrait::ReceivePlayerSpawnPointMsg has no fields.
    send_message(kTypeReceivePlayerSpawnPoint, {});
    log::write("rep: spawn point notice sent");
    // The first state bundle makes the client load the level of the level information.
    send_state_bundle({});
    m_inWorld = true;
    // The first record of the own character has the metadata state in slot 0 and the state of
    // the player component. The client compares the character id of the player component with
    // the character id of its login. Only an equal id makes the entity the local player
    // (client code at image RVA 0x6929DF0).
    replicate(m_clock.now());
    // A character that a different session left dead: this client gets the death notice too
    // (its first vitals state has the dead mark), or it has no way to the respawn screen.
    if (const world::Entity* own = m_world.find(m_observer.own())) {
        m_world.health().entered(*own);
    }
}

void RepSession::send_research_kick(carrier::Clock::time_point now) {
    if (now < *m_researchKickAt) {
        return;
    }
    m_researchKickAt.reset();
    send_kick(*m_options.research.kickReason);
    log::write("rep: RESEARCH SWITCH: message \"kicked\" (type %u) with reason %u sent",
               kTypeKickClient,
               *m_options.research.kickReason);
}

RepSession::Registration RepSession::read_registration(std::span<const std::uint8_t> body) {
    // RegistrationRequestV3Msg, client writer at image RVA 0x7CE860 and 0x7D54A0:
    //   u32, version map {key:u32 text}*, text, login token, client data, ...
    //   login token := signature address worldId characterId personaId steamAppId:u32
    //                  steamUserId channelId ticketId text text issueTime:u32 generateTime:u32
    //                  accountAge:u32 text tokenVersion:u32 u8 text present:u8 [flag:u8]
    //                  text text text                     (writers 0x7D5F30 to 0x7D42C0)
    //   client data := u32 u8 steamAppId:u32 steamId ticket ...     (writer 0x7D54A0)
    // Each field with no type is a text. The ticket is "steam|" and the hexadecimal text
    // of the ticket bytes.
    Registration request;
    hub::Reader reader(body);
    reader.u32();
    const std::uint32_t versionParts = reader.size();
    for (std::uint32_t index = 0; index < versionParts && reader.ok(); ++index) {
        reader.u32();
        reader.string();
    }
    reader.string();
    reader.string(); // Token signature.
    reader.string(); // Game server address.
    reader.string(); // World id.
    const std::string characterId = reader.string();
    if (!reader.ok() || characterId.empty()) {
        log::write("rep: the registration request has no character id");
        return request;
    }
    request.characterId = characterId;
    request.personaId = reader.string();
    reader.u32(); // Steam application id of the token.
    request.tokenSteamId = reader.string();
    reader.string(); // Channel id.
    reader.string(); // Ticket id.
    reader.string();
    reader.string();
    reader.u32(); // Issue time.
    reader.u32(); // Generate time.
    reader.u32(); // Account age.
    reader.string();
    reader.u32(); // Token version.
    reader.u8();
    reader.string();
    if (reader.u8() != 0) {
        reader.u8();
    }
    reader.string();
    reader.string();
    reader.string();
    reader.u32();
    reader.u8();
    reader.u32(); // Steam application id of the client.
    request.clientSteamId = reader.string();
    request.ticket = reader.string();
    if (!reader.ok()) {
        log::write("rep: the identity fields of the registration request did not parse");
        return request;
    }
    request.parsed = true;
    return request;
}

void RepSession::check_identity(const Registration& request) {
    // Stub authentication: the backend reports a Steam id that does not agree, and it does
    // not refuse the registration for it.
    const auto account = m_world.account_of(request.personaId);
    const auto ticketIdentity = steam::read_local_ticket(request.ticket);
    const std::string accountSteamId = account ? account->steamId : std::string{};
    const std::string ticketSteamId = ticketIdentity ? ticketIdentity->steamId : std::string{};
    log::write("rep: identity of persona %s: Steam id of the account %s, of the login token %s, "
               "of the client %s, of the ticket %s",
               request.personaId.c_str(),
               state::masked_steam_id(accountSteamId).c_str(),
               state::masked_steam_id(request.tokenSteamId).c_str(),
               state::masked_steam_id(request.clientSteamId).c_str(),
               state::masked_steam_id(ticketSteamId).c_str());
    if (!account) {
        log::write("rep: persona %s has no account", request.personaId.c_str());
    } else if (request.tokenSteamId != accountSteamId || request.clientSteamId != accountSteamId
               || ticketSteamId != accountSteamId) {
        log::write("rep: the Steam ids of the registration request are not equal to the Steam id of the account");
    } else {
        log::write("rep: the Steam ids of the registration request are equal");
    }
}

void RepSession::send_self_identification() {
    // PlayerManagerSelfIdentificationMsg, client reader at image RVA 0x14FD530:
    //   identity, actor reference x3, level block, identity
    //   level block := instance:u32 list<u32> flag flag u32 u32
    // Reference 1 is the address of the client messages that are not for an entity: the
    // player manager of the world. Reference 2 and the two identities are the client actor of
    // this session. Reference 3 is null: an actor id there makes the client start its bot
    // spawn and send type 1665 (client handler at image RVA 0x6454C00).
    hub::Writer body;
    write_identity(body, m_sessionActor);
    write_actor_reference(body, kWorldHub, m_world.player_manager(), kPlayerManagerType);
    write_actor_reference(body, kConnectionHub, m_sessionActor, kClientProxyType);
    body.u32(0);
    body.zeros(32);
    body.u32(m_contextInstance);
    body.size(0);
    body.boolean(false);
    body.boolean(false);
    body.u32(0);
    body.u32(0);
    write_identity(body, m_sessionActor);
    send_message(kTypePlayerManagerSelfIdentification, body.data());
    log::write("rep: self identification sent (session actor %s)", world::actor_text(m_sessionActor).c_str());
}

void RepSession::send_level_information() {
    // LevelInfoChangedMsg, client writer at image RVA 0x1500850:
    //   level:text terrain:text pair:u32,u32 pair:u32,u32 u64 list{u8,u32,u64,u32}
    //   flag instance:u8 reload:flag flag version:u64
    hub::Writer body;
    body.string(kLevelName);
    body.string(kTerrainProject);
    body.u32(0);
    body.u32(0);
    body.u32(0);
    body.u32(0);
    body.u64(0);
    body.size(0);
    body.boolean(false);
    body.u8(m_contextInstance);
    body.boolean(true);
    body.boolean(false);
    // The client does nothing for a level information whose version is the version of the
    // last one that it got, also when that one came on an earlier connection (client code at
    // image RVA 0x6446800). The session id is different for each connection.
    body.u64(m_id);
    send_message(kTypeLevelInfoChanged, body.data());
    log::write("rep: level information sent (%s)", kLevelName);
}

void RepSession::send_state_bundle(std::span<const std::uint8_t> payload, std::span<const std::uint32_t> stops) {
    // ReplicatedStateBundle, client reader at image RVA 0x6B07870:
    //   present:u8 [sequence:size64] instance:u8 u8 unreliable:flag control:flag
    //   [control block when the second flag is set] size payload
    //   control block := stopCount:size count:size (interest:u16)*count      (reader 0x6A6DDB0)
    // The first `stopCount` numbers are "stop", the others are "pause". The backend writes
    // the two counts from one list: the client makes no check of `stopCount`.
    hub::Writer body;
    body.u8(1);
    body.size64(++m_bundleSequence);
    body.u8(m_contextInstance);
    body.u8(0);
    body.boolean(false);
    body.boolean(!stops.empty());
    if (!stops.empty()) {
        body.size(static_cast<std::uint32_t>(stops.size()));
        body.size(static_cast<std::uint32_t>(stops.size()));
        for (const std::uint32_t interest : stops) {
            body.u16(static_cast<std::uint16_t>(interest));
        }
    }
    body.size(static_cast<std::uint32_t>(payload.size()));
    body.raw(payload);
    send_message(kTypeReplicatedStateBundle, body.data());
}

} // namespace soulwarden::backend::server::rep

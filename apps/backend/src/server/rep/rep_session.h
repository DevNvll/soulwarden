#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "gamedata/slice_ids.h"
#include "server/carrier/carrier.h"
#include "server/rep/client_input.h"
#include "server/rep/health_messages.h"
#include "server/rep/item_requests.h"
#include "server/rep/observer.h"
#include "server/rep/progress_requests.h"
#include "server/rep/research.h"
#include "server/rep/states/state_codecs.h"
#include "world/world.h"

// Game server entry point ("REP") session for one client.
//
// A session is one observer of the world and the owner of one character (roadmap, rules MP-1
// and MP-2). It has transport state (the bundle sequence, the client context instance), the
// values of the input of its client (the newest input number, the values for the check of
// its position reports and of its hit claims, and the state of the camera estimate), and the
// table of the entities that its client knows. It has no state of a character: the name, the position, the
// appearance, and the vitals are in the entity of the world, and the session reads them there.
//
// The transport is a byte stream on carrier channel 0 (reliable) and channel 1 (unreliable).
// The two directions use different frames:
//   client to server := crc32:u32 size:u32 payload, payload := target:16 bytes envelope
//                       (client writer at image RVA 0x6B0F430; the checksum covers the payload)
//   server to client := size envelope
//                       (client parser at image RVA 0x6AE44F0; "size" is the variable-length form)
//   envelope := flags:u8 [target:8 if bit 0] [source:8 if bit 1] [extension if bit 2] message
//   message  := present:u8 type:size body        (type is an index into typeindex.json;
//                                                  index 0 is followed by a 16-byte type id)
// Reader: client image RVA 0x61AC8D0 (envelope), 0x17B2430 (message), 0x61ACFE0 (type).

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeRegistrationRequest = 1;   // 8673a3cc-2848-4c87-aa72-cc860589d1b5
inline constexpr std::uint32_t kTypeRegistrationRequestV2 = 2; // da4e5889-a65c-4480-8642-0278160125a7
inline constexpr std::uint32_t kTypeRegistrationResponse = 3;  // 104145a7-ff95-44f1-9468-21fb41c8ac2b
inline constexpr std::uint32_t kTypeRegistrationRequestV3 = 19; // 0b826b33-89f5-49e0-b8cb-fe4433427778
inline constexpr std::uint32_t kTypePlayerManagerSelfIdentification = 1628; // 169443e0-a508-4653-b437-49b7a195f69c
inline constexpr std::uint32_t kTypeReceivePlayerSpawnPoint = 1617; // Javelin::ClientViewListenerTrait
inline constexpr std::uint32_t kTypeWaitingForGhostClient = 1626;   // Javelin::ClientViewListenerTrait
inline constexpr std::uint32_t kTypeWaitingForPlayerSpawnPoint = 1164; // Javelin::ClientViewListenerTrait
inline constexpr std::uint32_t kTypeLevelInfoChanged = 1635;   // 45918d03-02e7-42a4-8a30-1b205f8c37fe
inline constexpr std::uint32_t kTypeReplicatedStateBundle = 8; // 8a40aec2-ae07-4f92-9bf3-78fc0cc94fdf
inline constexpr std::uint32_t kTypeClientInput = 1589; // ActionListComponentServerFacet_ReceiveInputFromClient
inline constexpr std::uint32_t kTypePlayerManagerRejected = 1630; // Javelin::ClientMessagesTrait::PlayerManagerRejectedMsg
inline constexpr std::uint32_t kTypeKickClient = 271;             // KickClientTrait::KickClientMsg

/**
 * Result of the registration reply for a registration that the backend refuses: the client
 * shows a text about the login ticket and ends the connection (protocol notes 3.6).
 */
inline constexpr std::int32_t kRegistrationRefused = -160;
/** Wire reasons of KickClientMsg (protocol notes 3.6). */
inline constexpr std::uint32_t kKickDuplicateSession = 1;
inline constexpr std::uint32_t kKickCharacterDeactivated = 2;

/** Why a session sent its client away. */
enum class CloseCause {
    /** The registration was refused. */
    refused,
    /** A newer session has the character of this session. */
    replaced,
    /** The character of this session was deleted. */
    characterDeleted,
};

struct SessionOptions {
    /** World version text of the registration reply. */
    std::string serverVersion;
    /** Research switch for live checks. Off by default. */
    ResearchSwitch research;
    /**
     * True: the session observes the entities of other characters near its character, and
     * its client gets a record for each one. False: the client gets only its own character.
     */
    bool observePlayers{false};
    /** Ids of the player slice for the check of the component calls. Null or not loaded: no check of a target. */
    const gamedata::SliceIds* playerSlice{nullptr};
};

/** Counts of the client messages of a session, by the result of the id check (protocol notes 4.2). */
struct MessageCounts {
    /** Messages with the route of an entity that the session knows: component calls. */
    std::uint64_t componentCalls{0};
    /** Component calls whose target is a facet of the player slice for that entity. */
    std::uint64_t knownTargets{0};
    /** Component calls whose target is not a facet of the player slice for that entity. */
    std::uint64_t unknownTargets{0};
    /** Component calls whose entity field is not the entity group id of the route. */
    std::uint64_t wrongEntity{0};
    /** Component calls with no check of the target: the backend has no ids of the slice. */
    std::uint64_t uncheckedTargets{0};
    /** Messages with the route of the player manager. */
    std::uint64_t playerManager{0};
    /** Messages with a different route (not the registration request, whose route is null). */
    std::uint64_t unknownRoutes{0};
    /** Component calls whose target is a facet of the slice of a creature that the session observes. */
    std::uint64_t creatureTargets{0};
};

class RepSession final : public world::SessionSink {
public:
    using Send = std::function<void(std::uint8_t channel, std::span<const std::uint8_t>, carrier::Reliability)>;

    /** The session sent its client away: the cause and the time. */
    struct Closing {
        CloseCause cause;
        world::WorldClock::time_point since;
    };

    /**
     * @param id Id of this session: 1, 2, ... in one backend process, not used again.
     * @param world The world. The session reads the time only from its clock.
     */
    RepSession(std::uint32_t id, Send send, world::World& world, SessionOptions options);
    RepSession(const RepSession&) = delete;
    RepSession& operator=(const RepSession&) = delete;

    /** Handles one complete carrier message from the client. */
    void on_message(std::uint8_t channel, std::span<const std::uint8_t> data);

    /** Sends the state records that are due when the client is in the world. */
    void tick(carrier::Clock::time_point now);

    /** The world took the character of this session away: the client gets the message "kicked". */
    void character_lost(world::LostCause cause) override;

    /**
     * The world moved the character of this session to a different place: the client gets the
     * message for a loading screen when the place is far, and the check of its position
     * reports starts again from the new place.
     */
    void traveled(const world::Travel& travel) override;

    /** @return The teleport number of the last full movement state of its character that the client got. 0: none. */
    [[nodiscard]] std::optional<std::uint8_t> client_teleport() const override {
        return m_clientTeleport;
    }

    /** The character of this session did hurt an entity: the client gets the message "on damage dealt". */
    void damage_dealt(const world::DamageDealt& damage) override;

    /** The character of this session took damage: the client gets the message "on damage" (the mark with its direction). */
    void damage_taken(const world::DamageTaken& damage) override;

    /** The character of this session died: the client gets the message "on player death". */
    void died(float respawnSeconds) override;

    /** Development aid: the client gets one message with a type and a body of a command (tools/world message). */
    void development_message(std::uint32_t type, const std::vector<std::uint8_t>& body) override;

    [[nodiscard]] std::uint32_t id() const {
        return m_id;
    }
    /**
     * @return The mark of this session for a log line: " [s1]", and " [s1 4aae16c4]" from
     *         the registration on (the first 8 characters of the character id).
     */
    [[nodiscard]] const std::string& log_mark() const {
        return m_logMark;
    }
    /** @return The character id of the registration request. Empty before it. */
    [[nodiscard]] const std::string& character_id() const {
        return m_characterId;
    }
    /** @return True when the client sent a registration request. */
    [[nodiscard]] bool registered() const {
        return m_registered;
    }
    /**
     * @return A value from the moment at which the session sent its client away (a refused
     *         registration, or the message "kicked"). The owner of the session ends it when
     *         the client closed its connection, or some time after this moment.
     */
    [[nodiscard]] const std::optional<Closing>& closing() const {
        return m_closing;
    }
    /** @return The table of the entities that the client of this session knows. */
    [[nodiscard]] const Observer& observer() const {
        return m_observer;
    }
    [[nodiscard]] const MessageCounts& message_counts() const {
        return m_counts;
    }
    /** @return The number of creatures whose first record this session sent. */
    [[nodiscard]] std::uint64_t creature_records() const {
        return m_creatureRecords;
    }
    /** @return The largest number of creatures that the interest table of this session had at one time. */
    [[nodiscard]] std::size_t creatures_observed() const {
        return m_creaturesObserved;
    }
    /**
     * Writes the counts of the client messages and of the position reports to the log. The
     * owner calls it at the end of the session.
     */
    void log_message_counts() const;
    /** @return The values of the check of the position reports of the client of this session. */
    [[nodiscard]] const world::PositionTrack& position_track() const {
        return m_track;
    }
    /** @return The values of the check of the hit claims of the client of this session. */
    [[nodiscard]] const world::AttackTrack& attack_track() const {
        return m_attack;
    }

private:
    /** Where the position of the character of this session comes from. */
    enum class PositionSource {
        /** No input report with a position of the character or with a camera came. */
        none,
        /** The position field of the input reports: the exact position of the character. */
        report,
        /** The estimate from the camera of the input reports. */
        camera,
    };

    /** The fields of a registration request that the backend reads. */
    struct Registration {
        bool parsed{false};
        std::string characterId;
        std::string personaId;
        std::string tokenSteamId;
        std::string clientSteamId;
        std::string ticket;
    };

    void send_message(std::uint32_t type, std::span<const std::uint8_t> body);
    void handle_registration(std::uint32_t type, std::span<const std::uint8_t> body);
    [[nodiscard]] Registration read_registration(std::span<const std::uint8_t> body);
    void check_identity(const Registration& request);
    void send_registration_reply(std::int32_t result);
    void send_self_identification();
    void send_level_information();
    /**
     * Sends one state bundle. @param stops Interest numbers that the client must remove: they
     * go into the control block of the bundle (100 numbers at most).
     */
    void send_state_bundle(std::span<const std::uint8_t> payload, std::span<const std::uint32_t> stops = {});
    void send_kick(std::uint32_t reason);
    /**
     * The replication step: the stops, and for each entity of the interest table a record
     * with the component states that changed after the last record of this session. A record
     * that is larger than one carrier message goes out in more than one bundle, cut between
     * two fragments.
     */
    void replicate(carrier::Clock::time_point now);
    /** Adds and removes the entities of other characters with the two interest distances. */
    void update_interest(carrier::Clock::time_point now, const world::Entity& own);
    void send_stops();
    void send_record(const world::Entity& entity, Known& known, carrier::Clock::time_point now);
    /** The records of creatures that wait for one common bundle. */
    struct RecordBatch {
        hub::Bytes payload;
        unsigned records{0};
        /** Records of the batch that are the first record of their creature. */
        unsigned fresh{0};
    };
    /**
     * Puts the record of a creature into the batch, when a component state of the creature
     * changed after the last record of this session. A batch that is full goes out first. The
     * fragments of a creature make more than one record by the rule of send_record (a second
     * fragment for one slot, a record above the size of one carrier message): each next record
     * of the creature is in a later bundle.
     */
    void add_creature_record(const world::Entity& entity, Known& known, RecordBatch& batch);
    /** Sends the records of a batch in one bundle. No effect for an empty batch. */
    void send_batch(RecordBatch& batch);
    /** @return The number of creatures of the interest table. */
    [[nodiscard]] std::size_t creature_count() const;
    /** Compares the route and the target of a client message with the id rule. */
    void check_route(std::span<const std::uint8_t> route, std::uint32_t type, std::span<const std::uint8_t> body,
                     bool firstOfType);
    void handle_client_input(std::span<const std::uint8_t> body);
    /** The requests of the client for its bag (server/rep/item_requests.h). */
    void handle_drop_items(std::span<const std::uint8_t> body);
    void handle_set_lock_state(std::span<const std::uint8_t> body);
    /**
     * The requests of the client for its equipment: equip, unequip, and the swap of two slots
     * of the paperdoll. A request that the backend refuses changes nothing, and the client
     * gets the message "equip item fail".
     */
    void handle_equip_item(std::span<const std::uint8_t> body);
    void handle_unequip_item(std::span<const std::uint8_t> body);
    void handle_swap_items(std::span<const std::uint8_t> body);
    /**
     * Checks the start of a request for the equipment.
     * @return The facet target and the entity of the request when the entity is the entity of
     *         this session. No value, with a log line, for a different entity or a body that
     *         is too short: such a request gets no reply.
     */
    [[nodiscard]] std::optional<ComponentCall> equipment_call(const world::Entity& own,
                                                              std::uint32_t type,
                                                              const char* name,
                                                              std::span<const std::uint8_t> body);
    /** Sends the message "equip item fail" for a request that the backend refused. */
    void send_equip_fail(const ComponentCall& call);
    /**
     * The requests of the client for its attributes (server/rep/progress_requests.h): spend
     * free points, and make all points free again. The client has no reply message for them:
     * the attribute state is the answer, also for a request that the backend refused.
     */
    void handle_spend_attribute_points(std::span<const std::uint8_t> body);
    void handle_refund_attribute_points(std::span<const std::uint8_t> body);
    /**
     * The requests of the client for its weapon trees and its ability bars: a mastery point
     * for a node, the end of a selection ("apply"), an ability for a place of the bar, and
     * the reset of a tree, which gets the message "mastery tree respec response".
     */
    void handle_assign_pool_points(std::span<const std::uint8_t> body);
    void handle_apply_ability_points(std::span<const std::uint8_t> body);
    void handle_change_mapped_ability(std::span<const std::uint8_t> body);
    void handle_mastery_tree_respec(std::span<const std::uint8_t> body);
    /**
     * The respawn request of the client of a dead character (server/rep/health_messages.h).
     * The world makes the character live again at its respawn place. A request for a character
     * that is not dead changes nothing, and the client gets the message that ends its loading screen.
     */
    void handle_request_respawn(std::span<const std::uint8_t> body);
    /**
     * The end of a player cutscene of the client (server/rep/lock_messages.h): the client keeps
     * the input of its character blocked until the answer "cutscene end request finalized",
     * which the session sends at once.
     */
    void handle_cutscene_ended(std::span<const std::uint8_t> body);
    /** The end of an interaction of the client: the backend has no interaction state, and nothing changes. */
    void handle_interaction_cancel(std::span<const std::uint8_t> body);
    /**
     * Ends each lock of the input that the client of the own character can have: a committed
     * interaction (a conversation) and a player cutscene. For a travel and a respawn.
     */
    void end_input_lock(const char* cause);
    /**
     * @return True when the entity of a component call is the entity of this session. False,
     *         with a log line, for a different entity: such a request changes nothing.
     */
    [[nodiscard]] bool own_call(const world::Entity& own, const ComponentCall& call, std::uint32_t type, const char* name);
    /**
     * Gives the position of the character of one input report to the world, which checks it.
     * The log gets the first report that the world refused for each cause, and then the counts.
     */
    void report_position(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own);
    /**
     * Gives the attack data of one input report to the world: each input step that the session
     * did not get before, in the sequence of the input numbers, and each melee contact of
     * these steps as a hit claim (world/combat.h). A client sends an input in more than one
     * report: a second copy changes nothing.
     */
    void handle_attacks(const ClientInput& input);
    /** Gives the press of a key of the ability bar to the world (stage 3.3a), and writes a refusal to the log. */
    void press_ability(const ClientInput& input, const InputStep& step);
    /** Gives one melee contact to the world, and writes the result to the log. */
    void claim_hit(const InputStep& step, const InputContact& contact);
    /**
     * For an input report with a camera and with no position of the character.
     * @return True when the camera estimate gives the position: the client sends no position
     *         field, or it sent none for some time.
     */
    [[nodiscard]] bool use_camera_estimate(const ClientInput& input, carrier::Clock::time_point now,
                                           const world::Entity& own);
    /** Changes the position source and writes one log line. */
    void set_position_source(PositionSource source, const ClientInput& input);
    /**
     * After a travel of the character, for a session with the camera estimate. The session
     * compares the estimate of this report with the place of the travel, in the horizontal
     * plane and in the height (kTravelCameraRange).
     * @return True when the estimate is not near the place: the client made the report before
     *         it got the new position, or it did not take the position, and the estimate must
     *         not follow it. False, and the rule ends, when the estimate is near the place.
     */
    [[nodiscard]] bool camera_before_travel(const ClientInput& input, carrier::Clock::time_point now,
                                            const world::Entity& own);
    /** Starts the camera estimate again from a position of the entity that is exact. */
    void resume_camera_estimate(const ClientInput& input, carrier::Clock::time_point now, const world::Entity& own);
    /** Updates the position of the character from the camera of one input report. */
    void track_position(const ClientInput& input, carrier::Clock::time_point now, world::Entity& own);
    /**
     * Ends the wheel inputs of a character at rest: the movement of the camera along its axis
     * is the change of the camera distance.
     * @param behind Distance of the camera behind the known position.
     */
    void apply_wheel(float behind);
    /** A wheel input that the backend cannot follow: the camera distance is the start distance again. */
    void forget_camera_distance();
    /** Research switch: sends the kick message when its time came. */
    void send_research_kick(carrier::Clock::time_point now);

    std::uint32_t m_id;
    std::string m_logMark;
    /**
     * Client context instance of the level information and of each state bundle. The client
     * keeps the instance of its last connection, and it makes a client context only when the
     * level information has a different one (client code at image RVA 0x6446800). Thus each
     * session has its own number: a client that leaves the world can enter it again.
     */
    std::uint8_t m_contextInstance;
    SessionOptions m_options;
    /** Research switch: time of the kick message. No value when no kick waits. */
    std::optional<carrier::Clock::time_point> m_researchKickAt;
    bool m_researchKickArmed{false};

    /**
     * Sequence of the reliable state bundles of this session: 1, 2, ... with no gap. The
     * client drops each bundle after a gap (client code at image RVA 0x644B280).
     */
    std::uint64_t m_bundleSequence{0};
    /** Time of the last movement state of the own character. */
    carrier::Clock::time_point m_lastAnchor{};
    bool m_inWorld{false};
    std::optional<Closing> m_closing;

    Send m_send;
    world::World& m_world;
    const world::WorldClock& m_clock;
    /** Actor id of the client actor of this session, from the allocator of the world. */
    world::ActorId m_sessionActor;
    std::string m_characterId;
    bool m_registered{false};
    std::set<std::uint32_t> m_seenTypes;
    MessageCounts m_counts;

    Observer m_observer;
    /** Interest numbers whose stop did not go out. */
    std::vector<std::uint32_t> m_pendingStops;
    /** Counts of the creatures of this session, for the log line at its end. */
    std::uint64_t m_creatureRecords{0};
    std::size_t m_creaturesObserved{0};
    carrier::Clock::time_point m_lastInterestScan{};
    /** The world revision of the last travel for which this session compared its interest distances. */
    world::Revision m_interestTravel{0};
    /**
     * True from a travel of the character to the bundle with its new position: the message "on
     * player teleport" goes out after that bundle.
     */
    bool m_travelNotice{false};
    /** The place of a travel that the client of this session did not confirm. */
    struct TravelAnchor {
        state::WorldPosition place;
        /** Time of the first camera estimate after the travel. No value before it. */
        std::optional<carrier::Clock::time_point> firstCamera;
        /** True when the log has its line for a camera estimate that is not near the place. */
        bool logged{false};
    };
    /**
     * From a travel until the client confirms the new place (an accepted position report, or
     * a camera estimate near the place): the camera estimate does not follow a camera that is
     * not near the place (camera_before_travel). The time alone does not end it. No value at
     * all other times.
     */
    std::optional<TravelAnchor> m_travelAnchor;
    /**
     * From a travel until the client confirms the new place (m_travelAnchor), and at the
     * latest until this time: no new creature comes into the interest table. The client shows
     * its loading screen in that time, and a creature that comes in then can stay hidden (the
     * spawn action of a slice that shows its body only after it plays while the client loads).
     */
    std::optional<carrier::Clock::time_point> m_creaturesAfterTravel;
    /** True when the last interest scan left the creatures out for this rule: the end of the wait starts a scan. */
    bool m_creaturesWaited{false};
    /**
     * The teleport number of the last full movement state of the own character that this
     * session sent: the number that its client has. 0 before the first one, which is the
     * start number of a client. World::travel counts the next number from it.
     */
    std::uint8_t m_clientTeleport{0};
    /** Number of travels of the character in this session, for the log line at its end. */
    std::uint32_t m_travels{0};
    /** Numbers of damage marks, of deaths, and of respawns of the character in this session, for the log line at its end. */
    std::uint64_t m_damageMarks{0};
    std::uint32_t m_deaths{0};
    std::uint32_t m_respawns{0};
    /** Numbers of the ends of a cutscene that the client sent, and of the ends of an interaction, for the log line at its end. */
    std::uint32_t m_cutsceneEnds{0};
    std::uint32_t m_interactionEnds{0};
    /**
     * True from the first input report with a camera: the loading screen of the client is
     * gone, and the client has its local player entity. The client needs it before it gets
     * the record of a different player (client code at image RVA 0x1747C60).
     */
    bool m_cameraSeen{false};

    /** Newest input report of the client and the time of the last log line for it. */
    ClientInput m_input;
    carrier::Clock::time_point m_lastInputLog{};
    PositionSource m_positionSource{PositionSource::none};
    /**
     * The values of the client for the check of its position reports: the last input number,
     * the accepted positions of the time window, and the counts. The world uses them in
     * World::report_position. They describe the input of one client, thus they are here and
     * not in the entity.
     */
    world::PositionTrack m_track;
    /**
     * Time of the last input report with the position of the character. Before the first one:
     * the time of the first report whose flags say that the client sends its position.
     */
    std::optional<carrier::Clock::time_point> m_reportSign;
    /** Number of changes of the position source, for the limit of the log lines. */
    unsigned m_sourceChanges{0};
    /** True for each cause of a refused position report that has its log line. */
    std::array<bool, world::kPositionVerdictCount> m_verdictLogged{};
    /**
     * The values of the client for the check of its hit claims: its attack inputs, its last
     * hits, and the counts. The world uses them in Combat::claim. They describe the input of one
     * client, thus they are here and not in the entity.
     */
    world::AttackTrack m_attack;
    /** True for each cause of a refused hit claim that has its log line. */
    std::array<bool, world::kHitVerdictCount> m_hitVerdictLogged{};
    /** The presses of the ability keys of the session, and the first refusal of each cause in the log. */
    std::uint64_t m_abilityPresses{0};
    std::array<bool, world::kCastVerdictCount> m_castVerdictLogged{};
    /** Number of melee contacts and of items of the two projectile lists that the client sent. */
    std::uint64_t m_contacts{0};
    std::uint64_t m_projectileItems{0};
    carrier::Clock::time_point m_lastRefusalLog{};
    /** True while the character moves: the position then follows the camera. */
    bool m_followCamera{false};
    /** Camera position at the start of its last rest, and the time of that start. */
    state::WorldPosition m_restCamera;
    carrier::Clock::time_point m_cameraRestStart{};
    /**
     * Distance of the camera behind the character when no obstacle is behind the camera. The
     * pointer wheel of the user changes it.
     */
    float m_cameraDistance;
    /**
     * The same distance for the known position. It is smaller than m_cameraDistance when the
     * position is an estimate that used a smaller distance.
     */
    float m_knownBehind;
    /**
     * Distance of the camera behind the known position at the first wheel input, while the
     * camera goes to its new distance. No value at all other times.
     */
    std::optional<float> m_wheelStartBehind;
    carrier::Clock::time_point m_lastWheel{};
};

} // namespace soulwarden::backend::server::rep

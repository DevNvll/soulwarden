#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "gamedata/slice_ids.h"
#include "server/carrier/carrier.h"
#include "server/rep/rep_session.h"
#include "server/rep/research.h"
#include "service/datagram_transport.h"
#include "world/world.h"

namespace soulwarden::backend::service {

/** Why a session ended. session_end_text gives the text of the log line. */
enum class SessionEnd {
    /** The secure session closed: a DTLS close notice, a DTLS read error, or a new handshake from the same port. */
    secureSessionClosed,
    /** The client closed the carrier connection. The client does this when the user leaves the world. */
    carrierClosed,
    /** No datagram came from the client for kSessionIdleLimit. */
    idle,
    /** No registration request came in kRegistrationLimit after the start of the session. */
    noRegistration,
    /** A handler of this session stopped with an exception. */
    error,
    /** The registration was refused: the client got the reply with a result that is not 0. */
    refused,
    /** A newer session has the character: the client got the message "kicked". */
    replaced,
    /** The character was deleted: the client got the message "kicked". */
    characterDeleted,
    /** The backend stops. */
    backendStop,
};

[[nodiscard]] const char* session_end_text(SessionEnd cause);

/** A session with no datagram for this time ends. The client has its own limit of 30 s. */
inline constexpr std::chrono::seconds kSessionIdleLimit{30};
/** A session with no registration request this long after its start ends. */
inline constexpr std::chrono::seconds kRegistrationLimit{30};
/**
 * A session that sent its client away (a refused registration, the message "kicked") ends
 * this long after the message, when the client did not close its connection before. The
 * carrier needs the time to give the message to the client. In live runs the client closed
 * its connection less than 0.1 s after each of these messages.
 */
inline constexpr std::chrono::milliseconds kCloseLimit{1000};

struct RepServerOptions {
    /** World version text of the registration reply. */
    std::string serverVersion;
    /**
     * Folder for the capture files. Each session has its own folder below it (s0001, s0002,
     * ...) with its own file counter. Empty: no capture.
     */
    std::string captureDirectory;
    /** Research switch for live checks (server/rep/research.h). Off by default. */
    server::rep::ResearchSwitch research;
    /**
     * True: each session observes the entities of other characters near its character
     * (server/rep/rep_session.h). Off by default: a client then gets only its own character.
     */
    bool observePlayers{false};
    /** Ids of the player slice for the check of the component calls of a client. Null: no check of a target. */
    const gamedata::SliceIds* playerSlice{nullptr};
    /**
     * Test seam: called for each complete carrier message of a client, before the session
     * reads it. An exception of this function is an exception of the message handler.
     */
    std::function<void(std::uint32_t session, std::uint8_t channel, std::span<const std::uint8_t> data)> messageTap;
};

/**
 * Game server entry point ("REP"). The client opens a DTLS 1.2 session to the transport and
 * then speaks the datagram carrier protocol. Each peer of the transport is one session: it
 * has a session id, one carrier connection, and one RepSession. A session is an observer of
 * the world: the state of its character is in the entity table of the world object.
 *
 * The transport calls the handler functions from one thread, the world thread. The server
 * reads the time only from the world clock, and each tick of the transport is one tick of
 * the world.
 */
class RepServer final : public DtlsHandler {
public:
    RepServer(DatagramTransport& transport, world::World& world, RepServerOptions options);

    /**
     * Ends each session with the cause "backend stop": the world saves the position of each
     * character. Call it when the transport does not call the handler functions any more.
     */
    void stop();

    void on_connected(PeerId peer) override;
    void on_datagram(PeerId peer, std::span<const std::uint8_t> data) override;
    void on_disconnected(PeerId peer) override;
    void on_tick() override;

    /** @return The number of sessions, with the sessions that ended and wait for the next tick. */
    [[nodiscard]] std::size_t session_count() const {
        return m_clients.size();
    }

    /** @return The session with this id, or null (a read function for the tests). */
    [[nodiscard]] const server::rep::RepSession* session(std::uint32_t id) const;

private:
    struct Client {
        std::uint32_t id{0};
        PeerId peer{0};
        std::unique_ptr<server::carrier::Connection> connection;
        std::unique_ptr<server::rep::RepSession> session;
        world::WorldClock::time_point opened{};
        world::WorldClock::time_point lastDatagram{};
        /** Datagrams and messages of this session: the number of a capture file and of a log line. */
        unsigned packets{0};
        /** Capture folder of this session. Empty until its first capture file. */
        std::string captureDirectory;
        std::uint64_t datagramsIn{0};
        std::uint64_t datagramsOut{0};
        std::uint64_t bytesOut{0};
        /** True from the end of the session. The next tick erases the client. */
        bool ended{false};
    };

    void capture(Client& client, const char* direction, std::span<const std::uint8_t> data);
    void on_message(Client& client, std::uint8_t channel, std::span<const std::uint8_t> data);
    /**
     * Ends a session one time: the session leaves the world (the world saves the position of
     * its character and removes its entity), and the log gets the cause. It only marks the
     * client: the next tick erases it, because this function can run inside a call of the
     * carrier connection of the client. This function sends no message and no close notice.
     */
    void end_session(Client& client, SessionEnd cause);

    DatagramTransport& m_transport;
    world::World& m_world;
    RepServerOptions m_options;
    /** Stage DG1: the account rule of the level information of the sessions (server/rep/level_contexts.h). It outlives each session. */
    server::rep::LevelContexts m_levelContexts;
    /** The address of a client stays the same until its erase: the carrier connection keeps it. */
    std::map<PeerId, Client> m_clients;
    std::uint32_t m_nextSession{1};
};

} // namespace soulwarden::backend::service

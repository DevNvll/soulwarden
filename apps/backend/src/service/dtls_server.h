#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "service/datagram_transport.h"
#include "service/network.h"
#include "service/tls_identity.h"

namespace soulwarden::backend::service {

/**
 * Loopback DTLS 1.2 datagram listener for the game server transport. Its listener thread
 * calls the handler: it is the world thread of the backend program.
 */
class DtlsServer final : public DatagramTransport {
public:
    DtlsServer();
    ~DtlsServer() override;
    DtlsServer(const DtlsServer&) = delete;
    DtlsServer& operator=(const DtlsServer&) = delete;

    /** Binds the socket and starts the listener thread. @return False on any failure. */
    [[nodiscard]] bool start(const std::string& host, std::uint16_t port, const TlsIdentity& identity, DtlsHandler* handler);

    void stop();

    /** Sends one clear datagram to a connected peer. Call from the listener thread only. */
    bool send(PeerId peer, std::span<const std::uint8_t> data) override;

    /** Closes one peer. Call from the listener thread only. */
    void close(PeerId peer) override;

private:
    struct Shared;
    struct Session;

    void run();
    /** Handshake timers, and the end of each session that is closed or that never connected. */
    void tick_sessions(std::chrono::steady_clock::time_point now);
    void receive(PeerId peer, const sockaddr_in& address, const std::uint8_t* data, std::size_t size);
    void advance(Session& session);
    Session* create_session(PeerId peer, const sockaddr_in& address);

    std::unique_ptr<Shared> m_shared;
    std::map<PeerId, std::unique_ptr<Session>> m_sessions;
    net::Socket m_socket{net::kInvalidSocket};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    DtlsHandler* m_handler{nullptr};
};

} // namespace soulwarden::backend::service

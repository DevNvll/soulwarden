#pragma once

#include <cstdint>
#include <span>

// The two sides of the secure datagram layer of the game server. DtlsServer is the transport
// of the backend program. A test gives the game server a transport with no socket.

namespace soulwarden::backend::service {

/** Stable id of one remote endpoint: IPv4 address and port in host order. */
using PeerId = std::uint64_t;

/** Receives clear datagrams from the transport. All calls come from one thread: the world thread. */
class DtlsHandler {
public:
    virtual ~DtlsHandler() = default;
    virtual void on_connected(PeerId peer) = 0;
    virtual void on_datagram(PeerId peer, std::span<const std::uint8_t> data) = 0;
    virtual void on_disconnected(PeerId peer) = 0;
    /** Called about every 10 ms for timers. */
    virtual void on_tick() = 0;
};

/** What the game server needs from the secure datagram layer. Call from the world thread only. */
class DatagramTransport {
public:
    virtual ~DatagramTransport() = default;
    /** Sends one clear datagram to a connected peer. @return False when the peer is not connected. */
    virtual bool send(PeerId peer, std::span<const std::uint8_t> data) = 0;
    /**
     * Closes one peer with a close notice. The handler gets on_disconnected for a connected
     * peer after this call. The client does a new handshake from the same port when it gets
     * the close notice (live run of stage 0.6a): the game server does not call this function
     * at the end of a session.
     */
    virtual void close(PeerId peer) = 0;
};

} // namespace soulwarden::backend::service

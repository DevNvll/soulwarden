#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "server/carrier/carrier.h"
#include "server/hub/wire.h"
#include "support/client_frames.h"

// A client endpoint with no game process. It writes carrier datagrams from the layout of the
// protocol notes (3.2) and reads the datagrams of the server.
//
// A "host" is the game server under test. A host has these functions:
//   void connect(std::uint64_t peer)                                   the secure session opens
//   void datagram(std::uint64_t peer, std::span<const std::uint8_t>)   one clear datagram arrives
//   void tick()                                                        one timer tick of the server
//   void advance(std::chrono::milliseconds)                            the clock of the server
//   std::vector<Bytes> take(std::uint64_t peer)                        datagrams for a peer since the last call

namespace soulwarden::backend::tests {

using server::hub::Bytes;

/** One message that the server sent: the type index, the body, and the complete frame. */
struct Sent {
    std::uint32_t type{};
    Bytes body;
    Bytes frame;
};

class FakeClient {
public:
    /** @param port UDP port of the client. The peer id is the loopback address and the port. */
    explicit FakeClient(std::uint16_t port) : peer((static_cast<std::uint64_t>(0x7F000001U) << 16) | port) {
        // A carrier connection object of the backend puts the messages of the server together.
        // It sends nothing: the client writes its own datagrams.
        m_receiver = std::make_unique<server::carrier::Connection>(
            [](std::span<const std::uint8_t>) {},
            [this](std::uint8_t, std::span<const std::uint8_t> frame) {
                // Server frame: size envelope, envelope := flags present type body.
                server::hub::Reader reader(frame);
                reader.size();
                reader.u8();
                reader.u8();
                Sent sent;
                sent.type = reader.size();
                const auto body = frame.subspan(reader.position());
                sent.body.assign(body.begin(), body.end());
                sent.frame.assign(frame.begin(), frame.end());
                got.push_back(std::move(sent));
            });
    }

    /** Carrier connect request: system message 1 with handshake version 5. */
    Bytes connect_request() {
        const std::uint8_t request[5] = {0, 0, 0, 5, 1};
        return wrap(3, request, false, true);
    }
    Bytes registration(const Registration& fields) {
        return wrap(0, client_frame(kRegistrationRequestV3, registration_body(fields)), true);
    }
    /** Input report of a camera that looks north with no pitch. */
    Bytes input(std::uint16_t number, float cameraX, float cameraY, float cameraZ) {
        return wrap(1, client_frame(kClientInput, input_body(number, cameraX, cameraY, cameraZ)), false);
    }
    /** Input report of the camera of a character at this position (camera to the north, no pitch). */
    Bytes input_at(std::uint16_t number, float x, float y, float z) {
        return input(number, x + 0.5F, y - 4.0F, z + 1.65F);
    }
    /** Input report with one entry of the test (support/client_frames.h). */
    Bytes input_report(const InputEntry& entry) {
        return wrap(1, client_frame(kClientInput, input_entry_body(entry)), false);
    }
    /**
     * Input report of a client with the position report of the game hook: the position of the
     * character (x, y, z), and the camera of a character at that position.
     */
    Bytes report_at(std::uint16_t number, float x, float y, float z) {
        return input_report(report_entry(number, x, y, z));
    }
    /** Carrier close: system message 3 with reason 0. */
    Bytes carrier_close() {
        const std::uint8_t close[2] = {0, 3};
        return wrap(3, close, false);
    }
    /** A message of a type that the backend has no handler for. */
    Bytes other_message(std::uint32_t type) {
        const std::uint8_t body[4] = {1, 2, 3, 4};
        return wrap(0, client_frame(type, body), true);
    }
    /**
     * A component call: a message with a route of 16 bytes, whose body starts with the facet
     * target and the entity group id (each one a big-endian number).
     */
    Bytes component_call(std::uint32_t type, std::span<const std::uint8_t> route, std::uint64_t target, std::uint64_t entity) {
        server::hub::Writer body;
        body.u64(target);
        body.u64(entity);
        return wrap(0, client_frame_to(route, type, body.data()), true);
    }
    /** A component call with a body of the caller: the body starts with the facet target and the entity group id. */
    Bytes component_message(std::uint32_t type, std::span<const std::uint8_t> route, std::span<const std::uint8_t> body) {
        return wrap(0, client_frame_to(route, type, body), true);
    }
    /**
     * Acknowledgement of all server datagrams that this client read and did not acknowledge:
     * system message 6 with the continuous form (first and last datagram number).
     * @return An empty value when no datagram waits for an acknowledgement.
     */
    Bytes acknowledgement() {
        if (!m_hasUnacknowledged) {
            return {};
        }
        m_hasUnacknowledged = false;
        const std::uint8_t data[6] = {0x40,
                                      static_cast<std::uint8_t>(m_lastServerDatagram >> 8),
                                      static_cast<std::uint8_t>(m_lastServerDatagram),
                                      static_cast<std::uint8_t>(m_firstUnacknowledged >> 8),
                                      static_cast<std::uint8_t>(m_firstUnacknowledged),
                                      6};
        return wrap(3, data, false);
    }

    /** Reads the datagrams of the server. */
    void read(const std::vector<Bytes>& datagrams) {
        for (const Bytes& data : datagrams) {
            if (data.size() >= 4) {
                const auto number = static_cast<std::uint16_t>((data[2] << 8) | data[3]);
                if (!m_hasUnacknowledged) {
                    m_firstUnacknowledged = number;
                    m_hasUnacknowledged = true;
                }
                m_lastServerDatagram = number;
            }
            received.push_back(data);
            m_receiver->receive(data);
        }
    }

    /** @return True when a datagram of the server for this client has the text. */
    [[nodiscard]] bool received_text(const std::string& text) const {
        for (const Bytes& data : received) {
            const std::string bytes(data.begin(), data.end());
            if (bytes.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    template <class Host>
    void connect(Host& host) {
        host.connect(peer);
        host.datagram(peer, connect_request());
    }

    /** Reads what the server sent to this client and acknowledges it. @return The datagrams. */
    template <class Host>
    std::vector<Bytes> exchange(Host& host) {
        std::vector<Bytes> datagrams = host.take(peer);
        read(datagrams);
        const Bytes reply = acknowledgement();
        if (!reply.empty()) {
            host.datagram(peer, reply);
        }
        return datagrams;
    }

    /** @return The type indexes of the messages of the server, for example "3 1628 8 ". */
    [[nodiscard]] std::string types() const {
        std::string text;
        for (const Sent& sent : got) {
            text += std::to_string(sent.type) + " ";
        }
        return text;
    }

    std::uint64_t peer;
    /** The messages of the server, in sequence. */
    std::vector<Sent> got;
    /** Each datagram of the server that this client read. */
    std::vector<Bytes> received;

private:
    /** One carrier datagram with one message: datagram header, message header, data. */
    Bytes wrap(std::uint8_t channel, std::span<const std::uint8_t> data, bool reliable, bool connecting = false) {
        Bytes out{0x80, 0x01};
        put16(out, ++m_datagram);
        out.push_back(static_cast<std::uint8_t>(0x20 | (reliable ? 0x01 : 0) | (connecting ? 0x80 : 0)));
        put16(out, static_cast<std::uint16_t>(data.size()));
        out.push_back(channel);
        put16(out, ++m_sequence[channel]);
        if (reliable) {
            ++m_reliable[channel];
        }
        put16(out, m_reliable[channel]);
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    static void put16(Bytes& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    std::uint16_t m_datagram{0};
    std::uint16_t m_sequence[4]{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    std::uint16_t m_reliable[4]{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    std::uint16_t m_firstUnacknowledged{0};
    std::uint16_t m_lastServerDatagram{0};
    bool m_hasUnacknowledged{false};
    std::unique_ptr<server::carrier::Connection> m_receiver;
};

} // namespace soulwarden::backend::tests

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <span>
#include <vector>

// Server side of the datagram carrier that the client uses for the game server transport.
//
// Wire format (big-endian), proved against the client reader at image RVA 0xF898E0 and
// 0xF77EB0, and the public GridMate carrier source:
//
//   datagram := flags:u8 version:u8 sequence:u16 message*
//               flags   = 0x80 | compressed(bit 0)
//               version = 0x01
//   message  := flags:u8 size:u16 [channel:u8] [chunks:u16] [sequence:u16] [reliable:u16] data
//               flags: 0x01 reliable, 0x04 chunk count present, 0x08 sequence is previous+1,
//                      0x10 reliable sequence is implied, 0x20 channel present, 0x80 connecting
//   A message on channel 3 is a system message. Its last data byte is the message id.

namespace soulwarden::backend::server::carrier {

using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint8_t kChannelCount = 4;
inline constexpr std::uint8_t kSystemChannel = 3;
/** Handshake version that the client sends in its connect request. */
inline constexpr std::uint32_t kHandshakeVersion = 5;

enum class Reliability { unreliable, reliable };

enum class State { connecting, connected, disconnected };

/** One carrier connection to one client. Not thread-safe; use it from the listener thread. */
class Connection {
public:
    using Transmit = std::function<void(std::span<const std::uint8_t>)>;
    using Deliver = std::function<void(std::uint8_t channel, std::span<const std::uint8_t>)>;
    using Notify = std::function<void()>;

    Connection(Transmit transmit, Deliver deliver);

    /** Called when the peer completes the carrier handshake. */
    void on_established(Notify callback) {
        m_onEstablished = std::move(callback);
    }

    /** Processes one clear datagram from the client. */
    void receive(std::span<const std::uint8_t> datagram);

    /** Queues one user message. Large reliable messages are split into chunks. */
    void send(std::uint8_t channel, std::span<const std::uint8_t> data, Reliability reliability);

    /** Sends queued messages, acknowledgements, resends, and keep-alives. */
    void tick(Clock::time_point now);

    [[nodiscard]] State state() const {
        return m_state;
    }
    /**
     * @return The sequence number of the datagram of the client that carried the message of the delivery that runs now (the last chunk
     *         of a message in chunks). The datagrams of the client have one sequence for all channels: the order in which the client
     *         sent two messages of different channels (code review of stage 2.5b, finding ST-R2).
     */
    [[nodiscard]] std::uint16_t delivered_datagram() const {
        return m_deliveredDatagram;
    }

private:
    struct Message {
        std::uint8_t channel{};
        bool reliable{};
        bool connecting{};
        std::uint16_t chunks{1};
        std::uint16_t sequence{};
        std::uint16_t reliableSequence{};
        /** The sequence number of the datagram that carried the message. */
        std::uint16_t datagram{};
        Bytes data;
    };

    struct SentDatagram {
        std::uint16_t sequence{};
        Clock::time_point sentAt;
        std::vector<Message> reliable;
    };

    void queue_message(std::uint8_t channel, std::span<const std::uint8_t> data, Reliability reliability, bool front);
    void send_system(std::uint8_t id, std::span<const std::uint8_t> payload, Reliability reliability);
    void handle_system(std::span<const std::uint8_t> data);
    void read_acknowledgement(std::span<const std::uint8_t> data);
    void acknowledge(std::uint16_t sequence);
    void release_received();
    void flush(Clock::time_point now);
    Bytes build_acknowledgement();

    Transmit m_transmit;
    Deliver m_deliver;
    Notify m_onEstablished;
    State m_state{State::connecting};

    std::uint16_t m_datagramSequence{0};
    std::uint16_t m_deliveredDatagram{0};
    std::uint16_t m_sendSequence[kChannelCount];
    std::uint16_t m_sendReliableSequence[kChannelCount];
    std::uint16_t m_receivedSequence[kChannelCount];
    std::uint16_t m_receivedReliableSequence[kChannelCount];

    std::deque<Message> m_toSend;
    std::list<SentDatagram> m_sent;
    std::list<Message> m_received[kChannelCount];
    std::vector<std::uint16_t> m_toAcknowledge;
    Clock::time_point m_lastSend{};
    Clock::time_point m_lastAcknowledgement{};
};

} // namespace soulwarden::backend::server::carrier

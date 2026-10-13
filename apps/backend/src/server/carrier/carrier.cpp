#include "server/carrier/carrier.h"

#include <algorithm>
#include <cstring>

#include "core/logging/log.h"
#include "middleware/compression/lz4.h"

namespace soulwarden::backend::server::carrier {
namespace {

constexpr std::uint8_t kDatagramFlag = 0x80;
constexpr std::uint8_t kDatagramCompressed = 0x01;
constexpr std::uint8_t kDatagramVersion = 0x01;

constexpr std::uint8_t kFlagReliable = 0x01;
constexpr std::uint8_t kFlagChunks = 0x04;
constexpr std::uint8_t kFlagSequential = 0x08;
constexpr std::uint8_t kFlagSequentialReliable = 0x10;
constexpr std::uint8_t kFlagChannel = 0x20;
constexpr std::uint8_t kFlagConnecting = 0x80;

constexpr std::uint8_t kSystemConnectRequest = 1;
constexpr std::uint8_t kSystemConnectAck = 2;
constexpr std::uint8_t kSystemDisconnect = 3;
constexpr std::uint8_t kSystemClockSync = 4;
constexpr std::uint8_t kSystemCarrierFirst = 5;
constexpr std::uint8_t kSystemAcks = 6;

constexpr std::uint8_t kAckBits = 0x80;
constexpr std::uint8_t kAckContinuous = 0x40;
constexpr std::uint8_t kAckKeepAlive = 0x20;

/** Maximum data bytes in one message. Larger reliable messages are sent in chunks. */
constexpr std::size_t kMaxMessageData = 1024;
/**
 * Maximum clear datagram size: the datagram header (4 bytes), the largest message header (10
 * bytes), and the largest message data. The client closes its connection when it gets a
 * larger datagram (live, stage 1.7: a datagram of 1,187 bytes with two messages made the
 * client close the connection with the transport error 2, directly after the first record;
 * the largest datagram of each run before that had 1,037 bytes).
 */
constexpr std::size_t kMaxDatagram = 4 + 10 + kMaxMessageData;
constexpr std::size_t kMaxInflatedDatagram = 128 * 1024;
constexpr auto kResendAfter = std::chrono::milliseconds(250);
constexpr auto kKeepAliveAfter = std::chrono::milliseconds(100);
constexpr auto kAcknowledgeAfter = std::chrono::milliseconds(40);

/** @return The signed distance from a to b in the 16-bit sequence space. */
int distance(std::uint16_t from, std::uint16_t to) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(to - from));
}

struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t position{0};
    bool overrun{false};

    [[nodiscard]] std::size_t left() const {
        return data.size() - position;
    }
    std::uint8_t u8() {
        if (left() < 1) {
            overrun = true;
            return 0;
        }
        return data[position++];
    }
    std::uint16_t u16() {
        if (left() < 2) {
            overrun = true;
            position = data.size();
            return 0;
        }
        const std::uint16_t value = static_cast<std::uint16_t>((data[position] << 8) | data[position + 1]);
        position += 2;
        return value;
    }
    std::span<const std::uint8_t> bytes(std::size_t size) {
        if (left() < size) {
            overrun = true;
            position = data.size();
            return {};
        }
        const auto view = data.subspan(position, size);
        position += size;
        return view;
    }
};

void put_u16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

} // namespace

Connection::Connection(Transmit transmit, Deliver deliver)
    : m_transmit(std::move(transmit)), m_deliver(std::move(deliver)) {
    for (std::uint8_t channel = 0; channel < kChannelCount; ++channel) {
        m_sendSequence[channel] = 0xFFFF;
        m_sendReliableSequence[channel] = 0xFFFF;
        m_receivedSequence[channel] = 0xFFFF;
        m_receivedReliableSequence[channel] = 0xFFFF;
    }
}

void Connection::receive(std::span<const std::uint8_t> datagram) {
    Reader reader{datagram};
    const std::uint8_t flags = reader.u8();
    const std::uint8_t version = reader.u8();
    const std::uint16_t sequence = reader.u16();
    if (reader.overrun || (flags & 0xFE) != kDatagramFlag || version != kDatagramVersion) {
        log::write("carrier: datagram with an unknown header %02x %02x was ignored", flags, version);
        return;
    }
    Bytes inflated;
    if ((flags & kDatagramCompressed) != 0) {
        // The messages after the header are one LZ4 block.
        if (!compression::lz4_decode(datagram.subspan(4), kMaxInflatedDatagram, inflated)) {
            log::write("carrier: compressed datagram %u (%zu bytes) is malformed", sequence, datagram.size());
            return;
        }
        reader = Reader{inflated};
    }
    acknowledge(sequence);

    std::uint16_t previousSequence[kChannelCount]{};
    std::uint16_t previousReliable[kChannelCount]{};
    std::uint8_t channel = 0;
    while (reader.left() > 0 && !reader.overrun) {
        Message message;
        const std::uint8_t messageFlags = reader.u8();
        const std::uint16_t size = reader.u16();
        message.reliable = (messageFlags & kFlagReliable) != 0;
        if ((messageFlags & kFlagChannel) != 0) {
            channel = reader.u8();
        }
        if (channel >= kChannelCount) {
            log::write("carrier: message with channel %u was rejected", channel);
            return;
        }
        message.channel = channel;
        message.chunks = (messageFlags & kFlagChunks) != 0 ? reader.u16() : 1;
        if ((messageFlags & kFlagSequential) != 0) {
            ++previousSequence[channel];
        } else {
            previousSequence[channel] = reader.u16();
        }
        message.sequence = previousSequence[channel];
        if ((messageFlags & kFlagSequentialReliable) != 0) {
            if (message.reliable) {
                ++previousReliable[channel];
            }
        } else {
            previousReliable[channel] = reader.u16();
        }
        message.reliableSequence = previousReliable[channel];
        message.connecting = (messageFlags & kFlagConnecting) != 0;
        const auto data = reader.bytes(size);
        if (reader.overrun || size == 0) {
            break;
        }
        // Carrier-level system messages are handled now and are never queued. Each one still
        // takes a sequence number on the system channel, so later messages can leave a gap.
        if (channel == kSystemChannel && data.back() > kSystemCarrierFirst) {
            if (data.back() == kSystemAcks) {
                read_acknowledgement(data.first(data.size() - 1));
            }
            continue;
        }
        if (distance(m_receivedSequence[channel], message.sequence) <= 0) {
            continue; // Already processed.
        }
        message.data.assign(data.begin(), data.end());
        auto& queue = m_received[channel];
        auto position = queue.end();
        bool duplicate = false;
        while (position != queue.begin()) {
            auto previous = std::prev(position);
            if (previous->sequence == message.sequence) {
                duplicate = true;
                break;
            }
            if (distance(previous->sequence, message.sequence) > 0) {
                break;
            }
            position = previous;
        }
        if (!duplicate) {
            queue.insert(position, std::move(message));
        }
    }
    release_received();
}

void Connection::release_received() {
    for (std::uint8_t channel = 0; channel < kChannelCount; ++channel) {
        auto& queue = m_received[channel];
        while (!queue.empty()) {
            Message& front = queue.front();
            if (!front.reliable) {
                // An unreliable message waits for the last reliable message sent before it.
                if (distance(m_receivedReliableSequence[channel], front.reliableSequence) > 0) {
                    break;
                }
                Message message = std::move(front);
                queue.pop_front();
                m_receivedSequence[channel] = message.sequence;
                if (channel == kSystemChannel) {
                    handle_system(message.data);
                } else {
                    m_deliver(channel, message.data);
                }
                continue;
            }
            if (distance(m_receivedReliableSequence[channel], front.reliableSequence) != 1) {
                break; // A gap in the reliable sequence.
            }
            const std::size_t chunks = front.chunks;
            if (queue.size() < chunks) {
                break;
            }
            // Check that all chunks are present and sequential.
            auto iterator = queue.begin();
            std::uint16_t expected = front.reliableSequence;
            std::size_t found = 0;
            for (; iterator != queue.end() && found < chunks; ++iterator, ++found) {
                if (!iterator->reliable || iterator->reliableSequence != expected) {
                    break;
                }
                ++expected;
            }
            if (found < chunks) {
                break;
            }
            Bytes payload;
            std::uint16_t lastSequence = 0;
            std::uint16_t lastReliable = 0;
            for (std::size_t index = 0; index < chunks; ++index) {
                Message& part = queue.front();
                payload.insert(payload.end(), part.data.begin(), part.data.end());
                lastSequence = part.sequence;
                lastReliable = part.reliableSequence;
                queue.pop_front();
            }
            m_receivedSequence[channel] = lastSequence;
            m_receivedReliableSequence[channel] = lastReliable;
            if (channel == kSystemChannel) {
                handle_system(payload);
            } else {
                m_deliver(channel, payload);
            }
        }
    }
}

void Connection::handle_system(std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return;
    }
    const std::uint8_t id = data.back();
    const auto payload = data.first(data.size() - 1);
    switch (id) {
    case kSystemConnectRequest: {
        if (payload.size() < 4) {
            return;
        }
        const std::uint32_t version = (static_cast<std::uint32_t>(payload[0]) << 24) | (payload[1] << 16)
                                      | (payload[2] << 8) | payload[3];
        if (m_state != State::connecting) {
            return; // The request is resent until the client sees the reply.
        }
        if (version != kHandshakeVersion) {
            log::write("carrier: handshake version %u is not supported", version);
            m_state = State::disconnected;
            return;
        }
        m_state = State::connected;
        const std::uint8_t reply[4] = {0, 0, 0, static_cast<std::uint8_t>(kHandshakeVersion)};
        send_system(kSystemConnectAck, reply, Reliability::reliable);
        log::write("carrier: connection established, handshake version %u", version);
        if (m_onEstablished) {
            m_onEstablished();
        }
        break;
    }
    case kSystemDisconnect:
        log::write("carrier: the client closed the connection, reason %u", payload.empty() ? 0u : payload[0]);
        m_state = State::disconnected;
        break;
    case kSystemClockSync:
    case kSystemConnectAck:
        break;
    default:
        log::write("carrier: system message %u (%zu bytes) was ignored", id, payload.size());
        break;
    }
}

void Connection::read_acknowledgement(std::span<const std::uint8_t> data) {
    Reader reader{data};
    const std::uint8_t flags = reader.u8();
    if ((flags & kAckBits) != 0) {
        const std::uint16_t last = reader.u16();
        const std::size_t count = flags & 0x7F;
        const auto bits = reader.bytes(count);
        if (reader.overrun) {
            return;
        }
        m_sent.remove_if([&](const SentDatagram& datagram) {
            if (datagram.sequence == last) {
                return true;
            }
            const int offset = distance(datagram.sequence, last) - 1;
            return offset >= 0 && static_cast<std::size_t>(offset) < count * 8
                   && (bits[static_cast<std::size_t>(offset) / 8] & (1 << (offset % 8))) != 0;
        });
    } else if ((flags & kAckContinuous) != 0) {
        const std::uint16_t last = reader.u16();
        const std::uint16_t first = reader.u16();
        if (reader.overrun) {
            return;
        }
        m_sent.remove_if([&](const SentDatagram& datagram) {
            return distance(first, datagram.sequence) >= 0 && distance(datagram.sequence, last) >= 0;
        });
    } else if ((flags & kAckKeepAlive) == 0) {
        const std::uint16_t last = reader.u16();
        if (reader.overrun) {
            return;
        }
        m_sent.remove_if([&](const SentDatagram& datagram) { return datagram.sequence == last; });
    }
}

void Connection::acknowledge(std::uint16_t sequence) {
    if (std::find(m_toAcknowledge.begin(), m_toAcknowledge.end(), sequence) == m_toAcknowledge.end()) {
        m_toAcknowledge.push_back(sequence);
    }
}

Bytes Connection::build_acknowledgement() {
    Bytes data;
    if (m_toAcknowledge.empty()) {
        data.push_back(kAckKeepAlive);
        return data;
    }
    std::sort(m_toAcknowledge.begin(), m_toAcknowledge.end(), [](std::uint16_t a, std::uint16_t b) {
        return distance(a, b) > 0;
    });
    const std::uint16_t first = m_toAcknowledge.front();
    const std::uint16_t last = m_toAcknowledge.back();
    const int span = distance(first, last);
    if (span == 0) {
        data.push_back(0);
        put_u16(data, last);
    } else if (static_cast<std::size_t>(span) == m_toAcknowledge.size() - 1) {
        data.push_back(kAckContinuous);
        put_u16(data, last);
        put_u16(data, first);
    } else {
        const std::size_t count = std::min<std::size_t>((static_cast<std::size_t>(span) + 7) / 8, 0x7F);
        data.push_back(static_cast<std::uint8_t>(kAckBits | count));
        put_u16(data, last);
        Bytes bits(count, 0);
        for (const std::uint16_t sequence : m_toAcknowledge) {
            const int offset = distance(sequence, last) - 1;
            if (offset >= 0 && static_cast<std::size_t>(offset) < count * 8) {
                bits[static_cast<std::size_t>(offset) / 8] |= static_cast<std::uint8_t>(1 << (offset % 8));
            }
        }
        data.insert(data.end(), bits.begin(), bits.end());
    }
    m_toAcknowledge.clear();
    return data;
}

void Connection::queue_message(std::uint8_t channel,
                               std::span<const std::uint8_t> data,
                               Reliability reliability,
                               bool front) {
    std::size_t chunks = 1;
    if (data.size() > kMaxMessageData) {
        reliability = Reliability::reliable;
        chunks = (data.size() + kMaxMessageData - 1) / kMaxMessageData;
    }
    std::size_t offset = 0;
    std::vector<Message> messages;
    do {
        const std::size_t size = std::min(kMaxMessageData, data.size() - offset);
        Message message;
        message.channel = channel;
        message.reliable = reliability == Reliability::reliable;
        message.connecting = m_state == State::connecting;
        message.chunks = static_cast<std::uint16_t>(chunks);
        message.sequence = ++m_sendSequence[channel];
        if (message.reliable) {
            ++m_sendReliableSequence[channel];
        }
        message.reliableSequence = m_sendReliableSequence[channel];
        message.data.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                            data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        messages.push_back(std::move(message));
        offset += size;
        --chunks;
    } while (offset < data.size());
    if (front) {
        m_toSend.insert(m_toSend.begin(), std::make_move_iterator(messages.begin()), std::make_move_iterator(messages.end()));
    } else {
        for (Message& message : messages) {
            m_toSend.push_back(std::move(message));
        }
    }
}

void Connection::send(std::uint8_t channel, std::span<const std::uint8_t> data, Reliability reliability) {
    if (m_state != State::connected || channel >= kSystemChannel || data.empty()) {
        return;
    }
    queue_message(channel, data, reliability, false);
}

void Connection::send_system(std::uint8_t id, std::span<const std::uint8_t> payload, Reliability reliability) {
    Bytes data(payload.begin(), payload.end());
    data.push_back(id);
    queue_message(kSystemChannel, data, reliability, false);
}

void Connection::flush(Clock::time_point now) {
    while (!m_toSend.empty()) {
        Bytes datagram;
        datagram.push_back(kDatagramFlag);
        datagram.push_back(kDatagramVersion);
        const std::uint16_t sequence = ++m_datagramSequence;
        put_u16(datagram, sequence);

        SentDatagram sent;
        sent.sequence = sequence;
        sent.sentAt = now;
        std::uint8_t channel = 0;
        bool wroteSequence[kChannelCount]{};
        bool wroteReliable[kChannelCount]{};
        std::uint16_t lastSequence[kChannelCount]{};
        std::uint16_t lastReliable[kChannelCount]{};
        while (!m_toSend.empty()) {
            Message& message = m_toSend.front();
            const bool writeChannel = message.channel != channel;
            const std::uint8_t messageChannel = message.channel;
            bool writeSequence = true;
            bool writeReliable = true;
            if (wroteSequence[messageChannel]) {
                writeSequence = distance(lastSequence[messageChannel], message.sequence) != 1;
            }
            if (wroteReliable[messageChannel]) {
                writeReliable = message.reliable
                                    ? distance(lastReliable[messageChannel], message.reliableSequence) != 1
                                    : lastReliable[messageChannel] != message.reliableSequence;
            }
            const std::size_t header = 3 + (writeChannel ? 1 : 0) + (message.chunks > 1 ? 2 : 0)
                                       + (writeSequence ? 2 : 0) + (writeReliable ? 2 : 0);
            if (datagram.size() + header + message.data.size() > kMaxDatagram && datagram.size() > 4) {
                break;
            }
            std::uint8_t flags = 0;
            if (message.reliable) flags |= kFlagReliable;
            if (message.chunks > 1) flags |= kFlagChunks;
            if (!writeSequence) flags |= kFlagSequential;
            if (!writeReliable) flags |= kFlagSequentialReliable;
            if (writeChannel) flags |= kFlagChannel;
            if (message.connecting) flags |= kFlagConnecting;
            datagram.push_back(flags);
            put_u16(datagram, static_cast<std::uint16_t>(message.data.size()));
            if (writeChannel) {
                datagram.push_back(messageChannel);
                channel = messageChannel;
            }
            if (message.chunks > 1) {
                put_u16(datagram, message.chunks);
            }
            if (writeSequence) {
                put_u16(datagram, message.sequence);
            }
            if (writeReliable) {
                put_u16(datagram, message.reliableSequence);
            }
            datagram.insert(datagram.end(), message.data.begin(), message.data.end());
            wroteSequence[messageChannel] = true;
            wroteReliable[messageChannel] = true;
            lastSequence[messageChannel] = message.sequence;
            lastReliable[messageChannel] = message.reliableSequence;
            if (message.reliable) {
                sent.reliable.push_back(std::move(message));
            }
            m_toSend.pop_front();
        }
        m_transmit(datagram);
        m_lastSend = now;
        if (!sent.reliable.empty()) {
            m_sent.push_back(std::move(sent));
        }
    }
}

void Connection::tick(Clock::time_point now) {
    if (m_state == State::disconnected) {
        return;
    }
    // Put reliable messages of unacknowledged datagrams back at the front of the queue.
    for (auto iterator = m_sent.begin(); iterator != m_sent.end();) {
        if (now - iterator->sentAt < kResendAfter) {
            ++iterator;
            continue;
        }
        for (auto message = iterator->reliable.rbegin(); message != iterator->reliable.rend(); ++message) {
            m_toSend.push_front(std::move(*message));
        }
        iterator = m_sent.erase(iterator);
    }
    // Acknowledge with the next data datagram, or alone after a short delay. The delay stops
    // an acknowledgement exchange at the tick rate when both sides have no data.
    const bool acknowledgementDue =
        !m_toAcknowledge.empty() && (!m_toSend.empty() || now - m_lastAcknowledgement >= kAcknowledgeAfter);
    const bool keepAlive = now - m_lastSend >= kKeepAliveAfter;
    if (acknowledgementDue || keepAlive) {
        Message message;
        message.channel = kSystemChannel;
        message.reliable = false;
        message.connecting = m_state == State::connecting;
        message.sequence = ++m_sendSequence[kSystemChannel];
        message.reliableSequence = m_sendReliableSequence[kSystemChannel];
        message.data = build_acknowledgement();
        message.data.push_back(kSystemAcks);
        m_toSend.push_front(std::move(message));
        m_lastAcknowledgement = now;
    }
    flush(now);
}

} // namespace soulwarden::backend::server::carrier

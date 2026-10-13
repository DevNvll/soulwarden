#include "server/hub/wire.h"

#include <array>
#include <cstring>

namespace soulwarden::backend::server::hub {

void Writer::u16(std::uint16_t value) {
    m_data.push_back(static_cast<std::uint8_t>(value >> 8));
    m_data.push_back(static_cast<std::uint8_t>(value));
}

void Writer::u32(std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        m_data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::u64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        m_data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::f32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
}

void Writer::size(std::uint32_t value) {
    if (value < 0x80) {
        m_data.push_back(static_cast<std::uint8_t>(value));
    } else if (value < 0x4000) {
        m_data.push_back(static_cast<std::uint8_t>(0x80 | (value & 0x3F)));
        m_data.push_back(static_cast<std::uint8_t>(value >> 6));
    } else if (value < 0x200000) {
        m_data.push_back(static_cast<std::uint8_t>(0xC0 | (value & 0x1F)));
        m_data.push_back(static_cast<std::uint8_t>(value >> 5));
        m_data.push_back(static_cast<std::uint8_t>(value >> 13));
    } else if (value < 0x10000000) {
        m_data.push_back(static_cast<std::uint8_t>(0xE0 | (value & 0x0F)));
        m_data.push_back(static_cast<std::uint8_t>(value >> 4));
        m_data.push_back(static_cast<std::uint8_t>(value >> 12));
        m_data.push_back(static_cast<std::uint8_t>(value >> 20));
    } else {
        m_data.push_back(static_cast<std::uint8_t>(0xF0 | (value & 0x07)));
        m_data.push_back(static_cast<std::uint8_t>(value >> 3));
        m_data.push_back(static_cast<std::uint8_t>(value >> 11));
        m_data.push_back(static_cast<std::uint8_t>(value >> 19));
        m_data.push_back(static_cast<std::uint8_t>(value >> 27));
    }
}

void Writer::size64(std::uint64_t value) {
    // The first byte has N leading one bits for N more bytes and the low bits of the value.
    // The other bytes follow in little-endian order.
    for (unsigned extra = 0; extra < 8; ++extra) {
        const unsigned firstBits = 7 - extra;
        const unsigned totalBits = firstBits + 8 * extra;
        if (totalBits >= 64 || value < (1ULL << totalBits)) {
            const std::uint8_t prefix = static_cast<std::uint8_t>(0xFF00U >> extra);
            m_data.push_back(static_cast<std::uint8_t>(prefix | (value & ((1U << firstBits) - 1))));
            std::uint64_t rest = value >> firstBits;
            for (unsigned index = 0; index < extra; ++index) {
                m_data.push_back(static_cast<std::uint8_t>(rest));
                rest >>= 8;
            }
            return;
        }
    }
    m_data.push_back(0xFF);
    for (unsigned index = 0; index < 8; ++index) {
        m_data.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}

void Writer::string(std::string_view value) {
    size(static_cast<std::uint32_t>(value.size()));
    m_data.insert(m_data.end(), value.begin(), value.end());
}

std::span<const std::uint8_t> Reader::raw(std::size_t count) {
    if (m_failed || left() < count) {
        m_failed = true;
        return {};
    }
    const auto view = m_data.subspan(m_position, count);
    m_position += count;
    return view;
}

std::uint8_t Reader::u8() {
    const auto view = raw(1);
    return view.empty() ? 0 : view[0];
}

std::uint16_t Reader::u16() {
    const auto view = raw(2);
    if (view.empty()) {
        return 0;
    }
    return static_cast<std::uint16_t>((view[0] << 8) | view[1]);
}

std::uint32_t Reader::u32() {
    const auto view = raw(4);
    if (view.empty()) {
        return 0;
    }
    return (static_cast<std::uint32_t>(view[0]) << 24) | (static_cast<std::uint32_t>(view[1]) << 16)
           | (static_cast<std::uint32_t>(view[2]) << 8) | view[3];
}

std::uint64_t Reader::u64() {
    const std::uint64_t high = u32();
    return (high << 32) | u32();
}

float Reader::f32() {
    const std::uint32_t bits = u32();
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t Reader::size64() {
    // The count of leading one bits in the first byte is the count of bytes that follow.
    const std::uint8_t first = u8();
    unsigned extra = 0;
    while (extra < 8 && (first & (0x80U >> extra)) != 0) {
        ++extra;
    }
    std::uint64_t value = extra < 7 ? (first & ((1U << (7 - extra)) - 1)) : 0;
    const unsigned firstBits = extra < 8 ? 7 - extra : 0;
    for (unsigned index = 0; index < extra; ++index) {
        value |= static_cast<std::uint64_t>(u8()) << (firstBits + 8 * index);
    }
    return value;
}

std::uint32_t Reader::size() {
    const std::uint8_t first = u8();
    if (first < 0x80) {
        return first;
    }
    if (first < 0xC0) {
        return (first & 0x3F) | (static_cast<std::uint32_t>(u8()) << 6);
    }
    if (first < 0xE0) {
        const std::uint32_t second = u8();
        return (first & 0x1F) | (second << 5) | (static_cast<std::uint32_t>(u8()) << 13);
    }
    if (first < 0xF0) {
        const std::uint32_t second = u8();
        const std::uint32_t third = u8();
        return (first & 0x0F) | (second << 4) | (third << 12) | (static_cast<std::uint32_t>(u8()) << 20);
    }
    const std::uint32_t second = u8();
    const std::uint32_t third = u8();
    const std::uint32_t fourth = u8();
    return (first & 0x07) | (second << 3) | (third << 11) | (fourth << 19) | (static_cast<std::uint32_t>(u8()) << 27);
}

std::string Reader::string() {
    const std::uint32_t length = size();
    const auto view = raw(length);
    return std::string(view.begin(), view.end());
}

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ ((value & 1) != 0 ? 0xEDB88320U : 0U);
            }
            values[index] = value;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

Bytes encode_frame(std::span<const std::uint8_t> payload) {
    Writer writer;
    writer.u32(crc32(payload));
    writer.u32(static_cast<std::uint32_t>(payload.size()));
    writer.raw(payload);
    return writer.data();
}

bool decode_frame(std::span<const std::uint8_t> frame, std::span<const std::uint8_t>& payload) {
    Reader reader(frame);
    const std::uint32_t checksum = reader.u32();
    const std::uint32_t length = reader.u32();
    payload = reader.raw(length);
    return reader.ok() && crc32(payload) == checksum;
}

} // namespace soulwarden::backend::server::hub

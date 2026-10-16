#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Primitive encodings of the actor message layer ("Hub") that the client uses on the game
// server transport. Proved against the client writers at image RVA 0x876CD0 (u32),
// 0x8770E0 (u64), 0x877970 (variable-length size), and 0x7D5C80 (flags).
//
//   u16, u32, u64   big-endian
//   f32        the 32 bits of the float, big-endian (client reader at image RVA 0x87A270)
//   size       prefix code: 0xxxxxxx | 10xxxxxx +1 byte | 110xxxxx +2 | 1110xxxx +3 | 11110xxx +4
//              The low bits are in the first byte. Later bytes are little-endian.
//   string     size, then the bytes
//   bool       one byte

namespace soulwarden::backend::server::hub {

using Bytes = std::vector<std::uint8_t>;

class Writer {
public:
    void u8(std::uint8_t value) {
        m_data.push_back(value);
    }
    void boolean(bool value) {
        m_data.push_back(value ? 1 : 0);
    }
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void f32(float value);
    void size(std::uint32_t value);
    /** Same prefix code as size, for 64-bit values (client reader at image RVA 0x87B770). */
    void size64(std::uint64_t value);
    void string(std::string_view value);
    void raw(std::span<const std::uint8_t> value) {
        m_data.insert(m_data.end(), value.begin(), value.end());
    }
    void zeros(std::size_t count) {
        m_data.insert(m_data.end(), count, 0);
    }

    [[nodiscard]] const Bytes& data() const {
        return m_data;
    }

private:
    Bytes m_data;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : m_data(data) {}

    [[nodiscard]] bool ok() const {
        return !m_failed;
    }
    [[nodiscard]] std::size_t left() const {
        return m_data.size() - m_position;
    }
    [[nodiscard]] std::size_t position() const {
        return m_position;
    }
    /** @return The bytes that the reader read from the position `start` to its position now. */
    [[nodiscard]] std::span<const std::uint8_t> consumed_since(std::size_t start) const {
        return start <= m_position ? m_data.subspan(start, m_position - start) : std::span<const std::uint8_t>{};
    }

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    float f32();
    std::uint32_t size();
    /** Same prefix code as size, for 64-bit values. */
    std::uint64_t size64();
    std::string string();
    std::span<const std::uint8_t> raw(std::size_t count);

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_position{0};
    bool m_failed{false};
};

/** @return The CRC-32 (IEEE 802.3) of the data. */
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data);

/** @return One transport frame: CRC-32 of the payload, payload size, payload. */
[[nodiscard]] Bytes encode_frame(std::span<const std::uint8_t> payload);

/**
 * Reads one transport frame.
 * @return False when the frame is short or the checksum does not match.
 */
[[nodiscard]] bool decode_frame(std::span<const std::uint8_t> frame, std::span<const std::uint8_t>& payload);

} // namespace soulwarden::backend::server::hub

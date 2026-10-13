#include "middleware/compression/zlib.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace soulwarden::backend::compression {
namespace {

constexpr int kMaxCodeBits = 15;
constexpr std::size_t kLiteralCodes = 288;
constexpr std::size_t kDistanceCodes = 30;
constexpr std::size_t kLengthCodeCount = 19;
constexpr unsigned kEndOfBlock = 256;
constexpr std::uint32_t kAdlerModulus = 65521;

/** Base length and extra bits of the length symbols 257 to 285 (RFC 1951, section 3.2.5). */
constexpr std::array<std::uint16_t, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
/** Base distance and extra bits of the distance symbols 0 to 29. */
constexpr std::array<std::uint16_t, 30> kDistanceBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                      33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistanceExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
/** Order of the code length symbols in the header of a dynamic block. */
constexpr std::array<std::uint8_t, kLengthCodeCount> kLengthCodeOrder{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                                      11, 4,  12, 3, 13, 2, 14, 1, 15};

/** Reads bits from the low bit of each byte, as deflate packs them. */
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data) : m_data(data) {}

    [[nodiscard]] bool ok() const {
        return !m_failed;
    }
    [[nodiscard]] std::size_t position() const {
        return m_position;
    }

    unsigned bits(int count) {
        while (m_bitCount < count) {
            if (m_position >= m_data.size()) {
                m_failed = true;
                return 0;
            }
            m_buffer |= static_cast<std::uint32_t>(m_data[m_position++]) << m_bitCount;
            m_bitCount += 8;
        }
        const unsigned value = m_buffer & ((1U << count) - 1);
        m_buffer >>= count;
        m_bitCount -= count;
        return value;
    }

    /** Drops the bits that remain of the current byte. */
    void align() {
        m_buffer = 0;
        m_bitCount = 0;
    }

    std::span<const std::uint8_t> bytes(std::size_t count) {
        if (m_data.size() - m_position < count) {
            m_failed = true;
            return {};
        }
        const auto result = m_data.subspan(m_position, count);
        m_position += count;
        return result;
    }

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_position{0};
    std::uint32_t m_buffer{0};
    int m_bitCount{0};
    bool m_failed{false};
};

/** Canonical Huffman code: the number of codes of each length, and the symbols in code order. */
struct HuffmanCode {
    std::array<std::uint16_t, kMaxCodeBits + 1> count{};
    std::array<std::uint16_t, kLiteralCodes> symbol{};

    /** @return False when the lengths give more codes than a prefix code can have. */
    bool build(std::span<const std::uint8_t> lengths) {
        count.fill(0);
        for (const std::uint8_t length : lengths) {
            ++count[length];
        }
        int left = 1;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            left = (left << 1) - count[length];
            if (left < 0) {
                return false;
            }
        }
        std::array<std::uint16_t, kMaxCodeBits + 2> offset{};
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            offset[length + 1] = static_cast<std::uint16_t>(offset[length] + count[length]);
        }
        for (std::size_t index = 0; index < lengths.size(); ++index) {
            if (lengths[index] != 0) {
                symbol[offset[lengths[index]]++] = static_cast<std::uint16_t>(index);
            }
        }
        return true;
    }

    /** @return The next symbol, or -1 when the bits are not a code. */
    int decode(BitReader& reader) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= kMaxCodeBits; ++length) {
            code |= static_cast<int>(reader.bits(1));
            if (!reader.ok()) {
                return -1;
            }
            const int codes = count[length];
            if (code - codes < first) {
                return symbol[index + (code - first)];
            }
            index += codes;
            first = (first + codes) << 1;
            code <<= 1;
        }
        return -1;
    }
};

bool inflate_codes(BitReader& reader,
                   const HuffmanCode& literals,
                   const HuffmanCode& distances,
                   std::size_t maximumSize,
                   std::vector<std::uint8_t>& output) {
    for (;;) {
        const int symbol = literals.decode(reader);
        if (symbol < 0) {
            return false;
        }
        if (static_cast<unsigned>(symbol) < kEndOfBlock) {
            if (output.size() >= maximumSize) {
                return false;
            }
            output.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (static_cast<unsigned>(symbol) == kEndOfBlock) {
            return true;
        }
        const std::size_t lengthIndex = static_cast<std::size_t>(symbol) - kEndOfBlock - 1;
        if (lengthIndex >= kLengthBase.size()) {
            return false;
        }
        const std::size_t length = kLengthBase[lengthIndex] + reader.bits(kLengthExtra[lengthIndex]);
        const int distanceSymbol = distances.decode(reader);
        if (distanceSymbol < 0 || static_cast<std::size_t>(distanceSymbol) >= kDistanceBase.size()) {
            return false;
        }
        const std::size_t distance = kDistanceBase[static_cast<std::size_t>(distanceSymbol)]
                                     + reader.bits(kDistanceExtra[static_cast<std::size_t>(distanceSymbol)]);
        if (!reader.ok() || distance > output.size() || output.size() + length > maximumSize) {
            return false;
        }
        // The source and the target can overlap: copy one byte at a time.
        for (std::size_t index = 0; index < length; ++index) {
            output.push_back(output[output.size() - distance]);
        }
    }
}

bool inflate_stored(BitReader& reader, std::size_t maximumSize, std::vector<std::uint8_t>& output) {
    reader.align();
    const auto header = reader.bytes(4);
    if (!reader.ok()) {
        return false;
    }
    const std::size_t length = header[0] | (static_cast<std::size_t>(header[1]) << 8);
    const std::size_t complement = header[2] | (static_cast<std::size_t>(header[3]) << 8);
    if (length != (~complement & 0xFFFF) || output.size() + length > maximumSize) {
        return false;
    }
    const auto data = reader.bytes(length);
    if (!reader.ok()) {
        return false;
    }
    output.insert(output.end(), data.begin(), data.end());
    return true;
}

bool inflate_fixed(BitReader& reader, std::size_t maximumSize, std::vector<std::uint8_t>& output) {
    std::array<std::uint8_t, kLiteralCodes> lengths{};
    for (std::size_t symbol = 0; symbol < kLiteralCodes; ++symbol) {
        lengths[symbol] = symbol < 144 ? 8 : symbol < 256 ? 9 : symbol < 280 ? 7 : 8;
    }
    HuffmanCode literals;
    literals.build(lengths);
    std::array<std::uint8_t, kDistanceCodes> distanceLengths{};
    distanceLengths.fill(5);
    HuffmanCode distances;
    distances.build(distanceLengths);
    return inflate_codes(reader, literals, distances, maximumSize, output);
}

bool inflate_dynamic(BitReader& reader, std::size_t maximumSize, std::vector<std::uint8_t>& output) {
    const std::size_t literalCount = reader.bits(5) + 257;
    const std::size_t distanceCount = reader.bits(5) + 1;
    const std::size_t lengthCodeCount = reader.bits(4) + 4;
    if (!reader.ok() || literalCount > 286 || distanceCount > kDistanceCodes) {
        return false;
    }
    std::array<std::uint8_t, kLengthCodeCount> codeLengths{};
    for (std::size_t index = 0; index < lengthCodeCount; ++index) {
        codeLengths[kLengthCodeOrder[index]] = static_cast<std::uint8_t>(reader.bits(3));
    }
    HuffmanCode lengthCode;
    if (!reader.ok() || !lengthCode.build(codeLengths)) {
        return false;
    }
    // The lengths of the literal codes and of the distance codes are one list.
    std::array<std::uint8_t, kLiteralCodes + kDistanceCodes> lengths{};
    std::size_t index = 0;
    while (index < literalCount + distanceCount) {
        const int symbol = lengthCode.decode(reader);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            lengths[index++] = static_cast<std::uint8_t>(symbol);
            continue;
        }
        std::uint8_t value = 0;
        std::size_t repeat = 0;
        if (symbol == 16) {
            if (index == 0) {
                return false;
            }
            value = lengths[index - 1];
            repeat = 3 + reader.bits(2);
        } else if (symbol == 17) {
            repeat = 3 + reader.bits(3);
        } else {
            repeat = 11 + reader.bits(7);
        }
        if (!reader.ok() || index + repeat > literalCount + distanceCount) {
            return false;
        }
        for (; repeat != 0; --repeat) {
            lengths[index++] = value;
        }
    }
    HuffmanCode literals;
    HuffmanCode distances;
    if (lengths[kEndOfBlock] == 0 || !literals.build(std::span(lengths).first(literalCount))
        || !distances.build(std::span(lengths).subspan(literalCount, distanceCount))) {
        return false;
    }
    return inflate_codes(reader, literals, distances, maximumSize, output);
}

std::uint32_t adler32(std::span<const std::uint8_t> data) {
    std::uint32_t low = 1;
    std::uint32_t high = 0;
    for (const std::uint8_t byte : data) {
        low = (low + byte) % kAdlerModulus;
        high = (high + low) % kAdlerModulus;
    }
    return (high << 16) | low;
}

} // namespace

bool zlib_decode(std::span<const std::uint8_t> stream, std::size_t maximumSize, std::vector<std::uint8_t>& output) {
    output.clear();
    constexpr std::size_t kHeaderSize = 2;
    constexpr std::size_t kChecksumSize = 4;
    constexpr unsigned kMethodDeflate = 8;
    constexpr unsigned kPresetDictionary = 0x20;
    if (stream.size() < kHeaderSize + kChecksumSize || (stream[0] & 0x0F) != kMethodDeflate
        || ((static_cast<unsigned>(stream[0]) << 8) | stream[1]) % 31 != 0 || (stream[1] & kPresetDictionary) != 0) {
        return false;
    }
    BitReader reader(stream.subspan(kHeaderSize));
    bool last = false;
    while (!last) {
        last = reader.bits(1) != 0;
        const unsigned type = reader.bits(2);
        if (!reader.ok()) {
            return false;
        }
        bool ok = false;
        if (type == 0) {
            ok = inflate_stored(reader, maximumSize, output);
        } else if (type == 1) {
            ok = inflate_fixed(reader, maximumSize, output);
        } else if (type == 2) {
            ok = inflate_dynamic(reader, maximumSize, output);
        }
        if (!ok) {
            return false;
        }
    }
    reader.align();
    const auto checksum = reader.bytes(kChecksumSize);
    if (!reader.ok()) {
        return false;
    }
    const std::uint32_t expected = (static_cast<std::uint32_t>(checksum[0]) << 24)
                                   | (static_cast<std::uint32_t>(checksum[1]) << 16)
                                   | (static_cast<std::uint32_t>(checksum[2]) << 8) | checksum[3];
    return adler32(output) == expected;
}

std::vector<std::uint8_t> zlib_store(std::span<const std::uint8_t> data) {
    // Header: deflate with a window of 32 kilobytes, no preset dictionary, fastest level.
    constexpr std::uint8_t kMethodAndWindow = 0x78;
    constexpr std::uint8_t kHeaderFlags = 0x01;
    /** Largest size of a stored block: its size field has 16 bits. */
    constexpr std::size_t kMaxStoredBlock = 0xFFFF;
    std::vector<std::uint8_t> stream{kMethodAndWindow, kHeaderFlags};
    std::size_t position = 0;
    do {
        const std::size_t size = std::min(data.size() - position, kMaxStoredBlock);
        const bool last = position + size == data.size();
        // Block header: bit 0 marks the last block, type 0 is stored. Then the size and its
        // complement, low byte first.
        stream.push_back(last ? 1 : 0);
        stream.push_back(static_cast<std::uint8_t>(size & 0xFF));
        stream.push_back(static_cast<std::uint8_t>(size >> 8));
        stream.push_back(static_cast<std::uint8_t>(~size & 0xFF));
        stream.push_back(static_cast<std::uint8_t>((~size >> 8) & 0xFF));
        stream.insert(stream.end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                      data.begin() + static_cast<std::ptrdiff_t>(position + size));
        position += size;
    } while (position < data.size());
    const std::uint32_t checksum = adler32(data);
    for (int shift = 24; shift >= 0; shift -= 8) {
        stream.push_back(static_cast<std::uint8_t>(checksum >> shift));
    }
    return stream;
}

} // namespace soulwarden::backend::compression

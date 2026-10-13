#include "middleware/compression/lz4.h"

namespace soulwarden::backend::compression {

bool lz4_decode(std::span<const std::uint8_t> block, std::size_t maximumSize, std::vector<std::uint8_t>& output) {
    output.clear();
    std::size_t position = 0;
    const auto read_length = [&](std::size_t base, std::size_t& length) {
        length = base;
        if (base != 15) {
            return true;
        }
        for (;;) {
            if (position >= block.size()) {
                return false;
            }
            const std::uint8_t extra = block[position++];
            length += extra;
            if (extra != 255) {
                return true;
            }
        }
    };
    while (position < block.size()) {
        const std::uint8_t token = block[position++];
        std::size_t literals = 0;
        if (!read_length(token >> 4, literals) || block.size() - position < literals
            || output.size() + literals > maximumSize) {
            return false;
        }
        output.insert(output.end(), block.begin() + static_cast<std::ptrdiff_t>(position),
                      block.begin() + static_cast<std::ptrdiff_t>(position + literals));
        position += literals;
        if (position == block.size()) {
            return true; // The last sequence has literals only.
        }
        if (block.size() - position < 2) {
            return false;
        }
        const std::size_t offset = block[position] | (static_cast<std::size_t>(block[position + 1]) << 8);
        position += 2;
        std::size_t match = 0;
        if (!read_length(token & 0x0F, match)) {
            return false;
        }
        match += 4;
        if (offset == 0 || offset > output.size() || output.size() + match > maximumSize) {
            return false;
        }
        const std::size_t start = output.size() - offset;
        for (std::size_t index = 0; index < match; ++index) {
            output.push_back(output[start + index]);
        }
    }
    return true;
}

std::vector<std::uint8_t> lz4_encode_literals(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> block;
    std::size_t length = data.size();
    if (length < 15) {
        block.push_back(static_cast<std::uint8_t>(length << 4));
    } else {
        block.push_back(0xF0);
        length -= 15;
        while (length >= 255) {
            block.push_back(255);
            length -= 255;
        }
        block.push_back(static_cast<std::uint8_t>(length));
    }
    block.insert(block.end(), data.begin(), data.end());
    return block;
}

} // namespace soulwarden::backend::compression

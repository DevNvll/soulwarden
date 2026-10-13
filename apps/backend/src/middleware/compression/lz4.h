#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace soulwarden::backend::compression {

/**
 * Decodes one LZ4 block. The output is limited to maximumSize bytes.
 * @return False when the block is malformed or the output is larger than the limit.
 */
[[nodiscard]] bool lz4_decode(std::span<const std::uint8_t> block, std::size_t maximumSize, std::vector<std::uint8_t>& output);

/** Encodes data as one LZ4 block that contains literals only. Every LZ4 decoder accepts it. */
[[nodiscard]] std::vector<std::uint8_t> lz4_encode_literals(std::span<const std::uint8_t> data);

} // namespace soulwarden::backend::compression

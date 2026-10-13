#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace soulwarden::backend::compression {

/**
 * Decodes one zlib stream (RFC 1950: a 2-byte header, deflate data of RFC 1951, and the
 * Adler-32 checksum of the output). The output is limited to maximumSize bytes.
 * @return False when the stream is malformed, the checksum does not match, or the output is
 *         larger than the limit.
 */
[[nodiscard]] bool zlib_decode(std::span<const std::uint8_t> stream, std::size_t maximumSize, std::vector<std::uint8_t>& output);

/**
 * @return One zlib stream that has the data in stored blocks (no compression). Each zlib
 *         reader accepts it. The backend writes only small documents in this form.
 */
[[nodiscard]] std::vector<std::uint8_t> zlib_store(std::span<const std::uint8_t> data);

} // namespace soulwarden::backend::compression

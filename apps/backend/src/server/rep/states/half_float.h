#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

// The IEEE half float of the wire (big-endian through hub::Writer::u16): the potency of a status entry (client reader
// 0x2A42F80 -> 0x87AE90) and the remaining time of a delayed event (0x3066370).

namespace soulwarden::backend::server::rep {

/** @return The IEEE half of a float (round to nearest). A value above the largest half is the largest half. */
inline std::uint16_t half_of(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000U);
    const int exponent = static_cast<int>((bits >> 23) & 0xFFU) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFU;
    if (!std::isfinite(value) || exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7BFFU);
    }
    if (exponent <= 0) {
        return sign;
    }
    mantissa += 0x1000U;
    std::uint32_t result = (static_cast<std::uint32_t>(exponent) << 10) + (mantissa >> 13);
    if (result >= 0x7C00U) {
        result = 0x7BFFU;
    }
    return static_cast<std::uint16_t>(sign | result);
}

} // namespace soulwarden::backend::server::rep

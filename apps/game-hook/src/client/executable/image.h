#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace soulwarden::client::executable {

/** One mapped section of the game executable. */
struct Section {
    char name[9]{};
    std::uint8_t* begin{};
    std::size_t size{};
    bool writable{};
    bool executable{};
};

/** @return The mapped sections of the main executable. The list is built once. */
[[nodiscard]] std::span<const Section> sections() noexcept;

/** @return The load address of the main executable. */
[[nodiscard]] std::uint8_t* base() noexcept;

/** @return The first match of a byte pattern in a range, or null. */
[[nodiscard]] std::uint8_t* find(std::uint8_t* begin, std::size_t size, const void* pattern, std::size_t patternSize) noexcept;

} // namespace soulwarden::client::executable

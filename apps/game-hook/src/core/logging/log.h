#pragma once

#include <Windows.h>

namespace soulwarden::core::log {

/** Opens the hook log in the client data directory. Safe to call more than once. */
void initialize() noexcept;

/** Appends one formatted line. Each line carries a millisecond tick and the thread id. */
void write(const char* format, ...) noexcept;

/** @return The client data directory as a wide path without a trailing separator. */
[[nodiscard]] const wchar_t* data_directory() noexcept;

} // namespace soulwarden::core::log

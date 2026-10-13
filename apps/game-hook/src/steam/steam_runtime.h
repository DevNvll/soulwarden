#pragma once

#include <cstdint>

namespace soulwarden::steam {

using UserHandle = std::int32_t;
using ApiCall = std::uint64_t;

/** Steam application id of New World: Aeternum. */
inline constexpr std::uint32_t kAppId = 1063730;

/**
 * Fallback identity: an individual account in the public universe. It applies when the
 * launcher supplies no Steam account. The backend has the same value for a login with no
 * local ticket.
 */
inline constexpr std::uint64_t kLocalSteamId = 0x0110000100A207A1ULL;

/** Starts the process-local Steam runtime. @return True on success. */
[[nodiscard]] bool initialize() noexcept;

/** Stops the runtime and drops every registration. */
void shutdown() noexcept;

/** Delivers queued callbacks on the calling thread. */
void run_callbacks() noexcept;

/** @return The single local user handle. */
[[nodiscard]] UserHandle user_handle() noexcept;

/** Registers a game-owned callback object for one callback id. */
void register_callback(void* callback, int callbackId) noexcept;

/** Removes a game-owned callback object. */
void unregister_callback(void* callback) noexcept;

/** @return The interface for a version string, or null when the version is unknown. */
[[nodiscard]] void* find_interface(const char* version) noexcept;

/** Runs the context initializer once per runtime start. @return The context storage. */
[[nodiscard]] void* context_init(void* contextInitData) noexcept;

/** Queues a callback payload. The payload is copied. */
void post_callback(int callbackId, const void* payload, std::uint32_t size) noexcept;

} // namespace soulwarden::steam

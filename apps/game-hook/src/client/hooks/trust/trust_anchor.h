#pragma once

namespace soulwarden::client::hooks::trust {

/**
 * Makes the client trust the local game server.
 * The client keeps one certificate authority for the game server transport. This function
 * reads the certificate of the local backend and points the client at it.
 * @return True when the trust anchor was replaced.
 */
[[nodiscard]] bool install() noexcept;

} // namespace soulwarden::client::hooks::trust

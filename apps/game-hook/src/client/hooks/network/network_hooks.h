#pragma once

namespace soulwarden::client::hooks::network {

/**
 * Installs the egress guard and the endpoint routes.
 * Name resolution, sockets, WinHTTP, and WinINet go to the configured local backend.
 * @return True when every hook is attached.
 */
[[nodiscard]] bool install() noexcept;

} // namespace soulwarden::client::hooks::network

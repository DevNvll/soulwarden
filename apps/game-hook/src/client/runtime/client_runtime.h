#pragma once

namespace soulwarden::client::runtime {

/** Installs the hooks that must be active before the first game network call. */
void attach() noexcept;

/** Starts the client features that need the game runtime. Safe to call more than once. */
void activate() noexcept;

} // namespace soulwarden::client::runtime

#pragma once

#include "client/hooking/detour.h"

// The hook of the present call of the game. The client has one present call, in the
// submission thread of its renderer (client_targets.h). The hook is at the instructions of
// that call site, not in the present function: with NVIDIA Streamline the function is a
// function of sl.interposer.dll, with no Streamline it is a function of the DXGI library, and
// a different thread of such a library can call it too. The call site is the same in each case.

namespace soulwarden::client::toolwindow::graphics {

/** @return True when the bytes at the present call are the code of the supported build. */
[[nodiscard]] bool present_call_matches() noexcept;

/** @return The detour of the present call site. The caller installs it. */
[[nodiscard]] hooking::detour::Spec present_call_detour() noexcept;

} // namespace soulwarden::client::toolwindow::graphics

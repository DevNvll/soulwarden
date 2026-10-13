#pragma once

#include <span>

namespace soulwarden::client::hooking::detour {

/** Describes one target-to-replacement attachment. The original is published in place. */
struct Spec {
    /** In: address of the target function. Out: trampoline to the original function. */
    void** original{};
    void* replacement{};
};

/** Attaches all specs in one transaction or leaves every target unchanged. */
[[nodiscard]] bool install(std::span<const Spec> specs) noexcept;

} // namespace soulwarden::client::hooking::detour

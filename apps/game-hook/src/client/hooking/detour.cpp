#include "client/hooking/detour.h"

#include <Windows.h>

#include <detours.h>

namespace soulwarden::client::hooking::detour {

bool install(std::span<const Spec> specs) noexcept {
    if (DetourTransactionBegin() != NO_ERROR) {
        return false;
    }
    DetourUpdateThread(GetCurrentThread());
    for (const Spec& spec : specs) {
        if (*spec.original == nullptr
            || DetourAttach(spec.original, spec.replacement) != NO_ERROR) {
            DetourTransactionAbort();
            return false;
        }
    }
    return DetourTransactionCommit() == NO_ERROR;
}

} // namespace soulwarden::client::hooking::detour

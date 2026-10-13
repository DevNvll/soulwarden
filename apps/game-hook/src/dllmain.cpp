#include <Windows.h>

#include "client/runtime/client_runtime.h"
#include "core/logging/log.h"
#include "steam/steam_runtime.h"

// The game imports eight functions from steam_api64.dll. This DLL provides them and
// starts the client hooks before the game calls its first network function.

/** @return True when the Steam-compatible runtime and the client hooks start. */
extern "C" __declspec(dllexport) bool SteamAPI_Init() noexcept {
    soulwarden::client::runtime::activate();
    return soulwarden::steam::initialize();
}

/** Stops the Steam-compatible runtime. */
extern "C" __declspec(dllexport) void SteamAPI_Shutdown() noexcept {
    soulwarden::steam::shutdown();
}

/** Delivers one batch of callbacks on the caller thread. */
extern "C" __declspec(dllexport) void SteamAPI_RunCallbacks() noexcept {
    soulwarden::steam::run_callbacks();
}

/** @return The single local Steam user handle. */
extern "C" __declspec(dllexport) soulwarden::steam::UserHandle SteamAPI_GetHSteamUser() noexcept {
    return soulwarden::steam::user_handle();
}

/** Registers a game-owned callback object. */
extern "C" __declspec(dllexport) void SteamAPI_RegisterCallback(void* callback,
                                                                int callbackId) noexcept {
    soulwarden::steam::register_callback(callback, callbackId);
}

/** Removes a game-owned callback object. */
extern "C" __declspec(dllexport) void SteamAPI_UnregisterCallback(void* callback) noexcept {
    soulwarden::steam::unregister_callback(callback);
}

/** @return The interface for a version string. The user handle is not used. */
extern "C" __declspec(dllexport) void*
SteamInternal_FindOrCreateUserInterface(soulwarden::steam::UserHandle, const char* version) noexcept {
    return soulwarden::steam::find_interface(version);
}

/** @return The context storage for one accessor, initialized once per runtime start. */
extern "C" __declspec(dllexport) void* SteamInternal_ContextInit(void* contextInitData) noexcept {
    return soulwarden::steam::context_init(contextInitData);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        soulwarden::core::log::initialize();
        soulwarden::core::log::write("hook: attached, module %p", static_cast<void*>(module));
        soulwarden::client::runtime::attach();
    }
    return TRUE;
}

#include "client/toolwindow/tool_window.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "client/hooking/detour.h"
#include "client/toolwindow/client_targets.h"
#include "client/toolwindow/graphics/present_hook.h"
#include "client/toolwindow/graphics/renderer.h"
#include "client/toolwindow/input/pointer_guard.h"
#include "client/toolwindow/input/window_input.h"
#include "client/toolwindow/model/admin_client.h"
#include "core/logging/log.h"

namespace soulwarden::client::toolwindow {
namespace {

Settings g_settings;
/** True until start() installed the hooks, and after stop(). */
std::atomic<bool> g_stopped{true};
std::atomic<bool> g_visible{false};
/** One thread at a time changes the visible state and the pointer. */
SRWLOCK g_visibleLock = SRWLOCK_INIT;

/** @return The text of an environment variable, or an empty text. */
const char* variable(const char* name, char* buffer, DWORD size) noexcept {
    const DWORD length = GetEnvironmentVariableA(name, buffer, size);
    if (length == 0 || length >= size) {
        buffer[0] = '\0';
    }
    return buffer;
}

} // namespace

const Settings& settings() noexcept {
    return g_settings;
}

bool stopped() noexcept {
    return g_stopped.load();
}

bool visible() noexcept {
    return g_visible.load();
}

void stop(const char* format, ...) noexcept {
    char cause[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(cause, sizeof(cause), format, arguments);
    va_end(arguments);
    if (!g_stopped.exchange(true)) {
        (void)set_visible(false);
    }
    // A button can be down on the tool window at this moment: the window thread releases the
    // pointer capture of the hook and clears the button state (input/window_input.cpp).
    input::reset();
    core::log::write("tool window: OFF for this start of the game: %s. The game continues with no tool window.", cause);
}

bool set_visible(bool visible) noexcept {
    AcquireSRWLockExclusive(&g_visibleLock);
    const bool possible = !visible || (!g_stopped.load() && graphics::ready());
    if (possible && g_visible.load() != visible) {
        if (visible) {
            input::reset();
            graphics::forget_input();
            g_visible = true;
            input::release_pointer();
            model::set_active(true);
        } else {
            g_visible = false;
            model::set_active(false);
            input::reset();
            graphics::forget_input();
            input::restore_pointer();
        }
        core::log::write("tool window: %s", visible ? "shown" : "hidden");
    }
    ReleaseSRWLockExclusive(&g_visibleLock);
    return possible;
}

void start() noexcept {
    char text[64];
    if (std::strcmp(variable("SOULWARDEN_TOOL_WINDOW", text, sizeof(text)), "0") == 0) {
        core::log::write("tool window: off (SOULWARDEN_TOOL_WINDOW=0). No hook of the tool window is installed.");
        return;
    }
    g_settings.harness = std::strcmp(variable("SOULWARDEN_HARNESS", text, sizeof(text)), "1") == 0;
    const unsigned long key = std::strtoul(variable("SOULWARDEN_TOOL_WINDOW_KEY", text, sizeof(text)), nullptr, 0);
    if (key >= 1 && key <= 254) {
        g_settings.toggleKey = static_cast<UINT>(key);
    }
    // A test of the start with a fault. It is diagnostics: only a process with the harness gate reads it.
    if (g_settings.harness) {
        variable("SOULWARDEN_TOOL_WINDOW_FAULT", text, sizeof(text));
        g_settings.fault = std::strcmp(text, "hook") == 0        ? Fault::hook
                           : std::strcmp(text, "device") == 0    ? Fault::device
                           : std::strcmp(text, "swapchain") == 0 ? Fault::swapChain
                                                                 : Fault::none;
        if (g_settings.fault != Fault::none) {
            core::log::write("tool window: TEST FAULT %s (SOULWARDEN_TOOL_WINDOW_FAULT)", text);
        }
    }

    // Each code address must have the code of the supported client build.
    if (g_settings.fault == Fault::hook || !graphics::present_call_matches()) {
        stop("the client code at the present call (image address 0x%llX) is not the code of the supported build",
             static_cast<unsigned long long>(targets::kPresentCallSite));
        return;
    }
    if (!input::window_procedure_matches()) {
        stop("the client code at the window procedure (image address 0x%llX) is not the code of the supported build",
             static_cast<unsigned long long>(targets::kWindowProcedure));
        return;
    }
    if (!input::install_pointer_guard()) {
        stop("an import slot of the client for the pointer (SetCursorPos, ClipCursor) does not have the expected function");
        return;
    }
    if (!input::install_raw_input_filter()) {
        stop("the import slot of the client for the raw input (GetRawInputData) does not have the expected function");
        return;
    }
    // From here the hooks can run: they do nothing while the tool window is stopped.
    g_stopped = false;
    const hooking::detour::Spec specs[]{graphics::present_call_detour(), input::window_procedure_detour()};
    if (!hooking::detour::install(specs)) {
        stop("the hook of the present call or of the window procedure was not installed");
        return;
    }
    model::start();
    core::log::write("tool window: on, key 0x%02X. Hooks: present call at image address 0x%llX, window procedure at 0x%llX",
                     g_settings.toggleKey,
                     static_cast<unsigned long long>(targets::kPresentCallSite),
                     static_cast<unsigned long long>(targets::kWindowProcedure));
}

} // namespace soulwarden::client::toolwindow

#pragma once

#include <Windows.h>

// The tool window: one Dear ImGui window that the hook draws on the picture of the game. It
// shows the state of a character of the account and sends commands to the admin interface
// of the local backend (items, coin, level, attributes, skills).
//
// Threads:
// - The submission thread of the renderer makes each Dear ImGui call (graphics/renderer.h).
// - The window thread of the game reads the input messages and puts them in a queue
//   (input/window_input.h). It makes no Dear ImGui call.
// - One worker thread sends the requests to the backend (model/admin_client.h).
//
// The tool window must never stop the game: when a part of its start fails, it stays off for
// the rest of the process, and the hook log has one line with the cause.

namespace soulwarden::client::toolwindow {

/** A fault that a test makes in the start of the tool window (SOULWARDEN_TOOL_WINDOW_FAULT, harness gate). */
enum class Fault {
    none,
    /** The client code at the present call is not the code of the supported build. */
    hook,
    /** The client has no Direct3D 12 device at the first present call. */
    device,
    /** The swap chain gives no description at the first present call. */
    swapChain,
};

struct Settings {
    /** Virtual key that shows and hides the window (SOULWARDEN_TOOL_WINDOW_KEY, default F8). */
    UINT toggleKey{VK_F8};
    Fault fault{Fault::none};
    /** True when the process has SOULWARDEN_HARNESS=1: the input code then counts the messages for the log. */
    bool harness{false};
};

/**
 * Starts the tool window: reads its switches, checks the client code, and installs the hook
 * of the present call and the hook of the window procedure. With SOULWARDEN_TOOL_WINDOW=0 it
 * installs nothing. Call it one time, before the trace points of the diagnostics.
 */
void start() noexcept;

[[nodiscard]] const Settings& settings() noexcept;

/**
 * Turns the tool window off for the rest of the process and writes one log line with the
 * cause. The hooks stay and give each call to the game with no change. Any thread.
 */
void stop(const char* format, ...) noexcept;

/** @return True after stop(), and when the tool window was not started. */
[[nodiscard]] bool stopped() noexcept;

/** @return True while the window shows. Any thread. */
[[nodiscard]] bool visible() noexcept;

/**
 * Shows or hides the window. While it shows, the pointer and the keys go to the window and
 * not to the game. Any thread.
 * @return False when the window cannot show: the tool window is off, or the renderer is not ready.
 */
bool set_visible(bool visible) noexcept;

} // namespace soulwarden::client::toolwindow

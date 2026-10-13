#pragma once

#include <Windows.h>

struct IDXGISwapChain3;

// The Direct3D 12 renderer of the tool window. The client presents on a thread of its own
// (the submission thread of its renderer, protocol notes 6.1). The hook calls on_present on
// that thread, directly before the present call of the game. Each Dear ImGui call and each
// call of the Direct3D objects of the tool window is on that thread.
//
// The renderer uses the device and the command queue of the game. It draws one command list
// on the back buffer that the game presents next, and it keeps no reference to a back buffer
// after the frame: the game can resize its swap chain with no call of the hook.

namespace soulwarden::client::toolwindow::graphics {

/**
 * Submission thread: the game presents this swap chain now. The first call makes the
 * Direct3D objects and the Dear ImGui context. A later call draws the tool window when it
 * shows. A failure turns the tool window off (toolwindow::stop) and never stops the game.
 */
void on_present(IDXGISwapChain3* chain, UINT flags) noexcept;

/** @return True when the first present call made all objects. Any thread. */
[[nodiscard]] bool ready() noexcept;

/** @return The game window (the output window of the swap chain), or null before the renderer is ready. Any thread. */
[[nodiscard]] HWND window() noexcept;

/** The window closed: the next frame starts with no key and no button down. Any thread. */
void forget_input() noexcept;

/**
 * A test of the layout: sets the scale of the text and of the controls in percent (100 to
 * 300), as a picture with a larger height does. 0: the scale of the picture height again. The
 * harness calls it. Any thread.
 */
void force_scale(unsigned percent) noexcept;

} // namespace soulwarden::client::toolwindow::graphics

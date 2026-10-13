#pragma once

#include <Windows.h>

#include "client/hooking/detour.h"

struct ImGuiIO;

// The input of the tool window. The hook replaces the root window procedure of the game
// window (window thread). While the tool window shows, the procedure keeps the pointer and
// key messages from the game and puts them in a queue. The submission thread of the renderer
// takes the queue before each Dear ImGui frame. The window thread makes no Dear ImGui call:
// Dear ImGui has no lock, and the present call is on a different thread.
//
// Rules while the tool window shows (protocol notes 6.3):
// - Pointer, key, and character messages go to the queue. The game does not get them.
// - The game reads its keys and its pointer from raw input (WM_INPUT). It gets no raw packet
//   of a key press, of a button press, of the wheel, or of a pointer movement. It gets the
//   raw packets of a key release and of a button release: a key that was down when the
//   window opened is then released in the game.
//   A raw pointer packet can have a button release together with a movement, a different
//   button press, or the wheel. The game gets such a packet too, and it reads only the
//   releases of it: the hook replaces the import slot of the game for
//   USER32!GetRawInputData (install_raw_input_filter).
// - The game never gets the toggle key (default F8), also while the window does not show.
// - Alt+F4 and each other message go to the game with no change.

namespace soulwarden::client::toolwindow::input {

/** @return The detour of the root window procedure. The caller installs it. */
[[nodiscard]] hooking::detour::Spec window_procedure_detour() noexcept;

/** @return True when the bytes at the window procedure are the code of the supported build. */
[[nodiscard]] bool window_procedure_matches() noexcept;

/**
 * Submission thread: gives the queued messages to Dear ImGui in their sequence.
 * @param scaleX, scaleY Size of the back buffer divided by the client size of the window.
 */
void drain(ImGuiIO& io, float scaleX, float scaleY) noexcept;

/**
 * Replaces the import slot of the client for USER32!GetRawInputData: the game then reads only
 * the button releases of a raw pointer packet while the tool window shows. The slot must
 * have the address of the USER32 function: if not, nothing changes.
 * @return False when the slot does not have the expected function, or the write failed.
 */
[[nodiscard]] bool install_raw_input_filter() noexcept;

/**
 * Empties the queue, and makes the window thread release the pointer capture and the button
 * state of the tool window (the tool window closed or stopped). Any thread.
 */
void reset() noexcept;

/** @return True when the system shows its pointer on the game window (the game hides it in the world). */
[[nodiscard]] bool system_pointer_visible() noexcept;

} // namespace soulwarden::client::toolwindow::input

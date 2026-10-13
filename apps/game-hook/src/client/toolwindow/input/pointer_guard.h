#pragma once

#include <cstdint>

// The pointer guard. In the world the game puts the pointer back to the center of its window
// and limits it to the window (USER32!SetCursorPos and USER32!ClipCursor). While the tool
// window shows, the guard holds these two calls of the game, so that the user can move the
// pointer to the tool window. It changes only the two import slots of the client image: a
// call of a different module is not changed.

namespace soulwarden::client::toolwindow::input {

/**
 * Replaces the two import slots of the client. Each slot must have the address of its USER32
 * function: if not, nothing changes.
 * @return False when a slot does not have the expected function, or the write failed.
 */
[[nodiscard]] bool install_pointer_guard() noexcept;

/**
 * Writes one import slot of the client image when it has the address of the USER32 function
 * `name`. @return False when the slot has a different value, or the write failed: nothing
 * changed then.
 */
[[nodiscard]] bool replace_import_slot(std::uintptr_t slotAddress, const char* name, void* replacement) noexcept;

/** The tool window opened: the pointer is free. Any thread. */
void release_pointer() noexcept;

/** The tool window closed: the limit that the game set last applies again. Any thread. */
void restore_pointer() noexcept;

} // namespace soulwarden::client::toolwindow::input

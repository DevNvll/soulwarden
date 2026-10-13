#include "client/toolwindow/input/pointer_guard.h"

#include <Windows.h>

#include "client/executable/image.h"
#include "client/toolwindow/client_targets.h"
#include "client/toolwindow/tool_window.h"

namespace soulwarden::client::toolwindow::input {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
/** The limit that the game set last. Protected by the lock. */
RECT g_gameClip{};
bool g_gameClipSet = false;

BOOL WINAPI guarded_set_cursor_pos(int x, int y) noexcept {
    if (visible()) {
        return TRUE;
    }
    return SetCursorPos(x, y);
}

BOOL WINAPI guarded_clip_cursor(const RECT* rect) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_gameClipSet = rect != nullptr;
    if (rect != nullptr) {
        g_gameClip = *rect;
    }
    const bool hold = visible();
    ReleaseSRWLockExclusive(&g_lock);
    return hold ? TRUE : ClipCursor(rect);
}

} // namespace

bool replace_import_slot(std::uintptr_t slotAddress, const char* name, void* replacement) noexcept {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* const expected = user32 != nullptr ? reinterpret_cast<void*>(GetProcAddress(user32, name)) : nullptr;
    auto* const slot = reinterpret_cast<void**>(executable::base() + slotAddress);
    MEMORY_BASIC_INFORMATION information{};
    if (expected == nullptr || VirtualQuery(slot, &information, sizeof(information)) == 0
        || information.State != MEM_COMMIT || *slot != expected) {
        return false;
    }
    DWORD protection = 0;
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection) == 0) {
        return false;
    }
    *slot = replacement;
    VirtualProtect(slot, sizeof(void*), protection, &protection);
    return true;
}

bool install_pointer_guard() noexcept {
    // The first slot only when the second one is correct too: no half guard.
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr
        || *reinterpret_cast<void**>(executable::base() + targets::kClipCursorImport)
               != reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor"))) {
        return false;
    }
    return replace_import_slot(targets::kSetCursorPosImport, "SetCursorPos", reinterpret_cast<void*>(&guarded_set_cursor_pos))
           && replace_import_slot(targets::kClipCursorImport, "ClipCursor", reinterpret_cast<void*>(&guarded_clip_cursor));
}

void release_pointer() noexcept {
    ClipCursor(nullptr);
}

void restore_pointer() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool set = g_gameClipSet;
    const RECT clip = g_gameClip;
    ReleaseSRWLockExclusive(&g_lock);
    ClipCursor(set ? &clip : nullptr);
}

} // namespace soulwarden::client::toolwindow::input

#include "client/toolwindow/input/window_input.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <imgui.h>

#include "client/executable/image.h"
#include "client/toolwindow/client_targets.h"
#include "client/toolwindow/graphics/renderer.h"
#include "client/toolwindow/input/pointer_guard.h"
#include "client/toolwindow/tool_window.h"
#include "core/logging/log.h"

// The key table of the Win32 part of Dear ImGui (imgui_impl_win32.cpp). The function has
// external linkage and no state. The hook does not use the message handler and the frame
// function of that part: they read the keyboard state of the calling thread.
ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM wParam, LPARAM lParam);

namespace soulwarden::client::toolwindow::input {
namespace {

using WindowProcedure = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

constexpr std::size_t kQueueSize = 256;
constexpr std::uint8_t kControl = 1;
constexpr std::uint8_t kShift = 2;
constexpr std::uint8_t kAlt = 4;
constexpr std::uint8_t kSuper = 8;
/** Bit 30 of the lParam of a key message: the key was down before this message (a repeat). */
constexpr LPARAM kKeyRepeat = LPARAM{1} << 30;
/** Message that the queue uses for a button release that no message of the system gave. */
constexpr UINT kReleaseButtons = WM_APP + 0x2A5;

/** One window message with the modifier keys that the window thread saw at that time. */
struct Event {
    UINT message{};
    WPARAM word{};
    LPARAM value{};
    std::uint8_t modifiers{};
};

/** In: the address of the window procedure of the game. Out: the trampoline to it. */
void* g_original = nullptr;

SRWLOCK g_lock = SRWLOCK_INIT;
Event g_queue[kQueueSize];
std::size_t g_count = 0;

std::atomic<bool> g_systemPointer{true};
/** Window thread only: the pointer buttons that are down in the tool window (bit 0 left, 1 right, 2 middle). */
unsigned g_buttons = 0;
/** Window thread only: the hook took the pointer capture for a button of the tool window. */
bool g_captured = false;
/** The button release flags of a raw pointer packet. */
constexpr USHORT kRawReleases = RI_MOUSE_BUTTON_1_UP | RI_MOUSE_BUTTON_2_UP | RI_MOUSE_BUTTON_3_UP
                                | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
/**
 * The raw pointer packet that the game reads now, while the tool window shows: the game gets
 * only its button releases. Null at all other times. The window thread sets it for the time of
 * one call of the window procedure of the game.
 */
std::atomic<HRAWINPUT> g_releasesOnly{nullptr};
/** Counts for the log of a harness run: raw pointer packets with a release that the game got while the window showed. */
std::uint32_t g_releasePackets = 0;
std::uint32_t g_mixedReleasePackets = 0;

// Message counts for the log of a harness run: they show which messages the system sends.
enum Counter : std::size_t {
    kKeyDown, kKeyUp, kChar, kMouseMove, kButtonDown, kButtonUp, kWheel, kRawKeyboard, kRawMouse, kCounterCount
};
std::uint32_t g_counters[kCounterCount]{};
std::uint32_t g_loggedCounters[kCounterCount]{};
DWORD g_lastCounterLog = 0;

void count(Counter counter) noexcept {
    ++g_counters[counter];
}

void log_counters() noexcept {
    const DWORD now = GetTickCount();
    if (now - g_lastCounterLog < 5000 || std::memcmp(g_counters, g_loggedCounters, sizeof(g_counters)) == 0) {
        return;
    }
    g_lastCounterLog = now;
    std::memcpy(g_loggedCounters, g_counters, sizeof(g_counters));
    core::log::write("tool window: messages of the game window: key down %u, key up %u, char %u, pointer move %u, "
                     "button down %u, button up %u, wheel %u, raw keyboard %u, raw pointer %u",
                     g_counters[kKeyDown], g_counters[kKeyUp], g_counters[kChar], g_counters[kMouseMove],
                     g_counters[kButtonDown], g_counters[kButtonUp], g_counters[kWheel], g_counters[kRawKeyboard],
                     g_counters[kRawMouse]);
}

std::uint8_t modifiers() noexcept {
    return static_cast<std::uint8_t>(((GetKeyState(VK_CONTROL) & 0x8000) != 0 ? kControl : 0)
                                     | ((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? kShift : 0)
                                     | ((GetKeyState(VK_MENU) & 0x8000) != 0 ? kAlt : 0)
                                     | (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0 ? kSuper : 0));
}

/** Window thread: keeps one message for the submission thread. A full queue drops the message. */
void push(UINT message, WPARAM word, LPARAM value) noexcept {
    const Event event{message, word, value, modifiers()};
    AcquireSRWLockExclusive(&g_lock);
    if (g_count < kQueueSize) {
        g_queue[g_count++] = event;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void update_system_pointer() noexcept {
    CURSORINFO information{};
    information.cbSize = sizeof(information);
    if (GetCursorInfo(&information) != 0) {
        g_systemPointer = (information.flags & CURSOR_SHOWING) != 0;
    }
}

/** @return The bit of the button of a button message, or 0 for a different message. */
unsigned button_bit(UINT message) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP: return 1;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP: return 2;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP: return 4;
    default: return 0;
    }
}

/**
 * The function that the game calls for a raw input packet (the import slot of
 * USER32!GetRawInputData). While the tool window shows, the game gets a raw pointer packet
 * only when the packet has a button release: a button that was down when the window opened
 * is then released in the game. Such a packet can have other events too (a movement, the
 * press of a different button, the wheel). The game must not get these: this function gives
 * the game the packet with the releases only.
 */
UINT WINAPI raw_input_data_for_game(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT headerSize) noexcept {
    const UINT result = GetRawInputData(handle, command, data, size, headerSize);
    if (result == static_cast<UINT>(-1) || command != RID_INPUT || data == nullptr || handle == nullptr
        || g_releasesOnly.load() != handle || result < sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) {
        return result;
    }
    auto* const packet = static_cast<RAWINPUT*>(data);
    if (packet->header.dwType != RIM_TYPEMOUSE) {
        return result;
    }
    RAWMOUSE& pointer = packet->data.mouse;
    pointer.usButtonFlags &= kRawReleases;
    pointer.usButtonData = 0;
    // No movement: a relative movement of zero. An absolute position would be a movement.
    pointer.usFlags &= static_cast<USHORT>(~(MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP));
    pointer.lLastX = 0;
    pointer.lLastY = 0;
    return result;
}

/** Window thread: ends the pointer capture and the button state of the tool window. */
void end_capture(HWND window) noexcept {
    g_buttons = 0;
    if (g_captured) {
        g_captured = false;
        if (GetCapture() == window) {
            ReleaseCapture();
        }
    }
}

void drop_buttons(HWND window) noexcept {
    if (g_buttons != 0) {
        push(kReleaseButtons, 0, 0);
    }
    end_capture(window);
}

/** A pointer message while the tool window shows. The game does not get it. */
void pointer_message(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    if (message == WM_MOUSEMOVE) {
        update_system_pointer();
    }
    // The harness makes the game window the foreground window with the second extra button.
    // The tool window has no function for an extra button and for the horizontal wheel.
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK
        || message == WM_MOUSEHWHEEL) {
        return;
    }
    const unsigned bit = button_bit(message);
    if (bit != 0) {
        const bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP;
        if (!up) {
            // With the capture, the release of a button comes also when the pointer is out of
            // the window: a drag of the tool window then ends.
            if (g_buttons == 0 && GetCapture() == nullptr) {
                SetCapture(window);
                g_captured = true;
            }
            g_buttons |= bit;
        } else {
            g_buttons &= ~bit;
            if (g_buttons == 0 && g_captured) {
                g_captured = false;
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
            }
        }
    }
    push(message, word, value);
}

/**
 * Reads one raw input packet.
 * @return True when the game gets the packet.
 */
bool raw_packet_for_game(LPARAM value, bool shown) noexcept {
    RAWINPUT packet{};
    UINT size = sizeof(packet);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(value), RID_INPUT, &packet, &size, sizeof(RAWINPUTHEADER))
        == static_cast<UINT>(-1)) {
        return !shown;
    }
    if (packet.header.dwType == RIM_TYPEKEYBOARD) {
        count(kRawKeyboard);
        if (packet.data.keyboard.VKey == settings().toggleKey) {
            return false;
        }
        return !shown || (packet.data.keyboard.Flags & RI_KEY_BREAK) != 0;
    }
    if (packet.header.dwType == RIM_TYPEMOUSE) {
        count(kRawMouse);
        if (!shown) {
            return true;
        }
        // The game gets each button release, also from a packet with other events: it reads
        // the packet through raw_input_data_for_game, which gives it the releases only. A
        // packet with no release (a movement, a button press, the wheel) stays with the hook.
        const USHORT flags = packet.data.mouse.usButtonFlags;
        if ((flags & kRawReleases) == 0) {
            return false;
        }
        if (settings().harness) {
            const bool mixed = (flags & ~kRawReleases) != 0 || packet.data.mouse.lLastX != 0
                               || packet.data.mouse.lLastY != 0;
            ++g_releasePackets;
            g_mixedReleasePackets += mixed ? 1 : 0;
            core::log::write("tool window: raw pointer packet with a button release while the window shows (button flags "
                             "0x%04x, movement %ld %ld%s): the game gets the release only; %u such packets, %u with other "
                             "events",
                             static_cast<unsigned>(flags),
                             static_cast<long>(packet.data.mouse.lLastX),
                             static_cast<long>(packet.data.mouse.lLastY),
                             (packet.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0 ? ", absolute" : "",
                             g_releasePackets,
                             g_mixedReleasePackets);
        }
        return true;
    }
    return true;
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    const auto original = reinterpret_cast<WindowProcedure>(g_original);
    if (window == nullptr || window != graphics::window()) {
        return original(window, message, word, value);
    }
    const bool off = stopped();
    const bool shown = !off && visible();
    if (!shown && (g_buttons != 0 || g_captured)) {
        // The window closed with a button down (the harness command, or the toggle key), or
        // the tool window stopped (a failure of the renderer): the capture of the hook must
        // not stay. This is before the test for the stopped state: a tool window that is off
        // has no later place for this work.
        end_capture(window);
    }
    if (off) {
        return original(window, message, word, value);
    }
    const bool harness = settings().harness;
    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (harness) {
            count(down ? kKeyDown : kKeyUp);
            log_counters();
        }
        if (word == settings().toggleKey) {
            if (down && (value & kKeyRepeat) == 0) {
                (void)set_visible(!shown);
            }
            return 0;
        }
        if (!shown || (message == WM_SYSKEYDOWN && word == VK_F4)) {
            break;
        }
        push(message, word, value);
        return 0;
    }
    case WM_CHAR:
        if (harness) {
            count(kChar);
        }
        if (!shown) {
            break;
        }
        push(WM_CHAR, word, value);
        return 0;
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        if (!shown) {
            break;
        }
        return 0;
    case WM_INPUT:
        if (raw_packet_for_game(value, shown)) {
            if (!shown) {
                break;
            }
            // The game reads this packet in its window procedure: it gets the releases only.
            g_releasesOnly = reinterpret_cast<HRAWINPUT>(value);
            const LRESULT result = original(window, message, word, value);
            g_releasesOnly = nullptr;
            return result;
        }
        // The default procedure does the cleanup of a packet that the game does not read.
        return DefWindowProcW(window, message, word, value);
    case WM_KILLFOCUS:
    case WM_SETFOCUS:
        if (shown) {
            if (message == WM_KILLFOCUS) {
                drop_buttons(window);
            }
            push(message, word, value);
        }
        break;
    case WM_CAPTURECHANGED:
        if (shown && reinterpret_cast<HWND>(value) != window) {
            g_captured = false;
            drop_buttons(window);
        }
        break;
    default:
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
            if (harness) {
                count(message == WM_MOUSEMOVE    ? kMouseMove
                      : message == WM_MOUSEWHEEL ? kWheel
                      : (message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP
                         || message == WM_XBUTTONUP)
                          ? kButtonUp
                          : kButtonDown);
                log_counters();
            }
            if (shown) {
                pointer_message(window, message, word, value);
                return 0;
            }
        }
        break;
    }
    return original(window, message, word, value);
}

} // namespace

hooking::detour::Spec window_procedure_detour() noexcept {
    g_original = executable::base() + targets::kWindowProcedure;
    return {&g_original, reinterpret_cast<void*>(&window_procedure)};
}

bool install_raw_input_filter() noexcept {
    return replace_import_slot(targets::kGetRawInputDataImport, "GetRawInputData",
                               reinterpret_cast<void*>(&raw_input_data_for_game));
}

bool window_procedure_matches() noexcept {
    return std::memcmp(executable::base() + targets::kWindowProcedure,
                       targets::kWindowProcedureBytes,
                       sizeof(targets::kWindowProcedureBytes))
           == 0;
}

void drain(ImGuiIO& io, float scaleX, float scaleY) noexcept {
    Event events[kQueueSize];
    AcquireSRWLockExclusive(&g_lock);
    const std::size_t eventCount = g_count;
    std::memcpy(events, g_queue, eventCount * sizeof(Event));
    g_count = 0;
    ReleaseSRWLockExclusive(&g_lock);

    for (std::size_t index = 0; index < eventCount; ++index) {
        const Event& event = events[index];
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.modifiers & kControl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.modifiers & kShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.modifiers & kAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (event.modifiers & kSuper) != 0);
        const UINT message = event.message;
        if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL) {
            // The position of a pointer message is in the client area of the window. With the
            // pointer capture it can be out of the area: the two words have a sign.
            io.AddMousePosEvent(static_cast<float>(static_cast<short>(LOWORD(event.value))) * scaleX,
                                static_cast<float>(static_cast<short>(HIWORD(event.value))) * scaleY);
        }
        switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: io.AddMouseButtonEvent(0, true); break;
        case WM_LBUTTONUP: io.AddMouseButtonEvent(0, false); break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: io.AddMouseButtonEvent(1, true); break;
        case WM_RBUTTONUP: io.AddMouseButtonEvent(1, false); break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: io.AddMouseButtonEvent(2, true); break;
        case WM_MBUTTONUP: io.AddMouseButtonEvent(2, false); break;
        case kReleaseButtons:
            for (int button = 0; button < 3; ++button) {
                io.AddMouseButtonEvent(button, false);
            }
            break;
        case WM_MOUSEWHEEL:
            io.AddMouseWheelEvent(0.0F, static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.word)) / WHEEL_DELTA);
            break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            const ImGuiKey key = ImGui_ImplWin32_KeyEventToImGuiKey(event.word, event.value);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
            }
            break;
        }
        case WM_CHAR:
            if (event.word > 0 && event.word < 0x10000) {
                io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.word));
            }
            break;
        case WM_KILLFOCUS: io.AddFocusEvent(false); break;
        case WM_SETFOCUS: io.AddFocusEvent(true); break;
        default: break;
        }
    }
}

void reset() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_count = 0;
    ReleaseSRWLockExclusive(&g_lock);
    update_system_pointer();
    // Only the window thread can release the pointer capture. A message with no function
    // makes the window procedure run: it ends the capture and the button state when the tool
    // window does not show, also after a stop.
    const HWND window = graphics::window();
    if (window != nullptr) {
        PostMessageW(window, WM_NULL, 0, 0);
    }
}

bool system_pointer_visible() noexcept {
    return g_systemPointer.load();
}

} // namespace soulwarden::client::toolwindow::input

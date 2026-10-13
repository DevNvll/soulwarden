#include "client/automation/harness.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "client/toolwindow/graphics/renderer.h"
#include "client/toolwindow/tool_window.h"
#include "core/logging/log.h"

namespace soulwarden::client::automation {
namespace {

constexpr DWORD kPollMilliseconds = 50;
/**
 * A relative pointer movement goes to the game in steps of this size, with this wait between
 * them. The camera of the client does not turn by a constant angle for one large movement:
 * 900 units in one event gave 69 to 120 degrees, and 30 events of 30 units gave 89 to 90.
 */
constexpr long kLookStepUnits = 30;
constexpr DWORD kLookStepMilliseconds = 20;
/** Wait between two notches of the pointer wheel: the game reads one notch in each frame. */
constexpr DWORD kWheelStepMilliseconds = 60;
/** A pointer drag: the time between two pointer positions, and the rest time at its start and at its end. */
constexpr long kDragStepMilliseconds = 20;
constexpr DWORD kDragHoldMilliseconds = 150;

wchar_t g_commandPath[MAX_PATH]{};
wchar_t g_resultPath[MAX_PATH]{};

struct WindowSearch {
    HWND best{};
    long bestArea{};
};

BOOL CALLBACK consider_window(HWND window, LPARAM parameter) noexcept {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) {
        return TRUE;
    }
    RECT rect{};
    GetClientRect(window, &rect);
    const long area = (rect.right - rect.left) * (rect.bottom - rect.top);
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    if (area > search->bestArea) {
        search->best = window;
        search->bestArea = area;
    }
    return TRUE;
}

/** @return The largest visible top-level window of this process. */
HWND game_window() noexcept {
    WindowSearch search;
    EnumWindows(consider_window, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

void send_key(WORD virtualKey, bool down) noexcept {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

void send_text(const char* utf8) noexcept {
    wchar_t wide[512]{};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 511);
    for (int index = 0; index + 1 < length; ++index) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = wide[index];
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        Sleep(15);
    }
}

/** Moves the pointer to a client-area pixel of the game window. */
bool move_pointer(int x, int y) noexcept {
    const HWND window = game_window();
    if (window == nullptr) {
        return false;
    }
    POINT point{x, y};
    ClientToScreen(window, &point);
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 1 || height <= 1) {
        return false;
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>((static_cast<long long>(point.x - left) * 65535) / (width - 1));
    input.mi.dy = static_cast<LONG>((static_cast<long long>(point.y - top) * 65535) / (height - 1));
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &input, sizeof(input));
    return true;
}

void send_button(DWORD flags, DWORD data = 0) noexcept {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    input.mi.mouseData = data;
    SendInput(1, &input, sizeof(input));
}

/** @return True when the game window is the foreground window after a short wait. */
bool wait_for_foreground(HWND window) noexcept {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (GetForegroundWindow() == window) {
            return true;
        }
        Sleep(30);
    }
    return false;
}

/**
 * Makes the game window the foreground window. Keyboard input of SendInput goes only to the
 * foreground window. On the agent desktop the window has no keyboard focus of the compositor,
 * and Wine then takes the foreground state away at random times. SetForegroundWindow does not
 * give it back, but a pointer button on the window does. The function first uses the second
 * extra button, which has no default function in the game, and then the left button.
 */
void focus_game() noexcept {
    const HWND window = game_window();
    if (window == nullptr || GetForegroundWindow() == window) {
        return;
    }
    SetForegroundWindow(window);
    if (wait_for_foreground(window)) {
        return;
    }
    RECT client{};
    GetClientRect(window, &client);
    move_pointer(client.right / 2, client.bottom / 2);
    struct Button {
        DWORD down;
        DWORD up;
        DWORD data;
        const char* name;
    };
    for (const Button& button : {Button{MOUSEEVENTF_XDOWN, MOUSEEVENTF_XUP, XBUTTON2, "extra"},
                                 Button{MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, 0, "left"}}) {
        Sleep(60);
        send_button(button.down, button.data);
        Sleep(60);
        send_button(button.up, button.data);
        if (wait_for_foreground(window)) {
            core::log::write("harness: foreground state back with the %s button", button.name);
            return;
        }
    }
    core::log::write("harness: the game window is not the foreground window");
}

std::string describe_state() noexcept {
    const HWND window = game_window();
    RECT client{};
    POINT origin{};
    if (window != nullptr) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    // The foreground window gets the keyboard input of SendInput. The thread state shows
    // which window of the game thread has the keyboard focus.
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundProcess = 0;
    char foregroundClass[48]{};
    if (foreground != nullptr) {
        GetWindowThreadProcessId(foreground, &foregroundProcess);
        GetClassNameA(foreground, foregroundClass, sizeof(foregroundClass));
    }
    GUITHREADINFO thread{};
    thread.cbSize = sizeof(thread);
    if (window != nullptr) {
        GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &thread);
    }
    char text[384];
    snprintf(text,
             sizeof(text),
             "window=%p foreground=%d client=%ldx%ld origin=%ld,%ld cursor=%ld,%ld "
             "foregroundWindow=%p class=%s own=%d active=%p focus=%p capture=%p",
             static_cast<void*>(window),
             window != nullptr && foreground == window ? 1 : 0,
             client.right,
             client.bottom,
             origin.x,
             origin.y,
             cursor.x,
             cursor.y,
             static_cast<void*>(foreground),
             foregroundClass,
             foregroundProcess == GetCurrentProcessId() ? 1 : 0,
             static_cast<void*>(thread.hwndActive),
             static_cast<void*>(thread.hwndFocus),
             static_cast<void*>(thread.hwndCapture));
    return text;
}

/** Runs one command line. @return A short result text. */
std::string execute(char* line) noexcept {
    char* context = nullptr;
    const char* verb = strtok_s(line, " \t", &context);
    if (verb == nullptr) {
        return "empty";
    }
    const auto next_number = [&context](long fallback) -> long {
        const char* token = strtok_s(nullptr, " \t", &context);
        return token != nullptr ? std::strtol(token, nullptr, 0) : fallback;
    };
    if (std::strcmp(verb, "state") == 0) {
        return describe_state();
    }
    if (std::strcmp(verb, "focus") == 0) {
        focus_game();
        return describe_state();
    }
    if (std::strcmp(verb, "key") == 0) {
        const WORD key = static_cast<WORD>(next_number(0));
        const long hold = next_number(60);
        focus_game();
        send_key(key, true);
        Sleep(static_cast<DWORD>(hold));
        send_key(key, false);
        return "ok";
    }
    if (std::strcmp(verb, "keydown") == 0 || std::strcmp(verb, "keyup") == 0) {
        focus_game();
        send_key(static_cast<WORD>(next_number(0)), std::strcmp(verb, "keydown") == 0);
        return "ok";
    }
    if (std::strcmp(verb, "text") == 0) {
        focus_game();
        send_text(context != nullptr ? context : "");
        return "ok";
    }
    if (std::strcmp(verb, "move") == 0 || std::strcmp(verb, "click") == 0
        || std::strcmp(verb, "rclick") == 0) {
        // click X Y [MS]: MS is the time that the button stays down. Some buttons of the
        // client act only after a long press ("Confirm" of the delete popup of the main menu).
        const int x = static_cast<int>(next_number(0));
        const int y = static_cast<int>(next_number(0));
        const long hold = next_number(60);
        focus_game();
        if (!move_pointer(x, y)) {
            return "no window";
        }
        if (std::strcmp(verb, "move") != 0) {
            const bool right = std::strcmp(verb, "rclick") == 0;
            Sleep(60);
            send_button(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN);
            Sleep(static_cast<DWORD>(hold));
            send_button(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
        }
        return "ok";
    }
    if (std::strcmp(verb, "mousedown") == 0 || std::strcmp(verb, "mouseup") == 0) {
        // mousedown left|right: presses a pointer button and keeps it down.
        // mouseup left|right [DX DY]: releases it. With DX and DY the release and a relative
        // pointer movement are one input event: one raw input packet with the two events.
        const char* button = strtok_s(nullptr, " \t", &context);
        const bool right = button != nullptr && std::strcmp(button, "right") == 0;
        const bool down = std::strcmp(verb, "mousedown") == 0;
        const long dx = next_number(0);
        const long dy = next_number(0);
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? (right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN)
                                : (right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
        if (!down && (dx != 0 || dy != 0)) {
            input.mi.dwFlags |= MOUSEEVENTF_MOVE;
            input.mi.dx = static_cast<LONG>(dx);
            input.mi.dy = static_cast<LONG>(dy);
        }
        if (down) {
            focus_game();
        }
        SendInput(1, &input, sizeof(input));
        return "ok";
    }
    if (std::strcmp(verb, "drag") == 0) {
        // drag X1 Y1 X2 Y2 [MS]: presses the left button at the first position, moves the
        // pointer to the second position in MS milliseconds, and releases the button there. A
        // user interface element of the client starts a drag only for a pointer that moves
        // with the button down, thus the movement goes in small steps.
        const int startX = static_cast<int>(next_number(0));
        const int startY = static_cast<int>(next_number(0));
        const int endX = static_cast<int>(next_number(0));
        const int endY = static_cast<int>(next_number(0));
        const long duration = std::max(next_number(600), kDragStepMilliseconds);
        const long stepCount = duration / kDragStepMilliseconds;
        focus_game();
        if (!move_pointer(startX, startY)) {
            return "no window";
        }
        Sleep(kDragHoldMilliseconds);
        send_button(MOUSEEVENTF_LEFTDOWN);
        Sleep(kDragHoldMilliseconds);
        for (long step = 1; step <= stepCount; ++step) {
            move_pointer(startX + static_cast<int>((endX - startX) * step / stepCount),
                         startY + static_cast<int>((endY - startY) * step / stepCount));
            Sleep(static_cast<DWORD>(kDragStepMilliseconds));
        }
        Sleep(kDragHoldMilliseconds);
        send_button(MOUSEEVENTF_LEFTUP);
        return "ok";
    }
    if (std::strcmp(verb, "look") == 0) {
        // look DX DY: relative pointer movement, the form that the camera of the game reads.
        const long long dx = next_number(0);
        const long long dy = next_number(0);
        const long long distance = std::max(std::llabs(dx), std::llabs(dy));
        const long long stepCount = (distance + kLookStepUnits - 1) / kLookStepUnits;
        focus_game();
        for (long long step = 1; step <= stepCount; ++step) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dx = static_cast<LONG>(dx * step / stepCount - dx * (step - 1) / stepCount);
            input.mi.dy = static_cast<LONG>(dy * step / stepCount - dy * (step - 1) / stepCount);
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(input));
            Sleep(kLookStepMilliseconds);
        }
        return "ok";
    }
    if (std::strcmp(verb, "wheel") == 0) {
        // wheel NOTCHES: turns the pointer wheel. In the world a positive number moves the
        // camera nearer to the character, and a negative number moves it away.
        const long notches = next_number(0);
        focus_game();
        for (long step = 0; step < std::labs(notches); ++step) {
            send_button(MOUSEEVENTF_WHEEL, static_cast<DWORD>(notches > 0 ? WHEEL_DELTA : -WHEEL_DELTA));
            Sleep(kWheelStepMilliseconds);
        }
        return "ok";
    }
    if (std::strcmp(verb, "base") == 0) {
        char text[32];
        snprintf(text, sizeof(text), "%p", static_cast<void*>(GetModuleHandleW(nullptr)));
        return text;
    }
    if (std::strcmp(verb, "read") == 0) {
        // read ADDRESS LENGTH: research aid, returns the bytes in hexadecimal.
        const char* addressText = strtok_s(nullptr, " \t", &context);
        const auto address = addressText != nullptr ? std::strtoull(addressText, nullptr, 16) : 0ULL;
        long length = next_number(16);
        if (length > 512) {
            length = 512;
        }
        MEMORY_BASIC_INFORMATION information{};
        if (address < 0x10000 || VirtualQuery(reinterpret_cast<void*>(address), &information, sizeof(information)) == 0
            || information.State != MEM_COMMIT || (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
            return "unreadable";
        }
        const auto end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
        if (address + static_cast<std::uint64_t>(length) > end) {
            length = static_cast<long>(end - address);
        }
        std::string text;
        char cell[4];
        for (long index = 0; index < length; ++index) {
            snprintf(cell, sizeof(cell), "%02x", reinterpret_cast<const std::uint8_t*>(address)[index]);
            text += cell;
        }
        return text;
    }
    if (std::strcmp(verb, "write") == 0) {
        // write ADDRESS HEX: research aid, writes bytes to committed and writable client memory.
        const char* addressText = strtok_s(nullptr, " \t", &context);
        const char* hexText = strtok_s(nullptr, " \t", &context);
        const auto address = addressText != nullptr ? std::strtoull(addressText, nullptr, 16) : 0ULL;
        const std::size_t length = hexText != nullptr ? std::strlen(hexText) / 2 : 0;
        MEMORY_BASIC_INFORMATION information{};
        if (length == 0 || address < 0x10000
            || VirtualQuery(reinterpret_cast<void*>(address), &information, sizeof(information)) == 0
            || information.State != MEM_COMMIT || (information.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0
            || address + length > reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize) {
            return "unwritable";
        }
        for (std::size_t index = 0; index < length; ++index) {
            const char cell[3]{hexText[2 * index], hexText[2 * index + 1], '\0'};
            reinterpret_cast<std::uint8_t*>(address)[index] = static_cast<std::uint8_t>(std::strtoul(cell, nullptr, 16));
        }
        return "ok";
    }
    if (std::strcmp(verb, "toolwindow") == 0) {
        // toolwindow [0|1]: hides or shows the tool window with no key (the key needs the
        // foreground state of the game window). With no number: only the state.
        // toolwindow scale PERCENT: the scale of the text and of the controls of a larger
        // picture (150 is the scale of a picture of 1080 lines), 0 for the automatic scale.
        // toolwindow stop: a test of the stop of the tool window (the state after a failure of
        // the renderer). The tool window stays off for the rest of the start.
        const char* option = strtok_s(nullptr, " \t", &context);
        if (option != nullptr && std::strcmp(option, "stop") == 0) {
            toolwindow::stop("the harness command \"toolwindow stop\" (a test)");
            return "stopped";
        }
        if (option != nullptr && std::strcmp(option, "scale") == 0) {
            toolwindow::graphics::force_scale(static_cast<unsigned>(std::max(0L, next_number(0))));
            return "ok";
        }
        const long wanted = option != nullptr ? std::strtol(option, nullptr, 0) : -1;
        if (wanted >= 0 && !toolwindow::set_visible(wanted != 0)) {
            return "not possible";
        }
        return toolwindow::visible() ? "shown" : "hidden";
    }
    if (std::strcmp(verb, "wait") == 0) {
        Sleep(static_cast<DWORD>(next_number(100)));
        return "ok";
    }
    return "unknown command";
}

bool read_command(std::vector<char>& content) noexcept {
    const HANDLE file = CreateFileW(
        g_commandPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD size = GetFileSize(file, nullptr);
    content.assign(size + 1, '\0');
    DWORD read = 0;
    ReadFile(file, content.data(), size, &read, nullptr);
    CloseHandle(file);
    DeleteFileW(g_commandPath);
    return true;
}

void write_result(const std::string& text) noexcept {
    wchar_t temporary[MAX_PATH]{};
    swprintf(temporary, MAX_PATH, L"%ls.tmp", g_resultPath);
    const HANDLE file =
        CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    MoveFileExW(temporary, g_resultPath, MOVEFILE_REPLACE_EXISTING);
}

DWORD WINAPI harness_thread(LPVOID) noexcept {
    core::log::write("harness: started");
    std::vector<char> content;
    HWND lastForeground = nullptr;
    for (;;) {
        Sleep(kPollMilliseconds);
        // Keyboard input goes to the foreground window. A change of it explains lost input.
        const HWND foreground = GetForegroundWindow();
        if (foreground != lastForeground) {
            lastForeground = foreground;
            core::log::write("harness: foreground window %p (game window %p)",
                             static_cast<void*>(foreground),
                             static_cast<void*>(game_window()));
        }
        if (!read_command(content)) {
            continue;
        }
        std::string result;
        char* context = nullptr;
        for (char* line = strtok_s(content.data(), "\r\n", &context); line != nullptr;
             line = strtok_s(nullptr, "\r\n", &context)) {
            core::log::write("harness: %s", line);
            std::string copy = line;
            result += execute(copy.data());
            result += "\n";
        }
        write_result(result);
    }
}

} // namespace

void start() noexcept {
    char enabled[8]{};
    if (GetEnvironmentVariableA("SOULWARDEN_HARNESS", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') {
        return;
    }
    wchar_t directory[MAX_PATH]{};
    swprintf(directory, MAX_PATH, L"%ls\\harness", core::log::data_directory());
    CreateDirectoryW(directory, nullptr);
    swprintf(g_commandPath, MAX_PATH, L"%ls\\command.txt", directory);
    swprintf(g_resultPath, MAX_PATH, L"%ls\\result.txt", directory);
    DeleteFileW(g_commandPath);
    const HANDLE thread = CreateThread(nullptr, 0, harness_thread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
}

} // namespace soulwarden::client::automation

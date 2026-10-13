#include "client/hooks/movement/position_report.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "client/executable/image.h"
#include "core/logging/log.h"

namespace soulwarden::client::hooks::movement {
namespace {

// Addresses are image-relative and apply to NewWorld.exe 1.400.6031.6004151.
//
// The fill function of an input entry (0x2A236C0; one caller, the tick of the local player)
// asks the movement component for the value "within a plot" and writes the answer to bit 15
// of the entry flags:
//
//   0x2A242BB  mov   rax, [rsi]          48 8B 06
//   0x2A242BE  mov   rcx, rsi            48 8B CE
//   0x2A242C1  call  [rax + 0x6F0]       FF 90 F0 06 00 00     <- the 6 bytes of the change
//   0x2A242C7  mov   rdx, [rdi]          48 8B 17               rdi = the entry
//   0x2A242CA  mov   rcx, rdx            48 8B CA
//   0x2A242CD  btr   rcx, 15             48 0F BA F1 0F
//   0x2A242D2  bts   rdx, 15             48 0F BA EA 0F
//   0x2A242D7  test  al, al              84 C0
//   0x2A242D9  cmove rdx, rcx            48 0F 44 D1
//   0x2A242DD  mov   [rdi], rdx          48 89 17
//
// The change replaces the call with "mov al, 1" and four "nop" instructions. The code after
// the call reads only `al` of the result, and it loads each other register again. The
// function of the call (0x483BDE0) reads one byte of the housing component and changes
// nothing, thus the client loses no effect of it. The physics step of the same tick then
// copies the position of the character to the entry (0x2A2480D), and the entry writer sends
// it (0x2A25ACD).
constexpr std::uintptr_t kPlotFlagCode = 0x2A242BB;
constexpr std::uint8_t kPlotFlagCodeBytes[]{0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE, 0xFF, 0x90, 0xF0, 0x06,
                                            0x00, 0x00, 0x48, 0x8B, 0x17, 0x48, 0x8B, 0xCA, 0x48, 0x0F,
                                            0xBA, 0xF1, 0x0F, 0x48, 0x0F, 0xBA, 0xEA, 0x0F, 0x84, 0xC0,
                                            0x48, 0x0F, 0x44, 0xD1, 0x48, 0x89, 0x17};
/** Offset of the call instruction in the compared code, and the instructions that replace it. */
constexpr std::size_t kCallOffset = 6;
constexpr std::uint8_t kSetFlagBytes[]{0xB0, 0x01, 0x90, 0x90, 0x90, 0x90};

/** @return The text of an environment variable, or an empty text. */
const char* variable(const char* name, char* buffer, DWORD size) noexcept {
    const DWORD length = GetEnvironmentVariableA(name, buffer, size);
    if (length == 0 || length >= size) {
        buffer[0] = '\0';
    }
    return buffer;
}

} // namespace

bool install_position_report() noexcept {
    char text[64];
    if (std::strcmp(variable("SOULWARDEN_EXACT_POSITION", text, sizeof(text)), "0") == 0) {
        core::log::write("position report: off (SOULWARDEN_EXACT_POSITION=0). The client code is not changed.");
        return false;
    }
    std::uint8_t expected[sizeof(kPlotFlagCodeBytes)];
    std::memcpy(expected, kPlotFlagCodeBytes, sizeof(expected));
    // A test of a start with client code that is not the code of the supported build. It is
    // diagnostics: only a process with the harness gate reads it.
    if (std::strcmp(variable("SOULWARDEN_HARNESS", text, sizeof(text)), "1") == 0
        && std::strcmp(variable("SOULWARDEN_EXACT_POSITION_FAULT", text, sizeof(text)), "code") == 0) {
        core::log::write("position report: TEST FAULT code (SOULWARDEN_EXACT_POSITION_FAULT): one expected byte is wrong");
        expected[kCallOffset] ^= 0xFF;
    }
    std::uint8_t* const code = executable::base() + kPlotFlagCode;
    if (std::memcmp(code, expected, sizeof(expected)) != 0) {
        core::log::write("position report: OFF for this start of the game: the client code at image address 0x%llX "
                         "is not the code of the supported build. The client code is not changed, and the backend "
                         "estimates the position from the camera.",
                         static_cast<unsigned long long>(kPlotFlagCode));
        return false;
    }
    std::uint8_t* const call = code + kCallOffset;
    DWORD protection = 0;
    if (VirtualProtect(call, sizeof(kSetFlagBytes), PAGE_EXECUTE_READWRITE, &protection) == 0) {
        core::log::write("position report: OFF for this start of the game: the client code at image address 0x%llX "
                         "is not writable (error %lu). The client code is not changed, and the backend estimates the "
                         "position from the camera.",
                         static_cast<unsigned long long>(kPlotFlagCode + kCallOffset),
                         GetLastError());
        return false;
    }
    // The function runs only in the tick of a local player in the world: no thread is in
    // these instructions at the start of the client.
    std::uint8_t original[sizeof(kSetFlagBytes)];
    std::memcpy(original, call, sizeof(original));
    std::memcpy(call, kSetFlagBytes, sizeof(kSetFlagBytes));
    DWORD ignored = 0;
    if (VirtualProtect(call, sizeof(kSetFlagBytes), protection, &ignored) == 0) {
        // The page is writable still. The change is not complete: the original bytes go back,
        // and one more call tries to put the protection back.
        const DWORD error = GetLastError();
        std::memcpy(call, original, sizeof(original));
        const bool restored = VirtualProtect(call, sizeof(kSetFlagBytes), protection, &ignored) != 0;
        FlushInstructionCache(GetCurrentProcess(), call, sizeof(kSetFlagBytes));
        core::log::write("position report: OFF for this start of the game: the step \"put the protection of the client "
                         "code back\" failed at image address 0x%llX (error %lu). The original bytes are back, and the "
                         "protection of the page is %s. The backend estimates the position from the camera.",
                         static_cast<unsigned long long>(kPlotFlagCode + kCallOffset),
                         error,
                         restored ? "back too" : "not back: the page stays writable");
        return false;
    }
    if (FlushInstructionCache(GetCurrentProcess(), call, sizeof(kSetFlagBytes)) == 0) {
        // The processor can have the old instructions still: the change is not sure. The
        // original bytes go back, with the page writable for that time.
        const DWORD error = GetLastError();
        DWORD writable = 0;
        bool restored = false;
        if (VirtualProtect(call, sizeof(kSetFlagBytes), PAGE_EXECUTE_READWRITE, &writable) != 0) {
            std::memcpy(call, original, sizeof(original));
            restored = VirtualProtect(call, sizeof(kSetFlagBytes), protection, &ignored) != 0;
            FlushInstructionCache(GetCurrentProcess(), call, sizeof(kSetFlagBytes));
        }
        core::log::write("position report: OFF for this start of the game: the step \"flush the instruction cache\" "
                         "failed at image address 0x%llX (error %lu). %s The backend estimates the position from the "
                         "camera.",
                         static_cast<unsigned long long>(kPlotFlagCode + kCallOffset),
                         error,
                         restored ? "The original bytes and the protection of the page are back."
                                  : "The original bytes or the protection of the page are not back.");
        return false;
    }
    core::log::write("position report: on. The client sets flag bit 15 of each input entry (image address 0x%llX): "
                     "each input report has the position of the character.",
                     static_cast<unsigned long long>(kPlotFlagCode + kCallOffset));
    return true;
}

} // namespace soulwarden::client::hooks::movement

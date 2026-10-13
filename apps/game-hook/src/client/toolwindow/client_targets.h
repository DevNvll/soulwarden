#pragma once

#include <cstddef>
#include <cstdint>

// Addresses of the client that the tool window uses. All addresses are image-relative and
// apply to NewWorld.exe 1.400.6031.6004151 (protocol notes, section 6). Each one has static
// proof and a live check of stage 1.7. The hook compares the bytes at each code address
// before it changes it: with different bytes the tool window stays off.

namespace soulwarden::client::toolwindow::targets {

/**
 * The present call of the renderer, on its submission thread (the thread function 0x8D4250,
 * command 5). The instructions from this address:
 *   mov rax,[r14]; mov r8d,r15d; mov edx,r12d; mov rcx,r14; call [rax+0x40]
 * r14 is the swap chain that the game presents (IDXGISwapChain3), r12d the sync interval,
 * and r15d the present flags. The stack pointer is a multiple of 16 here.
 */
inline constexpr std::uintptr_t kPresentCallSite = 0x8D445C;
inline constexpr std::uint8_t kPresentCallSiteBytes[]{0x49, 0x8B, 0x06, 0x45, 0x8B, 0xC7, 0x41, 0x8B,
                                                      0xD4, 0x49, 0x8B, 0xCE, 0xFF, 0x50, 0x40};

/** The root window procedure of the game window: LRESULT CALLBACK(HWND, UINT, WPARAM, LPARAM). */
inline constexpr std::uintptr_t kWindowProcedure = 0x7159680;
inline constexpr std::uint8_t kWindowProcedureBytes[]{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                                      0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};

/** Global: ID3D12Device* of the renderer. It is the native device also with NVIDIA Streamline. */
inline constexpr std::uintptr_t kDeviceGlobal = 0xA2E96F0;
/** Global: the device of Streamline (a proxy object), or null. Read only for the log line. */
inline constexpr std::uintptr_t kProxyDeviceGlobal = 0xA2E96F8;
/** Global: the queue manager. [+0x78] is the record of the direct queue, [record+0x10] its ID3D12CommandQueue*. */
inline constexpr std::uintptr_t kQueueManagerGlobal = 0xA2E96C0;
inline constexpr std::size_t kQueueRecord = 0x78;
inline constexpr std::size_t kQueueRecordQueue = 0x10;
/** Flag byte of the Streamline state: 1 when the client uses the Streamline objects. Read only for the log line. */
inline constexpr std::uintptr_t kStreamlineFlag = 0xA42C608;

/** Import slots of the client for USER32!SetCursorPos and USER32!ClipCursor. Each has one user function. */
inline constexpr std::uintptr_t kSetCursorPosImport = 0x7ECA3F8;
inline constexpr std::uintptr_t kClipCursorImport = 0x7ECA580;
/**
 * Import slot of the client for USER32!GetRawInputData. It has one user function: the raw
 * input branch of the window procedure (the two calls at 0x7135319 and 0x7135337), which
 * reads the keys and the pointer of the game. The client image has no import of
 * GetRawInputBuffer.
 */
inline constexpr std::uintptr_t kGetRawInputDataImport = 0x7ECA660;

} // namespace soulwarden::client::toolwindow::targets

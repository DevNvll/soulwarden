#include "client/toolwindow/graphics/present_hook.h"

#include <Windows.h>

#include <cstring>

#include "client/executable/image.h"
#include "client/toolwindow/client_targets.h"
#include "client/toolwindow/graphics/renderer.h"

/** In: the address of the present call site. Out: the trampoline with the instructions of the game. */
extern "C" void* soulwarden_present_call_original = nullptr;

/** Called by the thunk with the arguments of the present call of the game. */
extern "C" void soulwarden_present_call(IDXGISwapChain3* chain, UINT syncInterval, UINT flags) noexcept {
    (void)syncInterval;
    soulwarden::client::toolwindow::graphics::on_present(chain, flags);
}

namespace soulwarden::client::toolwindow::graphics {
namespace {

/**
 * The replacement of the call site. It is not a function: the game jumps to it in the middle
 * of its own function, with a stack pointer that is a multiple of 16. At the call site the
 * game has the swap chain in r14, the sync interval in r12d, and the flags in r15d. The
 * instructions of the game that come next write rax, rcx, rdx, and r8 and then make a call:
 * no volatile register has a value that the game reads after this point.
 */
__attribute__((naked)) void present_call_thunk() noexcept {
    __asm__("subq $0x20, %rsp\n"
            "movq %r14, %rcx\n"
            "movl %r12d, %edx\n"
            "movl %r15d, %r8d\n"
            "callq soulwarden_present_call\n"
            "addq $0x20, %rsp\n"
            "jmpq *soulwarden_present_call_original(%rip)\n");
}

} // namespace

bool present_call_matches() noexcept {
    return std::memcmp(executable::base() + targets::kPresentCallSite,
                       targets::kPresentCallSiteBytes,
                       sizeof(targets::kPresentCallSiteBytes))
           == 0;
}

hooking::detour::Spec present_call_detour() noexcept {
    soulwarden_present_call_original = executable::base() + targets::kPresentCallSite;
    return {&soulwarden_present_call_original, reinterpret_cast<void*>(&present_call_thunk)};
}

} // namespace soulwarden::client::toolwindow::graphics

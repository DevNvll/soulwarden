#include "client/diagnostics/trace_points.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "client/executable/image.h"
#include "client/hooking/detour.h"
#include "core/logging/log.h"

namespace soulwarden::client::diagnostics {
namespace {

constexpr std::size_t kMaxPoints = 48;
constexpr std::uint32_t kMaxLogsPerPoint = 400;
/** Code range of the supported game image (image-relative). */
constexpr std::uint64_t kTextStart = 0x1000;
constexpr std::uint64_t kTextEnd = 0x7EC8000;

struct Point {
    std::uint64_t rva{};
    void* original{};
    std::uint32_t dumpSize{48};
    bool stack{false};
    std::atomic<std::uint32_t> hits{0};
};

Point g_points[kMaxPoints];
std::size_t g_pointCount = 0;

/** @return The number of readable bytes at an address, up to the limit. */
std::size_t readable(const void* address, std::size_t limit) noexcept {
    MEMORY_BASIC_INFORMATION information{};
    if (reinterpret_cast<std::uintptr_t>(address) < 0x10000
        || VirtualQuery(address, &information, sizeof(information)) == 0
        || information.State != MEM_COMMIT
        || (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return 0;
    }
    const auto end = reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
    const std::size_t available = end - reinterpret_cast<std::uintptr_t>(address);
    return available < limit ? available : limit;
}

void dump(const char* label, std::uint64_t value, std::uint32_t size) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(value);
    const std::size_t count = readable(bytes, size);
    if (count == 0) {
        return;
    }
    char text[1024 * 2 + 8];
    std::size_t used = 0;
    for (std::size_t index = 0; index < count && used + 3 < sizeof(text); ++index) {
        used += static_cast<std::size_t>(snprintf(text + used, sizeof(text) - used, "%02x", bytes[index]));
    }
    core::log::write("trace:   %s -> %s", label, text);
}

/** @return True when the bytes before a code address are a call instruction. */
bool follows_call(const std::uint8_t* address) noexcept {
    if (address[-5] == 0xE8) {
        return true;
    }
    for (const int offset : {2, 3, 6, 7}) {
        if (address[-offset] == 0xFF && (address[-offset + 1] & 0x38) == 0x10) {
            return true;
        }
    }
    return false;
}

/** Logs the return addresses in the game image that are on the stack above the trace point. */
void log_stack(const std::uint64_t* stack) noexcept {
    constexpr std::size_t kScanSlots = 768;
    constexpr std::size_t kMaxFrames = 24;
    const auto base = reinterpret_cast<std::uint64_t>(executable::base());
    const std::size_t slots = readable(stack, kScanSlots * sizeof(std::uint64_t)) / sizeof(std::uint64_t);
    char text[kMaxFrames * 12 + 8];
    std::size_t used = 0;
    std::size_t frames = 0;
    for (std::size_t index = 0; index < slots && frames < kMaxFrames; ++index) {
        const std::uint64_t value = stack[index];
        if (value < base + kTextStart || value >= base + kTextEnd) {
            continue;
        }
        if (!follows_call(reinterpret_cast<const std::uint8_t*>(value))) {
            continue;
        }
        used += static_cast<std::size_t>(snprintf(text + used, sizeof(text) - used, " %llX",
                                                  static_cast<unsigned long long>(value - base)));
        ++frames;
    }
    if (frames != 0) {
        core::log::write("trace:   stack%s", text);
    }
}

/** Called by each generated thunk. saved = r9, r8, rdx, rcx, return address, caller stack. */
void on_enter(Point* point, const std::uint64_t* saved) noexcept {
    const std::uint32_t hit = point->hits.fetch_add(1);
    if (hit >= kMaxLogsPerPoint) {
        return;
    }
    const auto base = reinterpret_cast<std::uint64_t>(executable::base());
    const std::uint64_t rcx = saved[3];
    const std::uint64_t rdx = saved[2];
    const std::uint64_t r8 = saved[1];
    const std::uint64_t r9 = saved[0];
    core::log::write("trace: 0x%llX #%u rcx=%llx rdx=%llx r8=%llx r9=%llx a5=%llx a6=%llx ret=0x%llX",
                     static_cast<unsigned long long>(point->rva),
                     hit,
                     static_cast<unsigned long long>(rcx),
                     static_cast<unsigned long long>(rdx),
                     static_cast<unsigned long long>(r8),
                     static_cast<unsigned long long>(r9),
                     static_cast<unsigned long long>(saved[9]),
                     static_cast<unsigned long long>(saved[10]),
                     static_cast<unsigned long long>(saved[4] - base));
    dump("rcx", rcx, point->dumpSize);
    dump("rdx", rdx, point->dumpSize);
    dump("r8", r8, point->dumpSize);
    dump("r9", r9, point->dumpSize);
    if (point->stack) {
        log_stack(saved + 4);
    }
}

/**
 * Called by each probe thunk. saved = r15..r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, flags, and
 * then the stack of the probed code.
 */
void on_probe(Point* point, const std::uint64_t* saved) noexcept {
    const std::uint32_t hit = point->hits.fetch_add(1);
    // A probe can be in code that runs each frame. Log the first hits, then one in 120.
    if ((hit >= 3 && hit % 120 != 0) || hit / 120 >= kMaxLogsPerPoint) {
        return;
    }
    const auto* stack = saved + 16;
    core::log::write("probe: 0x%llX #%u rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx "
                     "r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx rbp=%llx rsp=%llx",
                     static_cast<unsigned long long>(point->rva),
                     hit,
                     static_cast<unsigned long long>(saved[14]),
                     static_cast<unsigned long long>(saved[11]),
                     static_cast<unsigned long long>(saved[13]),
                     static_cast<unsigned long long>(saved[12]),
                     static_cast<unsigned long long>(saved[9]),
                     static_cast<unsigned long long>(saved[8]),
                     static_cast<unsigned long long>(saved[7]),
                     static_cast<unsigned long long>(saved[6]),
                     static_cast<unsigned long long>(saved[5]),
                     static_cast<unsigned long long>(saved[4]),
                     static_cast<unsigned long long>(saved[3]),
                     static_cast<unsigned long long>(saved[2]),
                     static_cast<unsigned long long>(saved[1]),
                     static_cast<unsigned long long>(saved[0]),
                     static_cast<unsigned long long>(saved[10]),
                     static_cast<unsigned long long>(reinterpret_cast<std::uint64_t>(stack)));
    dump("rsp", reinterpret_cast<std::uint64_t>(stack), point->dumpSize);
    dump("rbp", saved[10], point->dumpSize);
}

/** Copies thunk code to the executable arena. @return The thunk address, or null. */
void* place_thunk(const std::uint8_t* code, std::size_t size) noexcept {
    static std::uint8_t* arena = nullptr;
    static std::size_t used = 0;
    constexpr std::size_t kArenaSize = 0x4000;
    if (arena == nullptr) {
        arena = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, kArenaSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (arena == nullptr) {
            return nullptr;
        }
    }
    if (used + size > kArenaSize) {
        return nullptr;
    }
    std::uint8_t* thunk = arena + used;
    std::memcpy(thunk, code, size);
    used += (size + 15) & ~static_cast<std::size_t>(15);
    FlushInstructionCache(GetCurrentProcess(), thunk, size);
    return thunk;
}

/**
 * Emits one probe thunk. A probe can be at any instruction start: the thunk keeps the flags,
 * all general registers, and the volatile vector registers.
 */
void* make_probe_thunk(Point* point) noexcept {
    std::uint8_t code[256];
    std::size_t size = 0;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const std::uint8_t byte : bytes) {
            code[size++] = byte;
        }
    };
    const auto emit_u64 = [&](std::uint64_t value) {
        std::memcpy(code + size, &value, 8);
        size += 8;
    };
    emit({0x9C});                                          // pushfq
    emit({0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57});      // push rax, rcx, rdx, rbx, rbp, rsi, rdi
    emit({0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53}); // push r8, r9, r10, r11
    emit({0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}); // push r12, r13, r14, r15
    emit({0x48, 0x89, 0xE2});                              // mov rdx, rsp
    emit({0x48, 0x89, 0xE3});                              // mov rbx, rsp
    emit({0x48, 0x83, 0xE4, 0xF0});                        // and rsp, -16
    emit({0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00});      // sub rsp, 0x80
    for (std::uint8_t index = 0; index < 6; ++index) {     // movdqu [rsp+0x20+16*i], xmm<i>
        emit({0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 | (index << 3)), 0x24,
              static_cast<std::uint8_t>(0x20 + 16 * index)});
    }
    emit({0x48, 0xB9});                                    // mov rcx, point
    emit_u64(reinterpret_cast<std::uint64_t>(point));
    emit({0x48, 0xB8});                                    // mov rax, on_probe
    emit_u64(reinterpret_cast<std::uint64_t>(&on_probe));
    emit({0xFF, 0xD0});                                    // call rax
    for (std::uint8_t index = 0; index < 6; ++index) {     // movdqu xmm<i>, [rsp+0x20+16*i]
        emit({0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 | (index << 3)), 0x24,
              static_cast<std::uint8_t>(0x20 + 16 * index)});
    }
    emit({0x48, 0x89, 0xDC});                              // mov rsp, rbx
    emit({0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C}); // pop r15, r14, r13, r12
    emit({0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58}); // pop r11, r10, r9, r8
    emit({0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59});            // pop rdi, rsi, rbp, rbx, rdx, rcx
    // The stack now has the saved rax, then the saved flags. Put the flags in the first
    // slot and the jump target in the second slot, then leave through both.
    emit({0x48, 0xB8});                                    // mov rax, &point->original
    emit_u64(reinterpret_cast<std::uint64_t>(&point->original));
    emit({0x48, 0x8B, 0x00});                              // mov rax, [rax]     (target)
    emit({0x48, 0x87, 0x44, 0x24, 0x08});                  // xchg [rsp+8], rax  (rax = flags)
    emit({0x48, 0x87, 0x04, 0x24});                        // xchg [rsp], rax    (rax restored)
    emit({0x9D});                                          // popfq
    emit({0xC3});                                          // ret                (to the target)
    return place_thunk(code, size);
}

/** Emits one entry thunk. @return The thunk address, or null. */
void* make_thunk(Point* point) noexcept {
    std::uint8_t code[160];
    std::size_t size = 0;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
        for (const std::uint8_t byte : bytes) {
            code[size++] = byte;
        }
    };
    const auto emit_u64 = [&](std::uint64_t value) {
        std::memcpy(code + size, &value, 8);
        size += 8;
    };
    emit({0x51, 0x52, 0x41, 0x50, 0x41, 0x51});            // push rcx, rdx, r8, r9
    emit({0x48, 0x83, 0xEC, 0x68});                        // sub rsp, 0x68
    emit({0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20});            // movdqu [rsp+0x20], xmm0
    emit({0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30});            // movdqu [rsp+0x30], xmm1
    emit({0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40});            // movdqu [rsp+0x40], xmm2
    emit({0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50});            // movdqu [rsp+0x50], xmm3
    emit({0x48, 0x8D, 0x94, 0x24, 0x68, 0x00, 0x00, 0x00}); // lea rdx, [rsp+0x68]
    emit({0x48, 0xB9});                                    // mov rcx, point
    emit_u64(reinterpret_cast<std::uint64_t>(point));
    emit({0x48, 0xB8});                                    // mov rax, on_enter
    emit_u64(reinterpret_cast<std::uint64_t>(&on_enter));
    emit({0xFF, 0xD0});                                    // call rax
    emit({0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20});            // movdqu xmm0, [rsp+0x20]
    emit({0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30});
    emit({0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40});
    emit({0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50});
    emit({0x48, 0x83, 0xC4, 0x68});                        // add rsp, 0x68
    emit({0x41, 0x59, 0x41, 0x58, 0x5A, 0x59});            // pop r9, r8, rdx, rcx
    emit({0x48, 0xB8});                                    // mov rax, &point->original
    emit_u64(reinterpret_cast<std::uint64_t>(&point->original));
    emit({0xFF, 0x20});                                    // jmp [rax]
    return place_thunk(code, size);
}

/** Installs the points of one list. Probes use the probe thunk and can be at any instruction. */
void install_list(const char* variable, bool probe) noexcept {
    char list[1024]{};
    const DWORD length = GetEnvironmentVariableA(variable, list, sizeof(list));
    if (length == 0 || length >= sizeof(list)) {
        return;
    }
    char* context = nullptr;
    for (char* token = strtok_s(list, ",", &context); token != nullptr && g_pointCount < kMaxPoints;
         token = strtok_s(nullptr, ",", &context)) {
        Point& point = g_points[g_pointCount];
        char* suffix = nullptr;
        point.rva = std::strtoull(token, &suffix, 16);
        if (suffix != nullptr && *suffix == ':') {
            point.dumpSize = static_cast<std::uint32_t>(std::strtoul(suffix + 1, &suffix, 10));
            if (point.dumpSize > 1024) {
                point.dumpSize = 1024;
            }
        }
        // A trailing "s" adds the return addresses that are on the stack.
        point.stack = suffix != nullptr && *suffix == 's';
        point.original = executable::base() + point.rva;
        void* thunk = probe ? make_probe_thunk(&point) : make_thunk(&point);
        if (thunk == nullptr) {
            break;
        }
        const hooking::detour::Spec spec{&point.original, thunk};
        const bool installed = hooking::detour::install({&spec, 1});
        core::log::write("trace: %s 0x%llX %s",
                         probe ? "probe" : "point",
                         static_cast<unsigned long long>(point.rva),
                         installed ? "installed" : "FAILED");
        if (installed) {
            ++g_pointCount;
        }
    }
}

} // namespace

void install_trace_points() noexcept {
    char enabled[8]{};
    if (GetEnvironmentVariableA("SOULWARDEN_HARNESS", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') {
        return;
    }
    install_list("SOULWARDEN_TRACE", false);
    install_list("SOULWARDEN_PROBE", true);
}

} // namespace soulwarden::client::diagnostics

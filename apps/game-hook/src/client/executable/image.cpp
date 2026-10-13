#include "client/executable/image.h"

#include <Windows.h>

#include <cstring>

namespace soulwarden::client::executable {
namespace {

constexpr std::size_t kMaxSections = 32;
Section g_sections[kMaxSections];
std::size_t g_sectionCount = 0;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK load_sections(PINIT_ONCE, PVOID, PVOID*) noexcept {
    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* header = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections && g_sectionCount < kMaxSections; ++index) {
        Section& section = g_sections[g_sectionCount++];
        std::memcpy(section.name, header[index].Name, 8);
        section.begin = image + header[index].VirtualAddress;
        section.size = header[index].Misc.VirtualSize;
        section.writable = (header[index].Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        section.executable = (header[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    }
    return TRUE;
}

} // namespace

std::span<const Section> sections() noexcept {
    InitOnceExecuteOnce(&g_once, load_sections, nullptr, nullptr);
    return {g_sections, g_sectionCount};
}

std::uint8_t* base() noexcept {
    return reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
}

std::uint8_t* find(std::uint8_t* begin, std::size_t size, const void* pattern, std::size_t patternSize) noexcept {
    if (patternSize == 0 || size < patternSize) {
        return nullptr;
    }
    const auto* needle = static_cast<const std::uint8_t*>(pattern);
    std::uint8_t* cursor = begin;
    std::uint8_t* const end = begin + size - patternSize + 1;
    while (cursor < end) {
        cursor = static_cast<std::uint8_t*>(std::memchr(cursor, needle[0], static_cast<std::size_t>(end - cursor)));
        if (cursor == nullptr) {
            return nullptr;
        }
        if (std::memcmp(cursor, needle, patternSize) == 0) {
            return cursor;
        }
        ++cursor;
    }
    return nullptr;
}

} // namespace soulwarden::client::executable

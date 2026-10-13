#include "client/hooks/trust/trust_anchor.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "client/executable/image.h"
#include "client/network/backend_http.h"
#include "core/logging/log.h"

namespace soulwarden::client::hooks::trust {
namespace {

constexpr char kPemBegin[] = "-----BEGIN CERTIFICATE-----\n";
constexpr char kPemEnd[] = "-----END CERTIFICATE-----";
constexpr std::size_t kMinimumPemSize = 512;

/** Process-lifetime copy of the local certificate. The client keeps a pointer to it. */
char* g_localCertificate = nullptr;

/** @return The start of the embedded game certificate authority, or null. */
std::uint8_t* find_embedded_authority() noexcept {
    for (const executable::Section& section : executable::sections()) {
        if (section.writable || section.executable) {
            continue;
        }
        std::uint8_t* cursor = section.begin;
        std::size_t remaining = section.size;
        for (;;) {
            std::uint8_t* match = executable::find(cursor, remaining, kPemBegin, sizeof(kPemBegin) - 1);
            if (match == nullptr) {
                break;
            }
            const std::size_t tail = static_cast<std::size_t>(section.begin + section.size - match);
            const std::size_t length = strnlen(reinterpret_cast<const char*>(match), tail);
            if (length >= kMinimumPemSize
                && std::strstr(reinterpret_cast<const char*>(match), kPemEnd) != nullptr) {
                return match;
            }
            remaining = tail - 1;
            cursor = match + 1;
        }
    }
    return nullptr;
}

} // namespace

bool install() noexcept {
    std::string pem;
    if (!network::backend_get("/v1/tls/ca.pem", pem) || pem.find(kPemBegin) != 0) {
        core::log::write("trust: the backend certificate is not available");
        return false;
    }
    std::uint8_t* authority = find_embedded_authority();
    if (authority == nullptr) {
        core::log::write("trust: the client certificate authority was not found");
        return false;
    }
    g_localCertificate = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, pem.size() + 1));
    if (g_localCertificate == nullptr) {
        return false;
    }
    std::memcpy(g_localCertificate, pem.c_str(), pem.size() + 1);

    // The client reads the authority through one or more data pointers. Redirect each one.
    const auto wanted = reinterpret_cast<std::uintptr_t>(authority);
    unsigned replaced = 0;
    for (const executable::Section& section : executable::sections()) {
        if (!section.writable) {
            continue;
        }
        auto* slot = reinterpret_cast<std::uintptr_t*>(section.begin);
        const std::size_t count = section.size / sizeof(std::uintptr_t);
        for (std::size_t index = 0; index < count; ++index) {
            if (slot[index] == wanted) {
                slot[index] = reinterpret_cast<std::uintptr_t>(g_localCertificate);
                ++replaced;
            }
        }
    }
    core::log::write("trust: authority at RVA 0x%llX, %u pointer(s) replaced, local certificate %zu bytes",
                     static_cast<unsigned long long>(authority - executable::base()),
                     replaced,
                     pem.size());
    return replaced != 0;
}

} // namespace soulwarden::client::hooks::trust

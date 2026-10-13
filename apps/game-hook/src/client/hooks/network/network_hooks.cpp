#include "client/hooks/network/network_hooks.h"

#include <WinSock2.h>
#include <Windows.h>

#include <MSWSock.h>
#include <WS2tcpip.h>
#include <wininet.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "client/hooking/detour.h"
#include "core/logging/log.h"

// WinHTTP and WinINet headers cannot be included together. Declare the WinHTTP subset here.
extern "C" {
using HINTERNET_WINHTTP = void*;
__declspec(dllimport) HINTERNET_WINHTTP WINAPI WinHttpConnect(HINTERNET_WINHTTP,
                                                              LPCWSTR,
                                                              WORD,
                                                              DWORD);
__declspec(dllimport) HINTERNET_WINHTTP WINAPI
WinHttpOpenRequest(HINTERNET_WINHTTP, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
__declspec(dllimport) BOOL WINAPI
WinHttpSendRequest(HINTERNET_WINHTTP, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
__declspec(dllimport) BOOL WINAPI WinHttpAddRequestHeaders(HINTERNET_WINHTTP,
                                                           LPCWSTR,
                                                           DWORD,
                                                           DWORD);
__declspec(dllimport) BOOL WINAPI WinHttpSetOption(HINTERNET_WINHTTP, DWORD, LPVOID, DWORD);
__declspec(dllimport) BOOL WINAPI WinHttpCloseHandle(HINTERNET_WINHTTP);
}

namespace soulwarden::client::hooks::network {
namespace {

constexpr DWORD kWinHttpFlagSecure = 0x00800000;
constexpr DWORD kWinHttpAddRequestFlagAdd = 0x20000000;
constexpr DWORD kWinHttpAddRequestFlagReplace = 0x80000000;

char g_backendHost[64] = "127.0.0.1";
wchar_t g_backendHostWide[64] = L"127.0.0.1";
WORD g_backendHttpPort = 32200;

std::mutex g_mutex;
std::unordered_map<void*, std::wstring> g_connections;
std::atomic<std::uint32_t> g_blocked{0};

void read_settings() noexcept {
    char value[64]{};
    DWORD length = GetEnvironmentVariableA("SOULWARDEN_BACKEND_HTTP_HOST", value, sizeof(value));
    if (length != 0 && length < sizeof(value)) {
        std::memcpy(g_backendHost, value, length + 1);
        MultiByteToWideChar(CP_ACP, 0, value, -1, g_backendHostWide, 64);
    }
    length = GetEnvironmentVariableA("SOULWARDEN_BACKEND_HTTP_PORT", value, sizeof(value));
    if (length != 0 && length < sizeof(value)) {
        g_backendHttpPort = static_cast<WORD>(std::strtoul(value, nullptr, 10));
    }
}

// ---- egress guard ----------------------------------------------------------------------------

/** @return True when the address is on the local computer. */
bool is_local(const sockaddr* address) noexcept {
    if (address == nullptr) {
        return true;
    }
    if (address->sa_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        const std::uint32_t host = ntohl(ipv4->sin_addr.s_addr);
        return (host >> 24) == 127 || host == 0;
    }
    if (address->sa_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        static constexpr std::uint8_t kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        static constexpr std::uint8_t kMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        const std::uint8_t* bytes = ipv6->sin6_addr.u.Byte;
        if (std::memcmp(bytes, kLoopback, 16) == 0) {
            return true;
        }
        static constexpr std::uint8_t kAny[16] = {};
        if (std::memcmp(bytes, kAny, 16) == 0) {
            return true;
        }
        return std::memcmp(bytes, kMappedPrefix, 12) == 0 && bytes[12] == 127;
    }
    return address->sa_family == AF_UNSPEC;
}

void describe(const sockaddr* address, char* output, std::size_t size) noexcept {
    if (address != nullptr && address->sa_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        const std::uint32_t host = ntohl(ipv4->sin_addr.s_addr);
        snprintf(output,
                 size,
                 "%u.%u.%u.%u:%u",
                 host >> 24,
                 (host >> 16) & 0xFF,
                 (host >> 8) & 0xFF,
                 host & 0xFF,
                 ntohs(ipv4->sin_port));
    } else if (address != nullptr && address->sa_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        snprintf(output, size, "[ipv6]:%u", ntohs(ipv6->sin6_port));
    } else {
        snprintf(output, size, "family %d", address != nullptr ? address->sa_family : -1);
    }
}

/** @return True when the call can continue. A blocked call gets a network-unreachable error. */
bool permit(const char* operation, const sockaddr* address) noexcept {
    char text[64];
    describe(address, text, sizeof(text));
    if (is_local(address)) {
        core::log::write("net: %s %s", operation, text);
        return true;
    }
    if (g_blocked.fetch_add(1) < 200) {
        core::log::write("net: BLOCKED %s %s", operation, text);
    }
    WSASetLastError(WSAENETUNREACH);
    return false;
}

// ---- Winsock ---------------------------------------------------------------------------------

auto g_getaddrinfo = &getaddrinfo;
auto g_GetAddrInfoW = &GetAddrInfoW;
auto g_connect = &connect;
auto g_WSAConnect = &WSAConnect;
auto g_sendto = &sendto;
auto g_WSAIoctl = &WSAIoctl;
auto g_bind = &bind;
LPFN_CONNECTEX g_connectEx = nullptr;

bool is_numeric_local(const char* node) noexcept {
    return node == nullptr || std::strncmp(node, "127.", 4) == 0 || std::strcmp(node, "::1") == 0
           || std::strcmp(node, "localhost") == 0 || std::strcmp(node, "0.0.0.0") == 0;
}

INT WSAAPI hook_getaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA* hints, PADDRINFOA* result) {
    if (is_numeric_local(node)) {
        return g_getaddrinfo(node, service, hints, result);
    }
    core::log::write("net: resolve %s:%s -> %s", node, service != nullptr ? service : "", g_backendHost);
    return g_getaddrinfo(g_backendHost, service, hints, result);
}

INT WSAAPI hook_GetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW* hints, PADDRINFOW* result) {
    char narrow[256]{};
    if (node != nullptr) {
        WideCharToMultiByte(CP_UTF8, 0, node, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    }
    if (node == nullptr || is_numeric_local(narrow)) {
        return g_GetAddrInfoW(node, service, hints, result);
    }
    core::log::write("net: resolveW %s -> %s", narrow, g_backendHost);
    return g_GetAddrInfoW(g_backendHostWide, service, hints, result);
}

int WSAAPI hook_connect(SOCKET socket, const sockaddr* name, int length) {
    if (!permit("connect", name)) {
        return SOCKET_ERROR;
    }
    return g_connect(socket, name, length);
}

int WSAAPI hook_WSAConnect(SOCKET socket,
                           const sockaddr* name,
                           int length,
                           LPWSABUF callerData,
                           LPWSABUF calleeData,
                           LPQOS sqos,
                           LPQOS gqos) {
    if (!permit("WSAConnect", name)) {
        return SOCKET_ERROR;
    }
    return g_WSAConnect(socket, name, length, callerData, calleeData, sqos, gqos);
}

int WSAAPI hook_sendto(SOCKET socket, const char* buffer, int length, int flags, const sockaddr* to, int toLength) {
    if (to != nullptr && !is_local(to)) {
        if (g_blocked.fetch_add(1) < 200) {
            char text[64];
            describe(to, text, sizeof(text));
            core::log::write("net: BLOCKED sendto %s (%d bytes)", text, length);
        }
        WSASetLastError(WSAENETUNREACH);
        return SOCKET_ERROR;
    }
    return g_sendto(socket, buffer, length, flags, to, toLength);
}

int WSAAPI hook_bind(SOCKET socket, const sockaddr* name, int length) {
    char text[64];
    describe(name, text, sizeof(text));
    core::log::write("net: bind %s", text);
    return g_bind(socket, name, length);
}

BOOL PASCAL hook_ConnectEx(SOCKET socket,
                           const sockaddr* name,
                           int length,
                           PVOID sendBuffer,
                           DWORD sendLength,
                           LPDWORD sent,
                           LPOVERLAPPED overlapped) {
    if (!permit("ConnectEx", name)) {
        return FALSE;
    }
    return g_connectEx(socket, name, length, sendBuffer, sendLength, sent, overlapped);
}

int WSAAPI hook_WSAIoctl(SOCKET socket,
                         DWORD code,
                         LPVOID input,
                         DWORD inputSize,
                         LPVOID output,
                         DWORD outputSize,
                         LPDWORD returned,
                         LPWSAOVERLAPPED overlapped,
                         LPWSAOVERLAPPED_COMPLETION_ROUTINE routine) {
    const int result =
        g_WSAIoctl(socket, code, input, inputSize, output, outputSize, returned, overlapped, routine);
    static constexpr GUID kConnectEx = WSAID_CONNECTEX;
    if (result == 0 && code == SIO_GET_EXTENSION_FUNCTION_POINTER && input != nullptr
        && inputSize == sizeof(GUID) && std::memcmp(input, &kConnectEx, sizeof(GUID)) == 0
        && output != nullptr && outputSize >= sizeof(void*)) {
        auto* pointer = static_cast<LPFN_CONNECTEX*>(output);
        if (*pointer != &hook_ConnectEx) {
            g_connectEx = *pointer;
            *pointer = &hook_ConnectEx;
        }
    }
    return result;
}

// ---- WinHTTP ---------------------------------------------------------------------------------

auto g_WinHttpConnect = &WinHttpConnect;
auto g_WinHttpOpenRequest = &WinHttpOpenRequest;
auto g_WinHttpSendRequest = &WinHttpSendRequest;
auto g_WinHttpSetOption = &WinHttpSetOption;
auto g_WinHttpCloseHandle = &WinHttpCloseHandle;

void remember(void* handle, const std::wstring& host) noexcept {
    std::lock_guard lock(g_mutex);
    g_connections[handle] = host;
}

std::wstring recall(void* handle) noexcept {
    std::lock_guard lock(g_mutex);
    const auto found = g_connections.find(handle);
    return found != g_connections.end() ? found->second : std::wstring{};
}

HINTERNET_WINHTTP WINAPI hook_WinHttpConnect(HINTERNET_WINHTTP session, LPCWSTR server, WORD port, DWORD reserved) {
    const HINTERNET_WINHTTP handle =
        g_WinHttpConnect(session, g_backendHostWide, g_backendHttpPort, reserved);
    core::log::write("http: connect %ls:%u -> backend (handle %p)", server, port, handle);
    if (handle != nullptr) {
        remember(handle, std::wstring(server != nullptr ? server : L"") + L":" + std::to_wstring(port));
    }
    return handle;
}

HINTERNET_WINHTTP WINAPI hook_WinHttpOpenRequest(HINTERNET_WINHTTP connection,
                                                 LPCWSTR verb,
                                                 LPCWSTR object,
                                                 LPCWSTR version,
                                                 LPCWSTR referrer,
                                                 LPCWSTR* acceptTypes,
                                                 DWORD flags) {
    const std::wstring host = recall(connection);
    const HINTERNET_WINHTTP request = g_WinHttpOpenRequest(
        connection, verb, object, version, referrer, acceptTypes, flags & ~kWinHttpFlagSecure);
    core::log::write("http: %ls %ls%ls%ls (request %p)",
                     verb != nullptr ? verb : L"GET",
                     (flags & kWinHttpFlagSecure) != 0 ? L"https://" : L"http://",
                     host.c_str(),
                     object != nullptr ? object : L"",
                     request);
    if (request != nullptr) {
        std::wstring headers = L"X-Soulwarden-Host: " + host + L"\r\nX-Soulwarden-Scheme: "
                               + ((flags & kWinHttpFlagSecure) != 0 ? L"https" : L"http") + L"\r\n";
        WinHttpAddRequestHeaders(request,
                                 headers.c_str(),
                                 static_cast<DWORD>(-1),
                                 kWinHttpAddRequestFlagAdd | kWinHttpAddRequestFlagReplace);
    }
    return request;
}

BOOL WINAPI hook_WinHttpSendRequest(HINTERNET_WINHTTP request,
                                    LPCWSTR headers,
                                    DWORD headersLength,
                                    LPVOID optional,
                                    DWORD optionalLength,
                                    DWORD totalLength,
                                    DWORD_PTR context) {
    const BOOL result = g_WinHttpSendRequest(
        request, headers, headersLength, optional, optionalLength, totalLength, context);
    core::log::write("http: send request %p body %lu/%lu -> %d (error %lu)",
                     request,
                     static_cast<unsigned long>(optionalLength),
                     static_cast<unsigned long>(totalLength),
                     result,
                     result ? 0UL : static_cast<unsigned long>(GetLastError()));
    return result;
}

BOOL WINAPI hook_WinHttpSetOption(HINTERNET_WINHTTP handle, DWORD option, LPVOID buffer, DWORD length) {
    if (g_WinHttpSetOption(handle, option, buffer, length)) {
        return TRUE;
    }
    // Transport-security options do not apply to the clear loopback route.
    core::log::write("http: option %lu not applied (error %lu)",
                     static_cast<unsigned long>(option),
                     static_cast<unsigned long>(GetLastError()));
    return TRUE;
}

BOOL WINAPI hook_WinHttpCloseHandle(HINTERNET_WINHTTP handle) {
    {
        std::lock_guard lock(g_mutex);
        g_connections.erase(handle);
    }
    return g_WinHttpCloseHandle(handle);
}

// ---- WinINet ---------------------------------------------------------------------------------

auto g_InternetConnectA = &InternetConnectA;
auto g_HttpOpenRequestA = &HttpOpenRequestA;

HINTERNET WINAPI hook_InternetConnectA(HINTERNET session,
                                       LPCSTR server,
                                       INTERNET_PORT port,
                                       LPCSTR user,
                                       LPCSTR password,
                                       DWORD service,
                                       DWORD flags,
                                       DWORD_PTR context) {
    const HINTERNET handle = g_InternetConnectA(
        session, g_backendHost, g_backendHttpPort, user, password, service, flags, context);
    core::log::write("inet: connect %s:%u -> backend (handle %p)", server != nullptr ? server : "", port, handle);
    if (handle != nullptr) {
        wchar_t wide[256]{};
        MultiByteToWideChar(CP_ACP, 0, server != nullptr ? server : "", -1, wide, 255);
        remember(handle, std::wstring(wide) + L":" + std::to_wstring(port));
    }
    return handle;
}

HINTERNET WINAPI hook_HttpOpenRequestA(HINTERNET connection,
                                       LPCSTR verb,
                                       LPCSTR object,
                                       LPCSTR version,
                                       LPCSTR referrer,
                                       LPCSTR* acceptTypes,
                                       DWORD flags,
                                       DWORD_PTR context) {
    const std::wstring host = recall(connection);
    const HINTERNET request = g_HttpOpenRequestA(
        connection, verb, object, version, referrer, acceptTypes, flags & ~INTERNET_FLAG_SECURE, context);
    core::log::write("inet: %s %ls%s (request %p)",
                     verb != nullptr ? verb : "GET",
                     host.c_str(),
                     object != nullptr ? object : "",
                     request);
    if (request != nullptr) {
        char narrow[300]{};
        snprintf(narrow, sizeof(narrow), "X-Soulwarden-Host: %ls\r\n", host.c_str());
        HttpAddRequestHeadersA(request,
                               narrow,
                               static_cast<DWORD>(-1),
                               HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    }
    return request;
}

} // namespace

bool install() noexcept {
    read_settings();
    core::log::write("net: backend %s:%u", g_backendHost, g_backendHttpPort);
    const hooking::detour::Spec specs[] = {
        {reinterpret_cast<void**>(&g_getaddrinfo), reinterpret_cast<void*>(&hook_getaddrinfo)},
        {reinterpret_cast<void**>(&g_GetAddrInfoW), reinterpret_cast<void*>(&hook_GetAddrInfoW)},
        {reinterpret_cast<void**>(&g_connect), reinterpret_cast<void*>(&hook_connect)},
        {reinterpret_cast<void**>(&g_WSAConnect), reinterpret_cast<void*>(&hook_WSAConnect)},
        {reinterpret_cast<void**>(&g_sendto), reinterpret_cast<void*>(&hook_sendto)},
        {reinterpret_cast<void**>(&g_WSAIoctl), reinterpret_cast<void*>(&hook_WSAIoctl)},
        {reinterpret_cast<void**>(&g_bind), reinterpret_cast<void*>(&hook_bind)},
        {reinterpret_cast<void**>(&g_WinHttpConnect), reinterpret_cast<void*>(&hook_WinHttpConnect)},
        {reinterpret_cast<void**>(&g_WinHttpOpenRequest), reinterpret_cast<void*>(&hook_WinHttpOpenRequest)},
        {reinterpret_cast<void**>(&g_WinHttpSendRequest), reinterpret_cast<void*>(&hook_WinHttpSendRequest)},
        {reinterpret_cast<void**>(&g_WinHttpSetOption), reinterpret_cast<void*>(&hook_WinHttpSetOption)},
        {reinterpret_cast<void**>(&g_WinHttpCloseHandle), reinterpret_cast<void*>(&hook_WinHttpCloseHandle)},
        {reinterpret_cast<void**>(&g_InternetConnectA), reinterpret_cast<void*>(&hook_InternetConnectA)},
        {reinterpret_cast<void**>(&g_HttpOpenRequestA), reinterpret_cast<void*>(&hook_HttpOpenRequestA)},
    };
    return hooking::detour::install(specs);
}

} // namespace soulwarden::client::hooks::network

#include "client/network/backend_http.h"

#include <WinSock2.h>
#include <Windows.h>

#include <WS2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace soulwarden::client::network {
namespace {

constexpr DWORD kSocketTimeoutMilliseconds = 3000;
/** The largest reply that the hook reads: the state of a character is some kilobytes. */
constexpr std::size_t kMaxReplyBytes = 4 * 1024 * 1024;

bool send_all(SOCKET handle, const std::string& data) noexcept {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(handle, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

bool backend_request(const char* method,
                     const std::string& path,
                     const char* header,
                     const std::string& body,
                     BackendReply& reply) noexcept {
    reply = {};
    char host[64] = "127.0.0.1";
    char portText[16] = "32200";
    GetEnvironmentVariableA("SOULWARDEN_BACKEND_HTTP_HOST", host, sizeof(host));
    GetEnvironmentVariableA("SOULWARDEN_BACKEND_HTTP_PORT", portText, sizeof(portText));

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle != INVALID_SOCKET) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(std::strtoul(portText, nullptr, 10)));
        DWORD timeout = kSocketTimeoutMilliseconds;
        setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        if (inet_pton(AF_INET, host, &address.sin_addr) == 1
            && connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
            std::string request = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + portText
                                  + "\r\nConnection: close\r\n";
            if (header != nullptr) {
                request += header;
                request += "\r\n";
            }
            if (!body.empty()) {
                request += "Content-Type: application/json\r\n";
            }
            request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
            if (send_all(handle, request)) {
                std::string text;
                char chunk[4096];
                while (text.size() < kMaxReplyBytes) {
                    const int received = recv(handle, chunk, sizeof(chunk), 0);
                    if (received <= 0) {
                        break;
                    }
                    text.append(chunk, static_cast<std::size_t>(received));
                    const std::size_t headerEnd = text.find("\r\n\r\n");
                    if (headerEnd != std::string::npos) {
                        const std::size_t lengthAt = text.find("Content-Length: ");
                        if (lengthAt != std::string::npos && lengthAt < headerEnd) {
                            const std::size_t bodySize = std::strtoul(text.c_str() + lengthAt + 16, nullptr, 10);
                            if (text.size() >= headerEnd + 4 + bodySize) {
                                break;
                            }
                        }
                    }
                }
                // The status line: "HTTP/1.1 NNN text".
                const std::size_t headerEnd = text.find("\r\n\r\n");
                if (headerEnd != std::string::npos && text.rfind("HTTP/1.1 ", 0) == 0 && text.size() >= 12) {
                    reply.status = static_cast<int>(std::strtol(text.c_str() + 9, nullptr, 10));
                    reply.body = text.substr(headerEnd + 4);
                }
            }
        }
        closesocket(handle);
    }
    WSACleanup();
    return reply.status != 0;
}

bool backend_get(const char* path, std::string& output) noexcept {
    BackendReply reply;
    if (!backend_request("GET", path, nullptr, {}, reply) || reply.status != 200) {
        return false;
    }
    output = std::move(reply.body);
    return true;
}

std::string url_encode(const std::string& text) {
    std::string encoded;
    char cell[4];
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '-'
            || byte == '_' || byte == '.') {
            encoded += character;
        } else {
            snprintf(cell, sizeof(cell), "%%%02X", byte);
            encoded += cell;
        }
    }
    return encoded;
}

} // namespace soulwarden::client::network

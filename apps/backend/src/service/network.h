#pragma once

// Portable socket layer for the native Linux build and the MinGW Windows build.

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>

namespace soulwarden::backend::net {
using Socket = SOCKET;
inline constexpr Socket kInvalidSocket = INVALID_SOCKET;
inline void close_socket(Socket socket) noexcept {
    closesocket(socket);
}
/** Ends the two directions of a connection: a thread that waits in recv gets the end of the stream. */
inline void shutdown_socket(Socket socket) noexcept {
    shutdown(socket, SD_BOTH);
}
inline bool startup() noexcept {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
inline int last_error() noexcept {
    return WSAGetLastError();
}
} // namespace soulwarden::backend::net
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace soulwarden::backend::net {
using Socket = int;
inline constexpr Socket kInvalidSocket = -1;
inline void close_socket(Socket socket) noexcept {
    ::close(socket);
}
/** Ends the two directions of a connection: a thread that waits in recv gets the end of the stream. */
inline void shutdown_socket(Socket socket) noexcept {
    ::shutdown(socket, SHUT_RDWR);
}
inline bool startup() noexcept {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}
inline int last_error() noexcept {
    return errno;
}
} // namespace soulwarden::backend::net
#endif

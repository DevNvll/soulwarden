#pragma once

#include <string>

namespace soulwarden::client::network {

/** The reply of the backend to one request. */
struct BackendReply {
    /** The HTTP status. 0: the backend gave no reply. */
    int status{0};
    std::string body;
};

/**
 * Sends one request to a management route or to an admin route of the local backend. The
 * request uses a plain loopback socket (not WinHTTP, which the hook redirects for the game)
 * and has no game service host. The call blocks for 3 seconds at most for each socket step:
 * do not call it on a thread of the game.
 * @param header One more header line with no line end ("Name: value"), or null.
 * @param body The body of the request. A request with a body has the JSON content type.
 * @return False when the backend gave no reply (`reply.status` is then 0).
 */
[[nodiscard]] bool backend_request(const char* method,
                                   const std::string& path,
                                   const char* header,
                                   const std::string& body,
                                   BackendReply& reply) noexcept;

/**
 * Sends one GET request to a management route of the local backend.
 * @return True when the backend replies with status 200. The body goes to output.
 */
[[nodiscard]] bool backend_get(const char* path, std::string& output) noexcept;

/** @return A text for the query of a request: each byte that is not a letter, a digit, "-", "_", or "." is "%XX". */
[[nodiscard]] std::string url_encode(const std::string& text);

} // namespace soulwarden::client::network

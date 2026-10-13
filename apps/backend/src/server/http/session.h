#pragma once

#include <optional>

#include "server/http/services.h"
#include "service/http_server.h"

namespace soulwarden::backend::server::http {

/**
 * Finds the account that the access token of a request names (claim `personaId`). The client
 * sends the token in `x-nw-auth` to the gateway and in `x-amzn-token` to the Amazon Games
 * player services.
 * Stub authentication: the token has no signature that the backend checks.
 * @return No value when the request has no access token or the token names no account.
 */
[[nodiscard]] std::optional<state::Account> request_account(const Services& services,
                                                            const service::HttpRequest& request);

} // namespace soulwarden::backend::server::http

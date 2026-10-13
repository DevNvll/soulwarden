#pragma once

#include "server/http/router.h"

namespace soulwarden::backend::server::http {

/**
 * Finds the route of a request and runs it, then writes the log line and the capture file of
 * the request. The log line has the method, the service host, the target, the status, the
 * route name, and the two body sizes: it has no header, thus no token.
 *
 * Three routes get no log line and no capture file when the reply is 200: the health route of
 * the launcher, and the state route and the search route of the admin interface, which the
 * tool window calls at an interval while it shows.
 */
[[nodiscard]] service::HttpResponse handle_request(const Router& router, const service::HttpRequest& request);

} // namespace soulwarden::backend::server::http

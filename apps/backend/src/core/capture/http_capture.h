#pragma once

#include <string>

#include "service/http_server.h"

namespace soulwarden::backend::capture {

/** Enables protocol capture into a directory. Capture is off until this call. */
void enable(const std::string& directory);

/**
 * @return The target of a request for a log line and for a capture file. A request of the
 *         admin interface (a path below /admin) has the text "(not recorded)" in place of its
 *         query: a sender can put the token of the admin interface in the query by mistake,
 *         and the token must be in no log line and in no file. The route refuses such a
 *         request, and its record must not keep the token.
 */
[[nodiscard]] std::string recorded_target(const service::HttpRequest& request);

/** Writes one request and reply pair when capture is enabled. */
void record(const service::HttpRequest& request, const service::HttpResponse& response, const std::string& routeName);

} // namespace soulwarden::backend::capture

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "service/http_server.h"

namespace soulwarden::backend::server::http {

/** One matched request with the values of its path parameters. */
struct RouteContext {
    const service::HttpRequest& request;
    std::map<std::string, std::string> parameters;
};

using RouteHandler = std::function<service::HttpResponse(const RouteContext&)>;

/**
 * Registry of game HTTP routes.
 * A host pattern matches when the service host contains it. An empty pattern matches all hosts.
 * A path pattern uses `{name}` for one segment and a final `*` for the remaining path.
 */
class Router {
public:
    void add(std::string method, std::string host, std::string path, std::string name, RouteHandler handler);

    /** @return The handler reply, or a 404 reply when no route matches. */
    [[nodiscard]] service::HttpResponse dispatch(const service::HttpRequest& request, std::string& routeName) const;

private:
    struct Route {
        std::string method;
        std::string host;
        std::vector<std::string> segments;
        std::string name;
        RouteHandler handler;
    };

    std::vector<Route> m_routes;
};

/** @return A JSON reply with the given status. */
[[nodiscard]] service::HttpResponse json_reply(int status, const std::string& body);

} // namespace soulwarden::backend::server::http

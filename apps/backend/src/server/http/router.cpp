#include "server/http/router.h"

namespace soulwarden::backend::server::http {
namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string segment =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!segment.empty()) {
            segments.push_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return segments;
}

} // namespace

void Router::add(std::string method, std::string host, std::string path, std::string name, RouteHandler handler) {
    m_routes.push_back({std::move(method), std::move(host), split_path(path), std::move(name), std::move(handler)});
}

service::HttpResponse Router::dispatch(const service::HttpRequest& request, std::string& routeName) const {
    const std::vector<std::string> segments = split_path(request.path);
    for (const Route& route : m_routes) {
        if (route.method != "*" && route.method != request.method) {
            continue;
        }
        if (!route.host.empty() && request.host.find(route.host) == std::string::npos) {
            continue;
        }
        RouteContext context{request, {}};
        bool matched = true;
        std::size_t index = 0;
        for (; index < route.segments.size(); ++index) {
            const std::string& pattern = route.segments[index];
            if (pattern == "*") {
                index = segments.size();
                break;
            }
            if (index >= segments.size()) {
                matched = false;
                break;
            }
            if (pattern.size() > 2 && pattern.front() == '{' && pattern.back() == '}') {
                context.parameters[pattern.substr(1, pattern.size() - 2)] = segments[index];
            } else if (pattern != segments[index]) {
                matched = false;
                break;
            }
        }
        if (!matched || index != segments.size()) {
            continue;
        }
        routeName = route.name;
        return route.handler(context);
    }
    routeName = "unknown";
    return json_reply(404, R"({"message":"soulwarden: route not implemented"})");
}

service::HttpResponse json_reply(int status, const std::string& body) {
    service::HttpResponse response;
    response.status = status;
    response.body = body;
    return response;
}

} // namespace soulwarden::backend::server::http

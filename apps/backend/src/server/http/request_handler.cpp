#include "server/http/request_handler.h"

#include <string>

#include "core/capture/http_capture.h"
#include "core/logging/log.h"
#include "server/http/routes.h"

namespace soulwarden::backend::server::http {

service::HttpResponse handle_request(const Router& router, const service::HttpRequest& request) {
    std::string routeName;
    service::HttpResponse response = router.dispatch(request, routeName);
    const bool polled = routeName == "management.health"
                        || (response.status == 200
                            && (routeName == kAdminStateRoute || routeName == kAdminCatalogRoute
                                || routeName == kAdminCreatureCatalogRoute));
    if (!polled) {
        log::write("http: %s %s %s%s -> %d [%s] request %zu reply %zu",
                   routeName == "unknown" ? "UNKNOWN" : "ok",
                   request.method.c_str(),
                   request.host.c_str(),
                   capture::recorded_target(request).c_str(),
                   response.status,
                   routeName.c_str(),
                   request.body.size(),
                   response.body.size());
        capture::record(request, response, routeName);
    }
    return response;
}

} // namespace soulwarden::backend::server::http

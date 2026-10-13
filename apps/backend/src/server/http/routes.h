#pragma once

#include "server/http/router.h"
#include "server/http/services.h"

namespace soulwarden::backend::server::http {

/** Channel service: region list and public API endpoints. */
void register_channel_routes(Router& router);

/** Amazon Games account token service. */
void register_omni_routes(Router& router, const Services& services);

/** Javelin HTTP gateway. */
void register_gateway_routes(Router& router, const Services& services);

/** AWS SDK service endpoints. */
void register_aws_routes(Router& router);

/**
 * Development commands for the world (management routes: a request with a service host gets
 * 403). The backend program adds them only with the option --dev-commands, which needs the
 * harness gate.
 */
void register_dev_routes(Router& router, const Services& services);

class AdminAccess;

/** Names of the admin routes that the tool window calls at an interval (request_handler.h). */
inline constexpr const char* kAdminStateRoute = "admin.state";
inline constexpr const char* kAdminCatalogRoute = "admin.catalog_items";
inline constexpr const char* kAdminCreatureCatalogRoute = "admin.catalog_creatures";

/**
 * The admin interface for the tool window of the game hook (admin_routes.cpp): the state of a
 * character of the account of a token with the creatures near it, commands for it, the item
 * search, and the search of the creature kinds. The routes need
 * no harness gate. A request needs a token of `access`, which must live as long as the router.
 */
void register_admin_routes(Router& router, const Services& services, const AdminAccess& access);

} // namespace soulwarden::backend::server::http

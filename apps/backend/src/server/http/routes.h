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

/**
 * The entitlement service of the Amazon Games SDK (stage 1.1b, entitlement_routes.cpp): the entitlements of the account of
 * the token (GET) and the sync (POST), from the grant policy of world/ownership.h.
 */
void register_entitlement_routes(Router& router, const Services& services);

/** Host of the remote configuration documents of the client (an S3 bucket in the original service). */
inline constexpr const char* kRemoteConfigurationHost = "ags-javelin-remote-config.s3.amazonaws.com";
/**
 * Path of the one document that the backend has: the gameplay settings of the product (the
 * Steam application of the game), for each client version.
 */
inline constexpr const char* kGameplayConfigurationPath =
    "/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless";
/**
 * The document. It sets one registered setting of the client. The client reads the body only
 * when the reply has its length (the HTTP writer gives it): 50 bytes.
 */
inline constexpr const char* kMeleeReportDocument = R"({"javelin.enable-client-authoritative-melee":true})";

/** Switches of the AWS routes. */
struct AwsOptions {
    /**
     * The reply to the request for the gameplay configuration document has the document. Off:
     * the reply is "no such key", as for each other document, and the client reports no melee
     * contact (backend option --no-melee-report).
     */
    bool meleeReport{true};
};

/** AWS SDK service endpoints. */
void register_aws_routes(Router& router, const AwsOptions& options);

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
inline constexpr const char* kAdminPlaceCatalogRoute = "admin.catalog_places";
inline constexpr const char* kAdminPresetCatalogRoute = "admin.catalog_presets";

/**
 * The admin interface for the tool window of the game hook (admin_routes.cpp): the state of a
 * character of the account of a token with the creatures near it, commands for it, the item
 * search, and the search of the creature kinds. The routes need
 * no harness gate. A request needs a token of `access`, which must live as long as the router.
 */
void register_admin_routes(Router& router, const Services& services, const AdminAccess& access);

} // namespace soulwarden::backend::server::http

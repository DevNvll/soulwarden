#include <nlohmann/json.hpp>

#include "server/http/routes.h"

// The client reads `<channel host>/STEAM_APP_ID.<app id>.json` at startup. Each region entry
// names the public APIs by client tag. All hosts below resolve to this backend through the hook.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

constexpr const char* kRegionId = "soulwarden-local";
constexpr const char* kAwsRegion = "us-east-1";

json public_api(const char* tag, const char* endpoint, bool staged) {
    json api = {
        {"awsRegion", kAwsRegion},
        {"version", "1.0"},
        {"apiEndpoint", endpoint},
        {"clientTag", tag},
    };
    if (staged) {
        api["stage"] = "prod";
    }
    return api;
}

service::HttpResponse channel_document(const RouteContext&) {
    json document = {
        {"channelName", "Retail"},
        {kRegionId,
         {
             {"displayName", "Soulwarden Local"},
             {"localizedName", "@mm_us_east"},
             {"poolId", "us-east-1:00000000-0000-4000-8000-000000000001"},
             {"publicApis",
              json::array({
                  public_api("authStack", "auth.soulwarden.local", true),
                  public_api("loginGateway", "login.soulwarden.local", true),
                  public_api("JavelinGatewayServiceV2", "gateway.soulwarden.local", true),
                  public_api("JavelinGatewayService-CF", "gateway-cf.soulwarden.local", false),
              })},
         }},
        {"platformMetadata",
         {
             {"ENTITLEMENTS.STEAM_APP_ID.1063730", {{"app", "STEAM_APP_ID.1063730"}}},
             {"OMNI.STEAM_APP_ID.1063730",
              {
                  {"omniTokenUrl", "https://tokenservice.soulwarden.local"},
                  {"nwTokenUrl", "https://nwtoken.soulwarden.local/"},
                  {"omniStage", "prod"},
                  {"omniGameAlias", "new-world"},
              }},
         }},
    };
    return json_reply(200, document.dump(2));
}

} // namespace

void register_channel_routes(Router& router) {
    router.add("GET", "cloudfront.net", "/{document}", "channel.document", channel_document);
}

} // namespace soulwarden::backend::server::http

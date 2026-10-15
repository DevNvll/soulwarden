#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "server/http/routes.h"
#include "server/http/session.h"

// The entitlement service of the Amazon Games SDK of the client (stage 1.1b): host client.entitlementservice.amazongames.com.
// The client sends at its start, after the token and before the character list:
//   POST /players/{persona}/games/new-world/platforms/steam/entitlements/sync   body {"syncTypes": [...]} (builder at image
//        RVA 0x74D5100; types FirstTimeLogin, PrimeGamingSync, CodeRedemptionSync, TwitchDropsSync, SteamRetailSync)
//   GET  /players/{persona}/games/new-world/platforms/steam/entitlements        (builders 0x74CAA10, 0x74CB580, 0x74CC700)
// The GET reply: {"entitlements": [{"alias": TEXT, "amount": {...}}], "hasMoreResults": false} (root parser 0x74C2770,
// entry parser 0x74C2E60, amount parser 0x74C15D0 with the signed integers freeAmount, paidAmount, pendingAmount,
// lossAmount, totalAmount). The sync reply: {} (an empty body of success, callback 0x641ADE0).
//
// The backend owns the grant: a sync applies the grant policy of world/ownership.h again (a task of the world), and it
// contacts no service of Steam, Prime, or Twitch. The persona of the path must be the account of the token: a request for
// a different account gets 403 and changes nothing.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

/** @return The account of the token when it is the persona of the path, or no value (the reply is then `refusal`). */
std::optional<state::Account> path_account(const Services& services, const RouteContext& context, service::HttpResponse& refusal) {
    const auto account = request_account(services, context.request);
    if (!account) {
        log::write("entitlements: no account for the access token");
        refusal = json_reply(401, R"({"message":"Unauthorized"})");
        return std::nullopt;
    }
    if (account->personaId != context.parameters.at("persona")) {
        log::write("entitlements: the token of account %s names a different account in the path: refused, nothing changed",
                   account->personaId.c_str());
        refusal = json_reply(403, R"({"message":"Forbidden"})");
        return std::nullopt;
    }
    return account;
}

/** GET: the entitlements of the account (the rows and the grant policy). */
service::HttpResponse list_entitlements(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto account = path_account(services, context, refusal);
    if (!account) {
        return refusal;
    }
    const std::string personaId = account->personaId;
    world::CallStatus status = world::CallStatus::done;
    const auto aliases = services.world.call<std::vector<std::string>>(
        [personaId](world::World& target) { return target.ownership().account_entitlements(personaId); }, kWorldCallLimit, &status);
    if (!aliases) {
        log::write("entitlements: the world did not give the entitlements of account %s", personaId.c_str());
        return json_reply(503, R"({"message":"Try again"})");
    }
    json entries = json::array();
    for (const std::string& alias : *aliases) {
        // One owned unit, with no paid amount: the account owns the entitlement (research dlc-entitlements.md, Q2).
        entries.push_back({{"alias", alias},
                           {"amount", {{"freeAmount", 1}, {"paidAmount", 0}, {"pendingAmount", 0}, {"lossAmount", 0}, {"totalAmount", 1}}}});
    }
    log::write("entitlements: account %s has %zu entitlement(s)", personaId.c_str(), entries.size());
    return json_reply(200, json{{"entitlements", entries}, {"hasMoreResults", false}}.dump());
}

/** POST sync: the grant policy again (a grant that is there changes nothing). */
service::HttpResponse sync_entitlements(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto account = path_account(services, context, refusal);
    if (!account) {
        return refusal;
    }
    const std::string personaId = account->personaId;
    world::CallStatus status = world::CallStatus::done;
    const auto granted = services.world.call<bool>(
        [personaId](world::World& target) { return target.ownership().grant_account(personaId, "sync"); }, kWorldCallLimit, &status);
    if (!granted || !*granted) {
        // The client can send a request with a 5xx reply again; the next world entry gives the grant too.
        log::write("entitlements: no sync for account %s (%s)", personaId.c_str(),
                   status == world::CallStatus::notStarted ? "the world did not start the task" : "the database failed");
        return json_reply(503, R"({"message":"Try again"})");
    }
    return json_reply(200, "{}");
}

} // namespace

void register_entitlement_routes(Router& router, const Services& services) {
    const auto bind = [&services](service::HttpResponse (*handler)(const Services&, const RouteContext&)) {
        return [&services, handler](const RouteContext& context) { return handler(services, context); };
    };
    router.add("GET", "entitlementservice", "/players/{persona}/games/{game}/platforms/{platform}/entitlements",
               "entitlements.list", bind(list_entitlements));
    router.add("POST", "entitlementservice", "/players/{persona}/games/{game}/platforms/{platform}/entitlements/sync",
               "entitlements.sync", bind(sync_entitlements));
}

} // namespace soulwarden::backend::server::http

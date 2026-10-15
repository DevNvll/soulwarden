#include <chrono>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "core/logging/log.h"
#include "middleware/steam/local_ticket.h"
#include "server/http/routes.h"
#include "state/identity.h"
#include "state/world.h"

// Amazon Games account services used by the client SDK ("OmniSDK").
// Route: tokenservice.amazongames.com  POST /games/{game}/tokens
// The reply fields come from the client reader in ags_fed_acc_token.cpp.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/** The claim `personaId` names the account in each later request (server/http/session.h). */
std::string access_token(const state::Account& account) {
    const std::int64_t now = now_seconds();
    const json claims = {
        {"iss", "soulwarden-local"},
        {"sub", account.accountId},
        {"aud", "new-world"},
        {"iat", now},
        {"exp", now + 86400},
        {"personaId", account.personaId},
    };
    return encoding::local_jwt(claims.dump());
}

const char* origin_text(state::AccountOrigin origin) {
    switch (origin) {
    case state::AccountOrigin::adopted:
        return "adopted";
    case state::AccountOrigin::created:
        return "created";
    case state::AccountOrigin::found:
        break;
    }
    return "found";
}

/**
 * Development aid: gives an account that has no character the seed character. The name of a
 * character is unique for all accounts, thus a name in use gives no character.
 */
void seed_character(const Services& services, const state::Account& account) {
    if (services.seedCharacter.empty() || !services.store.list_characters(account.personaId).empty()) {
        return;
    }
    state::Character character;
    character.characterId = state::new_uuid();
    character.personaId = account.personaId;
    character.worldId = state::local_world().worldId;
    character.name = services.seedCharacter;
    character.createdDate = state::now_iso8601();
    character.modifiedDate = character.createdDate;
    character.ftueCompleted = true;
    switch (services.store.insert_character(character)) {
    case state::InsertResult::inserted:
        log::write("omni: seeded character %s (%s) for account %s",
                   character.name.c_str(),
                   character.characterId.c_str(),
                   account.personaId.c_str());
        break;
    case state::InsertResult::nameInUse:
        log::write("omni: no seed character for account %s: the name %s is in use",
                   account.personaId.c_str(),
                   character.name.c_str());
        break;
    case state::InsertResult::failed:
        log::write("omni: no seed character for account %s: the database failed", account.personaId.c_str());
        break;
    }
}

/**
 * POST /games/{game}/tokens
 * Stub authentication: the ticket of the request names the Steam account and is not
 * validated. A request with no local ticket gets the fallback identity, and it does not take
 * an account from before the account support: only a ticket names the player of that data.
 */
service::HttpResponse create_token(const Services& services, const RouteContext& context) {
    const state::LocalIdentity& local = state::local_identity();
    const json body = json::parse(context.request.body, nullptr, false);
    std::optional<steam::TicketIdentity> ticket;
    if (body.is_object() && body.contains("platformAuth") && body["platformAuth"].is_object()
        && body["platformAuth"].contains("ticket") && body["platformAuth"]["ticket"].is_string()) {
        ticket = steam::read_local_ticket(body["platformAuth"]["ticket"].get<std::string>());
    }
    const std::string& steamId = ticket ? ticket->steamId : local.fallbackSteamId;
    const std::string& personaName = ticket ? ticket->personaName : local.fallbackPersonaName;

    state::AccountOrigin origin = state::AccountOrigin::found;
    const auto account = services.store.login_account(steamId, personaName, ticket.has_value(), origin);
    if (!account) {
        log::write("omni: no account for Steam id %s: the database failed", state::masked_steam_id(steamId).c_str());
        return json_reply(500, R"({"message":"Internal error"})");
    }
    log::write("omni: account %s, Steam id %s%s, %s, %s",
               account->personaId.c_str(),
               state::masked_steam_id(account->steamId).c_str(),
               account->steamId == local.fallbackSteamId ? " (fallback identity)" : "",
               origin_text(origin),
               ticket ? "local ticket" : "no local ticket");
    seed_character(services, *account);
    // The DLC of the account (stage 1.1b, world/ownership.h): the grant at each login, with no purchase and no setting.
    // A task of the world; with no start in the time limit the world entry and the sync of the client give it.
    const std::string personaId = account->personaId;
    world::CallStatus status = world::CallStatus::done;
    const auto granted = services.world.call<bool>(
        [personaId](world::World& target) { return target.ownership().grant_account(personaId, "login"); }, kWorldCallLimit, &status);
    if (!granted) {
        log::write("omni: the world did not give the entitlements of account %s at the login: the world entry gives them",
                   account->personaId.c_str());
    }

    const json reply = {
        {"accessToken", access_token(*account)},
        {"expiresIn", 86400},
        {"fallbackToken", "soulwarden-local-fallback-token"},
        {"region", local.region},
        {"isNewAccount", false},
        {"account",
         {
             {"identityId", account->accountId},
             {"personaId", account->personaId},
             {"type", "full"},
             {"ageGroup", "ADULT"},
         }},
        {"platformAccount",
         {
             {"identityId", account->steamId},
             {"personaId", account->personaId},
             {"identityType", "steam"},
             {"platform", "steam"},
             {"ageGroup", "ADULT"},
         }},
    };
    return json_reply(200, reply.dump());
}

} // namespace

void register_omni_routes(Router& router, const Services& services) {
    router.add("POST", "tokenservice", "/games/{game}/tokens", "omni.create_token",
               [&services](const RouteContext& context) { return create_token(services, context); });
}

} // namespace soulwarden::backend::server::http

#include "server/http/session.h"

#include <string>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

/** @return The access token of a request, or an empty text. */
std::string access_token(const service::HttpRequest& request) {
    for (const char* name : {"x-nw-auth", "x-amzn-token"}) {
        std::string token = request.header(name);
        if (!token.empty()) {
            return token;
        }
    }
    return {};
}

} // namespace

std::optional<state::Account> request_account(const Services& services, const service::HttpRequest& request) {
    std::string payload;
    if (!encoding::jwt_payload(access_token(request), payload)) {
        return std::nullopt;
    }
    const json claims = json::parse(payload, nullptr, false);
    if (!claims.is_object() || !claims.contains("personaId") || !claims["personaId"].is_string()) {
        return std::nullopt;
    }
    return services.store.find_account(claims["personaId"].get<std::string>());
}

} // namespace soulwarden::backend::server::http

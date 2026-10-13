#include "server/http/admin_access.h"

#include <utility>

#include "state/identity.h"

namespace soulwarden::backend::server::http {
namespace {

/** @return True for equal texts. The time does not depend on the place of the first difference. */
bool equal_tokens(std::string_view left, std::string_view right) {
    unsigned char difference = left.size() == right.size() ? 0 : 1;
    for (std::size_t index = 0; index < right.size(); ++index) {
        const char other = index < left.size() ? left[index] : '\0';
        difference = static_cast<unsigned char>(difference | (static_cast<unsigned char>(other) ^ static_cast<unsigned char>(right[index])));
    }
    return difference == 0;
}

} // namespace

bool AdminAccess::add(std::string token, std::string steamId) {
    if (token.size() < kAdminTokenMinimum || !state::is_steam_id(steamId)) {
        return false;
    }
    m_grants.push_back({std::move(token), std::move(steamId)});
    return true;
}

std::optional<std::string> AdminAccess::steam_id(const service::HttpRequest& request) const {
    const std::string token = request.header(kAdminTokenHeader);
    std::optional<std::string> found;
    // Each token is compared: the time does not show which one is equal.
    for (const Grant& grant : m_grants) {
        if (equal_tokens(token, grant.token) && !found) {
            found = grant.steamId;
        }
    }
    return found;
}

} // namespace soulwarden::backend::server::http

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "service/http_server.h"

// Who can use the admin interface (admin_routes.cpp). The launcher makes a new random token
// for each start of the game and gives it to the backend and to the game hook in their
// environments. The token is for one account: the account of the Steam id that the launcher
// gives to the game of that start. A request with the token can read and change only the
// characters of that account.
//
// The token is a secret of one start. The backend writes it to no log line and to no capture
// file (core/capture/http_capture.cpp), and a request has it only in the header X-Soulwarden-Admin,
// never in its path or its query.

namespace soulwarden::backend::server::http {

/** Request header with the token. */
inline constexpr const char* kAdminTokenHeader = "X-Soulwarden-Admin";
/** A token has this number of characters or more. A shorter one is not accepted as a token. */
inline constexpr std::size_t kAdminTokenMinimum = 32;

class AdminAccess {
public:
    /**
     * Adds one token for the account of a Steam id.
     * @return False when the token is too short, or the text is not a Steam id of an individual account.
     */
    [[nodiscard]] bool add(std::string token, std::string steamId);

    /** @return True when one token or more is there. With no token the backend has no admin route. */
    [[nodiscard]] bool enabled() const {
        return !m_grants.empty();
    }

    /**
     * @return The Steam id of the account of the token of a request, or no value when the
     *         request has no token or a token that is not known. The compare of a token does
     *         not stop at the first different character.
     */
    [[nodiscard]] std::optional<std::string> steam_id(const service::HttpRequest& request) const;

private:
    struct Grant {
        std::string token;
        std::string steamId;
    };
    std::vector<Grant> m_grants;
};

} // namespace soulwarden::backend::server::http

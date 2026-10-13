#pragma once

#include <optional>
#include <string>
#include <string_view>

// Local ticket of the Soulwarden game hook. The hook gives it to the client in place of a Steam
// authentication ticket, and the client sends it to the account token service, to the login
// queue, and to the game server:
//
//   ticket := "SOULWARDEN-LOCAL-TICKET" ";" version ";" steam-id ";" persona-hex
//             version     := "1"
//             steam-id    := 17 decimal digits, an individual account in the public universe
//             persona-hex := lower-case hexadecimal text of the UTF-8 persona name
//
// The ticket names an account. It is not a credential and it proves nothing.

namespace soulwarden::backend::steam {

/** Account that a local ticket names. */
struct TicketIdentity {
    /** Steam id 64 in decimal text. */
    std::string steamId;
    /** UTF-8 text with no control characters. Empty when the ticket has no usable name. */
    std::string personaName;
};

/**
 * Reads a local ticket.
 * @param text Hexadecimal text of the ticket bytes, as the client sends it. The prefix
 *        "steam|" of the game server registration is accepted.
 * @return No value when the text is not a local ticket of this version.
 */
[[nodiscard]] std::optional<TicketIdentity> read_local_ticket(std::string_view text);

} // namespace soulwarden::backend::steam

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

// Research switch of the game server for live checks of the client. It is not product
// behavior: all values are off by default, and the backend program sets them only from the
// option --research-session, which the launcher gives only to a run with SOULWARDEN_HARNESS=1.
//
// The first three values make the backend send what a server sends when it refuses or ends a
// session (protocol notes 3.6), for a session that the product does not refuse and does not
// end. The product behavior is in RepSession: a registration that the world refuses gets the
// result -160, and a session that loses its character gets the message "kicked".
//
// The value `refuseEquipment` makes the backend refuse each request of a client for its
// equipment with the message "equip item fail" (protocol notes 4.11): the client sends only
// requests that its own checks permit, thus a live client gets that message from the product
// only for a request that is not from its screen.

namespace soulwarden::backend::server::rep {

struct ResearchSwitch {
    /**
     * Result of the registration reply (type 3), read by the client as a signed number. With
     * a value that is not 0 the session sends only the reply.
     */
    std::optional<std::int32_t> registrationResult;
    /**
     * Wire reason of a PlayerManagerRejectedMsg (type 1630). The session sends it after a
     * registration reply with result 0, in place of the self identification.
     */
    std::optional<std::uint32_t> rejectReason;
    /**
     * Wire reason of a KickClientMsg (type 271). The session sends it `kickDelay` after the
     * first camera position of the client (the loading screen is then gone).
     */
    std::optional<std::uint32_t> kickReason;
    std::chrono::milliseconds kickDelay{10000};
    /** True: each equip, unequip, and swap request of a client is refused with no check and changes nothing. */
    bool refuseEquipment{false};

    [[nodiscard]] bool active() const {
        return registrationResult.has_value() || rejectReason.has_value() || kickReason.has_value() || refuseEquipment;
    }
};

/**
 * Reads one value of the option --research-session: "registration-result=N", "reject=N",
 * "kick=N", "kick-delay=SECONDS", or "refuse-equipment=1".
 * @return False when the text is not one of these forms.
 */
[[nodiscard]] bool read_research_option(const std::string& text, ResearchSwitch& research);

} // namespace soulwarden::backend::server::rep

#include "middleware/steam/local_ticket.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "core/encoding/hex.h"
#include "state/identity.h"

namespace soulwarden::backend::steam {
namespace {

constexpr std::string_view kClientPrefix = "steam|";
constexpr std::string_view kMarker = "SOULWARDEN-LOCAL-TICKET";
constexpr std::string_view kVersion = "1";
constexpr char kSeparator = ';';
constexpr std::size_t kFieldCount = 4;
/** A persona name of Steam has 32 characters at most, and a character has 4 bytes at most in UTF-8. */
constexpr std::size_t kPersonaNameLimit = 32 * 4;

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        fields.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) {
            return fields;
        }
        start = end + 1;
    }
}

/**
 * @return True for correct UTF-8 text with no control characters. Text that is not UTF-8
 *         makes the JSON writer throw, thus a name from a ticket gets this check.
 */
bool is_display_text(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t tail = 0;
        std::uint32_t codePoint = lead;
        if (lead < 0x80) {
            if (lead < 0x20 || lead == 0x7F) {
                return false;
            }
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            tail = 1;
            codePoint = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            tail = 2;
            codePoint = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            tail = 3;
            codePoint = lead & 0x07;
        } else {
            return false;
        }
        if (index + tail >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= tail; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        // Reject the forms that are too long, the surrogates, and values after the last code point.
        if ((tail == 2 && codePoint < 0x800) || (tail == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF
            || (codePoint >= 0xD800 && codePoint <= 0xDFFF) || (codePoint >= 0x80 && codePoint < 0xA0)) {
            return false;
        }
        index += tail + 1;
    }
    return true;
}

} // namespace

std::optional<TicketIdentity> read_local_ticket(std::string_view text) {
    if (text.substr(0, kClientPrefix.size()) == kClientPrefix) {
        text.remove_prefix(kClientPrefix.size());
    }
    std::string ticket;
    if (!encoding::hex_decode(text, ticket)) {
        return std::nullopt;
    }
    const std::vector<std::string> fields = split(ticket, kSeparator);
    if (fields.size() != kFieldCount || fields[0] != kMarker || fields[1] != kVersion
        || !state::is_steam_id(fields[2])) {
        return std::nullopt;
    }
    TicketIdentity identity;
    identity.steamId = fields[2];
    std::string personaName;
    if (encoding::hex_decode(fields[3], personaName) && personaName.size() <= kPersonaNameLimit
        && is_display_text(personaName)) {
        identity.personaName = std::move(personaName);
    }
    return identity;
}

} // namespace soulwarden::backend::steam

#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace soulwarden::backend::state {

/**
 * Values of the local service that are not player data. The fallback identity applies to a
 * login that has no local ticket of the game hook.
 */
struct LocalIdentity {
    /** Steam id of the fallback identity. The game hook has the same value (kLocalSteamId). */
    std::string fallbackSteamId{"76561197970884513"};
    std::string fallbackPersonaName{"Soulwarden"};
    std::string region{"us-east-1"};
};

[[nodiscard]] inline const LocalIdentity& local_identity() {
    static const LocalIdentity identity;
    return identity;
}

/** Decimal digits of a Steam id 64 of an individual account. */
inline constexpr std::size_t kSteamIdDigits = 17;
/** High 32 bits of the Steam id of an individual account in the public universe. */
inline constexpr std::uint64_t kIndividualAccountBits = 0x01100001ULL;

/** @return True for the decimal text of a Steam id of an individual account. */
[[nodiscard]] inline bool is_steam_id(const std::string& text) {
    if (text.size() != kSteamIdDigits || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    return (std::strtoull(text.c_str(), nullptr, 10) >> 32) == kIndividualAccountBits;
}

/** @return A Steam id for a log line: only its last 4 digits. A log must not have the full id. */
[[nodiscard]] inline std::string masked_steam_id(const std::string& steamId) {
    constexpr std::size_t kShownDigits = 4;
    return steamId.size() > kShownDigits ? "..." + steamId.substr(steamId.size() - kShownDigits) : "(none)";
}

} // namespace soulwarden::backend::state

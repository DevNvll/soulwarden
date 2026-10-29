#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The messages of the entry into a game mode and of its leave (stage QG9a, docs/systems/instances.md, protocol notes 4.67). Each facet body
// starts with `target:u64 entity:u64` (reader 0x15095D0); the numbers are big-endian.
//
//   3094 DungeonEntranceClientFacet_OnDungeonActivateResult (server to client; reader 0x26D52A0, handler 0x2FFB4F0): result:u32, the value
//        of eDungeonActivateResponse (Success 0, ..., EntranceActivationFailure 10, ...; registration 0x54D473F to 0x54D484D). The target is
//        the DungeonEntrance component of the entrance (its facet target by the XOR rule), the entity G of the entrance.
//   3309 GameModeParticipantComponentServerFacet_RequestLeaveGameMode (client to server; reader 0x3273BB0, caller 0x32D2A10): a mode
//        reference `id:16 u64 u64` (16 raw bytes and two u64).
//   946  PlayerArenaComponentServerFacet_TeleportIntoDungeon (client to server; reader 0x259E1E0, send stub 0x2760D20): no field.

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeDungeonActivateResult = 3094;
inline constexpr std::uint32_t kTypeRequestLeaveGameMode = 3309;
inline constexpr std::uint32_t kTypeTeleportIntoDungeon = 946;
/**
 * 1980 GameModeParticipantComponentServerFacet_OnReceivedClientEvent (client to server; reader 0x26FEC60): event:u32. The escape menu and the
 * leave confirmation of the dungeon HUD send the event 2612035792 = 9BB080D0 (SendClientEvent; static read E2d of stage QG9a): the leave.
 */
inline constexpr std::uint32_t kTypeGameModeClientEvent = 1980;
inline constexpr std::uint32_t kGameModeLeaveEvent = 0x9BB080D0;
/** The GameModeParticipant component of the player slice (slot 24), by its name in slice-ids.json (component 8DE633590B8FBEE0). */
inline constexpr const char* kGameModeParticipantComponentName = "RootPlayer/b5a705af-93a1-4059-a67c-988a44154fa5";

/** @return The body of 3094 to the DungeonEntrance component `target` of the entrance G `entity`. */
[[nodiscard]] std::vector<std::uint8_t> dungeon_result_body(std::uint64_t target, std::uint64_t entity, std::uint32_t result);

/** The body of 3309: the facet prefix and the mode reference. */
struct LeaveGameModeRequest {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::array<std::uint8_t, 16> id{};
    std::uint64_t first{0};
    std::uint64_t second{0};
};
/** @return The body of 3309, or no value for a body that does not have its form (exactly 48 bytes). */
[[nodiscard]] std::optional<LeaveGameModeRequest> read_leave_game_mode(std::span<const std::uint8_t> body);

} // namespace soulwarden::backend::server::rep

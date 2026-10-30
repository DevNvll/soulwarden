#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The GameModeParticipant state of a character (stage QG9a, docs/systems/instances.md, protocol notes 4.67; static read E of stage QG9a).
//
// GameModeParticipant (type 3312, constructor 0x329ACB0), slot 24 of the player slice. Group 0 has six fields; this encoder sends two:
//   field 0  activeGameModes  a replicated map (field table 0x81B6E18, reader 0x32D77B0): `changes:size = 0` selects the full replacement
//                             0x32D96E0, then the optional stamp `01 R:size64` (0x61ACCA0) and the map 0x328D5F0: `count:size`, each entry
//                             `A:16raw G:u64 F:u64` (the server-facet reference of the game mode entity) and `C:u32 M:u32 W:u64 X:u64`
//                             (C the CRC-32 of the lower-case GameModeId that IsInGameMode 0x32BA3B0 compares; M of the GameModeMapId; W the
//                             entry time, Unix nanoseconds; X a value with no name that no checked reader tests: 0)
//   field 1  flags            six raw bytes of a bitset (reader 0x32D94C0, 0x5D387A0): bit 22 PlayerCanQuit (0x2F2C60) shows the extra
//                             mode button of the escape menu (ButtonGameExtra). Stage QG9e: no bit. The click of that button asks the
//                             GameMode entity G of the client for a penalty, and the client has no such entity: the button does nothing
//                             (static read LEAVE Q3; live: no popup, nothing sent). The dungeon leave of the menu (the PlayerArena state,
//                             message 926) needs no flag [agent: no button that does nothing]
// The other fields (the queues, the mutation context, lastTeamIndex) and groups 1 and 2 (the matchmaking service, the group eligibility)
// stay at their defaults: no fragment.
//
// View     Fragments
// owner    the membership while the character is in an instance, and no flag; after its leave the empty map (the client removes the
//          entry: the exit notification of 0x32B78C0); nothing before the first membership of the world entry
// observer no fragment [agent: the audience of the original server is not found; the remote player slice has the component]

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGameModeGroups = 1;
/** No bit of the flags. Bit 22 (PlayerCanQuit) is byte 2, bit 6, in the raw little-bit-order storage (static read E2e: `00 00 40 00 00 00`). */
constexpr std::uint8_t kNoFlags[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

} // namespace

hub::Bytes encode_game_mode_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)clock;
    if (view != View::owner || entity.kind != world::EntityKind::player || !entity.gameModeKnown) {
        return {};
    }
    // The revision stamp of the map: the revision of the last change of the component (a newer full replacement has a greater stamp).
    const auto found = entity.changed.find(world::ComponentKey::gameModeParticipant);
    const std::uint64_t revision = found != entity.changed.end() && found->second != 0 ? found->second : 1;
    StateWriter state(kGameModeGroups);
    hub::Writer& modes = state.field(0, 0);
    modes.size(0);
    modes.u8(1);
    modes.size64(revision);
    const std::optional<world::GameModeMembership>& membership = entity.gameModeMembership;
    modes.size(membership ? 1 : 0);
    if (membership) {
        modes.raw(membership->actor);
        modes.u64(membership->group);
        modes.u64(membership->facet);
        modes.u32(membership->modeCrc);
        modes.u32(membership->mapCrc);
        modes.u64(membership->enteredNs);
        modes.u64(0);
    }
    state.field(0, 1).raw(kNoFlags);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

#include <chrono>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::PlayerComponentReplicatedState (tools/st PlayerComponentReplicatedState), slot 9 of the
// player slice. The client compares the character id of this state with the character id of
// its login: an equal id makes the entity the local player, a different id makes it a remote
// player (client code at image RVA 0x6929DF0). The compare is of all bytes: the text must be
// the text of the login token.
//
// The state has 3 groups. Group 1 has the account and session data of the owner (22 fields:
// loginMatchId, accountIsLocked, sessionStartTimePoint, ...). Group 2 has the identity of the
// character (constructor at image RVA 0x6711E90).
//
// View     Fields
// owner    group 1: sessionStartWallClockTimePoint (field 6, the Unix time in ns) and
//          sessionStartTimePoint (field 7, the server time in ns of the registration clock), from one
//          sample of the world clock (stage 3.3b-S1): the client computes its synced wall clock as
//          wallStart + (serverNow - serverStart) (getter 0x6830A40, apply 0x683C8D0), and the end of a
//          status is a time of that clock (live check L1: with no pair the client shows 20737 days for
//          a Unix end; a tray entry that comes before the pair keeps its wrong time). Group 2:
//          characterId, characterName, playerConnected. A field of group 1 is for the owner only.
// observer group 2: characterId, characterName, playerConnected

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 3;
constexpr unsigned kSessionGroup = 1;
constexpr unsigned kSessionStartWall = 6;
constexpr unsigned kSessionStartServer = 7;
constexpr unsigned kIdentityGroup = 2;
constexpr unsigned kCharacterId = 0;
constexpr unsigned kCharacterName = 1;
constexpr unsigned kPlayerConnected = 3;

} // namespace

hub::Bytes encode_player_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    StateWriter state(kGroups);
    if (view == View::owner) {
        // The wall clock pair of the client, from one sample of the world clock.
        const world::WorldClock::time_point now = clock.now();
        state.field(kSessionGroup, kSessionStartWall).u64(clock.wall_time_ns(now));
        state.field(kSessionGroup, kSessionStartServer).u64(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()));
    }
    // Identity in the text form: flags without bit 0, then the text.
    state.field(kIdentityGroup, kCharacterId).u8(0);
    state.field(kIdentityGroup, kCharacterId).string(entity.characterId);
    state.field(kIdentityGroup, kCharacterName).string(entity.name);
    // The loading screen of the client waits for a connected local player (client check at
    // image RVA 0x102EA20).
    state.field(kIdentityGroup, kPlayerConnected).boolean(true);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

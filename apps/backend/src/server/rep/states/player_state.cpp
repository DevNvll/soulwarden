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
// owner    group 2: characterId, characterName, playerConnected. The backend sends no field
//          of group 1 now. A field of group 1 is for the owner only.
// observer group 2: characterId, characterName, playerConnected

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 3;
constexpr unsigned kIdentityGroup = 2;
constexpr unsigned kCharacterId = 0;
constexpr unsigned kCharacterName = 1;
constexpr unsigned kPlayerConnected = 3;

} // namespace

hub::Bytes encode_player_state(const world::Entity& entity, View, const world::WorldClock&) {
    StateWriter state(kGroups);
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

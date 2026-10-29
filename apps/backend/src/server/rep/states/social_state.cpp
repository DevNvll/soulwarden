#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The Social state of a character (stage QG7; research social.md section A "Empty social window", protocol notes 4.63).
//
// MB::SocialReplicatedState (type 4176, constructor at image RVA 0x3ED7110, apply 0x3EF4450 then 0x3EF3E40), slot 55 of the player slice
// (the component JavSocial of the entity Social). Two groups:
//   group 0 field 0  the title           u32 (0: no title)
//   group 0 field 1  the pronoun         u8
//   group 0 field 2  the chatting state  u32
//   group 1 fields 0, 4, 5, 6            the war, friend, invite, and block maps: each an empty full snapshot `00 01 01 00` (no change
//                                        entries, the revision 1, the count 0)
// The client reads its game friend list from this state (JavSocialComponentBus.GetFriends; research social.md, "Friend list"). With no
// Social state the list of the component is not there: the faction poster of a conversation (changefaction/changefactionposter.luac,
// Show: GetFriends, RefreshStats: GetFriendCountInFaction) crashes the client (live check of stage QG7: an access at 0xD10 in the vector
// size getter 0x2F2D50 from a Lua method call). The backend has no friends, invites, or blocks: the empty state. Bytes (31 with the slot
// and the type): `37 90 41 03 07 00000000 00 00000000 71 00010100 00010100 00010100 00010100`.
//
// View     Fragments
// owner    the empty state one time, after the first message 2709 of the session (the player slice is ready; the rule of the other late
//          owner states, StateCodec::afterObjectivesEnable)
// observer no fragment (a later stage: the public title and pronoun of group 0)

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kSocialGroups = 2;
/** An empty full snapshot of a replicated map: no change entries, the revision stamp 1, no element. */
constexpr std::uint8_t kEmptyMap[] = {0x00, 0x01, 0x01, 0x00};

} // namespace

hub::Bytes encode_social_state(const world::Entity& entity, View view, const world::WorldClock&) {
    (void)entity;
    if (view != View::owner) {
        return {};
    }
    StateWriter state(kSocialGroups);
    state.field(0, 0).u32(0);
    state.field(0, 1).u8(0);
    state.field(0, 2).u32(0);
    for (const unsigned field : {0U, 4U, 5U, 6U}) {
        state.field(1, field).raw(kEmptyMap);
    }
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

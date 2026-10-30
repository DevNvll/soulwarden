#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The PlayerArena state of a character (stage QG9e, docs/systems/instances.md, the leave; static read LEAVE of stage QG9e, E4).
//
// PlayerArena (type 2406), slot 4 of the player slice. This encoder sends one field of group 0:
//   field 0  isInArena  a bool. ForfeitArena (0x2780110) sends its request 926 only while it is true. The dungeon leave of the escape menu
//                       (Popup_ExitDungeon, Yes: ForfeitArena(root, true)) is the user of this stage.
// The other fields of group 0 (14 to 16 and the others) stay at their defaults: no fragment.
//
// View     Fragments
// owner    after the first membership of the world entry: true while the membership is in a mode with IsDungeon, else false (`01 01 01`
//          and `01 01 00`; live: the fragment `01 01 01` of the development command, then the leave of the menu sends 926)
// observer no fragment [agent: the escape menu is of the local player]

namespace soulwarden::backend::server::rep {

hub::Bytes encode_player_arena_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)clock;
    if (view != View::owner || entity.kind != world::EntityKind::player || !entity.gameModeKnown) {
        return {};
    }
    const std::optional<world::GameModeMembership>& membership = entity.gameModeMembership;
    StateWriter state(1);
    state.field(0, 0).boolean(membership.has_value() && membership->dungeon);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

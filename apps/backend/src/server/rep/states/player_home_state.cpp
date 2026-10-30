#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The PlayerHome state of a character (stage DG3, docs/systems/instances.md "The checkpoints of an expedition"; research death-travel.md 4.1
// to 4.3).
//
// MB::PlayerHomeComponentReplicatedState (type 3652, constructor 0x34E2E40), slot 20 of the player slice. Group 0 field 0 homePointList is a
// replicated list (change reader 0x34FCFD0, full reader 0x34FD550); this encoder sends the full form only:
//   changes:size = 0, version: has:u8 = 0, count:size, then each element (element reader 0x34FCE70; writer 0x34F13D0, field for field):
//     actor:16raw  gde:u64  facet:u64  name:text  x:f32 y:f32 z:f32  cooldownDuration:u64 (ns)  cooldownEnd:u64 (synced wall clock, ns)
//     type:u32  overloaded:u8  failed:u8  id:text  unique:u32
// The client copies the list to the component (0x34F3E10) and writes the UI values Hud.LocalPlayer.HomePoints (0x34FD5F0); the respawn
// screen of a mode with DungeonRespawnsPermitted (0x34ECB00) shows only the points of the type 10 (Dungeon), and the button of a point
// sends 1938 with random 0 and the id of the point. Field 1 homePointId (the cooldown display of a used point) is not sent: the element
// has the end of its cooldown.
//
// View     Fragments
// owner    the active checkpoints of the instance of the character; after its leave the empty list (`00 00 00`); nothing before the first
//          list of the world entry (a character of the main world gets no fragment: its respawn screen keeps the random row)
// observer no fragment (the UI values are of the local player only: 0x34F0AB0)

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kPlayerHomeGroups = 1;

} // namespace

hub::Bytes encode_player_home_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)clock;
    if (view != View::owner || entity.kind != world::EntityKind::player || !entity.homePointsKnown) {
        return {};
    }
    StateWriter state(kPlayerHomeGroups);
    hub::Writer& list = state.field(0, 0);
    list.size(0);
    list.u8(0);
    list.size(static_cast<std::uint32_t>(entity.homePoints.size()));
    for (const world::HomePoint& point : entity.homePoints) {
        list.raw(point.actor);
        list.u64(point.group);
        list.u64(point.facet);
        list.string(point.name);
        list.f32(point.position.x);
        list.f32(point.position.y);
        list.f32(point.position.z);
        list.u64(point.cooldownNs);
        list.u64(point.cooldownEndNs);
        list.u32(point.type);
        list.u8(0);
        list.u8(0);
        list.string(point.id);
        list.u32(point.unique);
    }
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

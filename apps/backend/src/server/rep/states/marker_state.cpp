#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::MarkerComponentReplicatedState (constructor at image RVA 0x49471B0, apply function
// 0x4947630): the marker component of the entity "Marker" of a creature slice. Its slot is a
// value of the slice (game data). The marker is the name plate of the creature: the client
// makes its name, its level, and its health bar from the vitals state of the creature.
//
// View     Fields
// observer group 0: enabled (1 byte). A creature has no owner view.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kEnabled = 0;

} // namespace

hub::Bytes encode_marker_state(const world::Entity&, View, const world::WorldClock&) {
    StateWriter state(kGroups);
    state.field(0, kEnabled).boolean(true);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The node state of a gathering node and of the body of a creature (stage 3.6a; protocol notes 4.44, docs/systems/gathering.md).
//
// MB::GatherableControllerReplicatedState (type 12, constructor at image RVA 0x5B9EA90), the slot of the GatherableController component
// of the slice (game data: 3 for the master slices of the resources and for most creatures). One group:
//   field 0  gatherableState   u8 (readers 0x17B3C60, 0x87A190): Healthy 4, Depleted 8 (enum 0x3378D60)
//   field 1  replenishTime     u64 big-endian: not sent (live check L5 of stage 3.6a: the client restores no node at that time, in the
//                              server clock of 335 and in Unix milliseconds; the server sends Healthy at the respawn)
// The client applies the state (0x3371590) to the node that it made from its level data (live check L2: the managed actor of the
// natural node: its offline id; of a capital placement: the UUID rule). Depleted hides the node and its marker, Healthy shows it again.
//
// View     Fragments
// observer one fragment: field 0 (each observer of the node; the node state is shared, research Q1)

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;

} // namespace

hub::Bytes encode_gatherable_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    std::uint8_t node = 0;
    if (entity.kind == world::EntityKind::object && entity.object.nodeSlot != 0) {
        node = entity.object.nodeState;
    } else if (entity.kind == world::EntityKind::creature && entity.creature.skinSlot != 0) {
        node = entity.creature.skinState;
    }
    if (node == 0) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, 0).u8(node);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

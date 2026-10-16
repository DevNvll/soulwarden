#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The two states of the interaction base (stage 2.5a, world/interactions.h, protocol notes 4.35).
//
// MB::InteractorComponentReplicatedState (type 3752, constructor at image RVA 0x49193B0), slot 26 of the player slice and of
// the remote player slice. Two groups:
//   group 0 field 0  m_cachedCommittedInteractGDEID   u64 big-endian (reader 0x279E300): the G of the object of the committed
//                                                     interaction; 0x00000000FFFFFFFF: none (the apply 0x49199D0 ends the old
//                                                     object only on this value: research K8)
//   group 1 field 0  m_enabled                        bool: the backend sends no value (the client starts with 1)
// Live check L1 of stage 2.5a: `01 01 <G>` gives the Interactor facet +0x408 the G of the lore page, `01 01 00000000FFFFFFFF`
// the empty value.
//
// View     Fragments
// owner    group 0, when it changed: the G at a begin, the empty value at an end; a session that got a begin before the last
//          end gets the empty value first, then the new G (two fragments)
// observer the same
//
// MB::InteractReplicatedState (type 2930, constructor at image RVA 0x35E2C70), the slot of each Interact component of the slice
// of an object (game data). One group:
//   field 0  Enabled          bool (reader 0x17B3CB0): false hides the prompt of the client (live check L2)
//   field 1  HasInteractors   u32 big-endian (reader 0x17B3620): the number of users of the bound
//   field 2  CooldownUpdates  the backend sends no value (no option of stage 2.5a has a cooldown)
//
// View     Fragments
// observer one fragment for each bound of the object, with fields 0 and 1

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kInteractorGroups = 2;
constexpr unsigned kInteractGroups = 1;
/** The empty value of the cached object: not 0 and not eight bytes of 0xFF (research K8, client 0x49199D0). */
constexpr std::uint64_t kNoObject = 0x00000000FFFFFFFFULL;

hub::Bytes interactor_state(std::uint64_t group) {
    StateWriter state(kInteractorGroups);
    state.field(0, 0).u64(group);
    return state.finish();
}

} // namespace

std::vector<hub::Bytes> encode_interactor_parts(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    std::vector<hub::Bytes> parts;
    const bool endedSince = entity.interactionEnded != 0 && entity.interactionEnded > request.since;
    if (entity.interaction) {
        // A begin after an end that this session did not get: the empty value first, or the client keeps the old object.
        if (endedSince && request.since != 0) {
            parts.push_back(interactor_state(kNoObject));
        }
        parts.push_back(interactor_state(entity.interaction->reference.group));
    } else if (endedSince) {
        parts.push_back(interactor_state(kNoObject));
    }
    return parts;
}

std::vector<SlotState> encode_interact_states(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    (void)request;
    std::vector<SlotState> parts;
    for (const world::ObjectBound& bound : entity.object.bounds) {
        StateWriter state(kInteractGroups);
        state.field(0, 0).boolean(bound.enabled);
        state.field(0, 1).u32(bound.users);
        parts.push_back(SlotState{bound.slot, state.finish()});
    }
    return parts;
}

} // namespace soulwarden::backend::server::rep

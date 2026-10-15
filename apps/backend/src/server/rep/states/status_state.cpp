#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::StatusEffectsComponentReplicatedState (constructor at image RVA 0x4013900, apply at
// 0x4028F70), the component StatusEffects of a creature slice (slot of the slice: 8 for the Grey
// Wolf), stage 3.3a, protocol notes 4.22. Group 0 is empty; group 1 has the maps of the owner,
// group 2 shared values, and group 3 the maps of an observer: field 0 the full map (statusIndex:u16
// wallEnd:u64 potency:f16 stacks:u8 flags:u8) and field 1 the compact map. The client of an
// observer shows the icons at the name plate of a creature from the compact map only (live
// check L5: the full map alone shows no icon). A compact entry (reader 0x4044030, value reader
// 0x5CE77B0):
//   statusIndex:u16 packed:u8   bits 0 to 6: the stacks; bit 7: the potency 1
// The map is a list in the full form: 00, stamp := 01 sequence:size64, count:size, entries. A
// list with a larger stamp replaces the list of the client: an empty list removes each icon.
// The entry has no end time: the server sends the list again at the end of an effect.
//
// View     Fields
// observer group 3, field 1: one entry for each effect of the creature that shows at the name
//          plate (one stack, the potency 1). No fragment in the first record of a creature
//          with no such effect.
// owner    a creature has no owner.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 4;
constexpr unsigned kObserverGroup = 3;
constexpr unsigned kCompactMap = 1;
/** One stack and the potency 1. */
constexpr std::uint8_t kPackedOneStack = 0x81;

} // namespace

std::vector<hub::Bytes> encode_creature_status_state(const world::Entity& entity, View, const PartsRequest& request) {
    std::vector<const world::CreatureEffect*> shown;
    for (const world::CreatureEffect& effect : entity.creature.effects) {
        if (effect.shown) {
            shown.push_back(&effect);
        }
    }
    if (request.since == 0 && shown.empty()) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::status);
    const std::uint64_t sequence = changed == entity.changed.end() ? 1 : changed->second;
    StateWriter state(kGroups);
    hub::Writer& map = state.field(kObserverGroup, kCompactMap);
    map.size(0);
    map.u8(1);
    map.size64(sequence);
    map.size(static_cast<std::uint32_t>(shown.size()));
    for (const world::CreatureEffect* effect : shown) {
        map.u16(effect->index);
        map.u8(kPackedOneStack);
    }
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

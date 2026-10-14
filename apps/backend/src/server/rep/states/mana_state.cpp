#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::ManaComponentReplicatedState (constructor at image RVA 0x67088C0, apply at 0x683B740),
// slot 47 of the player slice (component Mana). One group with four fields, each a 32-bit
// float, big-endian (client reader at image RVA 0x2A42F30):
//   0 current   1 maximum   2 regeneration delay   3 regeneration rate
// The apply function copies the four values each time. The mana meter of the HUD (the blue
// bar below the crosshair, lyshineui/hud2024/vitals/manameter) reads `ManaPercent` from this
// component, not from the mana fields of the vitals state: with no mana state the meter keeps
// its start value and shows at full opacity. It goes away at 100 percent (opacity 0 at full).
// Live proof (user test 1): 50 of 100 shows half of the bar, 100 of 100 hides it.
//
// View     Fields
// owner    group 0: the four fields. The values are the mana of the vitals state of the
//          entity (Entity::vitals): the two states have the same values. The rates are 0:
//          the client does not change the value itself.
// observer no fragment: the remote player slice has no mana component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kCurrent = 0;
constexpr unsigned kMaximum = 1;
constexpr unsigned kRegenerationDelay = 2;
constexpr unsigned kRegenerationRate = 3;

} // namespace

hub::Bytes encode_mana_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, kCurrent).f32(entity.vitals.mana);
    state.field(0, kMaximum).f32(entity.vitals.manaMax);
    state.field(0, kRegenerationDelay).f32(0.0F);
    state.field(0, kRegenerationRate).f32(0.0F);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

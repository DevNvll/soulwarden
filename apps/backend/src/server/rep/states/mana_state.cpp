#include <algorithm>
#include <chrono>

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
//          entity (Entity::vitals): the two states have the same values. While the mana is not
//          full (stage 1.3b, I14), the delay that is left before the regeneration and the rate
//          of the server (ManaBaseRegenRate 2 times the factor). Full mana: the rates are 0. The
//          apply (0x683B740) writes only the server copy of the client and the meter one time:
//          the prediction of the client (0x676B8E0) uses a different copy, which only the message
//          4289 sets (stage 2.2, server/rep/mana_messages.h; the session sends it with each
//          later mana state).
// observer no fragment: the remote player slice has no mana component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kCurrent = 0;
constexpr unsigned kMaximum = 1;
constexpr unsigned kRegenerationDelay = 2;
constexpr unsigned kRegenerationRate = 3;

} // namespace

hub::Bytes encode_mana_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    if (view != View::owner) {
        return {};
    }
    float delay = 0.0F;
    float rate = 0.0F;
    if (entity.vitals.manaRegenFrom && entity.vitals.mana < entity.vitals.manaMax) {
        delay = std::max(std::chrono::duration<float>(*entity.vitals.manaRegenFrom - clock.now()).count(), 0.0F);
        rate = entity.vitals.manaRegenRate;
    }
    StateWriter state(kGroups);
    state.field(0, kCurrent).f32(entity.vitals.mana);
    // The base maximum (ManaMax of the base vitals, stage 3.5b.2): the client multiplies the maximum of its first mana state with its own
    // MaxMana parts (stat 0x31; live check of the final run of stage 3.5b.2: a first state with 110 gave the client 121, and the meter
    // stayed at 91 percent at full mana). The current value is the value with the parts (110 of the client maximum 110).
    state.field(0, kMaximum).f32(entity.vitals.manaBase);
    state.field(0, kRegenerationDelay).f32(delay);
    state.field(0, kRegenerationRate).f32(rate);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

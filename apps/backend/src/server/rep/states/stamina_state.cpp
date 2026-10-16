#include <algorithm>
#include <chrono>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "world/stamina.h"

// MB::StaminaComponentReplicatedState (type 4297, constructor at image RVA 0x671A230, apply at 0x683E5D0), slot 42 of the player slice
// (component Stamina). One group with six fields, each a 32-bit float, big-endian (client reader at image RVA 0x2A42F30):
//   0 cur   1 max   2 winded   3 regen   4 multMax   5 multRegen
// The apply function stores the six values in the server copy of the component (+0xD0 to +0xE4) and multMax also in the multiplier of
// the maximum (+0xC0). It does not set the value that the client predicts (+0xB0): live checks LS2 and LS3 of stage 3.4f-B (a state with
// 20 of 100, and with 0: the HUD stays full and the client dodges). The client of the owner predicts its stamina with the rules of the
// data (world/stamina.h); the server sends its own values for the record (the same rules, thus the same values in a normal game).
//
// View     Fields
// owner    group 0: the six fields (stage 3.4f-B): the stamina of the character (Entity::vitals), its maximum, the winded time that is
//          left (live check LS6: field 2 of the component counts down from m_maxWindedTime at 0), the delay of the regeneration that is
//          left (live check LS5: field 3 counts down from 1.0 after a payment), the multiplier of the maximum (1: no source), and the
//          multiplier of the regeneration (1 + the StaminaRate of the statuses).
// observer no fragment (the remote player slice has the component; its observer view is not proved, and a remote player shows no
//          stamina).
// The first record of a character with its full stamina and the multipliers 1 has no fragment (the client starts full itself).

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kCurrent = 0;
constexpr unsigned kMaximum = 1;
constexpr unsigned kWinded = 2;
constexpr unsigned kRegenDelay = 3;
constexpr unsigned kMaximumMultiplier = 4;
constexpr unsigned kRegenMultiplier = 5;

float left(world::WorldClock::time_point end, world::WorldClock::time_point now) {
    return std::max(std::chrono::duration<float>(end - now).count(), 0.0F);
}

} // namespace

std::vector<hub::Bytes> encode_stamina_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner || entity.kind != world::EntityKind::player) {
        return {};
    }
    const float regenMultiplier = world::Stamina::regeneration_multiplier(entity);
    const bool full = entity.vitals.stamina >= entity.vitals.staminaMax && regenMultiplier == 1.0F;
    if (full && request.since == 0) {
        return {};
    }
    const world::WorldClock::time_point now = request.clock != nullptr ? request.clock->now() : world::WorldClock::time_point{};
    float delay = 0.0F;
    if (entity.vitals.stamina < entity.vitals.staminaMax && request.clock != nullptr) {
        // The delay that is left: the later of the delay of a payment and the hold of a dodge (to its mark CanTransition).
        const world::WorldClock::time_point from =
            std::max(entity.vitals.staminaRegenFrom.value_or(now), entity.vitals.staminaHeldUntil);
        delay = left(from, now);
    }
    StateWriter state(kGroups);
    state.field(0, kCurrent).f32(entity.vitals.stamina);
    state.field(0, kMaximum).f32(entity.vitals.staminaMax);
    state.field(0, kWinded).f32(request.clock != nullptr ? left(entity.vitals.windedUntil, now) : 0.0F);
    state.field(0, kRegenDelay).f32(delay);
    state.field(0, kMaximumMultiplier).f32(1.0F);
    state.field(0, kRegenMultiplier).f32(regenMultiplier);
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

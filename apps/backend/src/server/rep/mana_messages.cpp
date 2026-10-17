#include "server/rep/mana_messages.h"

#include <algorithm>
#include <chrono>

namespace soulwarden::backend::server::rep {

hub::Bytes encode_mana_sync(std::uint64_t target, const world::Entity& entity, const world::WorldClock& clock) {
    // The same values as the mana state (states/mana_state.cpp), with the factor of the regeneration in place of the rate.
    float delay = 0.0F;
    if (entity.vitals.manaRegenFrom && entity.vitals.mana < entity.vitals.manaMax) {
        delay = std::max(std::chrono::duration<float>(*entity.vitals.manaRegenFrom - clock.now()).count(), 0.0F);
    }
    hub::Writer body;
    body.u64(target);
    body.u64(entity.id);
    body.f32(entity.vitals.mana);
    body.f32(entity.vitals.manaBase);
    body.f32(delay);
    body.f32(entity.vitals.manaRateFactor);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

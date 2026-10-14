#include "server/rep/lock_messages.h"

namespace soulwarden::backend::server::rep {

hub::Bytes cutscene_finalized_body(world::EntityId character) {
    hub::Writer body;
    body.u64(character + kPlayerCutsceneComponent);
    body.u64(character);
    return body.data();
}

hub::Bytes cancel_interaction_body(world::EntityId character) {
    hub::Writer body;
    body.u64(character + kInteractorComponent);
    body.u64(character);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

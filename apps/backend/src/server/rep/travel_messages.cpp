#include "server/rep/travel_messages.h"

namespace soulwarden::backend::server::rep {

hub::Bytes teleport_screen_body(world::EntityId character) {
    hub::Writer body;
    body.u64(character + kPlayerComponent);
    body.u64(character);
    body.u8(kTravelLoadingScreen);
    return body.data();
}

hub::Bytes player_teleport_body(world::EntityId character) {
    hub::Writer body;
    body.u64(character + kPlayerComponent);
    body.u64(character);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

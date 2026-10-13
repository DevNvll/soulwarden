#include "world/ids.h"

#include <stdexcept>

namespace soulwarden::backend::world {
namespace {

/** Bytes 4 to 7 and 12 to 15 of each actor id. */
constexpr std::array<std::uint8_t, 4> kTag{'S', 'O', 'U', 'L'};

} // namespace

ActorId make_actor_id(std::uint32_t serial, ActorKind kind) {
    const std::uint32_t low = serial * 2;
    ActorId actor;
    for (unsigned index = 0; index < 4; ++index) {
        actor.bytes[index] = static_cast<std::uint8_t>(low >> (8 * index));
        actor.bytes[4 + index] = kTag[index];
        actor.bytes[12 + index] = kTag[index];
    }
    actor.bytes[8] = static_cast<std::uint8_t>(kind);
    return actor;
}

EntityId entity_group_id(const ActorId& actor) {
    EntityId id = 0;
    for (unsigned index = 0; index < 8; ++index) {
        id |= static_cast<EntityId>(actor.bytes[index]) << (8 * index);
    }
    return id;
}

std::string actor_text(const ActorId& actor) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : actor.bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

ActorId IdAllocator::next(ActorKind kind) {
    std::uint32_t* counter = nullptr;
    std::uint32_t last = 0;
    switch (kind) {
    case ActorKind::playerCharacter:
        counter = &m_nextPlayer;
        last = kLastPlayerSerial;
        break;
    case ActorKind::session:
        counter = &m_nextSession;
        last = kLastSessionSerial;
        break;
    case ActorKind::otherEntity:
        counter = &m_nextOther;
        last = kLastOtherSerial;
        break;
    case ActorKind::serverActor:
        break;
    }
    if (counter == nullptr || *counter > last) {
        throw std::runtime_error("world: no free actor id of this kind");
    }
    return make_actor_id((*counter)++, kind);
}

} // namespace soulwarden::backend::world

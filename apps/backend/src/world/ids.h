#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <string>

// Ids of the actors of the world: the player manager, each session, and each entity.
//
// The client rules (protocol notes 4.2):
//   - An actor id is 16 bytes. The first 8 bytes, read as one little-endian number, are the
//     entity group id of an entity ("GDEID"). It is the key of the entity group maps of the
//     client and the base of each entity id and facet target of the slice instance.
//   - An id with an odd first byte, or with zero in bytes 4 to 7, is an offline ("managed") id:
//     the client makes no entity group for it.
//   - Two entities must not have equal first 8 bytes. Two player entities whose ids differ
//     only in byte 4 can get equal entity ids. Ids that differ only in bytes 0 to 3 are safe.
//
// The form of an id is a design of this project:
//   byte 0..3    serial * 2, little-endian        (thus the first byte is even)
//   byte 4..7    "SOUL"                           (equal in all ids, not zero)
//   byte 8       kind
//   byte 9..11   zero
//   byte 12..15  "SOUL"
// Bytes 8 to 15 are for the reader of a capture. The client does not use them.

namespace soulwarden::backend::world {

/** Entity group id: the first 8 bytes of the actor id as one little-endian number. */
using EntityId = std::uint64_t;
/** Id of a session of the game server: 1, 2, ... in one backend process. 0: no session. */
using SessionId = std::uint32_t;
/** A value of the revision counter of the world. 0: never. */
using Revision = std::uint64_t;
/**
 * Stage DG1 (docs/systems/instances.md): the space of an entity. The main world is space 0; each instance has a space of its own from the
 * allocator of the instance system (1, 2, ...; a number is not given two times in one backend process).
 */
using SpaceId = std::uint32_t;
inline constexpr SpaceId kMainSpace = 0;

struct ActorId {
    std::array<std::uint8_t, 16> bytes{};

    auto operator<=>(const ActorId&) const = default;
};

enum class ActorKind : std::uint8_t {
    /** An actor of the server that is not an entity: the player manager. */
    serverActor = 0x01,
    /** The client actor of one session. */
    session = 0x02,
    playerCharacter = 0x10,
    otherEntity = 0x20,
};

/** Serial ranges of the kinds. A serial is 1 to 0x7FFFFFFF: the id has serial * 2 in 32 bits. */
inline constexpr std::uint32_t kFirstPlayerSerial = 0x00000001;
inline constexpr std::uint32_t kLastPlayerSerial = 0x0FFFFFFF;
inline constexpr std::uint32_t kPlayerManagerSerial = 0x10000000;
inline constexpr std::uint32_t kFirstSessionSerial = 0x10000001;
inline constexpr std::uint32_t kLastSessionSerial = 0x1FFFFFFF;
inline constexpr std::uint32_t kFirstOtherSerial = 0x20000000;
inline constexpr std::uint32_t kLastOtherSerial = 0x7FFFFFFF;

[[nodiscard]] ActorId make_actor_id(std::uint32_t serial, ActorKind kind);

[[nodiscard]] EntityId entity_group_id(const ActorId& actor);

/** @return The 32 hexadecimal digits of an actor id, in the sequence of its bytes. */
[[nodiscard]] std::string actor_text(const ActorId& actor);

/**
 * Gives each actor of the world its id. An id is not used again in one backend process.
 * World thread only.
 */
class IdAllocator {
public:
    /** @return The actor id of the player manager: one value for the world. */
    [[nodiscard]] ActorId player_manager() const {
        return make_actor_id(kPlayerManagerSerial, ActorKind::serverActor);
    }

    /** @throws std::runtime_error when the range of the kind has no free serial. */
    [[nodiscard]] ActorId next(ActorKind kind);

private:
    std::uint32_t m_nextPlayer{kFirstPlayerSerial};
    std::uint32_t m_nextSession{kFirstSessionSerial};
    std::uint32_t m_nextOther{kFirstOtherSerial};
};

} // namespace soulwarden::backend::world

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "server/hub/wire.h"
#include "server/rep/item_requests.h"
#include "world/health.h"

// The messages for the health and the death of a character (protocol notes 4.18).
//
// The damage mark. The client of a character shows a red mark with the direction of the
// damage when it gets the message "on damage" for the vitals component of its own character:
//
//   VitalsComponentClientFacet_OnDamage (type 3601, client reader at image RVA 0x658C360,
//   handler 0x686C6A0):
//     target:u64 entity:u64                 the vitals facet of the character
//     source:u64                            the entity that did the damage
//     flags:2 bytes                         low byte first: bit 1 "the damage has a place"
//     part:f32                              the damage as a part of the health (live: the
//                                           mark shows for 0.05, 1, and 200)
//     x:f32 y:f32 z:f32                     the place of the source: the direction of the mark
//     count:size, then for each entry:
//       damage type:u8 amount:f32 part:f32  `part` is the part of the damage before the
//                                           defense that the character took: 1
//   The health itself is in the vitals state: this message changes no value.
//
// The death notice. The client shows its death screen when the vitals state of its character
// has the dead bit and it got this message for the player component of its character:
//
//   PlayerComponentClientFacet_OnPlayerDeath (type 3950, client handler at image RVA 0x68879F0):
//     target:u64 entity:u64                 the player facet of the character
//     time:f32                              seconds to the respawn screen (the text "you died"
//                                           with a count shows for this time)
//
// The respawn request of the client (the button of the respawn screen):
//
//   PlayerComponentServerFacet_RequestRespawn (type 1938, client sender at image RVA 0x69128E0):
//     target:u64 entity:u64                 the player facet of the character
//     random:u8                             1: the row "random location" (the client has no
//                                           list of respawn points)
//     id:text                               the id of a respawn point, or empty
//   The client shows its loading screen before it sends the request: the answer is a movement
//   state with a new teleport number and the message "on player teleport" (travel_messages.h).
//
// Only the session of the owner gets the two messages.
//
// The notice of a healer (stage 3.3b-S6, live check L3). The client of a healer shows a green number of the gain at the healed
// character when it gets the message "on healed player" for the vitals component of its own character:
//
//   VitalsComponentClientFacet_OnHealedPlayer (type 4298, client reader at image RVA 0x65A5A00, handler 0x687EAA0):
//     target:u64 entity:u64                 the vitals facet of the healer
//     healedTargetId:u64                    the id of the entity Stats of the healed character (an event of the client to that
//                                           entity; live check L3: the group id and the vitals facet of the healed character show
//                                           nothing)
//     amount:f32                            the gain (the client shows its whole part)
//   The health of the healed character is in its vitals state: this message changes no value. Only the session of the healer gets
//   it; the healed character gets its own number from bit 1 of healthChangeFlags of its vitals state.

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeDamageTaken = 3601;    // VitalsComponentClientFacet_OnDamage
inline constexpr std::uint32_t kTypePlayerDeath = 3950;    // PlayerComponentClientFacet_OnPlayerDeath
inline constexpr std::uint32_t kTypeRequestRespawn = 1938; // PlayerComponentServerFacet_RequestRespawn
inline constexpr std::uint32_t kTypeHealedPlayer = 4298;   // VitalsComponentClientFacet_OnHealedPlayer

/**
 * Ids of slices/player.dynamicslice of the supported client build: the root entity of the
 * slice, the entity "Stats", and its component Vitals. The facet target of the component for a
 * character is (entity XOR root XOR entity group id) plus the component id (protocol notes 4.2).
 */
inline constexpr std::uint64_t kPlayerSliceRoot = 0xDAEE4E2555650AB7ULL;
inline constexpr std::uint64_t kPlayerStatsEntity = 0xE6448D4D53852704ULL;
inline constexpr std::uint64_t kPlayerVitalsComponent = 0xEEEFFB82ABAFE4B1ULL;

/** Flag of the message "on damage": the damage has a place, and the client shows its direction (bit 1). */
inline constexpr std::uint16_t kDamageHasPlace = 1U << 1;

/** @return The id of the entity Stats of a character (the entity of its vitals component; protocol notes 4.2). */
[[nodiscard]] constexpr std::uint64_t player_stats_entity(world::EntityId character) {
    return kPlayerStatsEntity ^ kPlayerSliceRoot ^ character;
}

/** @return The facet target of the vitals component of a character. */
[[nodiscard]] constexpr std::uint64_t player_vitals_target(world::EntityId character) {
    return player_stats_entity(character) + kPlayerVitalsComponent;
}

/** @return The body of the message "on healed player" for the client of a healer (stage 3.3b-S6). */
[[nodiscard]] hub::Bytes healed_player_body(world::EntityId healer, world::EntityId healed, float amount);

/** @return The body of the message "on damage" for the client of a character that took damage. */
[[nodiscard]] hub::Bytes damage_taken_body(world::EntityId character, const world::DamageTaken& damage);

/** @return The body of the message "on player death" for the client of a character. */
[[nodiscard]] hub::Bytes player_death_body(world::EntityId character, float respawnSeconds);

/** The respawn request of a client. */
struct RespawnRequest {
    ComponentCall call;
    bool random{false};
    std::string id;
};

/** @return The fields of a respawn request, or no value for a body that is too short. */
[[nodiscard]] std::optional<RespawnRequest> read_respawn_request(std::span<const std::uint8_t> body);

} // namespace soulwarden::backend::server::rep

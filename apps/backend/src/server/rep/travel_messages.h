#pragma once

#include <cstdint>

#include "server/hub/wire.h"
#include "world/ids.h"

// The messages of the server for a travel of a character (protocol notes 4.16).
//
// The world moves a character with its movement state: a new position and a new teleport
// number (server/rep/states/movement_state.cpp). The client takes that position at once, with
// no picture change of its own. Two messages for the player component of the own character
// are for the picture of a travel to a place that is far, where the client must load the world:
//
//   PlayerComponentClientFacet_OnTeleportWithLoadingScreen (type 3951, client handler at image
//   RVA 0x6895A90), before the movement state:
//     target:u64 entity:u64                       the player facet of the character
//     kind:u8                                     1 (the kind of the loading screen)
//   The client shows a loading screen and sets a mark "teleport pending" in the player facet.
//   The loading screen stays while the mark is set.
//
//   PlayerComponentClientFacet_OnPlayerTeleport (type 3954, client handler at image RVA
//   0x6888870), after the movement state:
//     target:u64 entity:u64
//   The client clears the mark and gives the position of its player to its world streaming.
//   The loading screen then ends when the world at the new place is there.
//
// Only the session of the owner gets them: the handler of 3951 sets the mark with no check.

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeTeleportWithLoadingScreen = 3951; // PlayerComponentClientFacet_OnTeleportWithLoadingScreen
inline constexpr std::uint32_t kTypePlayerTeleport = 3954;            // PlayerComponentClientFacet_OnPlayerTeleport

/**
 * Id of the component Player of the entity RootPlayer in slices/player.dynamicslice of the
 * supported client build. The facet target of the component for a character is this id plus
 * the entity group id of the character: the component is in the root entity of the slice
 * (protocol notes 4.2).
 */
inline constexpr std::uint64_t kPlayerComponent = 0xDF0CE8C98D27619DULL;

/** The kind of the loading screen of a travel (the value 1 of the client handler). */
inline constexpr std::uint8_t kTravelLoadingScreen = 1;

/** @return The body of the message "on teleport with loading screen" for the client of a character. */
[[nodiscard]] hub::Bytes teleport_screen_body(world::EntityId character);

/** @return The body of the message "on player teleport" for the client of a character. */
[[nodiscard]] hub::Bytes player_teleport_body(world::EntityId character);

} // namespace soulwarden::backend::server::rep

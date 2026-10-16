#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "server/hub/wire.h"
#include "world/entity.h"
#include "world/ids.h"

// The messages that end a lock of the input of a client (protocol notes 4.20).
//
// A conversation with a character of the game (the key E at a person of a settlement: the
// option "Talk", action 63 `OpenConversation`, a committed interaction) is two things in the
// client, and the client starts both itself, with no message to the server:
//
// - The interaction of the client (protocol notes 4.20, `interaction.md` of the research).
// - A player cutscene: the conversation camera. Its start (client code at image RVA
//   0x2EF13F0) sets the mark at PlayerCutscene facet +0xC24, calls method +0x420 of an
//   interface of the player entity with 1 (the input of the character is blocked), and
//   turns the action map "default" off.
//
// At the end of the conversation (the last "Next", or Escape) the client ends the cutscene
// (image RVA 0x2F08890). For a cutscene whose end the server must confirm (facet +0xC22) it
// does not free the input: it sends
//
//   PlayerCutsceneComponentServerFacet_OnClientCutsceneEnded (type 6705, client sender at image
//   RVA 0x2EDA370):
//     target:u64 entity:u64                 the PlayerCutscene facet of the character
//
// and waits for the answer of the server:
//
//   PlayerCutsceneComponentClientFacet_OnServerCutsceneEndRequestFinalized (type 6709, client
//   handler at image RVA 0x2EFE730):
//     target:u64 entity:u64
//
// The handler clears the mark, calls method +0x420 with 0, and turns the action map "default"
// on again. With no answer the character never moves again, also after a travel (live proof,
// user test 1). With no cutscene the handler changes nothing that plays: the mark is 0 and
// the map is on.
//
// The client also ends its interaction:
//
//   InteractorComponentServerFacet_RequestCancelCommittedInteractionFromClient (type 2473,
//   client sender at image RVA 0x490AD30):
//     target:u64 entity:u64                 the Interactor facet of the character
//     reference := actor:16 bytes G:u64 target:u64   the object of the interaction (live check L1 of stage 2.5a: 48 bytes)
//
// Stage 2.5a: the interaction system ends the committed interaction of the character when the reference is its reference
// (world/interactions.h); a body that is not 48 bytes, or a different reference, changes nothing.
//
// The reply of the server to a request of the client (the interaction block of its input report; stage 2.5a):
//
//   InteractorComponentClientFacet_ReceiveInteractionResult (type 3743, client writer 0x35E6C80, reader 0x35EC230, handler
//   0x49175F0):
//     target:u64 entity:u64                 the Interactor facet of the character
//     reference:32 bytes                    the reference of the block (the handler compares all 32 bytes with its record)
//     action:u8 result:u8                   the action of the block; 1 accepted, 2 refused
//     text:size (0)  u32 (0)  16 bytes (0)  (research K2: a body of 71 bytes)
//
// Result 1 keeps a committed record; for a one-shot it restores the committed record or clears the request. Result 2 clears
// both records (live check L1: a conversation closes; the journal of a lore page stays open, a local window).
//
// The server ends an interaction of the client:
//
//   InteractorComponentClientFacet_CancelCurrentInteraction (type 3746, client handler at image
//   RVA 0x490EDA0):
//     target:u64 entity:u64
//
// In a committed interaction the client then cancels it: a conversation closes, and the
// message 6705 of its cutscene comes after it. With no committed interaction the client
// changes nothing.
//
// A travel and a respawn end each lock of this kind: the session of the owner sends 3746 and
// 6709 before the messages of the travel. Only the session of the owner gets these messages.

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeCutsceneEnded = 6705;            // PlayerCutsceneComponentServerFacet_OnClientCutsceneEnded
inline constexpr std::uint32_t kTypeCutsceneEndFinalized = 6709;     // PlayerCutsceneComponentClientFacet_OnServerCutsceneEndRequestFinalized
inline constexpr std::uint32_t kTypeCancelCommittedInteraction = 2473; // InteractorComponentServerFacet_RequestCancelCommittedInteractionFromClient
inline constexpr std::uint32_t kTypeCancelCurrentInteraction = 3746; // InteractorComponentClientFacet_CancelCurrentInteraction
inline constexpr std::uint32_t kTypeInteractionResult = 3743;       // InteractorComponentClientFacet_ReceiveInteractionResult

/**
 * Ids of the components PlayerCutscene and Interactor of the entity RootPlayer in
 * slices/player.dynamicslice of the supported client build. The facet target of each one for a
 * character is the id plus the entity group id of the character: the two components are in
 * the root entity of the slice (protocol notes 4.2).
 */
inline constexpr std::uint64_t kPlayerCutsceneComponent = 0xD49931510E1DD23FULL;
inline constexpr std::uint64_t kInteractorComponent = 0x6C5718DBFE6DF612ULL;

/** @return The body of the message "cutscene end request finalized" for the client of a character. */
[[nodiscard]] hub::Bytes cutscene_finalized_body(world::EntityId character);

/** @return The body of the message "cancel current interaction" for the client of a character. */
[[nodiscard]] hub::Bytes cancel_interaction_body(world::EntityId character);

/** The body of the message 2473 of a client: its component call and the reference of its interaction. */
struct InteractionCancel {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    world::InteractionReference reference;
};

/** @return The message 2473 of a body of exactly 48 bytes, or no value (a short or a long body is not the message). */
[[nodiscard]] std::optional<InteractionCancel> read_interaction_cancel(std::span<const std::uint8_t> body);

/** @return The body of the reply 3743 for the client of a character (71 bytes). */
[[nodiscard]] hub::Bytes interaction_result_body(world::EntityId character, const world::InteractionReference& reference,
                                                 std::uint8_t action, bool accepted);

} // namespace soulwarden::backend::server::rep

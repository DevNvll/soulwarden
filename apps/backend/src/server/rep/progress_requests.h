#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "server/hub/wire.h"
#include "server/rep/item_requests.h"
#include "world/progress.h"

// The requests of a client for its attributes (protocol notes 4.12) and for its weapon trees
// and ability bars (protocol notes 4.13). Each one is a call of a component of the player
// entity: its body starts with the facet target and the entity group id. The client changes
// its own values when it sends a request (but for a tree reset), and it has no reply message
// for a request that the server did or refused: the next state of the component is the answer.
// Only the tree reset has a reply message.

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeSpendAttributePoints = 1502;  // AttributeComponentServerFacet_RequestSpendAttributePoints
inline constexpr std::uint32_t kTypeRefundAttributePoints = 2409; // AttributeComponentServerFacet_RequestRefundSpentPoints
inline constexpr std::uint32_t kTypeAssignPoolPoints = 1112;      // ProgressionPointComponentServerFacet_RequestAssignPoolPoints
inline constexpr std::uint32_t kTypeApplyAbilityPoints = 2857;    // AbilityComponentServerFacet_RequestApplyAbilityPoints
inline constexpr std::uint32_t kTypeChangeMappedAbility = 2701;   // AbilityComponentServerFacet_RequestChangeMappedAbility
inline constexpr std::uint32_t kTypeMasteryTreeRespec = 186;      // AbilityComponentServerFacet_RequestMasteryTreeRespec
/** Stage QG1: the card choice of the territory standing. */
inline constexpr std::uint32_t kTypeSpendTerritoryStandingPoint = 4040; // ProgressionPointComponentServerFacet_RequestSpendTerritoryStandingPoint
/** Stage QG7: the join and the change of a faction. */
inline constexpr std::uint32_t kTypeRequestSetFaction = 3161; // FactionComponentServerFacet_RequestSetFaction
/** Stage QG7: the Faction component of the entity Social of the player slice (slice-ids.json; component id E9FFAD8E007C4913). */
inline constexpr const char* kFactionComponentName = "Social/2bbb6c0d-28a5-4c6c-9e02-1b28ebd8170c";
/** Server to client: the answer to a tree reset request. */
inline constexpr std::uint32_t kTypeMasteryTreeRespecResponse = 2858; // AbilityComponentClientFacet_MasteryTreeRespecResponse

/**
 * AttributeComponentServerFacet_RequestSpendAttributePoints (client writer at image RVA
 * 0x2747D80, reader 0x274F8D0):
 *   target:u64 entity:u64 types:size (type:u32)* points:size (points:u32)*
 * Each list has 5 elements at most, a type is 0 to 4, and a number of points is 0 to 700: the
 * points to add to the attribute, not its new value. The two lists have the same length.
 */
struct SpendRequest {
    ComponentCall call;
    std::vector<world::AttributeSpend> entries;
};

/**
 * ProgressionPointComponentServerFacet_RequestAssignPoolPoints (client writer at image RVA
 * 0x3CD7B30, reader 0x3CD9530):
 *   target:u64 entity:u64 poolId:u32 pointId:u32 count:u32
 * The count is 4,096 at most. The client sends 1 for a node of a weapon tree.
 */
struct AssignRequest {
    ComponentCall call;
    std::uint32_t pool{0};
    std::uint32_t node{0};
    std::uint32_t count{0};
};

/**
 * Stage QG1: ProgressionPointComponentServerFacet_RequestSpendTerritoryStandingPoint (the sender 0x3CD8E80 stores the point id at +0x20 and
 * the progression id at +0x24; the writer 0x3274450 writes +0x24 first; research territories.md section 5):
 *   target:u64 entity:u64 progressionId:u32 pointId:u32
 * progressionId is the CRC-32 of the territory id text ("6": 1DB87A14), not the pool; pointId the CRC-32 of the card (TB_GainStanding:
 * 68C1F9AA). The client has no reply message: the next points state (slot 67) is the answer.
 */
struct TerritoryPointRequest {
    ComponentCall call;
    std::uint32_t progressionId{0};
    std::uint32_t pointId{0};
};

/**
 * Stage QG7: FactionComponentServerFacet_RequestSetFaction (the sender 0x30A3310, the field writer 0x2745630, the field reader 0x3099BA0
 * with the enum validator 0x2E6A8A0: 0 to 4; research factions.md 2.2):
 *   target:u64 entity:u64 faction:u8
 * The target is the Faction component of the entity Social of the player slice. The join and each later change use it. The client has no
 * reply message: the next Faction state (slot 49) is the answer.
 */
struct SetFactionRequest {
    ComponentCall call;
    std::uint8_t faction{0};
};

/**
 * AbilityComponentServerFacet_RequestChangeMappedAbility (client writer at image RVA
 * 0x65A5580, reader 0x65A6270):
 *   target:u64 entity:u64 tableIndex:u16 slot:u32 abilityId:u32
 * The table index is the index of the ability table of the weapon in the list of the client
 * (gamedata::kAbilityTableOrder). The slot is 0 to 2.
 */
struct BarRequest {
    ComponentCall call;
    std::uint16_t tableIndex{0};
    std::uint32_t slot{0};
    std::uint32_t ability{0};
};

/**
 * AbilityComponentServerFacet_RequestMasteryTreeRespec (client writer at image RVA 0x65B2EE0,
 * reader 0x65B3740):
 *   target:u64 entity:u64 tableIndex:u16 flag:u8
 * The meaning of the flag is not known (the screen of the client sends 0).
 */
struct TreeResetRequest {
    ComponentCall call;
    std::uint16_t tableIndex{0};
    bool flag{false};
};

/** @return The request, or no value when the body does not have the form of the request. */
[[nodiscard]] std::optional<SpendRequest> read_spend_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<AssignRequest> read_assign_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<TerritoryPointRequest> read_territory_point_request(std::span<const std::uint8_t> body);
/** Stage QG7: @return Request 3161, or no value when the body does not have its form (17 bytes) or the faction is above 4 (the validator). */
[[nodiscard]] std::optional<SetFactionRequest> read_set_faction_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<BarRequest> read_bar_request(std::span<const std::uint8_t> body);
[[nodiscard]] std::optional<TreeResetRequest> read_tree_reset_request(std::span<const std::uint8_t> body);
/**
 * @return The start of a request with no field (the attribute reset, type 2409, and "apply
 *         ability points", type 2857), or no value when the body is not only that start.
 */
[[nodiscard]] std::optional<ComponentCall> read_plain_call(std::span<const std::uint8_t> body);

/**
 * @return The id of the ability table with this index in the list of the client, or no value
 *         when the list has no such index.
 */
[[nodiscard]] std::optional<std::uint32_t> ability_table(std::uint32_t tableIndex);

/**
 * @return The body of AbilityComponentClientFacet_MasteryTreeRespecResponse (client reader at
 *         image RVA 0x6568F80): target:u64 entity:u64 success:u8 tableIndex:u32. The target of
 *         a client facet is the target of the server facet of the same component: the value of
 *         the request. The client ends its wait for the answer only when the table index is 0
 *         or more; the pool of the weapon changes with the next state of slot 67.
 */
[[nodiscard]] hub::Bytes tree_reset_reply_body(const ComponentCall& call, bool success, std::uint32_t tableIndex);

} // namespace soulwarden::backend::server::rep

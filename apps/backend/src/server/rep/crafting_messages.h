#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "server/hub/wire.h"
#include "world/crafting.h"

// The messages of the crafting (stage 3.7a, world/crafting.h, protocol notes 4.50) and the empty storage search of the crafting screen.
// Each body starts with the facet target and the entity group id (big-endian; protocol notes 4.2). The numbers of the body are big-endian;
// `size` is the prefix code of protocol notes 3.3.
//
//   2997 SetCraftingDetails (client): recipeCrc:u32 quantity:u32 coinFee:u32 gearScore:u16 perkCount:size perkItemCrc:u32* upgrade:u8
//        ingredientCount:size {type:u8 quantity:u32 itemCrc:u32}*        (writer 0x2DF48A0, 0x2E173D0; readers 0x2DF4C10, 0x2E2A040,
//        0x2E29E90; live check L3: 51 bytes for the knife, the fee 1)
//   2999 CancelCrafting (client): no field                              (reader 0x259E1E0; live check L10b: after a final progress)
//   2993 OnCraftingProgressUpdate (server): recipeId:string completed:u32 total:u32   (writer 0x2949A30, reader 0x2949B10, handler
//        0x2E602A0: completed >= total, or an empty recipe with total 0, ends the crafting of the client, which sends 2999)
//   7044 RequestSearchStorage (client): searchUuid:16 parameters (15 bytes in live check L2)    (reader 0x2CE5530)
//   7039 RequestReplicateStorageSearchResults (client): no field        (reader 0x259E1E0)
//   7046 ReceiveStorageSearchResults (server): searchUuid:16 count:size entries*   (writer 0x2CD5D50; live check L2: a matching UUID
//        and the count 0 reach 0x2D13950; the screen stays loaded)

namespace soulwarden::backend::server::rep {

/** Type indexes from the typeindex.json file of the supported client build. */
inline constexpr std::uint32_t kTypeCraftRequest = 2997;            // CraftingComponentServerFacet_SetCraftingDetails
inline constexpr std::uint32_t kTypeCraftCancel = 2999;             // CraftingComponentServerFacet_CancelCrafting
inline constexpr std::uint32_t kTypeCraftProgress = 2993;           // CraftingComponentClientFacet_OnCraftingProgressUpdate
inline constexpr std::uint32_t kTypeStorageSearch = 7044;           // GlobalStorageComponentServerFacet_RequestSearchStorage
inline constexpr std::uint32_t kTypeStorageSearchReplicate = 7039;  // GlobalStorageComponentServerFacet_RequestReplicateStorageSearchResults
inline constexpr std::uint32_t kTypeStorageSearchResults = 7046;    // GlobalStorageComponentClientFacet_ReceiveStorageSearchResults

/**
 * The components of the player slice (slice-ids.json) that receive the crafting messages and the storage search (live checks L2 and L3:
 * the target of 2997 and of 7044 is the facet of these components).
 */
inline constexpr const char* kCraftingComponentName = "Crafting/f7c1e949-1ae0-410f-b36e-6226843fd706";
inline constexpr const char* kStorageSearchComponentName = "GlobalStorage/127579b6-b5be-4282-8985-bf1fce3c3f51";

/** @return Request 2997, or no value when the body does not have its form or a value is above a limit of the reader of the client. */
[[nodiscard]] std::optional<world::CraftRequest> read_craft_request(std::span<const std::uint8_t> body, std::uint64_t& entity);

/** The start of a message with no field (2999, 7039): the target and the entity. No value: not 16 bytes. */
struct FacetCall {
    std::uint64_t target{0};
    std::uint64_t entity{0};
};
[[nodiscard]] std::optional<FacetCall> read_facet_call(std::span<const std::uint8_t> body);

/** Request 7044: the target, the entity, and the search UUID (the parameters are not read: the result is empty). */
struct StorageSearch {
    FacetCall call;
    std::array<std::uint8_t, 16> uuid{};
};
[[nodiscard]] std::optional<StorageSearch> read_storage_search(std::span<const std::uint8_t> body);

/** @return The body of 2993 for the Crafting facet of a character. */
[[nodiscard]] hub::Bytes craft_progress_body(std::uint64_t target, std::uint64_t entity, const std::string& recipe, std::uint32_t completed,
                                             std::uint32_t total);
/** @return The body of 7046 with no result (the bag-only storage search of the crafting screen; no stage 5.2 storage). */
[[nodiscard]] hub::Bytes storage_search_result_body(std::uint64_t target, std::uint64_t entity, const std::array<std::uint8_t, 16>& uuid);

} // namespace soulwarden::backend::server::rep

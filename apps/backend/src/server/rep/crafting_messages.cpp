#include "server/rep/crafting_messages.h"

#include <algorithm>

namespace soulwarden::backend::server::rep {

std::optional<world::CraftRequest> read_craft_request(std::span<const std::uint8_t> body, std::uint64_t& entity) {
    hub::Reader reader(body);
    world::CraftRequest request;
    request.target = reader.u64();
    entity = reader.u64();
    request.recipe = reader.u32();
    request.quantity = reader.u32();
    request.coinFee = reader.u32();
    request.gearScore = reader.u16();
    // The limits of the reader of the client (research crafting.md 4.1): a body above one of them is not a request of the client.
    if (!reader.ok() || request.quantity > world::crafting_rules::kMaxQuantity || request.coinFee > world::crafting_rules::kMaxFee
        || request.gearScore > world::crafting_rules::kMaxGearScore) {
        return std::nullopt;
    }
    const std::uint32_t perks = reader.size();
    if (!reader.ok() || perks > world::crafting_rules::kMaxPerkItems) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < perks; ++index) {
        request.perkItems.push_back(reader.u32());
    }
    request.upgrade = reader.u8();
    const std::uint32_t ingredients = reader.size();
    if (!reader.ok() || request.upgrade > world::crafting_rules::kMaxUpgrade || ingredients > world::crafting_rules::kMaxIngredients) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < ingredients; ++index) {
        world::CraftIngredientClaim claim;
        claim.type = reader.u8();
        claim.quantity = reader.u32();
        claim.item = reader.u32();
        // The types 0 to 4 (checker 0x2E6A8A0) and the quantity limit 50,000 (0x2E6ACB0).
        if (claim.type > 4 || claim.quantity > world::crafting_rules::kMaxIngredientQuantity) {
            return std::nullopt;
        }
        request.ingredients.push_back(claim);
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return request;
}

std::optional<FacetCall> read_facet_call(std::span<const std::uint8_t> body) {
    if (body.size() != 16) {
        return std::nullopt;
    }
    hub::Reader reader(body);
    FacetCall call;
    call.target = reader.u64();
    call.entity = reader.u64();
    return reader.ok() ? std::optional<FacetCall>(call) : std::nullopt;
}

std::optional<StorageSearch> read_storage_search(std::span<const std::uint8_t> body) {
    if (body.size() < 32) {
        return std::nullopt;
    }
    hub::Reader reader(body);
    StorageSearch search;
    search.call.target = reader.u64();
    search.call.entity = reader.u64();
    const auto uuid = reader.raw(search.uuid.size());
    if (!reader.ok()) {
        return std::nullopt;
    }
    std::copy(uuid.begin(), uuid.end(), search.uuid.begin());
    return search;
}

hub::Bytes craft_progress_body(std::uint64_t target, std::uint64_t entity, const std::string& recipe, std::uint32_t completed,
                               std::uint32_t total) {
    hub::Writer body;
    body.u64(target);
    body.u64(entity);
    body.string(recipe);
    body.u32(completed);
    body.u32(total);
    return body.data();
}

hub::Bytes storage_search_result_body(std::uint64_t target, std::uint64_t entity, const std::array<std::uint8_t, 16>& uuid) {
    hub::Writer body;
    body.u64(target);
    body.u64(entity);
    body.raw(uuid);
    body.size(0);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

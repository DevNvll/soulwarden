#include "server/rep/lock_messages.h"

#include <algorithm>
#include <array>

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

std::optional<InteractionCancel> read_interaction_cancel(std::span<const std::uint8_t> body) {
    constexpr std::size_t kSize = 8 + 8 + 16 + 8 + 8;
    if (body.size() != kSize) {
        return std::nullopt;
    }
    hub::Reader reader(body);
    InteractionCancel cancel;
    cancel.target = reader.u64();
    cancel.entity = reader.u64();
    const auto actor = reader.raw(cancel.reference.actor.size());
    std::copy(actor.begin(), actor.end(), cancel.reference.actor.begin());
    cancel.reference.group = reader.u64();
    cancel.reference.target = reader.u64();
    return reader.ok() ? std::optional<InteractionCancel>(cancel) : std::nullopt;
}

hub::Bytes interaction_result_body(world::EntityId character, const world::InteractionReference& reference, std::uint8_t action,
                                   bool accepted) {
    hub::Writer body;
    body.u64(character + kInteractorComponent);
    body.u64(character);
    body.raw(reference.actor);
    body.u64(reference.group);
    body.u64(reference.target);
    body.u8(action);
    body.u8(accepted ? 1 : 2);
    body.size(0);
    body.u32(0);
    const std::array<std::uint8_t, 16> zero{};
    body.raw(zero);
    return body.data();
}

} // namespace soulwarden::backend::server::rep

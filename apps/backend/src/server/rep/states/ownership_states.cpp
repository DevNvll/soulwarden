#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The two states of the ownership of the DLC (stage 1.1b, world/ownership.h). Only the owner gets them: the remote player
// slice has neither component (research dlc-entitlements.md, Q2).
//
// MB::EntitlementComponentReplicatedState (type 3133, constructor at image RVA 0x302C5D0), slot 19 of the player slice.
// One group:
//   field 0  entitlements          the byte vector of the entitlements of the account: bit (index & 7) of byte
//                                  (index >> 3) for the entitlement with the EntitlementIndex `index` (getter at image RVA
//                                  0x303F960; the reader 0x3050A50 takes 575 bytes at most)
//   field 1  balances              the map of the balances (CRC to u32; reader 0x3050940): empty
//   field 2  entitlementsReceived  true
// MB::AchievementComponentReplicatedState (type 3210, constructor at image RVA 0x317FEC0), slot 66 of the player slice. One
// group: field 0 `achievements`, the byte vector of the achievements of the character, with the same bit rule (getter at
// image RVA 0x318D5E0; the expansion check of the client, 0x684B340, tests the bit of Expansion_2023_Unlock).
//
// A vector or a map is the full form of a replicated container (reader 0x279DC70):
//   changes:size (0)  version:(has:u8 1, sequence:size64)  count:size  element*
// The sequence is the world revision of the last change of the state (positive). Live check L1 of stage 1.1b: the client
// takes the full form with these bytes, and the bit of the achievement opens the expansion (the level above 60, the flail
// tree, the trade skill limit 250).
//
// View     Fragments
// owner    the state, when the entity has an entitlement (an achievement); none with no expansion data; stage 1.8a: an empty
//          achievement list after a lock of the last bit (Entity::achievementsEmptied)
// observer no fragment

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kEntitlementsField = 0;
constexpr unsigned kBalancesField = 1;
constexpr unsigned kReceivedField = 2;
constexpr unsigned kAchievementsField = 0;

/** @return The positive sequence of a container: the world revision of the last change of the state. */
std::uint64_t sequence_of(const world::Entity& entity, world::ComponentKey key) {
    const auto found = entity.changed.find(key);
    return found != entity.changed.end() && found->second != 0 ? found->second : 1;
}

/** Writes the full form of a byte vector with the bits of these indexes (ascending). */
void write_bits(hub::Writer& out, const std::vector<std::uint32_t>& indexes, std::uint64_t sequence) {
    std::vector<std::uint8_t> bytes(indexes.empty() ? 0 : indexes.back() / 8 + 1, 0);
    for (const std::uint32_t index : indexes) {
        bytes[index >> 3] = static_cast<std::uint8_t>(bytes[index >> 3] | (1U << (index & 7)));
    }
    out.size(0);
    out.u8(1);
    out.size64(sequence);
    out.size(static_cast<std::uint32_t>(bytes.size()));
    out.raw(bytes);
}

} // namespace

hub::Bytes encode_entitlement_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner || entity.entitlements.empty()) {
        return {};
    }
    const std::uint64_t sequence = sequence_of(entity, world::ComponentKey::entitlements);
    StateWriter state(kGroups);
    write_bits(state.field(0, kEntitlementsField), entity.entitlements, sequence);
    hub::Writer& balances = state.field(0, kBalancesField);
    balances.size(0);
    balances.u8(1);
    balances.size64(sequence);
    balances.size(0);
    state.field(0, kReceivedField).boolean(true);
    return state.finish();
}

hub::Bytes encode_achievement_state(const world::Entity& entity, View view, const world::WorldClock&) {
    // Stage 1.8a: a lock of the last bit (a quest task) gives an empty list: the client keeps the list that it got, and only an empty
    // full vector removes its last bit. A character that never had a bit gets no fragment (the first record of stage 1.1b).
    if (view != View::owner || (entity.achievements.empty() && entity.achievementsEmptied == 0)) {
        return {};
    }
    StateWriter state(kGroups);
    write_bits(state.field(0, kAchievementsField), entity.achievements, sequence_of(entity, world::ComponentKey::achievements));
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

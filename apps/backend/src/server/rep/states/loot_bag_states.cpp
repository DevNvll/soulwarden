#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The states of the personal loot bags (stage 3.5b.2, world/loot_bags.h, docs/systems/loot-bags.md, protocol notes 4.42).
//
// MB::GlobalStorageComponentReplicatedState (type 2938; constructor 0x2CF3C30, apply 0x2D09280; research storage.md), slot 77 of the
// player slice (the remote player slice has no storage component: owner only). Group 0 has four fields; the backend sends field 0,
// m_globalItemMap (reader 0x2D1B110): a map of an actor id to an item vector. Its full form:
//   changes:size = 0, versionPresent:u8 = 0, count:size, entries        (30 keys at most: the full map reader of the client)
//   entry := actor:16 itemCount:size item*itemCount                       (the item of the bag: states/container_state.cpp)
// Live check L5 of stage 3.5b.2: the key of a loot bag gives the item list under its prompt ("Iron Ore 9"); the panel does not read it.
//
// View     Fragments
// owner    the full map at each change (also the empty map after a change). No fragment in the first record of a character with no bag.
// observer no fragment.
//
// MB::LootDropReplicatedState (type 2027; constructor 0x2D356F0, apply 0x2D3F460), the slot of the LootDrop component of the slice of a
// bag (6 for the personal bag, game data). Group 0, field 0: m_rarityLevel u32 (reader 0x17B3620).
//
// View     Fragments
// observer the rarity (each eligible character of the bag observes it; a different character gets no record of the bag).

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;

} // namespace

std::vector<hub::Bytes> encode_global_storage_state(const world::Entity& entity, View view, const PartsRequest& request) {
    std::vector<hub::Bytes> parts;
    if (view != View::owner || (request.since == 0 && entity.storageMap.empty())) {
        return parts;
    }
    StateWriter state(kGroups);
    hub::Writer& map = state.field(0, 0);
    map.size(0); // No change entries: the full form.
    map.u8(0);   // No version.
    map.size(static_cast<std::uint32_t>(entity.storageMap.size()));
    for (const world::StorageKey& key : entity.storageMap) {
        map.raw(key.actor);
        map.size(static_cast<std::uint32_t>(key.items.size()));
        for (std::size_t index = 0; index < key.items.size(); ++index) {
            map.raw(encode_item(&key.items[index], static_cast<std::uint32_t>(index), world::sync::kLoad));
        }
    }
    parts.push_back(state.finish());
    return parts;
}

hub::Bytes encode_loot_drop_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::object || entity.object.lootDropSlot == 0) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, 0).u32(entity.object.rarity);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

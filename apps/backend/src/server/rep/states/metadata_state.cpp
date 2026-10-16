#include <array>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::GdeMetadataReplicatedState (tools/st GdeMetadata), slot 0 of each slice. It is the first
// state of a new entity group: the client makes the group from the slice asset and the actor
// id (client code at image RVA 0x1717FC0 and 0x178DB00).
//
// View     Fields
// owner    AssetId (player slice, sub id 2), GdeRef (actor id of the entity)
// observer the same fields. The client changes the asset of the player of a different client
//          to the remote player slice (sub id 100) itself, when it has its own player entity
//          (client code at image RVA 0x1747C60).
// A creature has only the observer view: AssetId (the slice of the creature, with the sub id
// of the asset catalog of the game) and GdeRef. The same for an object of the level data (stage 2.5a): the slice of its
// placement and its managed actor id.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kAssetId = 0;
constexpr unsigned kGdeRef = 1;
/** Asset id of slices/player.dynamicslice in the asset catalog of the supported build. */
constexpr std::array<std::uint8_t, 16> kPlayerSliceAsset{0xA6, 0x60, 0xEE, 0xEB, 0xEB, 0xC7, 0x5C, 0xB7,
                                                         0xBE, 0x6B, 0xAB, 0x11, 0xEB, 0x83, 0x17, 0x31};
/** Sub id of the player slice. slices/remoteplayer.dynamicslice has the same asset id with the sub id 100. */
constexpr std::uint32_t kOwnerSliceSubId = 2;
constexpr std::uint32_t kObserverSliceSubId = 2;

} // namespace

hub::Bytes encode_metadata_state(const world::Entity& entity, View view, const world::WorldClock&) {
    StateWriter state(kGroups);
    if (entity.kind == world::EntityKind::spell) {
        // The slice of the spell (game data: the vortex of Gravity Well, sub id 2) and the actor id of the spell entity
        // (live check L4 of stage 3.3b-S2a).
        if (entity.spell.slice != nullptr) {
            state.field(0, kAssetId).raw(entity.spell.slice->asset);
            state.field(0, kAssetId).u32(entity.spell.slice->subId);
        }
        state.field(0, kGdeRef).raw(entity.actor.bytes);
        return state.finish();
    }
    if (entity.kind == world::EntityKind::projectile) {
        // A projectile of a creature (stage 3.4f-A): its projectile slice (game data: basic_ai_arrow, sub id 2) and the actor id of its
        // entity (live check L2f: the metadata and the Projectile state in one first record show one arrow).
        if (entity.shot.slice != nullptr) {
            state.field(0, kAssetId).raw(entity.shot.slice->asset);
            state.field(0, kAssetId).u32(entity.shot.slice->subId);
        }
        state.field(0, kGdeRef).raw(entity.actor.bytes);
        return state.finish();
    }
    if (entity.kind == world::EntityKind::probe) {
        // Development aid: the slice of a command (tools/world probe).
        state.field(0, kAssetId).raw(entity.probe.asset);
        state.field(0, kAssetId).u32(entity.probe.subId);
        state.field(0, kGdeRef).raw(entity.actor.bytes);
        return state.finish();
    }
    if (entity.kind == world::EntityKind::object) {
        // An object of the level data (stage 2.5a): the slice of its placement and its managed actor id (live check L2: the
        // client applies the record to the object that it made from the level data, with no second body).
        state.field(0, kAssetId).raw(entity.object.asset);
        state.field(0, kAssetId).u32(entity.object.subId);
        state.field(0, kGdeRef).raw(entity.actor.bytes);
        return state.finish();
    }
    if (entity.kind == world::EntityKind::creature) {
        state.field(0, kAssetId).raw(entity.creature.body->asset);
        state.field(0, kAssetId).u32(entity.creature.body->subId);
        state.field(0, kGdeRef).raw(entity.actor.bytes);
        return state.finish();
    }
    state.field(0, kAssetId).raw(kPlayerSliceAsset);
    state.field(0, kAssetId).u32(view == View::owner ? kOwnerSliceSubId : kObserverSliceSubId);
    state.field(0, kGdeRef).raw(entity.actor.bytes);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

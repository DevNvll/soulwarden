#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The two owner states of the open of a chest (stage 3.5b.1, world/chests.h, docs/systems/chests.md, protocol notes 4.41).
//
// MB::GatheringComponentReplicatedState (type 81, constructor at image RVA 0x4904A90, apply at 0x4905130), slot 36 of the player slice
// (the remote player slice has no slot 36: owner only). Group 0 is empty; group 1 has nine fields (research gathering-stage.md Q2):
//   0 m_status u8 (None 0, Success 1, Failure 2, Done 3)   1 m_amtRemaining f32   2 m_gatherEfficiency f32   3 m_numGathering u8
//   4 m_gatherRefCount u32   5 m_waterGatherRefCount u32   6 m_isGathering bool   7 m_gatherableGDEID u64   8 m_syncGatherRefCount u32
// (big-endian numbers; the chained field masks of replication.h: the second mask byte follows the values of fields 0 to 6). The client
// applies fields 0 to 3 only when field 4 changes, and fields 6 and 7 only when field 8 changes: the chest system adds one to both at
// each change. Live check L3 of stage 3.5b.1: Success with remaining = the time of the data and field 2 = 1 shows the progress ring of
// the prompt for that time (field 2 = 2.5 made the ring 2.5 times faster: field 2 is a speed); Done makes the client end its committed
// interaction at once (it sends 2473).
//
// View     Fragments
// owner    the nine fields of group 1, at each change. No fragment in the first record of a character with no open (reference 0).
// observer no fragment (the remote player slice has no Gathering component).
//
// MB::LootTrackerComponentReplicatedState (type 982, constructor at image RVA 0x37350D0, reader 0x37781A0), slot 33 of the player slice.
// One group, field 0: a map of the loot claims of the character (research chests-ground-loot.md Q2, K3). Its full form:
//   changes:size = 0, versionPresent:u8 = 0, count:size, entries
//   entry := actor:16 state:u32 kind:u32 resetTime:u64 lootedTime:u64 privileged:u8 x:f32 y:f32 z:f32 tag:u32 selector:u8   (58 bytes)
// (reader 0x3779670; entry readers 0x3727A00, 0x3727C60; writer 0x375AFA0). A full map replaces the map of the client (live check L2:
// the map with the chest Looted removes its prompt; the map with it Initial, or the map with no entry for it, gives the prompt back at
// once; a map of two entries works). The time values: the world clock in microseconds (the unit of the cooldown state); live check L8:
// the client does not end a claim at its reset time in any tried unit, the server sends the map with no entry at the reset.
//
// View     Fragments
// owner    the full map of the claims near the character (world/chests.h) and of its loot bags (stage 3.5b.2, world/loot_bags.h: Rolled,
//          Looted, the kind AILoot 1 or Discarded 5, the selector of the rarity), at each change, also the empty map after a change. No
//          fragment in the first record of a character with no claim near it and no bag.
// observer no fragment.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGatheringGroups = 2;
constexpr unsigned kGatheringGroup = 1;
constexpr unsigned kTrackerGroups = 1;

} // namespace

std::vector<hub::Bytes> encode_gathering_state(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)request;
    std::vector<hub::Bytes> parts;
    const world::GatherProgress& gather = entity.gathering;
    if (view != View::owner || gather.reference == 0) {
        return parts;
    }
    StateWriter state(kGatheringGroups);
    state.field(kGatheringGroup, 0).u8(gather.status);
    state.field(kGatheringGroup, 1).f32(gather.remaining);
    state.field(kGatheringGroup, 2).f32(gather.speed);
    state.field(kGatheringGroup, 3).u8(gather.gatherers);
    state.field(kGatheringGroup, 4).u32(gather.reference);
    state.field(kGatheringGroup, 5).u32(0);
    state.field(kGatheringGroup, 6).boolean(gather.active);
    state.field(kGatheringGroup, 7).u64(gather.target);
    state.field(kGatheringGroup, 8).u32(gather.syncReference);
    parts.push_back(state.finish());
    return parts;
}

std::vector<hub::Bytes> encode_loot_tracker_state(const world::Entity& entity, View view, const PartsRequest& request) {
    std::vector<hub::Bytes> parts;
    if (view != View::owner || (request.since == 0 && entity.lootTracker.empty() && entity.bagTracker.empty() && entity.questObjectTracker.empty()
                                && entity.questGatherTracker.empty())) {
        return parts;
    }
    // One map: the claims of the chests (world/chests.h) and the entries of the loot bags (stage 3.5b.2, world/loot_bags.h).
    std::vector<world::LootTrackerEntry> entries = entity.lootTracker;
    entries.insert(entries.end(), entity.bagTracker.begin(), entity.bagTracker.end());
    // Stage 1.8e: the Looted entries of the destroyed quest objects of the character (world/quest_objects.h; live check I0). Code review of
    // stage DG1, finding DG1R-R3: they are objects of the main world: a character in an instance gets none of them.
    if (entity.space == world::kMainSpace) {
        entries.insert(entries.end(), entity.questObjectTracker.begin(), entity.questObjectTracker.end());
    }
    // Stage QG5a: the gathered quest objects. Stage DG3 (code review of stage DG2, finding DG2R-R3): the entries of the space of the
    // character only (a gather in a story instance has the space of the instance; a gather of the main world the main world).
    for (const world::LootTrackerEntry& entry : entity.questGatherTracker) {
        if (entry.space == entity.space) {
            entries.push_back(entry);
        }
    }
    StateWriter state(kTrackerGroups);
    hub::Writer& map = state.field(0, 0);
    map.size(0); // No change entries: the full form.
    map.u8(0);   // No version.
    map.size(static_cast<std::uint32_t>(entries.size()));
    for (const world::LootTrackerEntry& entry : entries) {
        map.raw(entry.actor);
        map.u32(entry.state);
        map.u32(entry.kind);
        map.u64(entry.resetTime);
        map.u64(entry.lootedTime);
        map.boolean(entry.privileged);
        map.f32(entry.position.x);
        map.f32(entry.position.y);
        map.f32(entry.position.z);
        map.u32(entry.tag);
        map.u8(entry.selector);
    }
    parts.push_back(state.finish());
    return parts;
}

} // namespace soulwarden::backend::server::rep

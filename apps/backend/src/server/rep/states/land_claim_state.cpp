#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "world/missions.h"

// The LandClaimManager state of the world entity (stage QG6a, docs/systems/mission-boards.md; research map.md 2.5 and the static read
// evidence/quests/qg6a/implement/static/landclaim/LANDCLAIM.md).
//
// MB::LandClaimManagerComponentReplicatedState (type 3086, constructor 0x2F75790), slot 2 of slices/globalgde.dynamicslice. One group of
// 18 fields. The fields of the claims are parallel full vectors (element n for the claim n of field 2). The client finds a claim by its key
// in field 2 and reads the element of that index of another field with no check of its length (the owner 0x2F81310, the governance
// 0x2F83EE0, the conflict, darkness, and influence getters: the static read evidence/quests/qg6a/implement/static/landclaim-owner/OWNER.md
// section 1). Thus each such field has one element for each claim. A territory entry reads the owner of the claim of the territory at once
// (0x2A09BA0 -> 0x29FF700); with no owner element the client stopped (live: an access at NewWorld.exe+0x2831BCF, address 0x10 and 0xE0).
//   field 2   replicatedClaimKeys                      u16: the territory id (reader 0x2F9C1C0)
//   field 3   replicatedConflictFaction                u8 (0: none)
//   fields 4, 5, 7, 13  the conflict lottery end, the conflict start, the darkness cycle end, the influence race start: u64 time points (0)
//   field 6   replicatedDarknessThreshold              u32 (0)
//   fields 9, 10, 11  the influence percentages of the three factions: u8 (0)
//   field 12  replicatedGovernance                     GovernanceTaxData: four f32 fees, isValid:u8, failedToPayUpkeep:u8 (all 0)
//   field 15  replicatedProgression                    TerritoryProgressionSummaryData (reader 0x2F9C440, element 0x2F6AA40):
//             territoryUpgradeCount:size {category:u32 categoryLevel:u8}*  activeProjectCount:size {activeProjectId:u32
//             progressionType:u8 activeState:u32}*  territoryLevel:u8
//   field 16  replicatedOwnerData                      LandClaimOwnerData (element 0x2F6AC60): company id (16 raw bytes; zero: no company),
//             company name (text), crest background index (u16) and color (4 f32), foreground index (u16) and color (4 f32), player name
//             (text), faction (u8), last claimed time (u64): the 63 zero bytes of a territory with no owner
//   field 17  replicatedWarDecThresholdMetFaction      u8 (0)
// LandClaimRequestBus GetTerritoryProgressionData(claimKey) (0x2F84390) gives the summary of a claim. The town screen of a board
// (townprojects.luac, IsActive of territorydatahandler) lists a project only when the summary of the claim of the player has it (activeState
// not Cancelled). Each project is AlwaysAvailable (type 6) and Active (state 0); no upgrade; the territory level 0 [agent: the backend has no
// town project state]. The other values are the quiet values of a territory with no owner, no war, and no conflict [agent]. The backend
// sends no replicatedPosData (field 14) and no faction control data (field 8): with a position the manager is "available" and the map and
// HUD code reads more (OWNER.md section 4).
//
// View     Fragments
// owner    no owner (the world entity)
// observer each session (the world entity is in each interest table)

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kClaimKeys = 2;
constexpr unsigned kConflictFaction = 3;
constexpr unsigned kConflictLotteryEnd = 4;
constexpr unsigned kConflictStart = 5;
constexpr unsigned kDarknessThreshold = 6;
constexpr unsigned kDarknessCycleEnd = 7;
constexpr unsigned kInfluence[] = {9, 10, 11};
constexpr unsigned kGovernance = 12;
constexpr unsigned kInfluenceRaceStart = 13;
constexpr unsigned kProgression = 15;
constexpr unsigned kOwner = 16;
constexpr unsigned kWarThresholdFaction = 17;
/** eTerritoryUpgradeType AlwaysAvailable and eSettlementProgressionState Active (LANDCLAIM.md section 1). */
constexpr std::uint8_t kAlwaysAvailable = world::town_rule::kAlwaysAvailableKey;
constexpr std::uint32_t kActive = 0;

/** The header of a full replicated vector: no change, a version (the revision of the state), the count. */
hub::Writer& full_vector(StateWriter& state, unsigned field, std::uint64_t version, std::size_t count) {
    hub::Writer& out = state.field(0, field);
    out.size(0);
    out.u8(1);
    out.size64(version);
    out.size(static_cast<std::uint32_t>(count));
    return out;
}

/** A full vector of `count` elements of `bytes` zero bytes (the quiet value of each claim). */
void zero_vector(StateWriter& state, unsigned field, std::uint64_t version, std::size_t count, std::size_t bytes) {
    full_vector(state, field, version, count).zeros(count * bytes);
}

/** The owner of a territory with no owner company (LandClaimOwnerData, OWNER.md section 2). */
void no_owner(hub::Writer& out) {
    out.zeros(16);   // the company id: zero is no company (0x2D40410)
    out.string("");  // the company name
    out.u16(0);      // the crest background index
    out.zeros(16);   // its color (four f32)
    out.u16(0);      // the crest foreground index
    out.zeros(16);   // its color
    out.string("");  // the name of the claiming player
    out.u8(0);       // the faction: None
    out.u64(0);      // the last claimed time
}

} // namespace

hub::Bytes encode_land_claim_state(const world::Entity& entity, View view, const world::WorldClock&) {
    (void)view;
    if (entity.kind != world::EntityKind::object || !entity.object.world || entity.object.landClaims.empty()) {
        return {};
    }
    const auto found = entity.changed.find(world::ComponentKey::landClaims);
    const std::uint64_t version = found != entity.changed.end() && found->second != 0 ? found->second : 1;
    const std::vector<world::LandClaim>& claims = entity.object.landClaims;
    const std::size_t count = claims.size();
    StateWriter state(kGroups);
    hub::Writer& keys = full_vector(state, kClaimKeys, version, count);
    for (const world::LandClaim& claim : claims) {
        keys.u16(claim.territory);
    }
    zero_vector(state, kConflictFaction, version, count, 1);
    zero_vector(state, kConflictLotteryEnd, version, count, 8);
    zero_vector(state, kConflictStart, version, count, 8);
    zero_vector(state, kDarknessThreshold, version, count, 4);
    zero_vector(state, kDarknessCycleEnd, version, count, 8);
    for (const unsigned field : kInfluence) {
        zero_vector(state, field, version, count, 1);
    }
    zero_vector(state, kGovernance, version, count, 18);
    zero_vector(state, kInfluenceRaceStart, version, count, 8);
    hub::Writer& progression = full_vector(state, kProgression, version, count);
    for (const world::LandClaim& claim : claims) {
        progression.size(0); // territoryUpgrades
        progression.size(static_cast<std::uint32_t>(claim.projects.size()));
        for (const std::uint32_t project : claim.projects) {
            progression.u32(project);
            progression.u8(kAlwaysAvailable);
            progression.u32(kActive);
        }
        progression.u8(0); // territoryLevel
    }
    hub::Writer& owners = full_vector(state, kOwner, version, count);
    for (std::size_t claim = 0; claim < count; ++claim) {
        no_owner(owners);
    }
    zero_vector(state, kWarThresholdFaction, version, count, 1);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "world/missions.h"

// The TerritoryInterface state of a mission board (stage QG6a, docs/systems/mission-boards.md; research territories.md section 8 and the
// static read evidence/quests/qg6a/implement/static/offers/OFFERS.md section 5).
//
// MB::TerritoryInterfaceReplicatedState (type 1746, constructor 0x29E3F00, apply 0x29FBEC0), slot 3 of town_project_board (the component
// of its entity Interaction). Three groups: group 0 territoryFunctionalityDown and territoryRefsSet (bools), group 1 field 0
// territoryProgressionData (reader 0x2A0BB00 -> 0x29D5CC0), group 2 field 0 territoryGovernanceData. The town getter of the client
// (GetCurrentCommunityGoals 0x4955A90) asks the TerritoryInterface of the provider for its progression data (the ready predicate +0x2AA,
// set by the apply when group 1 field 0 arrives) and runs the generator for each nonzero project of its active-project map. This encoder
// writes group 1 field 0 only (the smallest state of the report: 38 bytes for one project):
//   activeCount:size { key:u8 projectId:u32 timeProjectEnds:u64 currentProgress:u32 }*
//   completedCount:size (0) defaultProject: projectId:u32 end:u64 progress:u32 (zero) statusCount:size (0)
// The projects are the always available projects of the town (Missions::town_projects; key 6, the type enum of AlwaysAvailable) [agent:
// the backend has no town project state yet].
//
// View     Fragments
// owner    no owner (an object)
// observer each session that observes the board

namespace soulwarden::backend::server::rep {

hub::Bytes encode_territory_interface_state(const world::Entity& entity, View view, const world::WorldClock&) {
    (void)view;
    if (entity.kind != world::EntityKind::object || entity.object.territorySlot == 0) {
        return {};
    }
    StateWriter state(3);
    hub::Writer& progression = state.field(1, 0);
    progression.size(static_cast<std::uint32_t>(entity.object.projects.size()));
    for (const std::uint32_t project : entity.object.projects) {
        progression.u8(world::town_rule::kAlwaysAvailableKey);
        progression.u32(project);
        progression.u64(0); // timeProjectEnds
        progression.u32(0); // currentProgress
    }
    progression.size(0); // the completed projects
    progression.u32(0);  // the default project record
    progression.u64(0);
    progression.u32(0);
    progression.size(0); // the status effects
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

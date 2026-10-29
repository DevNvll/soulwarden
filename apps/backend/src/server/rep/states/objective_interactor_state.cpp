#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The provider state of a character (stage QG6a, docs/systems/mission-boards.md; research mission-boards.md 1.1 to 1.3).
//
// MB::ObjectiveInteractorReplicatedState (type 3829, constructor 0x3A1B9E0), slot 10 of the player slice. One group, four fields:
//   field 0  objectiveProviderId  16 raw bytes: the actor id of the board (reader 0x279E260 reads 0x10 bytes; not the u64 of Lua)
//   field 1  expirationTime       u64: the server clock in nanoseconds (reader 0x279E480; the TimePoint of GetTimeTillNextProviderUpdate
//                                 0x4958040, the domain of the clock correction 335): the start of the next seed of the town [agent]
//   field 2  missionParams        a full replicated vector of ObjectiveCreationParams (reader 0x3A68000): `00 01 sequence:size64 count:size`;
//                                 a town board has no explicit offer (the client generates the town offers: GetCurrentCommunityGoals
//                                 0x4955A90), so the count is 0; a faction board (stage QG6b) has its offers (the PvE list of 0x3A43320)
//   field 3  m_communityGoals     a direct map (reader 0x3A68C60 -> 0x495DDA0 -> 0x494F320): count:size, then for each territory
//                                 territory:u16 missionSeed:u32 unavailable:size u32* previousProjects:size u32* (no container header)
// The town entry of a territory is the stored seed of the character and the missions that its hand-ins made unavailable for that seed; the
// client marks an offer unavailable only when this seed is its current seed (0x3A3684C).
//
// View     Fragments
// owner    the state after the first open of a board in this world entry (Entity::missionBoard.opened)
// observer no fragment (the remote player slice has no ObjectiveInteractor component)

namespace soulwarden::backend::server::rep {

hub::Bytes encode_objective_interactor_state(const world::Entity& entity, View view, const world::WorldClock&) {
    const world::MissionBoardState& board = entity.missionBoard;
    if (view != View::owner || !board.opened) {
        return {};
    }
    const auto found = entity.changed.find(world::ComponentKey::objectiveInteractor);
    const std::uint64_t sequence = found != entity.changed.end() && found->second != 0 ? found->second : 1;
    StateWriter state(1);
    hub::Writer& provider = state.field(0, 0);
    for (const std::uint8_t part : board.provider) {
        provider.u8(part);
    }
    state.field(0, 1).u64(board.expirationNs);
    hub::Writer& params = state.field(0, 2);
    params.size(0);
    params.u8(1);
    params.size64(sequence);
    // Stage QG6b: the explicit offers of a faction board, in the order that 3816 names by index (0x495CE70); a town board has none. An
    // ObjectiveCreationParams element (reader 0x5CE92B0, 52 bytes with no ingredient): instanceId (0 before the accept, the accepted
    // instance after it), missionId, destinationOverride (the claim), acceptTime 0, originTerritory, available, isCommunityGoal 0, the
    // other flags 0, parent 0, no ingredient [client: FACTION.md section 3].
    params.size(static_cast<std::uint32_t>(board.faction ? board.offers.size() : 0));
    for (std::size_t index = 0; board.faction && index < board.offers.size(); ++index) {
        const world::MissionOffer& offer = board.offers[index];
        params.u64(offer.instance);
        params.u32(offer.mission);
        for (const std::uint8_t part : offer.destination) {
            params.u8(part);
        }
        params.u64(0);
        params.u16(offer.origin);
        params.boolean(offer.available);
        params.boolean(false); // isCommunityGoal
        params.boolean(false); // isPoiObjective
        params.boolean(false); // isDynamicPoiObjective
        params.u64(0);         // parentObjectiveId
        params.boolean(false); // isResetObjective
        params.size(0);        // selectedIngredientVector
    }
    hub::Writer& goals = state.field(0, 3);
    goals.size(static_cast<std::uint32_t>(board.towns.size()));
    for (const auto& [territory, town] : board.towns) {
        goals.u16(territory);
        goals.u32(town.seed);
        goals.size(static_cast<std::uint32_t>(town.unavailable.size()));
        for (const std::uint32_t mission : town.unavailable) {
            goals.u32(mission);
        }
        goals.size(static_cast<std::uint32_t>(town.previousProjects.size()));
        for (const std::uint32_t project : town.previousProjects) {
            goals.u32(project);
        }
    }
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

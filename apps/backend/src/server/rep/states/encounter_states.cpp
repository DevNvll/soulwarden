#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The states of an encounter root (stage QG5c2, world/encounters.h, docs/systems/encounters.md; protocol notes 4.62). Each session near the
// root gets them; they have the same bytes for each view (the root is public: a character that does not take part sees the stage of the
// fight too). The slots are values of the slice of the recipe (game data).
//
// MB::PositionInTheWorldReplicatedState (type 13), the place slot: the place and the rotation of the root (the placement times the
//   spawner: tools/gamedata/encounter_recipes.py). Live I1 of stage QG5c1: with the metadata state and the manager state it makes the entity
//   group of the root in the client.
// The manager state (type 6786, constructor 0x26E6700), the manager slot: `01 01 status:u32` (one group, field 0). The client only stores
//   it (0x26F0180); the Lua names eEncounterStatus_*: 1 NotStarted, 2 InProgress, 3 Succeeded, 4 Failed.
// The stage state (type 2133, constructor 0x26E58D0, reader 0x26F7550), the slot of each stage: `01 01 count:size (sequence:u32
//   status:u32)*count`, 10 entries at most. The apply 0x26F00C0 runs the handler 0x26F64A0 for each entry whose signed sequence is above its
//   last one (2: the start actions, 3 and 4: the end actions; an equal status returns before the actions). A stage with no status has no
//   fragment.
//
// View     Fragments
// owner    (no owner: the root has no session)
// observer each state
//
// The start object of a stage (a server child of the encounter system) has the states of its template (object_codecs) and one more:
// MB::VariationComponentReplicatedState (type 14, constructor 0x42C5E80, readers 0x279E210 and 0x2A42E10), the slot of the VariationData
//   component of its slice. One group: field 0 `index:u8`, field 1 `index:u16`. The apply 0x42CF180 uses field 0 when it has a value, else
//   field 1; 0x42D0720 maps the index (the file row order of the variation table of the slice) to the row, and applies its actions. The
//   backend sends only field 1: `01 02 index:u16` (static read C of stage QG5c2). Without it the activation 0x42D0300 clears
//   m_selectedVariant: the client shows the mesh of the object, but no prompt (live check L3: with it, "E OFFER BLIGHT" and a GatherOption
//   request).

namespace soulwarden::backend::server::rep {

hub::Bytes encode_encounter_place_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::encounter || entity.encounter.placeSlot == 0) {
        return {};
    }
    return position_in_the_world_state(entity.position, entity.encounter.rotation);
}

hub::Bytes encode_encounter_manager_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::encounter || entity.encounter.managerSlot == 0) {
        return {};
    }
    StateWriter state(1);
    state.field(0, 0).u32(entity.encounter.managerStatus);
    return state.finish();
}

hub::Bytes encode_variation_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::object || entity.object.variationSlot == 0) {
        return {};
    }
    StateWriter state(1);
    state.field(0, 1).u16(entity.object.variant);
    return state.finish();
}

std::vector<SlotState> encode_encounter_stage_states(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    (void)request;
    std::vector<SlotState> states;
    if (entity.kind != world::EntityKind::encounter) {
        return states;
    }
    for (const world::EncounterStageState& stage : entity.encounter.stages) {
        if (stage.slot == 0 || stage.history.empty()) {
            continue;
        }
        StateWriter state(1);
        hub::Writer& entries = state.field(0, 0);
        entries.size(static_cast<std::uint32_t>(stage.history.size()));
        for (const auto& [sequence, status] : stage.history) {
            entries.u32(sequence);
            entries.u32(status);
        }
        states.push_back(SlotState{stage.slot, state.finish()});
    }
    return states;
}

std::uint32_t encounter_place_slot(const world::Entity& entity) {
    return entity.encounter.placeSlot;
}

std::uint32_t encounter_manager_slot(const world::Entity& entity) {
    return entity.encounter.managerSlot;
}

std::uint32_t encounter_objectives_slot(const world::Entity& entity) {
    return entity.encounter.objectivesSlot;
}

std::span<const StateCodec> encounter_codecs() {
    // The slot of each state is a value of the slice of the recipe (game data).
    static constexpr std::array<StateCodec, 5> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::encounterManager, 0, kTypeEncounterManagerState, encode_encounter_manager_state, nullptr, false,
         encounter_manager_slot},
        {world::ComponentKey::encounterPlace, 0, kTypePositionInTheWorldState, encode_encounter_place_state, nullptr, false,
         encounter_place_slot},
        {world::ComponentKey::encounterObjectives, 0, kTypeObjectivesState, encode_encounter_objectives_state, nullptr, false,
         encounter_objectives_slot},
        {world::ComponentKey::encounterStages, 0, kTypeEncounterStageState, nullptr, nullptr, false, nullptr, encode_encounter_stage_states},
    }};
    return codecs;
}

} // namespace soulwarden::backend::server::rep

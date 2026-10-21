#include "gamedata/quest_data.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The two states of the quests (stage 1.8a, world/quests.h; docs/systems/quests.md; research quest-engine.md Q2, quests.md). Only the owner
// gets them: the remote player slice has neither component. The session sends them only after the first message 2709 of its client
// (StateCodec::afterObjectivesEnable; live check L1 of stage 1.8a).
//
// MB::ObjectivesComponentReplicatedState (type 3857, constructor 0x3A1DF90, apply 0x3A43530), slot 22 of the player slice. Two groups;
// this encoder writes group 1 only (group 0 field 0 is gracePeriodEndTime: no rule):
//   field 0  taskStartTimes      full map: instance:u64 index:u8 -> start:u64 (the synced wall clock, Unix nanoseconds; reader 0x3A68CB0)
//   field 1  trackedObjectives   full vector of instance:u64 (8 at most; reader 0x3A68340)
//   field 3  activeObjectives    full vector of 56-byte elements (reader 0x3A688F0, element 0x5B52F60):
//              objectiveId:u32 (CRC-32 of the lower-case ObjectiveID) instanceId:u64 missionId:u32 (0) destinationOverride:16 bytes (0)
//              acceptTime:u64 originTerritory:u16 available:u8 (1) isCommunityGoal:u8 isPoiObjective:u8 isDynamicPoiObjective:u8
//              parentObjectiveId:u64 isResetObjective:u8 selectedIngredients:size (0)
//   field 4  taskStates          full vector of 18-byte elements: instance:u64 index:u8 taskCRC:u32 progress:u32 state:u8 (reader 0x3ADDB50)
// Fields 2 (completedObjectives: a notice list of completed instances), 5, and 6 are not written (no completion and no POI objective in
// stage 1.8a). A full container is `00 01 sequence:size64 count:size element*` (reader 0x279DC70); the sequence is the world revision of
// the last change of the state.
//
// The order of the task elements is the index order of the tree: a child before its container (live check L1 of stage 1.8a: with the
// parent-first order of the research fixture the client shows the objective with no task line; with the index order it shows the line
// "Talk to Aelstrom on the beach" on the HUD and in the journal). A hole of the tree has no element.
//
// MB::SeasonsRewardsTrackedStatComponentReplicatedState (type 5606), slot 73: group 0 field 0 `initialized` = 1 (`01 01 01`; research
// quests.md 3.4: the seasons callback 0x3DDABF0 -> 0x3A57CC0 sets the pin gate of the objectives facet; live check L1 of stage 1.8a: the
// pin comes when this state comes).
//
// View     Fragments
// owner    each state (the objectives state only for a character with loaded quests)
// observer no fragment

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kObjectiveGroups = 2;
constexpr unsigned kQuestGroup = 1;
constexpr unsigned kStartTimesField = 0;
constexpr unsigned kTrackedField = 1;
constexpr unsigned kActiveField = 3;
constexpr unsigned kTaskStatesField = 4;

std::uint64_t sequence_of(const world::Entity& entity, world::ComponentKey key) {
    const auto found = entity.changed.find(key);
    return found != entity.changed.end() && found->second != 0 ? found->second : 1;
}

void full_header(hub::Writer& out, std::uint64_t sequence, std::size_t count) {
    out.size(0);
    out.u8(1);
    out.size64(sequence);
    out.size(static_cast<std::uint32_t>(count));
}

} // namespace

hub::Bytes encode_objectives_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner || !entity.quests.loaded) {
        return {};
    }
    const world::QuestLog& log = entity.quests;
    const std::uint64_t sequence = sequence_of(entity, world::ComponentKey::objectives);
    StateWriter state(kObjectiveGroups);
    // Field 0: the starts of the running timers.
    std::size_t timers = 0;
    for (const world::QuestInstance& quest : log.active) {
        for (const world::QuestTaskState& task : quest.tasks) {
            timers += task.startNs != 0 ? 1 : 0;
        }
    }
    hub::Writer& starts = state.field(kQuestGroup, kStartTimesField);
    full_header(starts, sequence, timers);
    for (const world::QuestInstance& quest : log.active) {
        for (std::size_t index = 0; index < quest.tasks.size(); ++index) {
            if (quest.tasks[index].startNs != 0) {
                starts.u64(quest.instance);
                starts.u8(static_cast<std::uint8_t>(index));
                starts.u64(quest.tasks[index].startNs);
            }
        }
    }
    hub::Writer& tracked = state.field(kQuestGroup, kTrackedField);
    full_header(tracked, sequence, log.tracked.size());
    for (const std::uint64_t instance : log.tracked) {
        tracked.u64(instance);
    }
    hub::Writer& active = state.field(kQuestGroup, kActiveField);
    full_header(active, sequence, log.active.size());
    for (const world::QuestInstance& quest : log.active) {
        active.u32(quest.objectiveCrc);
        active.u64(quest.instance);
        active.u32(0); // missionId
        for (int part = 0; part < 16; ++part) {
            active.u8(0); // destinationOverride
        }
        // acceptTime 0: the value of live check L1. The native TaskTimer of the client can take its start from the creation time of the
        // objective (research quest-display-18a.md Q4); the start of a timer goes in field 0. The stored grant time stays in the rows.
        active.u64(0);
        active.u16(quest.originTerritory);
        active.boolean(true);  // available
        active.boolean(false); // isCommunityGoal
        active.boolean(false); // isPoiObjective
        active.boolean(false); // isDynamicPoiObjective
        active.u64(quest.parent);
        active.boolean(false); // isResetObjective
        active.size(0);        // selectedIngredientVector
    }
    // Field 4: the task states, each quest in the index order of its tree (live check L1). The CRC of a task is in the element; the
    // encoder takes it from the quest data of the world (the entity keeps only the states).
    const gamedata::QuestData* data = log.data;
    std::size_t elements = 0;
    for (const world::QuestInstance& quest : log.active) {
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective_by_crc(quest.objectiveCrc) : nullptr;
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            elements += objective->nodes[index].task != gamedata::kNoTask ? 1 : 0;
        }
    }
    hub::Writer& tasks = state.field(kQuestGroup, kTaskStatesField);
    full_header(tasks, sequence, elements);
    for (const world::QuestInstance& quest : log.active) {
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective_by_crc(quest.objectiveCrc) : nullptr;
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            const gamedata::QuestNode& node = objective->nodes[index];
            if (node.task == gamedata::kNoTask) {
                continue;
            }
            tasks.u64(quest.instance);
            tasks.u8(static_cast<std::uint8_t>(index));
            tasks.u32(data->task(node.task).crc);
            tasks.u32(quest.tasks[index].progress);
            tasks.u8(quest.tasks[index].state);
        }
    }
    return state.finish();
}

hub::Bytes encode_seasons_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner) {
        return {};
    }
    (void)entity;
    StateWriter state(1);
    state.field(0, 0).boolean(true); // initialized
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

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
//   field 2  completedObjectives full vector of instance:u64 (reader 0x3A68E90): stage 1.8b, the notice list of the hand-ins of this world
//            entry (the apply 0x3A43530 calls 0x3A559B0 with the completion flag for a removed instance that the list names: the banner of a
//            completion, not "Objective Failed"; live check L3 of stage 1.8b). Written only when the list has an instance.
//   field 3  activeObjectives    full vector of 56-byte elements (reader 0x3A688F0, element 0x5B52F60):
//              objectiveId:u32 (CRC-32 of the lower-case ObjectiveID) instanceId:u64 missionId:u32 (0; stage QG6a: the mission of a board)
//              destinationOverride:16 bytes (0; stage QG6a: the actor id of the live provider board)
//              acceptTime:u64 originTerritory:u16 available:u8 (1) isCommunityGoal:u8 (stage QG6a: 1 for a town mission) isPoiObjective:u8
//              isDynamicPoiObjective:u8
//              parentObjectiveId:u64 isResetObjective:u8 selectedIngredients:size (0)
//   field 4  taskStates          full vector of 18-byte elements: instance:u64 index:u8 taskCRC:u32 progress:u32 state:u8 (reader 0x3ADDB50)
//   field 5  objectivePoiEntityIds full vector of u64 (stage QG5c2, readers 0x396BB40, 0x37799A0; live I2 of stage QG5c1): the G of each
//            encounter root whose participant the character is (Entity::encounterLinks): the client shows the objective of the root on its HUD
//            (the banner "Objective Started"); an empty list takes it off. Written from the first link of the world entry on (a character
//            with no link has the bytes of before).
// Field 6 is not written (no dynamic POI). A full container is `00 01 sequence:size64 count:size element*` (reader 0x279DC70); the sequence is
// the world revision of the last change of the state.
//
// The root of an encounter (stage QG5c2, encode_encounter_objectives_state) has its own ObjectivesComponent (the objectives slot of the slice,
// m_isPoiObjectivesComponent 1): group 1 field 3 with one active element of its objective (isPoiObjective 1: the POI branch of the apply
// 0x3A43B9A, which makes the objective of the encounter stages, 0x3A2E590 and 0x44650F0) and field 4 with the task elements of the generated
// task tree (static read B of stage QG5c2: from 1, the leaf of each user-wave stage and its wrapper, then the outer container; the progress
// of a leaf is the N of "N / M"). Each session near the root gets it. Stage QG5c4 (static read D of stage QG5c4): one leaf and one wrapper
// for each objective that makes a task, and field 0 with the start of each timed leaf (the timer of the HUD row).
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
constexpr unsigned kCompletedField = 2;
constexpr unsigned kActiveField = 3;
constexpr unsigned kTaskStatesField = 4;
constexpr unsigned kPoiEntitiesField = 5;

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
    if (!log.completed.empty()) {
        hub::Writer& completed = state.field(kQuestGroup, kCompletedField);
        full_header(completed, sequence, log.completed.size());
        for (const std::uint64_t instance : log.completed) {
            completed.u64(instance);
        }
    }
    hub::Writer& active = state.field(kQuestGroup, kActiveField);
    full_header(active, sequence, log.active.size());
    for (const world::QuestInstance& quest : log.active) {
        active.u32(quest.objectiveCrc);
        active.u64(quest.instance);
        // Stage QG6a (docs/systems/mission-boards.md): the missionId of a mission of a board (the client joins the row of the mission data by
        // it and builds the effective tree: factory 0x4461150), and destinationOverride, the actor id of the live provider board (the return
        // task of the template goes to it: TaskInteract case 23). A quest: 0 and 16 zero bytes.
        active.u32(quest.mission);
        for (const std::uint8_t part : quest.destination) {
            active.u8(part);
        }
        // acceptTime 0: the value of live check L1. The native TaskTimer of the client can take its start from the creation time of the
        // objective (research quest-display-18a.md Q4); the start of a timer goes in field 0. The stored grant time stays in the rows.
        active.u64(0);
        active.u16(quest.originTerritory);
        active.boolean(true);               // available
        active.boolean(quest.communityGoal); // isCommunityGoal (stage QG6a: a town mission)
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
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective_by_crc(quest.objectiveCrc, quest.mission) : nullptr;
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            elements += objective->nodes[index].task != gamedata::kNoTask ? 1 : 0;
        }
    }
    hub::Writer& tasks = state.field(kQuestGroup, kTaskStatesField);
    full_header(tasks, sequence, elements);
    for (const world::QuestInstance& quest : log.active) {
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective_by_crc(quest.objectiveCrc, quest.mission) : nullptr;
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
    // Field 5 (stage QG5c2): the encounter roots of the character, after its first link of this world entry.
    if (entity.encounterLinked) {
        hub::Writer& poi = state.field(kQuestGroup, kPoiEntitiesField);
        full_header(poi, sequence, entity.encounterLinks.size());
        for (const world::EntityId root : entity.encounterLinks) {
            poi.u64(root);
        }
    }
    return state.finish();
}

hub::Bytes encode_encounter_objectives_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::encounter || entity.encounter.objectivesSlot == 0 || entity.encounter.objectiveInstance == 0) {
        return {};
    }
    const world::EncounterBody& body = entity.encounter;
    const std::uint64_t sequence = sequence_of(entity, world::ComponentKey::encounterObjectives);
    StateWriter state(kObjectiveGroups);
    // Stage QG5c4: field 0, the start of each timed leaf (the HUD timer of its row: static read D of stage QG5c4, 0x3A41440, 0x3A68CB0).
    if (!body.taskStarts.empty()) {
        hub::Writer& starts = state.field(kQuestGroup, kStartTimesField);
        full_header(starts, sequence, body.taskStarts.size());
        for (const auto& [index, start] : body.taskStarts) {
            starts.u64(body.objectiveInstance);
            starts.u8(index);
            starts.u64(start);
        }
    }
    hub::Writer& active = state.field(kQuestGroup, kActiveField);
    full_header(active, sequence, 1);
    active.u32(body.objectiveCrc);
    active.u64(body.objectiveInstance);
    active.u32(0); // missionId
    for (int part = 0; part < 16; ++part) {
        active.u8(0); // destinationOverride
    }
    active.u64(0);  // acceptTime
    active.u16(0);  // originTerritory
    active.boolean(true);  // available
    active.boolean(false); // isCommunityGoal
    active.boolean(true);  // isPoiObjective (live I2 of stage QG5c1)
    active.boolean(false); // isDynamicPoiObjective
    active.u64(0);         // parentObjectiveId
    active.boolean(false); // isResetObjective
    active.size(0);        // selectedIngredientVector
    hub::Writer& tasks = state.field(kQuestGroup, kTaskStatesField);
    full_header(tasks, sequence, body.tasks.size());
    for (const world::EncounterTaskState& task : body.tasks) {
        tasks.u64(body.objectiveInstance);
        tasks.u8(task.index);
        tasks.u32(0); // taskCRC: not found (static read B: no compare of the CRC gates the progress).
        tasks.u32(task.progress);
        tasks.u8(task.state);
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
